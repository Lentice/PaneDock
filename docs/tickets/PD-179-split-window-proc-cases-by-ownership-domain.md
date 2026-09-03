# PD-179 — 把 `window_proc` 的巨型 case 依「所有權域」拆成具名函式

Phase 7 · architecture · Depends on: PD-178

- Source: 同 PD-178（2026-09-03 使用者重構需求）。opencode 獨立審查特別指出：若只是把兩個巨型 case 各搬成一個大函式，後續 `Pane` 型別成形時還要再拆一次，這一步就白做——因此本票的拆分軸線**必須是所有權域**（pane 域／sidebar 域／全域），不是 case 邊界。
- Priority: HIGH——`window_proc` 1084 行是這份程式碼最難導覽的單一區塊。

## Outcome

`window_proc` 從 1084 行降到 400 行以下，做法是把四個巨型 case 的內容依**所有權域**抽成具名函式：屬於某個 pane 的、屬於 sidebar 的、屬於整個視窗的。抽出後 `window_proc` 只剩訊息路由與既有的 shutdown／reentry 前置閘門。

行為與視覺零變更。

## 已確認的現況（2026-09-03 工作樹）

- `window_proc` 在 `src/app_shell/main.cpp:5204-6288`，**1084 行**，`switch (message)` 在 `5255`，共 **24 個** case。
- `5204-5254` 是 switch 之前的 shutdown 閘門與 shell-reentry 延遲閘門，會在 dispatch 前攔截訊息。**這段不得移動、不得改變攔截順序。**
- 四個過大的 case：
  | case | 行範圍 | 行數 |
  |---|---|---|
  | `WM_CREATE` | 5256-5457 | 約 200 |
  | `WM_COMMAND` | 5860-6069 | 約 210 |
  | `WM_DRAWITEM` | 5571-5699 | 約 128 |
  | `WM_CONTEXTMENU` | 5757-5859 | 約 103 |
- PD-178 完成後，`WM_DRAWITEM` 與 `WM_COMMAND` 的 pane 分派已收斂成各一行 + 一張表，兩者實際剩餘量會下降；本票以 PD-178 之後的樹為準重新量測。
- 五支 PowerShell 測試以 `-SourcePath "${PROJECT_SOURCE_DIR}/src/app_shell/main.cpp"` 直接掃 `main.cpp`（`tests/CMakeLists.txt:44-83`）。其中多數是正向斷言（`-notmatch` 即 throw），程式碼搬走會**直接變紅**；但下列四條是**反向斷言**，其守護的區塊若整段消失會靜靜地永遠通過：
  - `tests/release/address_bar_failure_check.ps1:23`
  - `tests/release/startup_frame_order_check.ps1:69`
  - `tests/release/startup_frame_order_check.ps1:80`
  - `tests/release/startup_frame_order_check.ps1:95`
  這些腳本以 `$source.IndexOf('void handle_navigation_failed(')` 之類的函式名字串切出函式體再比對，所以**函式一旦改名或移檔就失效**。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Read the relevant spec section and trace every caller before touching shared code.

`AGENTS.md`：

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`：

> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes.

`docs/design-spec.md` §9.1：

> | `app_shell` | WinMain、STA 初始化、訊息迴圈、主視窗、命令路由 | 資料模型計算、Shell 呼叫 |

`docs/design-spec.md` §9.4：關機順序（capture state → destroy 所有 live `IExplorerBrowser` → destroy pane HWND → destroy 主視窗 → 離開訊息迴圈 → `CoUninitialize`）

> 順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。

`docs/tickets.md` PD-057 記錄的地雷：

> 在把訊息交給 `DefSubclassProc` 之前就呼叫了 `finish_group_drag`,而後者會 `ReleaseCapture()`

`docs/tickets.md` Agent 交付規則：

> 預設每個 ticket 的實作範圍為半天至兩天;若超過,先拆 ticket。

## Files to read and trace first

- `src/app_shell/main.cpp:5204-6288`：整個 `window_proc`，特別是 switch 之前的兩道閘門。
- `src/app_shell/main.cpp:5256-5457`：`WM_CREATE`，注意 `startup_frame_only`（`AppState:535`）造成的「只建 frame、Shell 工作延後」分段。
- `tests/release/startup_frame_order_check.ps1`：它切出 `WM_CREATE` 前置段與 deferred case 的方式。
- `tests/release/address_bar_failure_check.ps1`：它切出 `handle_navigation_failed` 函式體的方式。
- `tests/release/shutdown_state_check.ps1`、`shell_reentry_gate_check.ps1`、`single_instance_relay_check.ps1`：確認它們比對的字串在搬移後仍存在。
- `src/app_shell/pane_control_id.h`（PD-178 產出）。

## Scope

1. 依**所有權域**抽出具名函式，全部留在 `main.cpp` 的匿名 namespace（本票不新增檔案）：
   - `handle_pane_command(AppState&, PaneControlId)`：pane 域的 `WM_COMMAND` 處理。
   - `handle_sidebar_command(HWND, AppState&, int id)`：sidebar 按鈕、Group 清單通知。
   - `handle_global_command(HWND, AppState&, int id)`：版型按鈕、view mode 選單、pinned 選單、tab 右鍵選單命令。
   - `draw_pane_control(const DRAWITEMSTRUCT&, AppState&, PaneControlId)` 與 `draw_global_control(const DRAWITEMSTRUCT&, AppState&)`。
   - `handle_context_menu(HWND, AppState&, POINT screen)`。
   - `create_main_window_children(HWND, AppState&)`：`WM_CREATE` 中建立子視窗的部分。**`startup_frame_only` 的分段語意與順序原樣保留**，不得把延後的 Shell 工作提前。
2. `window_proc` 的每個 case 縮成「解析參數 + 呼叫一個具名函式 + return」。
3. **switch 之前的 shutdown 閘門與 shell-reentry 延遲閘門（`5204-5254`）原地不動**，不抽、不改順序。
4. 同步更新受影響的 PowerShell 測試：若某腳本靠函式名字串切段，而該段已改名或移動，就更新腳本使其重新指向正確位置。**不得**以放寬 pattern 的方式讓它「剛好還是綠的」。
5. 對上列四條反向斷言，各執行一次「故意破壞被守護的不變量、確認該測試變紅、再還原」，把命令與輸出寫進交接區。

## Non-goals

- 不搬任何函式到新檔案（PD-180 起處理）。
- 不搬 `AppState` 欄位。
- 不改變任何訊息的處理順序、回傳值或 `DefWindowProcW` 的落點。
- 不動 `tab_strip_proc`、`group_list_proc`、`address_edit_proc` 等 subclass proc。
- 不註冊新的 window class。
- 不重開任意遞迴 pane 分割。

## Acceptance Criteria

1. `window_proc` 行數 < 400。
2. `main.cpp:5204-5254` 的兩道閘門在 diff 中未被修改。
3. 五支 PowerShell 測試全綠；且四條反向斷言各有一份「故意破壞會紅」的證據寫在交接區。
4. `ctest --test-dir build --output-on-failure` 全綠，含 `panedock_launch_smoke`。
5. 視覺與行為零變更。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# window_proc 行數
$s = Get-Content src/app_shell/main.cpp -Raw
$start = $s.IndexOf('LRESULT CALLBACK window_proc(')
$end = $s.IndexOf('bool register_window_class(', $start)
(($s.Substring($start, $end - $start)) -split "`n").Count
```

```powershell
git diff --check
```

## 交給使用者的實機檢查清單

1. 啟動：確認啟動通知、初次畫面順序與既有 build 一致（`WM_CREATE` 分段的回歸點）。
2. 四宮格 Group：每個 pane 的全部按鈕、位址列 Enter、tab 右鍵選單、pane 內容區右鍵選單（Shell 原生選單）。
3. Sidebar：新增／複製／刪除／上移／下移 Group，重新命名。
4. 版型按鈕列：1→2→3→4→1；view mode 選單；pinned 選單。
5. `WM_DPICHANGED`：把視窗拖到不同 DPI 的螢幕，確認所有 pane 正確縮放。
6. 關閉：確認乾淨結束、無殘留 process。

## Handoff requirements

在 `## 交接區` 記錄：`window_proc` 前後行數、抽出的函式清單與各自行數、四條反向斷言的「故意破壞會紅」證據（命令與輸出）、以及使用者實機檢查的回報結果。
