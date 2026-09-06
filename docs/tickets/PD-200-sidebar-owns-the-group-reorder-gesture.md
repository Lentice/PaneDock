# PD-200 — group list 的重排手勢收進 `sidebar::Sidebar`

Phase: 7
Depends on: PD-057（LISTBOX 選取與拖曳的訊息順序）、PD-196（`PaneTabStrip` 的同構前例）

## 為什麼是這一刀

使用者要求繼續拆 `main.cpp`。PD-191～198 已經窮盡了「哪些該搬進 `Pane`」，剩下的 4,609 行確實全是協調層的——所以本票問的是**另一個問題**：協調層自己有沒有可以獨立出去的模組。這不是重開 PD-197 的界線判定，那份界線談的是 pane 歸屬，沒有談協調層的內部切分。

盤點後只有一處通過「刪掉它會讓複雜度集中，還是只是搬家」：**group list 的重排拖曳**。理由：

- 它需要的外部資訊只有 group 的數量與 id，而 `Sidebar` 已經以 `groups_` 持有這兩者。其餘全是 listbox 本身的滑鼠狀態。
- `Sidebar` 已經擁有那個 HWND、hover index、rename 的 subclass proc 與 `draw_item`。重排拖曳是同一類東西，只是被留在外面。
- 與 PD-196 把 tab strip 收進 `PaneTabStrip` 完全同構。

**明確評估後不做的兩處**（避免下一個 session 重跑同樣的盤點）：

- **Group CRUD 協調**（`activate_group`／`add`／`duplicate`／`delete`／`move_group`，約 500 行）：它們呼叫 `apply_layout`、`rebind_panes`、`navigate_realized_panes`、`capture_locations`、`refresh_tab_strips`、`ShellCallScope`、`state.panes[].host().focus()`。抽成型別必須吃下幾乎整個 `AppState`，介面不會變窄——這正是淺模組。它們本來就是協調工作。
- **版面幾何**（`layout_rects`／`splitters`／`apply_layout`／`navigation_geometry`，約 850 行）：與 `DeferWindowPos` 的 parent-scoped 批次契約糾纏，PD-155 的 atomic live resize 與 PD-187 的風險紀錄在案，且無法自動化驗證。行數最多、收益最差、風險最高。

## 綁定約束

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> **Keep `src/core` free of HWND, COM and `windows.h`.** It is the only automated test seam in this project.

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

PD-057 的訊息順序（原註解逐字保留於新位置，**不得改寫**）：LISTBOX 在 `WM_LBUTTONDOWN`／`WM_LBUTTONUP` 之間跑自己的 capture-based 點擊追蹤，提早取得 capture 或提早釋放 capture 都會讓它送出 `LBN_SELCANCEL` 而不是 `LBN_SELCHANGE`，Group 就再也切不動。

## Scope

**1. `Sidebar` 吃下拖曳狀態機**

- `AppState::GroupDrag` 移除，成為 `Sidebar` 的私有 `Drag`。`AppState` 少一個欄位。
- `group_item_at_point`／`cancel_group_drag`／`update_group_drag`／`finish_group_drag` 成為 `Sidebar::item_at_point`／`cancel_drag`／`update_drag`／`finish_drag`。`item_at_point` 改用 `groups_.size()`／`groups_[i].id`，不再需要 `AppState`。
- 新增 `Sidebar::handle_list_message(HWND, UINT, WPARAM, LPARAM) -> std::optional<LRESULT>`：回傳值代表已處理（必要時它自己呼叫 `DefSubclassProc`，以保住 PD-057 的順序），`nullopt` 代表呼叫端往下傳。

**2. 提交仍是協調層的事**

- 新增 `struct GroupReorder { std::string group_id; std::size_t target_index; }` 與 `Sidebar::take_reorder_request()`。`Sidebar` **只回報**完成的手勢，`core::reorder_group` 與 `schedule_session_save` 由 `group_list_proc` 執行。理由：session 存檔與 model 變更是協調層服務，`Sidebar` 拿不到也不該拿到 `ApplicationState&`。
- `group_list_proc` 只剩下協調層工作：shutdown／Shell 重入兩道閘、`WM_NCDESTROY` 的 subclass 拆除、套用 reorder。

**3. `draw_item` 變深**

- `draw_item(item, group_index, placeholder)` → `draw_item(item)`。拖曳中的投影順序（`core::reorder_source_index`）與插入佔位改由 `Sidebar` 自己算——呼叫端沒有理由知道拖曳存在。`draw_global_control` 少 11 行。

## Non-goals

- 不動 Group CRUD、`activate_group`、`rebind_panes`、`apply_layout`（見上方理由）。
- 不把 `core::reorder_group` 或 session 存檔搬進 `Sidebar`。
- 不改 PD-057 的訊息順序或任何一行既有註解的語意。
- 不動 sidebar 的 drag-hover drop target（`make_sidebar_drag_hover_target`）——那是 OLE 拖放，與本票的滑鼠重排是兩件事。
- 不改 `docs/design-spec.md`：零行為、零視覺變更。

## 驗收

1. `AppState` 不再有 `group_drag` 欄位；`main.cpp` 不再有 `group_item_at_point`／`*_group_drag` 任何一支。
2. `Sidebar::draw_item` 只吃 `DRAWITEMSTRUCT*`。
3. 重排完成後由協調層套用並走 debounce 存檔，不得同步 `save_now`。
4. PD-057 的兩段註解與其描述的呼叫順序逐字保留。
5. 既有 build 與完整 CTest 通過，未關閉或弱化任何檢查。

## Agent 檢查

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`tests/release/shutdown_state_check.ps1` 的 `finish_group_drag` 斷言改指向新位置，並**加強**：除了原本的「有 `schedule_session_save`、無 `save_now`」，另外要求 `group_list_proc` 同時出現 `sidebar.take_reorder_request()` 與 `core::reorder_group(`——把「Sidebar 回報、協調層提交」這個新的分工釘住，防止日後有人把 model 變更塞進 `Sidebar`。

## 交接區

**結果**（2026-09-06）：`main.cpp` **4,609 → 4,470 行（淨減 139）**，`sidebar.cpp` 307 → 466。`AppState` 少一個欄位，`Sidebar` 的公開介面淨增 3 支（`handle_list_message`、`take_reorder_request`、`cancel_drag`），`draw_item` 由 3 參數降為 1。LLVM-MinGW Release build 無新增警告，CTest **24/24 通過**。

**沒有新增單元測試，這是必須說明的缺口**：拖曳狀態機綁在 LISTBOX HWND 上（`LB_ITEMFROMPOINT`、`SetCapture`、`TrackMouseEvent`），`src/core` 不得含 HWND，專案也沒有 sidebar 的測試 target。唯一已在 `core` 且被測到的部分是 `reorder_source_index`（`core_model_test`），本票沒有改它。替代驗證是上述強化後的來源掃描檢查，**它檢查的是結構不是行為**。

**因此以下必須人工驗證，本次未做**：(a) 單擊切換 Group 仍然有效（PD-057 的回歸面——若 capture 順序被破壞，症狀是點了沒反應）；(b) 拖曳重排時的投影順序與插入佔位繪製正確；(c) 拖出清單範圍、放開右鍵、Alt-Tab 奪走 capture 三種取消路徑；(d) 拖曳中觸發 Shell 重入或關閉視窗。

**一個實作上的陷阱**：`Sidebar::handle_list_message` 在 `WM_LBUTTONDOWN` 與 `WM_LBUTTONUP`（非拖曳）兩個分支裡**自己呼叫 `DefSubclassProc`**。這看起來像是把 subclass 的責任洩漏進 `Sidebar`，但 PD-057 要求的正是「控制項先看到訊息、我們後處理」，這個順序無法用「回傳 nullopt 讓呼叫端處理」表達——呼叫端只能在我們之後或之前呼叫，不能在中間。`DefSubclassProc` 在 subclass proc 的呼叫堆疊內從任何函式呼叫都合法。**不要**為了讓介面看起來乾淨而把它改回單一回傳點，那會直接破壞 Group 切換。

**過程中的一個自傷紀錄**：本次以 Python 重寫檔案時，文字模式輸出把 LF 轉成了 CRLF，讓 `git diff` 顯示整個 `main.cpp` 被改寫。已用 `sed -i 's/\r$//'` 還原，最終 diff 為 188 行。往後對這個 repo 做整檔改寫要用 `newline=''` 或直接用行內編輯工具。

**下一刀的候選與其風險**：`SessionWriter`（`save_now`／`schedule_session_save`／`cancel_session_save_timer`／`flush_session_file`／dirty flag／clean marker，約 100 行，tracker 2026-09-01 已列為候選）。它的接縫比本票窄，但 `shutdown_state_check.ps1` 用來源掃描釘住了 5 個同步 `save_now` 呼叫點與 timer 分支的確切形狀，而那份檢查是 shutdown 正確性的安全網。重構它必然要重寫那段檢查，**風險在於一邊改實作一邊改安全網**。本票刻意不與它同批進行。
