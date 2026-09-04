# PD-187 — 為每個 pane 註冊 `PaneDock.Pane` window class，子控制項改掛 pane HWND

Phase 7 · architecture · Depends on: PD-183, PD-185

- Source: 2026-09-04 使用者決定重開 `docs/tickets.md` §候選 的「為每個 pane 註冊獨立的 `PaneDock.Pane` window class」。**使用者給出的新證據（原話）：「為了權責分割更乾淨」。**
- Priority: MEDIUM——結構性改動，零視覺、零行為變更，但它是 PD-188（繪製責任下放）與 PD-189（命令路由下放）的唯一前置條件。

## 重開聲明：本票重開 `docs/tickets.md` §候選 的既有登記項

候選表原文（`docs/tickets.md` §候選）：

> 為每個 pane 註冊獨立的 `PaneDock.Pane` window class，子控制項改掛 pane HWND｜2026-09-03 使用者原始需求字面上包含這一項…但經 `/grill-with-docs` 與 opencode 獨立審查後**由使用者決定先不做**。理由：它買到的是訊息路由方便，**不是解耦**…觸發條件：PD-178～PD-183 完成後，若實際維護時仍因 pane 訊息路由踩到具體問題，屆時附上該問題再開票。

**本票重開它，並修正當初理由中不完整的部分。** 當初記錄的理由只評估了「訊息路由」這一項效益，並正確地指出它已被 PD-178 的 `decode_pane_control` 解掉。但該評估**漏掉了繪製與剪裁**這一項效益，而那一項有前科佐證：`paint_client_background`（`main.cpp:2313-2379`）目前在**主視窗**的畫布上計算並繪製全部 4 個 pane 的卡片與導覽列背景，這個結構直接產生過至少三張 bug 票——PD-041（主視窗缺 `WS_CLIPCHILDREN`，全視窗重繪蓋掉 pane 內容）、PD-045（active pane 下緣外框被容器裁切）、PD-063（圓角外框鋸齒）。

**使用者提供的新證據**：使用者於 2026-09-04 明確要求「為了權責分割更乾淨」開票，這是候選表要求的「使用者實際需求」層級的新證據，優先於當初「先不做」的暫緩決定。

**當初列出的三項代價本票全部承認，並用切票處理**：`DeferWindowPos` parent-scoped 批次契約（本票處理，見 Scope 5）、繪製分工重寫（切到 PD-188）、無法自動化驗證（本票附完整實機清單，並把可自動化的部分做成 source-level 與生命週期檢查）。

## Outcome

每個 pane 有一個自己的 HWND（`PaneDock.Pane` window class）。原本掛在主視窗底下的 11 個 pane 子控制項全部改掛到該 pane HWND。

**行為與視覺零變更。** 本票的 pane `WNDPROC` 只做兩件事：維持自己的背景與子控制項幾何，並把 `WM_COMMAND`／`WM_DRAWITEM`／`WM_NOTIFY`／`WM_CTLCOLOR*` 等原本送到主視窗的通知**原樣轉發**給主視窗，讓既有的 `decode_pane_control` 分派路徑一行不改地繼續運作。繪製責任下放是 PD-188，命令處理下放是 PD-189。

## 已確認的現況（2026-09-04 工作樹）

- `Pane::create(HWND parent, int pane_index)`（`src/app_shell/pane.cpp:25-79`）目前把**全部 11 個子控制項**（`explorer_container_`、`tab_strip_`、6 顆按鈕、`address_bar_`、`status_bar_`）直接建立在傳入的 `parent`（主視窗）之下，控制項 ID 以 `encode_pane_control(control, pane_index)` 編碼。
- `Pane::destroy()`（`pane.cpp:81-115`）的順序是 PD-183 建立的 §9.4 不變量：`RevokeDragDrop` → `explorer_host_.destroy()` → 其餘 chrome 子視窗 → `explorer_container_` **最後**。
- `apply_layout`（`main.cpp:2525` 起）目前用**一個** `WindowPositionBatch positions`（`main.cpp:2530`）涵蓋 sidebar、header 與 4 個 pane 的全部子控制項，另外每個 ExplorerBrowser 各自一批（`explorer_positions`，`:2534`）。`main.cpp:2531-2533` 的註解寫明原因：「`DeferWindowPos` requires every window in a batch to share one parent.」
- `WindowPositionBatch` 定義於 `main.cpp:878-925`（`BeginDeferWindowPos`／`DeferWindowPos`／`EndDeferWindowPos` 的 RAII 包裝，含固定 `kCapacity`）。
- PD-155 的 atomic live-resize geometry transaction、PD-108 的「跳過矩形未變動的 pane」、PD-097 的拖曳節流都建立在這個單一批次的形狀上。
- 4 個既有 subclass proc 已經帶著 pane index：`tab_strip_proc`（`main.cpp:3983`，註冊於 `:4626`，subclass ID = pane index）、`address_edit_proc`（`:3429`，註冊於 `:4658`）、`hover_tracking_proc`（`:4103`，註冊於 `:4643`／`:4671`）。
- `register_window_class`（`main.cpp:5550-5560`）是 PD-180 收斂出的註冊樣板，目前註冊三個對話框 class。
- `WM_DPICHANGED` 在主視窗統一處理（`main.cpp:5431`）並重排所有 pane。
- `draw_pane_card`（`main.cpp:2240`）**刻意畫在 `pane_rect` 之外**（左／上／右各 outset 約 2px@96dpi），讓圓角與外框顯示在 pane 之間的間隙裡。**這是 PD-188 的核心難題，本票必須為它預留空間**——見 Scope 3。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`：

> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups — that is both the crash surface and a CPU/disk spike from simultaneous folder enumerations.

`AGENTS.md`：

> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`：

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`：

> Event-driven idle path only. No busy loops, no polling timers.

`docs/design-spec.md` NFR-003 反應性：

> Group 切換、版型切換、tab 切換不得因為某個 Shell location 緩慢或無法連線而凍結 UI。

**本票的責任**：多一層 HWND 會增加 `SetWindowPos`／繪製訊息的數量，因此 PD-097（拖曳節流）、PD-108（跳過未變動的 pane）、PD-095（不重掃 item count）三項既有最佳化**必須在新結構上同樣成立**，不得因為批次拆分而失效。pane proc 內**不得**出現任何同步 Shell 呼叫或 location 解析——PD-168／PD-169 已把那些移出 UI 執行緒同步路徑，本票一行都不得回退。

`docs/design-spec.md` §9.4：關機順序為 capture state → destroy 所有 live `IExplorerBrowser` → destroy pane HWND → destroy 主視窗 → 離開訊息迴圈 → `CoUninitialize`

> 順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。

`docs/design-spec.md` FR-004（分隔比例）與 PD-155：live resize 期間的幾何重排必須是一次原子交易，不得出現部分套用的中間狀態。

`docs/tickets.md` §已否決的方向：

> 端到端 UI 自動化(WinAppDriver／UIAutomation)｜對 live Shell view 極易 flaky。

**本票不重開這條**：驗證仍以 build／ctest／生命週期檢查加上使用者實機清單為準。

`docs/tickets.md` Agent 交付規則：

> 預設每個 ticket 的實作範圍為半天至兩天;若超過,先拆 ticket。

## Files to read and trace first

- `src/app_shell/pane.h`、`src/app_shell/pane.cpp`（全檔）。
- `src/app_shell/main.cpp:878-925`：`WindowPositionBatch`。
- `src/app_shell/main.cpp:2525-2560` 與整個 `apply_layout`：批次與 pane 幾何。
- `src/app_shell/main.cpp:2313-2379`：`paint_client_background`（本票**不改**其內容，只需理解 PD-188 會動哪一段）。
- `src/app_shell/main.cpp:2240` 起：`draw_pane_card` 的 outset 行為。
- `src/app_shell/main.cpp:3429`／`:3983`／`:4103`：三個 subclass proc。
- `src/app_shell/main.cpp:4620-4680`：subclass 註冊點。
- `src/app_shell/main.cpp:5155` 起：`window_proc`，特別是 `WM_COMMAND`／`WM_DRAWITEM`／`WM_CTLCOLOR*`／`WM_SIZE`／`WM_DPICHANGED`（`:5431`）／`WM_ERASEBKGND`（`:5376` 附近）。
- `src/app_shell/main.cpp:5550-5560`：`register_window_class`（PD-180 樣板，本票沿用）。
- `src/app_shell/pane_control_id.h`：`encode_pane_control`／`decode_pane_control`（本票**不改**）。
- `docs/tickets/PD-183-pane-owns-explorer-host-lifetime.md`：`Pane::destroy()` 的 §9.4 順序不變量。
- `docs/tickets/PD-155-atomic-live-resize-geometry-transaction.md`、`PD-108`、`PD-097`、`PD-077`：批次與重繪的既有保證。

## Scope

1. 註冊 `PaneDock.Pane` window class，沿用 `register_window_class`（`main.cpp:5550`）的既有樣板。class style 必須包含 `CS_HREDRAW | CS_VREDRAW` 之外的最小集合；背景 brush 設為 `nullptr`（背景由 `WM_ERASEBKGND` 自行處理，避免閃爍）。
2. `Pane::create` 改為：先在 `parent` 上建立 pane HWND（`WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS`），把 `this` 存進該 HWND 的 `GWLP_USERDATA`，再把原本 11 個子控制項全部建立在 **pane HWND** 之下。控制項 ID 維持 `encode_pane_control`（PD-189 之前不動）。

   **初始化失敗契約（維持既有語意，不得放寬）**：
   - pane HWND 本身建立失敗時，比照既有 11 個控制項的處理——`create()` 內部呼叫 `destroy()` 清乾淨後回 `false`，**不留半成品**（`pane.cpp:74-77` 的既有形狀）。新增的 pane HWND 加入 `create()` 結尾那個 `complete` 檢查。
   - 呼叫端 `WM_CREATE`（`main.cpp:4625`）維持 `if (!chrome.create(...)) return -1;`——pane chrome 建不出來仍是**致命**啟動錯誤，走 PD-166 的同步 error dialog 路徑。**不得**降級成 recoverable warning。
   - `Pane::create` 必須對「重複呼叫」與「部分失敗後再呼叫」是安全的（它開頭已有 `destroy()`，新增 pane HWND 後這個保證必須仍成立）。
   - **本票不改** PD-092 的「一個 pane 的 Shell view realize 失敗時，`apply_layout` 不得中途放棄整段 pass」，也不改 PD-093 的延後 realize 排程、PD-166 的 fatal／recoverable 路由。這些是跨 pane 的收尾策略，留在協調層。

   **必須更新的過期註解**：`main.cpp:4621-4624` 目前寫著 explorer container「never paints or handles messages of its own, so no custom window class is needed」。本票之後這個前提對 **pane HWND** 不再成立（它有自己的 class 與 proc），註解必須改寫以免誤導——但 `explorer_container_` 本身維持 plain `STATIC`（它仍然只做 `SetWindowRgn` 裁切與當 `ExplorerHost::initialize` 的 parent），這一點不變。
3. **pane HWND 的矩形必須外擴**：`draw_pane_card` 目前刻意畫在 `pane_rect` 之外（見「現況」）。本票必須明確定義

   ```
   pane window rect = pane_rect 向左／上／右各外擴 kPaneCardOutset
   pane 內部的子控制項幾何 = 以 pane-local 座標計算，原點對應舊的 pane_rect.left/top
   ```

   並把 `kPaneCardOutset` 抽成具名常數（與 `draw_pane_card` 內既有的 `outset` 計算共用同一個來源，不得各算各的）。本票**不搬**繪製，但這個外擴必須在本票就位，否則 PD-188 無路可走。
4. `Pane::destroy()` 的順序在 PD-183 四步之後**追加第五步**：pane HWND **最後**才 `DestroyWindow`。並在該處註解寫明：pane HWND 一旦被 destroy，Windows 會連帶 destroy 全部子視窗（含 `explorer_container_`），因此它必須排在 `explorer_host_.destroy()` 與 `explorer_container_` 之後——這正是 §9.4「view 存活期間 destroy parent HWND」的同一條崩潰面。
5. **`DeferWindowPos` 批次重建**：`apply_layout` 的單一 chrome 批次拆成
   - 主視窗批次：sidebar、header、4 個 **pane HWND**
   - 每個 pane 一個批次：該 pane 的 11 個子控制項
   - 每個 ExplorerBrowser 各自一批（既有，不變）

   全部批次仍必須在**同一個 layout pass 內**提交，維持 PD-155 的原子性；PD-108 的「矩形未變動就跳過」最佳化必須同時套用在 pane HWND 層與子控制項層（pane 矩形沒變時，連它的子批次都不該建立）。`Pane::set_rect` 目前忽略的 `HDWP* deferred` 參數（`pane.cpp:130-139`）在本票應實際使用或明確移除，不得繼續留著假參數。
6. pane `WNDPROC` 本票只實作最小集合：
   - `WM_ERASEBKGND`：填 pane 目前在主視窗上會被填到的同一個底色，**視覺結果必須與現狀逐像素相同**
   - `WM_COMMAND`／`WM_DRAWITEM`／`WM_NOTIFY`／`WM_CTLCOLOR*`／`WM_MEASUREITEM`：**原樣轉發**給主視窗（`SendMessageW(GetParent(...), ...)`），讓既有 `decode_pane_control` 分派完全不變
   - 其餘一律 `DefWindowProcW`
7. **pane HWND 的關閉路徑（本票新增的一條崩潰面，必須明確處理）**：

   本票之前，pane 的 11 個子控制項是主視窗的直接子視窗；本票之後多了一層。這使「主視窗被 destroy 時，Windows 會由上而下連鎖 destroy 全部子視窗」這條既有行為多跨一層——連鎖會經過 pane HWND、再到 `explorer_container_`，而後者可能還住著一個 live `IExplorerBrowser`。這正是 §9.4 點名的崩潰面。

   要求：
   - `destroy_panes(state)`（PD-183 產出）仍必須在主視窗 `DestroyWindow` **之前**跑完，維持 §9.4 的既有順序。本票**不得**改變這個呼叫時機。
   - pane proc 必須處理 `WM_NCDESTROY`：清掉 `GWLP_USERDATA` 裡的 `Pane*`，並讓 `Pane` 的 HWND 成員在該 HWND 已被系統銷毀後不再被當成有效控制代碼使用（避免對已銷毀 HWND 呼叫 `DestroyWindow`／`SetWindowPos`）。
   - **加上一道防線**：若 pane HWND 在 `Pane::destroy()` 尚未跑過的情況下收到 `WM_DESTROY`（也就是被 parent 連鎖銷毀搶先），必須在該路徑上先呼叫 `explorer_host_.destroy()`，確保 view 不會比它的 parent 活得久。這是 fail-safe，不是正常路徑——正常路徑仍然是 `destroy_panes` 先跑。在該處註解寫明兩者的關係。
   - `Pane::destroy()` 必須對「HWND 已經被系統銷毀」是 idempotent 的（重複呼叫不得 UB）。

8. 三個既有 subclass proc（`tab_strip_proc`／`address_edit_proc`／`hover_tracking_proc`）的註冊與行為**不改**；它們掛在控制項自身，父視窗換人不影響。但必須逐一確認其中任何 `GetParent`／`MapWindowPoints`／`ScreenToClient` 的座標假設在多一層 HWND 之後仍然成立，並在交接區列出檢查結果。
9. `WM_DPICHANGED`（`main.cpp:5431`）維持在主視窗統一處理並重排所有 pane；確認 pane HWND 這一層不需要自己的 DPI 處理，並在交接區寫明依據。
10. 順手把本票碰到的函式簽章從 `AppState&` 收窄。

## Non-goals

- **不**把任何繪製從 `paint_client_background` 搬進 pane proc（PD-188）。
- **不**把任何 `WM_COMMAND`／通知的**處理**搬進 pane proc（PD-189）；本票只轉發。
- **不**改 `encode_pane_control`／`decode_pane_control`（PD-178 產出）。
- **不**改 realize/derealize 政策、`core::plan_realization`、shutdown reducer、Shell 呼叫閘門或導覽 generation 機制。
- **不**把啟動失敗的**回報**交給 `Pane`。fatal（同步 error dialog）與 recoverable（modeless OK 通知）的路由是 PD-166 定的，`startup_error_message`／`startup_notification` 留在協調層；`Pane` 只負責「自己建不起來就乾淨地回 `false`」。同理不改 PD-092 的 layout pass 收尾策略與 PD-093 的延後 realize 排程。
- **不**把關閉**決策**交給 `Pane`。`core::ShutdownSequence`（PD-162）與協調層的 `begin_shutdown`／`finish_shutdown` 仍是唯一決定「能不能關、要不要延後」的地方——它要同時看檔案操作進行中（PD-173）、拖曳進行中、Shell 呼叫深度（PD-172）與 `WM_ENDSESSION`（PD-032），這些天生跨越所有 pane，比照契約 (3) 留在協調層。`Pane` 只負責**自己的清理順序**（Scope 4／7），不負責時機判斷。
- **不**改 `core` 的任何東西。
- **不**改 Group 切換語意（保活 + 重新導覽，不重建 pane HWND——本票之後這條規則的字面意義更強：pane HWND 在程式生命週期內只建立一次）。
- **不**引入 UI 自動化。
- **不**改視覺：本票的驗收標準之一就是逐像素相同。

## Acceptance Criteria

1. 每個 pane 有自己的 HWND，11 個子控制項的 `GetParent` 都回傳該 pane HWND。
2. `Pane::destroy()` 的順序為：`RevokeDragDrop` → `explorer_host_.destroy()` → 其餘 chrome 子視窗 → `explorer_container_` → **pane HWND**，且有引用 §9.4 的註解。
3. **初始化路徑**：pane HWND 納入 `create()` 的 `complete` 檢查；任一失敗都整組清乾淨後回 `false`；`WM_CREATE` 仍以 `return -1` 視為致命；`main.cpp:4621-4624` 的過期註解已更新。
4. **關閉路徑**：`destroy_panes(state)` 仍在主視窗 `DestroyWindow` 之前跑完；pane proc 處理 `WM_NCDESTROY` 並清掉 `GWLP_USERDATA`；`WM_DESTROY` 的 fail-safe 會在 `Pane::destroy()` 未先跑過時先摧毀 view；`Pane::destroy()` 對已被系統銷毀的 HWND 是 idempotent。
5. `apply_layout` 的所有批次在同一 pass 內提交；PD-108 的跳過最佳化在 pane 層與子控制項層都成立。
6. `Pane::set_rect` 不再有被忽略的 `HDWP*` 參數。
7. pane proc 只有 Scope 6／7 列出的訊息有自訂處理，其餘走 `DefWindowProcW`。
8. **視覺逐像素相同**：以相同視窗大小、相同 DPI、相同 Group／版型，改動前後各截一張圖比對（方法見 Agent Checks）。
9. `ctest --test-dir build --output-on-failure` 全綠，含 `panedock_launch_smoke`、`panedock_explorer_host_lifetime`。
10. 連續 20 次切版型後無殘留 live view，關閉不殘留 process。
11. **NFR-003 不退步**：pane proc 內無同步 Shell 呼叫或 location 解析；PD-097／PD-108／PD-095 三項最佳化在新結構上仍成立（交接區逐項說明）；閒置時 CPU 為 0%。
12. 行為零變更。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# pane proc 不得在本票就處理命令或繪製
Select-String -Path src/app_shell/pane.cpp -Pattern 'draw_pane_card|paint_client_background|decode_pane_control'
# Pane 仍不得反向依賴協調層
Select-String -Path src/app_shell/pane.h -Pattern 'AppState|std::function|callback'
```

兩條都必須無結果。

```powershell
# set_rect 不得再留下被忽略的參數
Select-String -Path src/app_shell/pane.cpp -Pattern '\(void\)deferred'
```

必須無結果。

```powershell
# 關閉順序：destroy_panes 仍須存在且在主視窗銷毀之前（既有 shutdown_state_check 已斷言
# 'destroy_panes(' 字面，見 PD-183 交接區）——確認它仍綠，且 pane proc 有處理 WM_NCDESTROY
ctest --test-dir build -R panedock_shutdown_state --output-on-failure
Select-String -Path src/app_shell/main.cpp,src/app_shell/pane.cpp -Pattern 'WM_NCDESTROY'
```

第二條必須在新的 pane proc 內有結果。

```powershell
# 視覺逐像素比對：改動前後各跑一次，用 PrintWindow 擷取主視窗（不是 CopyFromScreen）
# 以相同的 session.json、相同視窗大小與 DPI 執行，輸出 PNG 後比對雜湊
$p = Start-Process build\PaneDock.exe -PassThru; Start-Sleep 4
# （擷取腳本見 docs/testing.md 的既有做法；比對結果貼進交接區）
$p.CloseMainWindow() | Out-Null
if (-not $p.WaitForExit(8000)) { throw 'process survived graceful close' }
```

```powershell
git diff --check
```

## 交給使用者的實機檢查清單

**本票動到視窗階層與 resize 批次契約，屬高風險，請完整執行。**

1. 拖曳主視窗邊框連續縮放 **30 秒**，確認：沒有殘影（PD-077）、沒有撕裂、沒有部分套用的中間狀態（PD-155）、CPU 不飆高（PD-097）。
2. 拖曳 pane 分隔線來回 20 次，確認同上。
3. 拖曳側邊欄寬度來回 10 次（PD-104）。
4. 切版型 1→2→3→4→1 連續 20 次，確認沒有崩潰、記憶體沒有單調成長。
5. 把視窗拖到**不同 DPI 的螢幕**來回 5 次，確認所有 pane 正確縮放（NFR-004）。
6. 每個 pane 的每顆按鈕各點一次（上一頁／下一頁／上一層／Refresh／View／Pinned／資料夾選單），確認命令仍正確送到**正確的 pane**。
7. tab 點擊切換、拖曳排序、跨 pane 拖曳各 3 次。
8. 位址列輸入、自動完成下拉選單各 3 次。
9. 在檔案區與空白處各開一次右鍵選單；觸發一個會開啟其他程式的動作後關閉主視窗，確認乾淨結束。
10. 檔案操作進行中關閉主視窗；拖曳進行中關閉主視窗（PD-173），確認乾淨結束。
11. **緩慢／無法連線的 location（NFR-003）**：把一個 pane 指向已拔除的隨身碟或離線網路磁碟，然後拖曳縮放、切版型、切 Group 各 3 次，確認 UI 全程不凍結、其他 pane 仍可操作。
12. 放著不動 1 分鐘，用工作管理員確認 CPU 為 0%。
13. 每次測試後檢查工作管理員無殘留 `PaneDock.exe`。

## Handoff requirements

在 `## 交接區` 記錄：pane HWND 的 class style 與建立參數、`kPaneCardOutset` 的定義與共用來源、`create()` 的失敗契約（pane HWND 納入 `complete` 檢查後的實際寫法）與 `main.cpp:4621-4624` 註解的最終措辭、`Pane::destroy()` 的五步順序與 §9.4 對應、`WM_DESTROY`／`WM_NCDESTROY` 的處理與 fail-safe 的實作位置（含「正常路徑仍是 `destroy_panes` 先跑」的註解措辭）、批次拆分後的實際結構與 PD-108 跳過最佳化的套用點、三個 subclass proc 的座標假設檢查結果、`WM_DPICHANGED` 維持在主視窗的依據、視覺逐像素比對的結果與方法、`AppState&` 計數前後值、`ctest` 全量結果、以及使用者實機檢查 11 項的逐項回報。

## 交接區

### 實作

- `PaneDock.Pane` 由 `Pane::register_window_class` 經既有
  `register_simple_window_class` 註冊；style 為
  `CS_HREDRAW | CS_VREDRAW`，background brush 為 `nullptr`。pane HWND 以
  extended style `0`、style
  `WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS`、主視窗為 parent、
  `Pane*` 為 `lpParam` 建立；11 個控制項都改以 pane HWND 為 parent，ID
  仍由 `encode_pane_control` 產生。`pane_test` 逐一斷言 11 個
  `GetParent` 結果。
- `kPaneCardOutset = 2` 定義在 `pane.h`；`pane_card_outset(dpi)` 是
  `draw_pane_card` 與 `apply_layout` 唯一共用的 DPI 縮放來源。pane window
  rect 向左／上／右外擴該值、bottom 不變；所有 child rect 以
  `pane_window_rect.left/top` 轉成 pane-local 座標，原本 `pane_rect` 的
  原點因此位於 local `(outset, outset)`。
- `create()` 先呼叫 `destroy()`，建立 pane HWND 後才建立 11 個 children；
  `complete` 同時檢查 pane HWND 與全部 11 個 child HWND。pane HWND 建立
  失敗或 `complete == false` 都呼叫 `destroy()` 後回 `false`；呼叫端仍是
  `if (!chrome.create(...)) return -1;`。建立處註解最終為：
  “`PaneDock.Pane` owns this pane's chrome. Its explorer container stays a
  plain `STATIC` used only for clipping and `ExplorerHost` parenting.”
- `Pane::destroy()` 固定五步：`RevokeDragDrop` →
  `explorer_host_.destroy()` → 其餘 chrome children →
  `explorer_container_` → pane HWND。註解明載 pane HWND 會遞迴摧毀包括
  container 的 children，所以依 §9.4 必須最後處理。
- `pane_window_proc` 位於 `pane.cpp`。`WM_DESTROY` 呼叫 idempotent 的
  `derealize()`；旁註為 “Fail-safe for parent-chain destruction. The normal
  §9.4 path is still `destroy_panes()` before the main HWND dies.”。
  `WM_NCDESTROY` 經 `window_destroyed()` 清除相符的 HWND 並清空
  `GWLP_USERDATA`。正常 shutdown 的 `destroy_panes(state)` 時機未改，且
  `panedock_shutdown_state` 仍斷言它存在。`WM_ERASEBKGND` 先填現有 canvas
  色 `RGB(243,246,249)`；PD-188 前暫以 `WM_PRINTCLIENT` 請主視窗把未搬動
  的既有背景繪製到 pane DC。命令／通知僅原樣轉發，無 Shell 呼叫、
  location 解析、命令處理或繪製函式移入 pane proc。
- `apply_layout` 現為：一個 main-window batch（sidebar、header、4 個 pane
  HWND）、只在該 pane rect 改變時才建立的最多四個 pane-child batch
  （各 11 children）、既有最多四個 ExplorerBrowser batch。全部在同一
  layout pass 內依序 commit。PD-108 同時作用於 pane HWND 與 child batch；
  未變 pane 不排 pane HWND，也不建立 child batch。`Pane::set_rect` 的假
  `HDWP*` 已移除。PD-097 的既有 resize 節流入口未動；PD-095 的 item
  count 路徑未動。

### 座標、DPI 與依賴檢查

- `tab_strip_proc`：所有 hit test 仍使用 tab-strip client coordinates；
  `ScreenToClient(strip, ...)` 與 main→strip 的 `MapWindowPoints` 都能跨越
  新 ancestor。原先送到直接 parent 的私有 selection message 改送
  `GetParent(GetParent(strip))` 的主視窗，避免把 PD-189 的處理提前放進
  pane proc。
- `address_edit_proc`：不使用 `GetParent`、`MapWindowPoints` 或
  `ScreenToClient`，父層變更不影響其 Enter／subclass lifetime 行為。
- `hover_tracking_proc`：右鍵合成的 `WM_COMMAND` 仍送直接 parent；現在
  由 pane proc 原樣轉發至主視窗。其餘 hover 座標皆為 control-local，
  無需改動。
- `WM_DPICHANGED` 維持主視窗集中處理：所有 DPI-dependent rect 與
  `kPaneCardOutset` 都在同一次 `apply_layout` 由主視窗 DPI 重算；處理時
  先清除四個 pane rect cache，確保即使建議 rect 數值碰巧相同也不會被
  PD-108 跳過。pane proc 不需要自己的 DPI handler。
- `main.cpp` 的 `AppState&` 字面計數：實作前 116，實作後 116；本票碰到
  且仍使用協調服務的 `apply_layout` 無法合理收窄，未新增任何
  `AppState&` 介面。

### 驗證

- Configure／Release build：PASS（LLVM-MinGW Clang/LLD + Ninja）。
- `ctest --test-dir build --output-on-failure`：22/22 PASS，0 failed，總計
  4.80 秒；包含 `panedock_launch_smoke` 1.42 秒、
  `panedock_explorer_host_lifetime` 0.44 秒、`panedock_shutdown_state`
  0.50 秒。另跑 `-R panedock_shutdown_state`：1/1 PASS，0.43 秒。
- 四條 `Select-String` 契約：三條禁止 pattern 均無結果；新 pane proc 的
  `WM_NCDESTROY` 位於 `pane.cpp`。`git diff --check`：PASS。
- 視覺逐像素比較：PASS。由同一 desktop session、同一 `session.json`、
  diagnostic mode、固定 window size 與 DPI，依序執行 committed `HEAD`
  baseline 與本票 build；兩者皆以 `PrintWindow(PW_RENDERFULLCONTENT)`
  （非 `CopyFromScreen`）擷取 1300×900 PNG。差異像素 0/1,170,000；兩檔
  SHA-256 均為
  `B980D9D8270A17D9BC585AC74320097C9D14E191586CD20B09C12F336A394A47`。

### 使用者實機檢查

票面清單實際共有 13 項（標題文字寫 11 項）。本次未以易 flaky 的 UI
automation 冒充實機驗收；下列都需使用者在指定真實桌面環境執行：

1. 視窗邊框連續縮放 30 秒：未驗證，需真實桌面。
2. pane splitter 來回 20 次：未驗證，需真實桌面。
3. sidebar 寬度來回 10 次：未驗證，需真實桌面。
4. 版型 1→2→3→4→1 共 20 次與記憶體觀察：未驗證，需真實桌面。
5. 不同 DPI 螢幕來回 5 次：未驗證，需 mixed-DPI 真實桌面。
6. 每 pane 全部按鈕路由：未驗證，需真實桌面。
7. tab 切換／排序／跨 pane 拖曳：未驗證，需真實桌面。
8. address bar 與 autocomplete：未驗證，需真實桌面。
9. Shell context menu、外部程式與關閉：未驗證，需真實桌面。
10. file operation／drag 進行中關閉：未驗證，需真實桌面。
11. 離線 location 下 resize／layout／Group：未驗證，需離線磁碟環境。
12. 閒置一分鐘 CPU 0%：未驗證，需工作管理員實測。
13. 每輪結束無殘留 process：自動 launch smoke 已涵蓋一般關閉；完整
    實機清單各輪仍未驗證。
