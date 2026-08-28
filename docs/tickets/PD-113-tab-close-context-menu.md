# PD-113 — Pane tab 右鍵選單:Close Tab / Close Other Tabs / Close All Tabs / Close Tabs to the Right

Phase 7 · app_shell · Depends on: 無

- Source: 使用者與 assistant 的 grilling session(2026-08-28)。
- Origin: 使用者要求在 pane 的 tab 上按滑鼠右鍵時彈出選單,提供關閉單一 tab 之外的批次關閉操作。
- Priority: MEDIUM——新增 chrome 互動,不改變核心 tab 模型或既有單鍵關閉行為,範圍小,可一次完成。

## 已確認的產品決策(grilling session 逐項紀錄)

1. **右鍵目標是命中的那個 tab,不一定是 active tab。** 使用者可以右鍵一個非目前作用中的 tab 並對它執行 Close Tab / Close Other Tabs / Close Tabs to the Right,`Close All Tabs` 則與命中哪個 tab 無關。
2. **四個選單項,由上到下:Close Tab、Close Other Tabs、Close All Tabs、Close Tabs to the Right。**
3. **右鍵點在 tab 外(空白區、捲動按鈕、`+` 新增按鈕)不彈出選單。**
4. **「關閉」語意完全沿用既有 `panedock::core::close_tab` 的不變量,不新增/不修改核心函式。** 該函式保證一個 pane 至少留一個 tab;對最後一個 tab 呼叫 close 時是把它的 location 重置成預設值,而不是真的從 vector 移除。`Close Other Tabs`/`Close All Tabs`/`Close Tabs to the Right` 都只是對既有 `close_tab_in_pane`(`src/app_shell/main.cpp:2476`)的多次呼叫,沒有例外語意。
5. **命令解析不採 PD-059 的「per-pane 編碼進 command id」慣例,改用暫存欄位存右鍵當下命中的 `pane_index` + `tab_id`,搭配 4 個固定 command id。** 理由:tab 數量是動態且無自然上限,不像 view-mode 選單只有固定 4 個選項;而選單顯示期間是 `TrackPopupMenu` 的 modal 迴圈,不會發生拖曳排序或使用者觸發的關閉(唯一可能重入的是 Shell 導覽完成回呼,只改動 tab 的 `location`/history,不影響 tab id 或順序),用字串暫存是安全的,程式碼也比替每個 tab 配一個 command id slot 更少。
6. **邊界時的選單項目用 `MF_GRAYED` 停用,不隱藏,比照 PD-028 Move Up/Down 的既有慣例：**
   - `Close Other Tabs`:pane 只有 1 個 tab 時灰階(沒有「其他」tab 可關)。
   - `Close Tabs to the Right`:命中的 tab 已經是該 pane 最後一個 tab 時灰階。
   - `Close Tab`/`Close All Tabs` 一律可用(對只剩 1 個 tab 的 pane 執行時,行為等同既有單鍵 Close 對最後一個 tab 的既有行為——重置成預設 location,不是新行為)。
7. **批次關閉前先把要關閉的 tab id 複製成快照(`std::vector<std::string>`)再逐一呼叫 `close_tab_in_pane`。** 因為 `close_tab_in_pane` 每次呼叫都會改變 `pane.tabs` 的內容與大小,不能在遍歷 `pane.tabs` 的同時直接呼叫它。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> App UI text must be English. No Chinese strings ship in the binary.

`AGENTS.md`:
> New non-trivial logic needs one focused runnable test or self-check.

`docs/tickets/PD-028-sidebar-brand-and-group-summary-restyle.md`(既有 `WM_CONTEXTMENU` 慣例,本票延續同一套模式)：
> 側邊欄 `LISTBOX` 新增 `WM_CONTEXTMENU` 處理:滑鼠右鍵座標轉成 client 座標...`TrackPopupMenu` 顯示...(邊界時 grayed)

## Files to read and trace first

- `src/app_shell/main.cpp:3793-3845`——現有 `WM_CONTEXTMENU` case,目前只判斷 `target == state->sidebar.window()`,其餘一律 `break`(落到 `DefWindowProc`)。本票要新增第二個分支判斷 `target` 是否為 `state.tab_strips[]` 其中之一。
- `src/app_shell/main.cpp:3091-3186`——`tab_strip_proc`(tab strip 的 `SetWindowSubclass` 回呼)。它處理 `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_MOUSELEAVE`/`WM_LBUTTONUP`/`WM_CAPTURECHANGED`,**沒有**攔截 `WM_RBUTTONDOWN`/`WM_CONTEXTMENU`——確認右鍵訊息會經 `DefSubclassProc` → 預設視窗程序冒泡到父視窗的 `WM_CONTEXTMENU`,與 Sidebar 走同一條既有路徑,不需要在這個函式裡新增右鍵處理。
- `src/app_shell/main.cpp:2744`起——`tab_item_at_point(const AppState&, HWND strip, POINT client_point)`,既有的 tab 命中判斷,`close_tab_at_point`(`main.cpp:2726-2738`)已示範同樣的「screen point → `MapWindowPoints` 轉 client 座標 → 呼叫 `tab_item_at_point`」寫法,本票直接照抄同一段轉換邏輯。
- `src/app_shell/main.cpp:2476-2492`——`close_tab_in_pane(HWND, AppState&, pane_index, tab_id)`,本票批次關閉的唯一呼叫對象,不修改其簽章或行為。
- `src/core/model.cpp:272-289`——`close_tab`,確認「pane 至少留一個 tab、最後一個 tab 重置成 default location」的既有不變量,本票完全依賴它,不新增核心函式。
- `src/app_shell/main.cpp:3819-3844`——Group 清單右鍵選單的完整 `TrackPopupMenu` 範本(`CreatePopupMenu`/`AppendMenuW`/`MF_GRAYED`/`SetForegroundWindow`/`TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, ...)`/`DestroyMenu`/轉發 `WM_COMMAND`),本票的選單建立與分派方式直接照抄這段結構。
- `src/app_shell/main.cpp:80-151`——既有 ID 常數區段盤點起點,新增常數前先 `rg -n "constexpr int k.*Id"` 找一段未占用區段(PD-111/112 也是同一狀態的 `ready` 票,尚未鎖定各自的號碼區段,施工時需重新盤點避免撞號)。
- `src/app_shell/main.cpp`(`AppState` 定義處,`rg -n "struct AppState"`)——確認新增兩個暫存欄位(pane index + tab id)放在哪裡合適,參考 `state.tab_drag`(`AppState::TabDrag`,`main.cpp:416`)這類「互動期間暫存狀態」欄位的既有寫法與型別選擇。

## Scope

1. `AppState` 新增兩個暫存欄位存右鍵選單當下命中的 pane index 與 tab id(例如 `std::optional<std::size_t> tab_context_menu_pane;` + `std::string tab_context_menu_tab_id;`,或包成一個小 struct——由實作 agent 依現有 `AppState` 風格決定,不強制型別)。
2. `WM_CONTEXTMENU` case 新增分支:`target` 屬於 `state.tab_strips[]` 時,轉 client 座標、呼叫 `tab_item_at_point` 找命中的 tab index;沒命中(空白區/scroll 按鈕/`+`)直接 `return 0`,不彈選單。
3. 命中後把 `(pane_index, tabs[index].id)` 存進步驟 1 的暫存欄位,`CreatePopupMenu` + `AppendMenuW` 建出四個項目(Close Tab / Close Other Tabs / Close All Tabs / Close Tabs to the Right),依決策 6 計算 `MF_GRAYED`,`TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, ...)` 取得 command,轉發 `WM_COMMAND`。
4. `WM_COMMAND` 新增四個 command id 的處理分支,讀回步驟 1 暫存的 `(pane_index, tab_id)`:
   - Close Tab → 直接呼叫 `close_tab_in_pane(window, state, pane_index, tab_id)`。
   - Close Other Tabs → 把該 pane 除了 `tab_id` 以外的所有 tab id 複製成快照,逐一呼叫 `close_tab_in_pane`。
   - Close All Tabs → 把該 pane 全部 tab id(含 `tab_id` 本身)複製成快照,逐一呼叫 `close_tab_in_pane`。
   - Close Tabs to the Right → 依 `pane.tabs` 目前順序找到 `tab_id` 的 index,把它之後的所有 tab id 複製成快照,逐一呼叫 `close_tab_in_pane`。
5. 四個 command 都在批次操作結束後沿用 `close_tab_in_pane` 內部既有的 `refresh_tab_strip`/`save_now` 呼叫,本票不額外重複呼叫。

## Non-goals

- 不修改 `panedock::core::close_tab`/`add_tab` 的簽章或不變量。
- 不新增鍵盤快速鍵觸發這四個動作(例如 Ctrl+Shift+W)——超出本票範圍,若之後需要另開票。
- 不處理跨 pane 的批次關閉(每個選單只作用在右鍵發生的那個 pane)。
- 不改變既有 tab 關閉按鈕(X)或 Ctrl+W 之類既有單鍵關閉路徑的行為。
- 不做 owner-draw 選單或圖示化選單項目——沿用 Group 選單同款的純文字 `TrackPopupMenu`。

## Acceptance

1. 右鍵點在任一 pane 的 tab 上彈出選單,四個項目文字為英文:`Close Tab`、`Close Other Tabs`、`Close All Tabs`、`Close Tabs to the Right`。
2. 右鍵點在 tab 條空白區、捲動按鈕、`+` 按鈕上不彈出選單。
3. Pane 只有 1 個 tab 時,`Close Other Tabs` 為灰階不可點擊。
4. 右鍵命中該 pane 最後一個 tab 時,`Close Tabs to the Right` 為灰階不可點擊。
5. `Close Tab` 對非 active 的 tab 執行時,只關閉該 tab,不影響其餘 tab 或目前 active tab。
6. `Close Other Tabs`/`Close All Tabs`/`Close Tabs to the Right` 執行後,pane 至少保留 1 個 tab(依 `close_tab` 既有不變量,最後一個 tab 被重置成預設 location 而非消失)。
7. 四個動作執行後 session document 立即反映變更(重啟程式後保留關閉結果)。
8. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
9. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kCloseTabId|kCloseOtherTabsId|kCloseAllTabsId|kCloseTabsToRightId|tab_context_menu" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:對某個 pane 開兩個以上的 tab,右鍵其中一個非 active tab,
# 確認選單四項文字與灰階狀態(最後一個 tab / 只剩 1 個 tab 時)符合 Acceptance 3-4,
# 逐項點擊驗證 5-7。本環境的 PrintWindow 截圖對這隻視窗可用;
# TrackPopupMenu 選單是獨立 top-level 視窗(class name #32768),
# 見 PD-059/PD-111 交接區記錄的既有做法擷取畫面。
```

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 新增的 ID 常數名稱與挑選的區段號碼,以及施工當下 `rg -n "constexpr int k.*Id"` 的盤點結果(供之後其他票避免撞號)。
- `AppState` 新增的暫存欄位型別與命名。
- 新增的 unit test / self-check 涵蓋範圍(Close Tabs to the Right 邊界、Close All 後 pane 仍保留 1 個 tab)。
- 互動式驗證(右鍵選單彈出、四個項目行為、灰階邊界)的實際完成度,若滑鼠模擬對這隻視窗失敗,依 PD-028 先例記錄為程式碼路徑核對 + 留給下一次真人操作的檢查項。

## 交接區

<!-- 實作 agent 填寫,append-only -->
