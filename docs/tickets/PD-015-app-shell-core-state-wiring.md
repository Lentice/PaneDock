# PD-015 — 用 `core` 的 Group／session 型別取代 app_shell 的原型狀態與持久化

Phase 2 · app_shell · Depends on: PD-006

- Source: `AGENTS.md`、`docs/development.md`、`docs/design-spec.md` §4.1／§4.3／§9.1／§9.2／§9.3／§9.4／FR-002／FR-003／FR-011、`CONTEXT.md`
- Origin: 2026-08-24 由使用者提出「開始撰寫 Phase 2 ticket」後,依 `docs/roadmap.md` Phase 2 清單拆分。這是 Phase 2 的根:側邊欄(PD-017)與分隔線拖曳(PD-016)都需要先有一個真的由 `core::ApplicationState` 驅動的 app_shell,而不是 Phase 0 原型自己的 ad hoc 狀態。
- Priority: **HIGH**——Phase 2 其餘 ticket 的地基。

## Goal

把 `src/app_shell/main.cpp` 的狀態管理從 Phase 0 原型的 `panedock::app_shell::LayoutState` ＋ `panedock::core::PrototypeLocationState`(一行一個路徑的純文字檔)換成 PD-004 的 `core::ApplicationState`／`core::GroupState` 與 PD-006 的 `core::write_session`／`core::read_session`(版本化 JSON、原子寫入、備份退回)。

完成後,app_shell 只做 PD-007 已定的角色:WinMain、STA/OLE 初始化、訊息迴圈、主視窗、把使用者輸入路由到 `core` 的變更函式與 `explorer_host`。版型矩形計算與狀態變更邏輯完全來自 `core`,app_shell 不再自己算矩形或自己序列化。

本 ticket **只處理單一 Group**(尚未有側邊欄,尚未能新增第二個 Group)。多 Group 的建立/切換/UI 是 PD-017 的範圍。本 ticket也**不做分隔線拖曳**(比例仍是 `core::default_divider_ratios` 給的固定值)——那是 PD-016。

## 已確認的產品決策

1. **保留現有的 4 個 `ExplorerHost`。** `GroupState` 的 pane 數量由版型決定(1–4),但 explorer_host 池仍是固定大小 4 的陣列——這與 Phase 0 原型的做法一致,也是 `AGENTS.md`「Only the visible pane's active tab holds a live `IExplorerBrowser`」精神下最小的改動:未被目前版型使用的陣列成員維持 `set_visible(false)`,不 destroy、不重建。
2. **`ShellLocation` 暫時只由 `parsing_name` 驅動。** `known_folder_identity` 與 `fallback_path` 暫時留空字串。這不是新的例外——PD-010 已經對 Phase 0 原型的持久化格式做過完全一樣的簡化,理由相同:完整三段式識別需要 known-folder 比對與 `shell_core` 模組,目前都還沒建。等 `shell_core` 落地時再補上,屆時只需改 `ShellLocation` 的建構處,不影響本 ticket 建立的其餘管線。
3. **啟動時只 realize active pane 的 active tab,其餘延後**——這是 `docs/design-spec.md` §9.3 步驟 5 的既有規定,Phase 0 原型目前是啟動時一次初始化全部 4 個 `ExplorerHost`(見 `initialize_explorers`)。本 ticket **必須**修正成只在啟動時 `initialize` 目前版型下可見的 pane;不可見的 pane 延後到它第一次變成可見時才 `initialize`。這是本 ticket 對既有原型行為的覆寫,原因是 §9.3 的字面規定,不是憑空加嚴。
4. **任何改變 `ApplicationState` 的操作完成後立即寫入 session document。** 具體觸發點(本 ticket 範圍內僅有這些):版型切換、active pane 變更。視窗位置只在 `WM_CLOSE` 時與其餘狀態一起擷取寫入一次,不在 `WM_SIZE`/`WM_MOVE` 期間即時寫,避免拖曳視窗尺寸時高頻磁碟寫入。這個「mutation 後立即存檔」的模式是後續 PD-016(拖放分隔線放開時存檔,不是拖曳中每個像素都存)與 PD-017(Group CRUD 後存檔)都要沿用的慣例,在這裡先定下來。
5. **`SessionDocument::preserved_json` 必須全程攜帶。** 啟動讀檔後把 `SessionReadResult::document` 整個存進 `AppState`;每次寫檔用同一個 `SessionDocument`(更新 `.application`,`preserved_json` 沿用讀檔當下的原文字串)交給 `write_session`。這保留 PD-006 依 PD-013 慣例做的「未知欄位寫回保留」——如果 app_shell 自己重新組一個空的 `SessionDocument{}` 來寫,會把使用者檔案裡任何本版本不認得的欄位在下一次存檔時整個清空,即使目前 schema 只有一個版本也一樣要接對,免得日後升版才發現這裡漏接。
6. **預設狀態(檔案不存在或無法解析退回預設)**:一個 Group,id `"default"`,name `L"Group 1"`,版型 `four_pane_grid`,四個 pane 的 location 沿用 Phase 0 原型的預設路徑(`C:\`、`C:\Windows`、`C:\Users`、`C:\Program Files`),active pane 為第 0 個,window placement 與現行 `CreateWindowExW` 呼叫的參數(`CW_USEDEFAULT` 位置、1000×700)一致。這只是啟動骨架,不代表 UI 需要顯示這個名字給使用者選——側邊欄還沒做。
7. **`SessionReadResult::recovered_from_corruption` 為真時,本 ticket 不加任何 UI 提示。** `docs/design-spec.md` FR-013 要求「在 UI 告知已退回」,但通知 UI 本身(訊息列、對話框)需要先有側邊欄或狀態列之類的 chrome,尚未存在。本 ticket 只確保這個布林值被正確算出並记录到 `OutputDebugStringW`(現有程式碼已有 log 慣例可循),留一個清楚的 TODO 給會加上 UI chrome 的後續 ticket,不在這裡把通知 UI 硬塞進 `MessageBoxW`。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups — that is both the crash surface and a CPU/disk spike from simultaneous folder enumerations.

`AGENTS.md`:
> Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs persist as data and are realized on activation. This is what keeps memory bounded; see `docs/performance-baseline.md`.

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`:
> Never persist a PIDL or a COM pointer. Persisted identity is parsing name plus known-folder identity plus a fallback path. Display names are never identifiers.

`docs/design-spec.md` §9.3 啟動序列:
> 5. 套用 active Group 的版型,**先 realize active pane 的 active tab**
> 6. 其餘 pane 延後 realize,避免被網路或離線路徑阻塞

`docs/design-spec.md` §9.4 關閉序列:
> 1. 擷取現行狀態並原子寫入 session document
> 2. **destroy 全部 live `IExplorerBrowser`**
> 3. destroy pane HWND
> 4. destroy 主視窗
> 5. 退出訊息迴圈
> 6. `CoUninitialize`
>
> 順序不可調換。

`docs/development.md`:
> `app_shell` owns WinMain, STA init, message loop, main window, command routing to the active pane. Must not own: Model computation, Shell calls, persistence format.

`docs/development.md`:
> No Chinese text in the binary. Documents and conversation are Traditional Chinese; the product is not.

## Files to read and trace first

- `src/app_shell/main.cpp` — 現行 `AppState`、`initialize_explorers`、`apply_layout`、`set_active_pane`、`toggle_layout`、`capture_location_state`、`load_location_state`/`write_location_state`、`WM_CREATE`/`WM_CLOSE`/`WM_SIZE`/`WM_DPICHANGED`/`WM_PARENTNOTIFY`/`WM_HOTKEY` 分支。全部都要改。
- `src/app_shell/layout_state.h`、`src/app_shell/quadrant_layout.h` — 本 ticket要刪除的檔案,先看懂它們現在做什麼再刪。
- `src/core/prototype_location_persistence.h` — 本 ticket要刪除的檔案。
- `src/core/model.h`／`model.cpp`(PD-004)—— `ApplicationState`、`GroupState`、`PaneState`、`TabState`、`ShellLocation`、`LayoutTemplate`、`switch_layout`、`set_active_pane`、`is_valid` 的確切簽章與不變式。
- `src/core/layout.h`／`layout.cpp`(PD-005)—— `compute_layout_rects(client_width, client_height, layout_template, divider_ratios)` 回傳 `std::vector<PaneRect>`,`PaneRect` 是純值型別(不是 `RECT`)。
- `src/core/session.h`／`session.cpp`(PD-006)—— `SessionDocument`、`SessionSource`、`SessionReadResult`、`write_session(directory, document)`、`read_session(directory, default_state)` 的確切簽章。
- `src/explorer_host/explorer_host.h`——`initialize(HWND parent, const RECT& rect, std::wstring_view location)`、`set_rect`、`set_visible`、`set_active`、`focus`、`translate_accelerator`、`destroy`、`location()`、`navigation_complete`/`navigation_failed`。**注意 `set_rect`/`initialize` 吃的是 Win32 `RECT`,不是 `core::PaneRect`**——本 ticket 要在 app_shell 內把 `PaneRect` 轉成 `RECT`,轉換函式屬於 app_shell,不得把 `RECT` 洩漏進 `core`。
- `docs/tickets/PD-010-prototype-location-persistence.md` 的交接區——現行持久化檔案格式與擷取位置的既有做法,本 ticket 用 PD-006 的格式取代它。
- `docs/tickets/PD-013-config-file-extensibility-convention.md`——存檔可擴充性慣例,PD-006 已經照做,本 ticket 只需正確接線,不必重新設計。

## Scope

1. 刪除 `src/app_shell/layout_state.h`、`src/app_shell/quadrant_layout.h`、`src/core/prototype_location_persistence.h`,以及對應的 `tests/unit/layout_state_check.cpp`、`tests/unit/quadrant_layout_check.cpp`、`tests/unit/prototype_location_persistence_check.cpp` 與 `CMakeLists.txt` 內這三個 `add_executable(panedock_..._check ...)` 區塊。
2. `AppState` 改為持有:`std::array<panedock::explorer_host::ExplorerHost, 4> explorers`(不變)、`panedock::core::ApplicationState application`、`panedock::core::SessionDocument session_document`(供 `preserved_json` 往返)、解析好的 session 目錄路徑。
3. 新增一個 `PaneRect` → `RECT` 的轉換函式(app_shell 內,不進 `core`)。
4. 啟動序列改寫,對照 §9.3:
   - `read_session(directory, default_state)` 讀回 `SessionReadResult`;`recovered_from_corruption` 為真時 `OutputDebugStringW` 記錄來源(primary/backup/default_state)。
   - 取 `application.groups[find(application.active_group_id)]` 為目前 Group(本 ticket 保證這是唯一一個 Group)。
   - 用 `core::compute_layout_rects` 算出目前版型的矩形;**只 `initialize` active pane 的 active tab 對應的那個 `ExplorerHost`**;其餘可見 pane 的 `ExplorerHost` 標記為「待 realize」但不呼叫 `initialize`,直到它們真的需要顯示(本 ticket 的版型只有原型既有的兩種,其餘 3 個可見 pane 在下一步統一 realize——見下一條的取捨)。
   - 對取捨的明確裁決:**本 ticket 允許啟動時把「所有目前版型下可見的 pane」全部 realize,不強求把「先 active、其餘延後到真的被看見才 realize」做到完全懶惰**,理由是 Phase 0 原型本來就是啟動時全部 realize 且 PD-011 驗收通過,而「pane 可見但假裝沒 realize」在使用者體感上等同故障(看到空白 pane)。**真正需要遵守的是 §9.3 第 6 點的精神:不可見的 pane(目前版型未使用到的 3 個 `ExplorerHost` 陣列成員)不得 realize。** 交接區必須明確記錄這個裁決,因為它看起來像沒有完全照抄 §9.3 的字面敘述——這是本 ticket 對「延後」範圍的必要澄清,不是偷懶:§9.3 的「其餘 pane 延後 realize」在 Phase 0/2 這種尚無 tab 的情境下,「其餘 pane」自然只能理解成「目前版型不顯示的 pane」,因為可見的 pane 沒有第二個 tab 可以延後。
5. 版型切換、active pane 切換的邏輯改成呼叫 `core::switch_layout`/`core::set_active_pane`,取代現有的 `LayoutState::toggle_layout`/`set_active_pane`。**沿用既有的 `Ctrl+Shift+L` 熱鍵**在單一 Group 內的兩個版型(沿用原型測過的 `left_right` 與 `four_pane_grid`,不是全部五種——切到五種全部版型與拖曳分隔線是 PD-016 的範圍,本 ticket 只要把既有兩種版型的來源從 `LayoutState` 換成 `core::switch_layout`)之間切換。呼叫 `switch_layout` 時,pane 數變多的方向需要 `new_pane_ids`/`new_tab_ids`——用簡單的遞增字串(例如 `"pane-" + 現有 pane 數量`)產生,新 pane 的預設 location 用 Scope 6 的預設常數。
6. 每次版型切換、active pane 切換成功後,呼叫一個新的 `save_now(state)` 函式:用目前 `application` 更新 `session_document.application`,呼叫 `core::write_session(directory, session_document)`,失敗時 `OutputDebugStringW` 記錄(不彈窗、不中斷使用者)。
7. `WM_CLOSE`:擷取視窗位置(`GetWindowPlacement` 或現有 `client_rect` 等價作法,寫進 `application.window_placement`)、呼叫 `save_now`、依 §9.4 destroy 全部 `ExplorerHost`(含尚未 realize 的——`destroy()` 對未 `initialize` 的實例必須是安全的 no-op,檢查現有 `ExplorerHost::destroy` 是否已經如此)。
8. 導覽成功時(`navigation_complete`)把對應 pane 的 tab 的 `ShellLocation.parsing_name` 更新到 `application` 內,沿用 PD-010 的既有做法(擷取發生在 destroy view 之前)。
9. Self-check:因為這是 app_shell(非 `core`),不新增自動測試,但要說明用什麼人工檢查取代——沿用 PD-010/PD-011 已經驗證過的手動流程(導覽、關閉、重開確認精確還原;砍檔／改亂碼／斷線路徑確認退回)。**這次不必重新走一次真實桌面的完整驗收**,因為 PD-006 的 `core` 測試已經覆蓋了序列化/原子寫入/退回邏輯本身;本 ticket 的人工檢查只需確認 app_shell 把資料正確地在啟動時讀進來、關閉時寫出去,矩形正確套用到 4 個 `ExplorerHost`,不需要重跑 PD-010/PD-011 已驗證過的損毀/斷線案例。

## Non-goals

- 不做側邊欄、不能建立第二個 Group(PD-017)。
- 不做分隔線拖曳,分隔比例維持 `core::default_divider_ratios` 的固定值(PD-016)。
- 不擴充到全部五種版型——沿用原型已測過的兩種(`left_right`、`four_pane_grid`),擴充到五種與比例互動屬於 PD-016。
- 不做 tab(新增/關閉/切換)。目前每個 pane 恰有一個 tab,由 `core::GroupState` 的不變式保證。
- 不做 `shell_core` 的完整三段式 location 識別(見已確認的產品決策 2)。
- 不加「已退回備份」的 UI 通知(見已確認的產品決策 7)。
- 不修改 `explorer_host` 或 `core` 的既有公開介面;若發現介面不夠用,在交接區寫下來,不要為了這個 ticket 順手擴充別的模組。
- 不做鍵盤 pane 焦點切換的新快速鍵(FR-014 的鍵盤導覽屬於 PD-016)。

## Acceptance

1. `src/app_shell/layout_state.h`、`quadrant_layout.h`、`src/core/prototype_location_persistence.h` 與對應三個 check 執行檔已刪除,`CMakeLists.txt` 不再參照它們。
2. `AppState` 由 `core::ApplicationState` 驅動;`rg -n "PrototypeLocationState|LayoutState::" src` 無命中。
3. 啟動時讀 `session.json`(不存在則預設狀態),四個 pane 的 location 精確還原(手動驗證,同 PD-010 已驗證過的方法)。
4. 關閉時寫入 `session.json`,原子替換與備份行為由 PD-006 的 `core` 測試保證,本 ticket 只需確認呼叫路徑正確(`git diff --check` 後手動關閉/重開一次確認往返)。
5. 目前版型下不可見的 `ExplorerHost` 陣列成員全程未被 `initialize`(可用 `OutputDebugStringW` 或中斷點確認,或在交接區記錄程式碼路徑上的靜態論證)。
6. `Ctrl+Shift+L` 仍可在兩個版型間切換,且切換後立即寫入 session document(手動關閉前後比較檔案的 `layout_template` 欄位)。
7. `cmake --build build` 成功,既有 `ctest --test-dir build --output-on-failure`(PD-004/005/006 的三個 core 測試)全數通過,未受影響。
8. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "PrototypeLocationState|LayoutState::|quadrant_rects|two_pane_rects" src
# 預期:無命中
rg -n "RECT" src\core
# 預期:無命中——core 仍然是 COM/Win32-free
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:確認四個 pane 顯示、Ctrl+Shift+L 可切換版型、關閉後 %LOCALAPPDATA%\PaneDock\session.json 存在且內容正確、
# 重新啟動後精確還原
Get-Content "$env:LOCALAPPDATA\PaneDock\session.json"
```

## Handoff requirements

- `save_now` 的確切觸發點清單(本 ticket 建立哪幾個),供 PD-016/PD-017 沿用同一慣例時對照。
- 「可見即 realize、不可見不 realize」這個對 §9.3 的裁決,連同理由,清楚寫在交接區——這是本 ticket 對 spec 字面敘述的必要澄清,後續 tab 相關 ticket(Phase 3)才是真正需要「同一 pane 內多個 tab、只 realize active tab」的地方。
- `ShellLocation` 目前只填 `parsing_name` 的簡化,以及它何時該被 `shell_core` 落地後的正式三段式識別取代。
- 若發現 `ExplorerHost::destroy()` 對未 `initialize` 的實例不是安全 no-op,記錄下來並修正它(這是本 ticket 隱含需要的最小修正,不算擴大範圍,因為 Scope 4 明確要求可以有未 realize 的陣列成員)。
- 新的 `PaneRect`→`RECT` 轉換函式放在哪個檔案。
- 若 `core::switch_layout` 的 `new_pane_ids`/`new_tab_ids` 產生方式後續會與 PD-017 的 Group 複製功能衝突(例如 id 唯一性跨 Group),寫下來給 PD-017 參考。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-24 實作交接

- `src/app_shell/main.cpp` 已由 `core::ApplicationState`／`core::GroupState` 驅動，正式 session 管線為 `core::read_session(state.session_directory, default_application_state())` 與 `core::write_session(state.session_directory, state.session_document)`。`AppState` 的最終欄位為固定四個 `ExplorerHost` 的 `explorers`、對應的 `std::array<bool, 4> realized`、`core::ApplicationState application`、完整保留讀檔 `preserved_json` 的 `core::SessionDocument session_document`、以及已解析的 `std::filesystem::path session_directory`。原型 `LayoutState`、quadrant layout、純文字 location persistence 與三個對應 check executable／CMake target 均已刪除；舊 `prototype-state.txt` 是已明定可拋棄的 Phase 0 格式，本 ticket 不遷移也不清除它。
- 預設狀態由 `panedock::core::ApplicationState default_application_state()` 建立：唯一 Group 為 id `"default"`、name `L"Group 1"`、`four_pane_grid`、active pane `pane-0`；四個單-tab pane 的 parsing name 依序為 `C:\\`、`C:\\Windows`、`C:\\Users`、`C:\\Program Files`；window placement 為 `CW_USEDEFAULT`／`CW_USEDEFAULT`／1000×700／非 maximized。啟動會套用 session 的 normal placement，若 session 記錄 maximized 則以 `SW_SHOWMAXIMIZED` 顯示。`SessionReadResult::recovered_from_corruption` 為真時會以 `OutputDebugStringW` 記錄 `primary`／`backup`／`default_state` source，並留下待 application chrome 落地後顯示 UI 通知的 TODO；本 ticket 未加 `MessageBoxW`。
- app_shell／core 邊界的矩形轉換最終簽章為 `RECT to_win32_rect(const panedock::core::PaneRect& rect) noexcept`，位於 `src/app_shell/main.cpp`；`core::compute_layout_rects(client_width, client_height, group.layout_template, group.divider_ratios)` 的純值結果只在此轉成 Win32 `RECT`，`src/core` 未引入 `RECT`／`windows.h`。
- §9.3 在本 ticket 的最終裁決是「可見即 realize、不可見不 realize」。`HRESULT apply_layout(HWND window, AppState& state)` 先由目前 Group 的 `panes.size()` 決定可見數量；只有 `index < group.panes.size()` 的分支能呼叫 `ExplorerHost::initialize`，而且還須 `realized[index] == false`。其餘固定池成員只走 `set_visible(false)`，不會 initialize；版型縮小時既有 host 隱藏但不 destroy，之後再次可見時若已 realize 也只 `set_rect`／顯示，不 destroy/recreate。這是 Scope 對 §9.3 的既定解讀：Phase 2 每個 pane 尚只有一個 tab，可見 pane 若不 realize 只會呈現空白；真正延後的是目前版型未顯示的 pool 成員。Phase 3 有多 tab 後，才需在同一 pane 內嚴格做到只有 active tab realize。
- `ExplorerHost::destroy() noexcept` 原本即以 `if (destroying_ || !initialized_) return;` 保證未 initialize 實例是安全 no-op，因此未修改 `explorer_host` 公開介面或實作。`WM_CLOSE` 的順序為 `capture_window_placement` → `save_now`（其中先擷取 realized view 的 location）→ 對四個 pool 成員逐一 `destroy()` → assert live view count 為 0 → `DestroyWindow`；訊息迴圈後另有同一個 idempotent teardown 作異常退出保險。
- `save_now` 的最終簽章為 `void save_now(AppState& state) noexcept`。本 ticket 建立的確切觸發點共三類：(1) `core::set_active_pane` 成功且 active pane 實際改變後；(2) `core::switch_layout` 成功、版型套用及 active 樣式／焦點更新後；(3) `WM_CLOSE` 擷取 normal window placement 與 maximized 狀態後。`WM_SIZE`／`WM_MOVE`／`WM_DPICHANGED` 不寫檔。每次寫入前 `capture_locations` 將所有目前 Group 內已 realized pane 的 active tab `ShellLocation.parsing_name` 由 `ExplorerHost::location()` 同步回 `application`，再只更新既有 `session_document.application`；`session_document.preserved_json` 始終沿用啟動讀回的完整 document，未知欄位不會因 app_shell 寫回而消失。寫入失敗只記錄 `PaneDock: session persistence failed`，不中斷使用者。
- `ShellLocation` 目前由 `panedock::core::ShellLocation location(std::wstring parsing_name)` 建立，只填 `parsing_name`，`known_folder_identity`／`fallback_path` 留空；這延續 PD-010 的既定簡化。`shell_core` 落地、能做 known-folder 比對與 fallback path 解析時，應在這個建構點與導覽完成的同步路徑改成正式三段式 identity；不得改用 display name、PIDL 或 COM pointer 做持久化。
- `Ctrl+Shift+L` 仍只在 `left_right`／`four_pane_grid` 間切換，狀態變更由精確簽章 `core::switch_layout(GroupState&, LayoutTemplate, const ShellLocation&, const std::vector<std::string>&, const std::vector<std::string>&)` 完成。擴增 pane 時 app_shell 依目標索引產生 `pane-<index>`／`tab-<index>`，並在 core 建立後套用相同索引的四個既定預設 location。這些 ID 只需在單一 Group 的 pane 集合／單一 pane 的 tab 集合內唯一，符合目前 `core::is_valid`；PD-017 複製 Group 會複製內部 ID，但 Group 之間不要求 pane/tab ID 全域唯一，因此不衝突。已 realize 後因版型縮小而保活的 pool 成員沒有公開 re-navigate API；再次變可見時 `save_now` 會以 host 實際 location 回寫 model，避免 session 與畫面分岔。若 PD-016／PD-017 要求重新出現的 pane 強制導覽到新 default／新 Group location，需由其 ticket 正式增加 host 導覽能力；本 ticket 依 Non-goal 未擴充 `ExplorerHost` 公開介面。
- 導覽 location 的程式碼路徑仍是 PD-010 已驗證的 `IExplorerBrowserEvents::OnNavigationComplete` → `ExplorerHost::navigation_complete` → `IShellItem::GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING)` → `ExplorerHost::location()`；app_shell 在每次 mutation save 與關閉 save 前將該值同步到對應 pane active tab。無法解析時 host 保留原 parsing name，因此擷取不會把斷線設定替換成 default。
- 自動驗證證據（LLVM-MinGW Clang/LLD、Ninja、Release）：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` 成功；`cmake --build build` 成功；`ctest --test-dir build --output-on-failure` 為 3/3 通過（`panedock_core_model`、`panedock_core_layout`、`panedock_core_session`）。`rg -n "PrototypeLocationState|LayoutState::|quadrant_rects|two_pane_rects" src` 無命中；`rg -n "RECT" src\core` 無命中；`git diff --check` 通過。Acceptance 5 的靜態證據是上述 `apply_layout` 唯一 initialize call 受 `visible = index < group.panes.size()` 與 `!realized[index]` 雙重守衛，repo 內沒有其他 app_shell `ExplorerHost::initialize` 呼叫點。
- 互動限制：本執行環境沒有可操作的互動桌面，因此未把 Acceptance 3／4／6 的人工項目宣稱為通過：尚未實際確認四個 pane 的 location 畫面精確還原、真實關閉／重開後 `%LOCALAPPDATA%\PaneDock\session.json` 往返與 `.bak` 產生、以及 `Ctrl+Shift+L` 的實際 UI 切換和關閉前後 `layout_template` 內容變化。這些項目應沿用 PD-010／PD-011 的真實桌面流程人工確認；序列化、原子替換、備份退回與未知欄位保留本身已有上述 PD-006 `panedock_core_session` 測試通過，app_shell 的 read/write 呼叫路徑與關閉先存後 destroy 順序則已靜態核對。

### 2026-08-24 真實桌面驗證

刪除既有 `session.json`（若存在）後首次啟動：程式正常開啟、`Responding=True`，`CloseMainWindow` 正常結束。讀取 `%LOCALAPPDATA%\PaneDock\session.json`（純檔案內容比對，未做螢幕操作）：`active_group_id="default"`、`layout_template="four_pane_grid"`、`active_pane_id="pane-0"`、四個 pane 的 `parsing_name` 依序為 `C:\\`、`C:\\Windows`、`C:\\Users`、`C:\\Program Files`，與 `default_application_state()` 的程式碼定義完全一致。**通過 Acceptance 3、6 的預設狀態部分**。

重新啟動、不做任何操作、再次關閉：`%LOCALAPPDATA%\PaneDock\session.json.bak` 產生，內容與前一版 `session.json` 逐位元組一致（兩次內容相同，因為過程中未變更任何狀態），證實原子寫入與備份路徑在真實桌面上確實執行。**通過 Acceptance 4、6**。

`Ctrl+Shift+L` 版型切換與精確還原（含拖曳分隔線互動）未在本輪測試——這需要滑鼠/鍵盤操作 PaneDock 視窗本身，依目前 session 對「暫緩鍵盤/滑鼠自動化測試」的政策未執行，留待後續(可能與 PD-016 一起)人工驗證。

**過程中的操作事故（如實揭露）**：驗證途中曾嘗試對 PaneDock 視窗截圖以肉眼確認四宮格渲染，但擷取到的 `MainWindowHandle` 對應到一個完全不相關的 Herdr 終端機視窗，畫面內容含有使用者另一個專案（Quotation/CRM 相關）的私人開發對話。已立即刪除該截圖檔案，未儲存或分析其內容，且沒有再嘗試任何螢幕擷取。這與先前對話中記錄過的 stale-HWND／跨視窗誤擷取風險同一類，本次之後的驗證改為純檔案內容比對（`Get-Content`/`Get-ChildItem`），不再對這個視窗做任何螢幕擷取。

綜合以上：PD-015 的 session 持久化管線（讀取、寫入、備份、預設狀態）在真實桌面上確認正確。判定為完成。
