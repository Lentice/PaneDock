# PD-022 — 不可解析 location 的可復原錯誤狀態與重試

Phase 4 · explorer_host + app_shell · Depends on: PD-020

- Source: `AGENTS.md`、`docs/design-spec.md` FR-012 / §9.3、`docs/tickets/PD-020-address-bar-and-navigation-buttons.md`、`docs/tickets/PD-010-prototype-location-persistence.md`
- Origin: 2026-08-24,`docs/roadmap.md` Phase 4「Unresolvable-location error state and retry」。Phase 4 四個條列裡唯一需要我們自己寫產品程式碼的一項——其餘三項(`IFileOperation`、剪貼簿、拖放)由 `IExplorerBrowser` 承載的原生 Shell view 直接提供,由 PD-023 驗證而非重新實作。
- Priority: HIGH——這是 FR-012 的唯一交付,而 FR-012 直接保護「不得因此刪除任何已儲存的設定」這條資料安全承諾。

## Goal

當一個 tab 的 location 無法解析(網路磁碟機離線、USB 拔除、使用者在網址列輸入不存在的路徑、OneDrive 尚未上線),pane 內要顯示一個**可復原**的錯誤狀態:說清楚是哪個 location 失敗、保留該 tab 的設定不變、並提供一個明確的重試入口。目前已經有一半:`ExplorerHost::navigation_failed()` 會蓋上一個寫死文字的 `STATIC` 覆蓋視窗,且 `ExplorerHost::navigate()` 在嘗試前就先 `location_ = location`,所以設定確實有保留。缺的是**失敗的 location 沒有顯示出來**,以及**沒有任何重試路徑**——使用者現在只能重新打一次網址列。

## 已確認的產品決策

1. **錯誤覆蓋視窗留在 `ExplorerHost` 裡,不上移到 app_shell。** 它的顯示/隱藏/搬移已經跟 `set_rect`/`set_visible`/`focus`/`destroy` 的既有生命週期綁死(見 `src/explorer_host/explorer_host.cpp` 對 `error_window_`/`error_visible_` 的處理);搬到 app_shell 會把這四條路徑全部重寫一遍,是比較大的 diff 而不是比較小的。
2. **錯誤覆蓋視窗從單一 `STATIC` 改為一個容器 child window,內含一個訊息 `STATIC` 與一個 `Retry` 按鈕。** 理由:`STATIC` 本身收不到 `BN_CLICKED`,而在 `WS_EX_TRANSPARENT`/子控制項上硬接點擊比直接放一顆 `BUTTON` 複雜。容器可以是一個自註冊的簡單 window class,或直接用既有 pane 的父視窗加兩個 child 控制項——**實作者選擇較小的那個做法**,只要 `set_rect`/`set_visible`/`destroy` 三條路徑都同步處理到。
3. **訊息文字必須含失敗的 location 字串。** 格式:`This location is not available:\n<location>\n\nReconnect the drive or check the path, then retry.`(app UI 一律英文,見 `AGENTS.md`)。location 直接取 `location_`——`navigate()` 已經在失敗前把它保留下來了。
4. **`Retry` 按鈕的行為是重新呼叫 `navigate(location_)`。** 不是「回到上一個成功的 location」,也不是「導覽到預設資料夾」——後兩者都會丟掉使用者要的目的地,違反 FR-012「保留其設定」。
5. **重試成功時錯誤覆蓋自動消失。** 這條已經有了:`navigation_complete()` 會把 `error_visible_ = false` 並隱藏視窗,不需要新程式碼,但 acceptance 要驗證它在新的容器結構下仍然成立。
6. **不做自動重試、不做輪詢偵測「磁碟機回來了」。** `AGENTS.md`:「Event-driven idle path only. No busy loops, no polling timers.」重試一律由使用者按鈕觸發。
7. **不改 session schema、不改 `core`。** 錯誤是一個純執行期的顯示狀態,不持久化。重新啟動時該 tab 一樣會嘗試導覽、一樣會失敗、一樣會顯示錯誤——這正是正確行為。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` FR-012:
> 無法解析的 Shell location 在 tab 內顯示可復原錯誤,保留設定,並可重試。不得因此刪除任何已儲存的設定。

`docs/design-spec.md` §9.3:
> - 無法解析的 location:tab 內可復原錯誤,保留設定(FR-012)

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

> **App UI text must be English.** No Chinese strings ship in the binary.

> **Never persist a PIDL or a COM pointer.** Persisted identity is parsing name plus known-folder identity plus a fallback path.

## Files to read and trace first

- `src/explorer_host/explorer_host.cpp` — 全部 `error_window_` / `error_visible_` 的出現處(目前約在 `set_rect`、`set_visible`、`focus`、`navigation_complete`、`navigation_failed`、`destroy` 六處)。**每一處都要跟著新結構調整**,漏掉 `destroy` 會洩漏視窗,漏掉 `set_rect` 會讓錯誤面板在拖分隔線後留在錯的位置。
- `src/explorer_host/explorer_host.cpp` 的 `navigate()` — 確認 `location_ = location` 發生在任何失敗之前(這是 FR-012「保留設定」目前唯一的實作依據,不要動它)。
- `src/explorer_host/explorer_host.h` — `error_window_`、`error_visible_`、`location_` 三個成員。
- `src/app_shell/main.cpp` 的 `handle_navigation_failed(AppState&, std::size_t)`(PD-020 reviewer 新增)—— 它會清 `suppress_history_record` 並刷新導覽 chrome。本 ticket 的 `Retry` 走的是 `ExplorerHost::navigate()`,跟這條路徑不衝突,但要確認重試失敗時旗標不會卡住。
- `docs/tickets/PD-020-address-bar-and-navigation-buttons.md` 的 交接區「Reviewer 修正」段——`navigate()` 的失敗路徑一律 `return S_OK`,呼叫端拿不到 FAILED HRESULT,這是既有設計,`Retry` 按鈕不要依賴回傳值判斷成功。

## Scope

1. 把 `ExplorerHost` 的錯誤覆蓋從單一 `STATIC` 改成訊息 `STATIC` ＋ `Retry` `BUTTON`(決策 2)。
2. `navigation_failed()` 每次都用目前的 `location_` 重新設定訊息文字(決策 3)——不能只在第一次建立視窗時設定一次,否則第二個失敗的 location 會顯示第一個的路徑。
3. 接上 `Retry` 的點擊:重新呼叫 `navigate(location_)`(決策 4)。注意 `navigate()` 會把 `location_` 指派給自己,是安全的,但實作時**先複製一份再呼叫**,避免自我指派期間的別名問題。
4. `set_rect` / `set_visible` / `focus` / `navigation_complete` / `destroy` 五條既有路徑同步處理新的控制項,行為與現況等價。
5. 一個 runnable self-check:延伸 `tests/unit/explorer_host_lifetime_check.cpp`,或新增一個同型的 check,驗證「導覽到一個保證不存在的路徑後,`location()` 仍然回傳原本請求的字串」——這是 FR-012「保留設定」可自動驗證的部分。COM/視窗部分不自動測(`docs/testing.md` 已否決為 `IExplorerBrowser` 加抽象層)。

## Non-goals

- 不做自動重試、不做磁碟機上線偵測、不加任何計時器(決策 6)。
- 不在 tab 條上加錯誤標記(小紅點之類)——spec 未要求,YAGNI;若使用者實際使用後覺得需要,記在交接區。
- 不區分失敗原因(離線 vs 路徑不存在 vs 權限不足)——一則訊息涵蓋全部。要區分就得解讀 HRESULT 並維護一張對照表,不成比例。
- 不改 `core`、不改 session schema、不改 `ExplorerHost` 既有公開簽章。
- 不處理 session document 損壞的 UI 告知(FR-013)——那是 Phase 5 的崩潰復原,不是本 ticket。

## Acceptance

1. 在網址列輸入一個不存在的路徑(例如 `Z:\definitely-not-there`)後按 Enter:pane 內出現錯誤面板,**面板上顯示該路徑字串**,且有一顆 `Retry` 按鈕。
2. 該 tab 的設定被保留:關閉並重新開啟 PaneDock 後,同一個 tab 仍然嘗試同一個 location(不是被預設資料夾覆寫)。這直接對應 FR-012 的「不得因此刪除任何已儲存的設定」。
3. 按 `Retry` 會重新嘗試同一個 location;仍然失敗時錯誤面板留著並更新,不崩潰、不重複疊加視窗。
4. 讓 location 變成可解析(例如建立該資料夾、或重新連線網路磁碟機)後按 `Retry`:錯誤面板消失,Shell view 正常顯示該資料夾。
5. 錯誤面板在拖曳分隔線、切換版型、切換 Group、切換 tab、`WM_DPICHANGED` 之後都停在正確的矩形內,不會殘留在舊位置或蓋到別的 pane。
6. 關閉應用程式時不留下孤兒視窗:`destroy()` 後 `error_window_` 及其子控制項全部銷毀。
7. 兩個不同 pane 同時處於錯誤狀態時,各自顯示自己的失敗 location,互不干擾。
8. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過;新增的 self-check 通過。
9. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 確認 core 邊界未被污染
rg -n "windows\.h|HWND|IUnknown" src/core
# 預期:無輸出
git diff --check
git status
# 預期:改動集中在 src/explorer_host/*、tests/unit/*、本文件
```

```powershell
Start-Process .\build\PaneDock.exe
Start-Sleep -Seconds 2
Get-Process PaneDock | Select-Object Responding, HandleCount
# 手動(留給使用者,本專案目前不做鍵盤/滑鼠自動化):
#   Acceptance 1、3、4、5、7 需要真實互動桌面逐項確認。
```

## Handoff requirements

- 記錄最終採用的容器做法(決策 2 的兩個選項哪一個、為什麼)與 `Retry` 按鈕的控制項 ID。
- 記錄 Acceptance 2(重啟後設定仍保留)是用什麼方式驗證的——這是本 ticket 最重要的一條,不能只靠讀程式碼下結論。
- 若發現 `navigation_failed()` 在某些情境下**不會**被觸發(例如 Shell 自己顯示了 "can't access" 頁面而回報導覽成功),明確記下來:那代表錯誤面板有覆蓋不到的情況,可能需要後續 ticket。
- 若有任何 Acceptance 因為沒有互動桌面而無法驗證,逐項明確標示「未驗證,需真實桌面」,不要猜測或編造結果。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-24 實作交接

#### 完成內容

- 最終採用自註冊的輕量 `PaneDock.ErrorPanel` child window 作為每個 `ExplorerHost` 的錯誤容器，內含原生 multiline `STATIC` 與 `BUTTON`。相較把兩個控制項直接掛在 app-shell 主視窗，容器能以單一 HWND 完整遮住 Shell view，並讓既有 `set_rect`／`set_visible`／`navigation_complete`／`destroy` 路徑維持一次搬移、顯示、隱藏與銷毀；沒有把錯誤 UI 所有權上移到 app_shell。
- `Retry` 的控制項 ID 最終為 `kRetryButtonId = 1`，ID 只在各自的 error-container `WM_COMMAND` 範圍內使用。容器以 `GWLP_USERDATA` 保存對應 `ExplorerHost*`；`BN_CLICKED` 呼叫 private `retry_navigation()`，先複製 `location_` 再呼叫既有 `navigate()`，沒有計時器、自動重試或輪詢。
- `navigation_failed()` 每次均以目前 `location_` 重設訊息，最終 UI 字串為 `This location is not available:\n<location>\n\nReconnect the drive or check the path, then retry.`。同一容器與兩個 child controls 會重用，不會在重試失敗時重複疊加 HWND；不同 pane 各有自己的 `ExplorerHost` 成員與控制項。
- `layout_error_controls()` 依 error container 的目前 client rect 與 `GetDpiForWindow` 排列訊息及 Retry 按鈕，並對極窄／極矮矩形夾限為非負尺寸。`set_rect` 搬移容器後重排 children；`set_visible` 顯示／隱藏容器；`focus` 在錯誤狀態聚焦 Retry；`navigation_complete` 沿用既有邏輯隱藏容器；`destroy` 銷毀容器（Win32 連帶銷毀兩個 children）並清空三個 HWND。
- `navigation_failed_callback_` 路徑未改動：首次失敗及 Retry 再失敗都仍通知 app_shell 的 `handle_navigation_failed`，因此既有 `suppress_history_record` 清旗標與 navigation chrome refresh 保持有效。沒有修改 core、session schema、ExplorerHost 公開簽章、`docs/tickets.md` 或 `docs/roadmap.md`。
- `tests/unit/explorer_host_lifetime_check.cpp` 現在連結真實 `panedock_explorer_host`，在 hidden parent 下初始化 host，再以保證不合法的 `?:\\PaneDock-PD-022-definitely-not-there` 呼叫 `navigate()`；self-check 驗證 `location()` 仍精確等於請求字串，之後 `destroy()` 使 `live_view_count() == 0` 且 parent 不再有 child HWND。這是 FR-012「失敗後保留目前請求」可自動驗證的 runtime 部分。

#### Acceptance 與驗證結果

- Acceptance 1（真實網址列輸入後顯示 location 與 Retry）：**未驗證，需真實桌面**。靜態路徑確認 `submit_address -> ExplorerHost::navigate -> navigation_failed` 會更新並顯示面板，但未冒充視覺／輸入驗收。
- Acceptance 2（實際關閉及重開 PaneDock 後仍嘗試相同 location）：**未驗證，需真實桌面**。本 session 沒有用真實 UI 輸入失敗 location、正常 `WM_CLOSE` 寫入 session，再重開觀察；因此沒有把 source inspection 或上述單次 host self-check 當成 restart round-trip 結果。Reviewer 必須照此步驟驗證，這仍是本 ticket 最重要的人工項目。
- Acceptance 3（點 Retry、再次失敗且不疊加／崩潰）：**未驗證，需真實桌面**。程式碼重用同一組 HWND 並重新呼叫相同 location，但未執行真實按鈕點擊。
- Acceptance 4（目的地恢復後 Retry 成功並隱藏面板）：**未驗證，需真實桌面**。成功 callback 的 hide 路徑保留，但未實際切換磁碟機／資料夾可用性。
- Acceptance 5（splitter、layout、Group、tab、DPI 後矩形）：**未驗證，需真實桌面**。所有既有 layout 入口仍經 `set_rect`，且子控制項會同步重排，但未宣稱視覺通過。
- Acceptance 6（關閉後無 orphan window）：self-check 通過；顯式 `host.destroy()` 後 `live_view_count() == 0` 且 `GetWindow(parent, GW_CHILD) == nullptr`。完整 app 的真實 WM_CLOSE 視覺觀察仍未執行。
- Acceptance 7（兩 pane 同時顯示各自 location）：**未驗證，需真實桌面**。instance state 靜態上互相獨立，但未冒充雙 pane 視覺驗收。
- 尚未能觀察 Shell 是否會在某些目的地自行顯示「can't access」頁面卻回報 navigation complete，因為沒有互動桌面；此覆蓋缺口狀態為**未驗證，需真實桌面**，Reviewer 應特別檢查網路磁碟、USB 與 OneDrive 類型。
- LLVM-MinGW configure：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` 通過。
- `cmake --build build`：通過，無新增警告。
- `ctest --test-dir build --output-on-failure`：3/3 通過（`panedock_core_model`、`panedock_core_layout`、`panedock_core_session`）。
- `build\\panedock_explorer_host_lifetime_check.exe`：`PASSED: explorer_host_lifetime_check`。
- `rg -n "windows\\.h|HWND|IUnknown" src/core`：無輸出（rg exit 1，符合預期），core 邊界未污染。
- 啟動 smoke check：本 session 啟動自己的 `build\\PaneDock.exe`，等待 2 秒後 `Responding=True`、`HandleCount=772`，隨後只終止該測試 process；這不是互動驗收。
- 工作開始前既有未追蹤 `.claude/` 未觸碰；未 commit。

### 2026-08-24 最終檢查補記

- 上述交接追加完成後再次執行完整 configure／build／CTest／self-check／core boundary grep，結果不變且全數通過；`git diff --check` 通過。最終 tracked diff 為 `src/explorer_host/explorer_host.h/.cpp`、`tests/unit/explorer_host_lifetime_check.cpp`、該 self-check 所需的 `CMakeLists.txt` link wiring，以及本 ticket 的 append-only 交接區。
