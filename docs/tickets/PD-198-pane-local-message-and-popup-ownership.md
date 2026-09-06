# PD-198 — 剩餘 pane 局部訊息與 popup 行為歸位

Phase 7 · architecture · Depends on: PD-197

- Source：PD-197 的全檔複驗發現下列單 pane 行為未列入 PD-191～197 的搬移表；依 PD-197 Scope 4 另開票，不擴大該票實作。
- Priority：MEDIUM。預估 1–2 天；僅收斂既有所有權，零行為、零視覺變更。

## Outcome

協調層選出目標 pane、維護全域選單狀態及重入閘門後，局部 EDIT 行為、container 裁切與 popup 選項執行交由 `Pane`。`main.cpp` 不再直接實作這些局部行為。

## 必讀與呼叫鏈

- `AGENTS.md`、`docs/tickets.md` 的 2026-09-05 PD-197 全檔複驗、`docs/development.md`。
- `docs/design-spec.md` §FR-002、§FR-004、§FR-005、§FR-010、§NFR-004、§9.4。
- PD-193／PD-195／PD-196／PD-197 交接區；不修改已完成票據。
- `src/app_shell/main.cpp`：`address_edit_proc` 與 `create_main_window_children` 的 subclass 接線；`address_bar_background_brush`／`release_address_bar_background_brush` 與 `handle_pane_control_message` 的 `WM_CTLCOLOREDIT`；`apply_container_region` 與 `apply_layout` commit 後的唯一呼叫；`apply_ui_font` 的 folder-context 字型設定；`handle_global_command` 的 view-mode／pinned／multi-tab-close 分支；`handle_context_menu` 的 tab 分支；`window_proc` 的 deferred-command/shutdown 閘門。
- `src/app_shell/pane.{h,cpp}`、`pane_host.h`、`pane_message_dispatch.h`、`pane_tab_strip.{h,cpp}`、`pane_control_id.h`、`window_helpers.{h,cpp}`。
- `src/core/model.{h,cpp}` 的既有 tab 操作、`src/shell_core/shell_core.h` 的 view-mode 表。
- `tests/unit/pane_test.cpp`、`pane_tab_strip_test.cpp`，`tests/release/address_bar_failure_check.ps1`／`shell_reentry_gate_check.ps1`／`pane_paint_ownership_check.ps1`／`shutdown_state_check.ps1`，以及 `tests/CMakeLists.txt`。

先用 `rg` 追完每支被改函式與 ID 常數的所有 caller，再修改共用入口。

## Scope

1. `address_edit_proc` 移入 `pane.cpp` 匿名 namespace，subclass reference data 使用 `Pane*`，Enter 仍呼叫 `submit_address()`；首次 click 全選、`WM_CHAR` 吞 Enter、`WM_NCDESTROY` 移除 subclass 的順序不變。接線由 `Pane::create` 接手，保留建立失敗回報，不能只搬函式卻繼續持有 `AppState*`。
2. 位址列 `WM_CTLCOLOREDIT` 與其 brush 由 pane 擁有的繪製路徑處理；RGB(251,252,253) 背景、RGB(76,89,107) 文字及 OPAQUE 不變。brush 維持一個共享資源，主視窗結束時釋放一次，不引入每 pane 複本。`Pane::apply_font` 接手目前漏在 `apply_ui_font` 裡的 `folder_context_button` 字型設定。
3. `apply_container_region(HWND,int,int,int)` 改為 `Pane::apply_container_region(int width,int height,int radius)`。函式體使用自己的 container，保留 `CreateRoundRectRgn`＋頂部矩形 `RGN_OR`、全部錯誤清理與 `SetWindowRgn(..., FALSE)`。`apply_layout` 只做 member call 機械替換：batch commit、region 與 invalidate 的原順序不動。
4. view-mode（360–391）與 pinned（500–771）仍由主視窗解碼目標 pane，保持現有 `WM_COMMAND` 重入延後。實際選取 view-mode、固定／自訂 pinned location 導覽與 Add Current Folder 交由 `Pane::handle_command(int)`。Manage Pinned Locations 的 app 對話框與資料更新留在 `handle_global_command`；不得新增 PaneHost 的開對話框服務。ID 範圍、64 項上限、menu 順序及 owner 均不變。
5. tab 右鍵選單的建構可成為 `int Pane::show_tab_context_menu(const std::string& tab_id, POINT screen)`；保留 main-window owner 與回傳 command ID。協調層仍以 HWND 找 pane，仍在開選單前設定 singleton `tab_context_menu_pane`／`tab_context_menu_tab_id`，仍在 cancel／dispatch 時清空，仍透過主視窗 `WM_COMMAND` 執行選定命令。Close Other／All／Right 的 tab-id 快照與逐 tab 關閉成為 `Pane::close_tabs(const std::string& tab_id, int command)`，沿用 `close_tab()`；不持有跨 mutation 的 `TabState*`。Close Tab／Other／All／Right ID 保留 780–783，不改 close-last-tab fallback。
6. 更新受搬移影響的 source checks，對負向斷言確認「區塊缺失」會失敗。加一個 focused runnable check，涵蓋多 tab 關閉的保留集合與最後 tab fallback，或 EDIT 真實 child HWND 的 Enter／全選行為；沿用現有測試設施，不新增框架。

## Non-goals 與既有決定

- 這是補齊 PD-197 明列 scope 以外的局部行為，不覆寫 PD-197 的 non-goals，也不承諾 3,300 行。不得刪註解、壓行或抽走全域程式碼湊數。
- `apply_layout`／layout 矩形／parent-scoped batch、`NavigationGeometry`、`inset_rect`、Group、startup／shutdown／DPI 分派、全域按鍵與滑鼠路由不搬。
- `perform_clipboard_paste` 的進度／取消／延後關閉仍由 app 協調；folder-context 的 active-pane 切換與 Shell menu owner 也維持 PD-197 決定。
- 四個 singleton（tab context pane/id、location-capture suppression、hovered-button）不搬成 per-pane；tab drag、`tab_strip_proc` 的跨 pane 部分及 `tab_drag_layout` 不搬。
- `tab_display_text` 是 PD-196 明列的 coordinator service，維持不變。`core`、Shell/file-operation engines、session schema 不變。不新增 PaneHost 服務、timer、dependency、UI 功能或效能最佳化。

## Binding constraints — quoted

`AGENTS.md`：

> The coordinator owns what spans panes — cross-pane tab drag, splitter drag, active-pane selection, layout rects, whole-window painting; `Pane` owns what is its own.

> Read the relevant spec section and trace every caller before touching shared code.

> Keep `src/core` free of HWND, COM and `windows.h`.

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`docs/development.md`：

> Make the smallest change that satisfies the acceptance criteria. Reuse before adding.

> Add one focused runnable test or self-check for new non-trivial logic. If the logic is not in `core`, say in the ticket's 交接區 why it could not be, and what manual check replaces it.

`docs/design-spec.md` §FR-004：

> 拖曳中的更新只重排幾何,內容重算與持久化在拖曳結束時處理。

`docs/design-spec.md` §FR-005：

> 每個 pane 至少一個 tab。

`docs/design-spec.md` §9.4：

> 順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。

## Acceptance criteria

1. 上述 EDIT、brush、container region、popup 選項與批次 tab-close 行為由 Pane 擁有；main 只留必要協調與窄分派。
2. popup owner、command ID、deferred dispatch、singleton 生命週期、字型／色值／裁切與關閉順序均不變。
3. `Pane`／`PaneTabStrip` 不持有 `AppState*`、其他 pane、Group 或 tab 複本；無新增 PaneHost 成員。
4. 全部既有 CTest（含 launch smoke）與 focused check 通過；scope 以外函式保持原樣。列出剩餘單 pane 行為與原因，不假稱 main 無 pane 相關程式碼。

## Agent checks

```powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

再啟動真正的 `build\PaneDock.exe`，正常 `WM_CLOSE` 後 5 秒內退出、無殘留，diagnostic 最終 `live_view_count=0`。使用正常可寫 session 的執行環境；禁止強制 kill 當作關閉成功。

## 使用者實機檢查

1. 四 pane 位址列首次 click 全選，輸入路徑 Enter 正常；失敗路徑保留使用者文字。
2. 各 pane view-mode／pinned（固定、自訂、新增、管理）及 tab 四種 close 命令正常。
3. resize／不同 DPI 的 container 頂部方角、底部圓角、footer 字型及位址列背景不變。
4. popup 開啟期間切換／關閉與重入路徑，及最後 tab 關閉、重開還原無回歸。

## 交接區

（實作者填寫：局部函式與 caller 歸屬、選單與 singleton 保留證據、focused check／完整 CTest／生命週期結果、實機未驗項目。）
