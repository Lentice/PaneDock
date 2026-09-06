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

### 實作與 caller 歸屬（2026-09-06）

| 行為 | 最終歸屬與接線 |
|---|---|
| EDIT subclass | `pane.cpp` 匿名 namespace 的 `address_edit_proc` 以 `Pane*` 作 reference data；`Pane::create` 在 child 建立完成後安裝，失敗走 `destroy()`／`false`，既有 main caller 仍回 `-1` 並顯示 startup UI 建立失敗。首次 click 的 SetFocus → EM_SETSEL、Enter → submit_address、WM_CHAR 吞 Enter、WM_NCDESTROY 移除 subclass → DefSubclassProc 順序保留。 |
| 位址列色彩／brush | `handle_pane_control_message` 保留原 shutdown／re-entry gate，`WM_CTLCOLOREDIT` 窄分派至 `Pane::color_address_bar(HWND,HDC)`；後者判斷自己的 EDIT，設定原 OPAQUE、RGB(251,252,253) 背景與 RGB(76,89,107) 文字。匿名 namespace 仍只有一個 function-local static brush；main 的原 WM_DESTROY 位置呼叫一次 `Pane::release_address_bar_background_brush()`，沒有 per-pane brush 或 DPI 重建。 |
| container region | `Pane::apply_container_region(int,int,int)` 讀自己的 `explorer_container_`；原函式體與全部失敗清理保留。`apply_layout` 唯一 caller 僅改成 member call，main/pane/Explorer batches commit → region → invalidate 的順序不變，仍 `SetWindowRgn(..., FALSE)`。 |
| folder-context font | 原 `apply_ui_font` 迴圈仍呼叫 `Pane::apply_font`，folder-context child 字型設定成為其中最後一項；沿用既有 `set_font`。 |
| view-mode／pinned 選項 | `handle_global_command` 照原 ID 範圍選 pane，再呼叫 `Pane::handle_command(int)`。Pane 選 view-mode，處理固定／自訂位置導覽與 Add Current Folder；兩條 pinned 導覽仍透過既有 `ShellCall` 進入同一個 app gate。Manage 分支與原 bound/host/cache guard 留在 main，沒有新增開對話框的 host service。 |
| tab popup／批次關閉 | `Pane::show_tab_context_menu(tab_id,screen)` 查自己的 tabs，保留四項文字／次序／disabled 條件及 main-window owner，回傳 command ID。main 仍以 HWND/hit-test 選 pane/tab、開選單前設定 singleton、取消或建立失敗後清空、選定後送主視窗 WM_COMMAND；dispatch 先複製並清空 singleton。Close Tab 沿用 `close_tab`，Other/All/Right 交 `close_tabs` 先快照 ID 再逐一呼叫 `close_tab`，不跨 mutation 使用 TabState 指標。 |

view-mode 360–391、pinned 500–771（每 pane 68 slots、64 個自訂上限）、Close Tab/Other/All/Right 780–783 原值不變；close ID 常數從 main 移至 `pane.h`，main 以 using 引入同一份定義。view-mode 與 pinned menu 建構函式逐字不變，仍以 pane HWND 為 TPM_RETURNCMD owner 並送 command 回 main；tab popup 與 Shell folder-context 仍以 main HWND 為 owner。`window_proc` 的 deferred WM_COMMAND、關閉 gate 與四個 singleton 均未更改。

### 檢查與證據

- 指定 PATH：`E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja`。執行票面 `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`、`cmake --build build`，均通過、無編譯警告。
- 完整 `ctest --test-dir build --output-on-failure` **24/24 通過，8.51 秒**；`panedock_launch_smoke` 1.61 秒。使用正常可寫 session 的執行環境，沒有改用假 session 路徑或修改 shutdown。第一輪為 23/24：新增 paint check 的預設 `$PSScriptRoot` 路徑解析失敗；改由 `tests/CMakeLists.txt` 明確傳入 `-PaneSourcePath` 後重跑完整 suite 通過。
- 新增既有 `panedock_pane` executable 內的 `test_close_tabs_preserves_the_requested_set_and_last_tab_fallback`：四 tabs 關閉 Other 留 b、Right 留 a/b、All 留最後 d 並導覽 My Computer；驗證 active ID、保留 tab 完整資料、fallback history 清空、view/sort 不變、再次關閉最後 tab、Right 無目標／無右側、未知命令與 shutdown no-op，以及沿用 debounce save。這是 Pane 的 Win32／PaneHost 操作流程測試，不能移入保持 HWND/COM-free 的 core；沒有新增框架或假 ExplorerHost。
- `address_bar_failure_check` 保留 `Pane::navigation_failed`／`refresh_navigation_chrome` 錨點，加查 pane-owned EDIT 接線／Enter／全選／移除 subclass。`pane_paint_ownership_check` 加查 `Pane::color_address_bar`／`apply_container_region`；`shell_reentry_gate_check` 加查 Pane pinned 導覽 gate 與 main tab singleton／command 路徑；`shutdown_state_check` 加查 ID 快照委派 `close_tab`，不直接同步存檔。
- 以 build 下暫存 source 副本進行反證，未改動產品檔：刪除 navigation_failed、refresh_navigation_chrome、address_edit_proc、paint_client_background、color_address_bar、apply_container_region、handle_command、close_tabs 八個區塊，全數失敗；另將 SetWindowRgn 改 TRUE、移除 pinned 導覽 ShellCall、EDIT reference 改 AppState*，三者亦被拒絕。原有三個 bounds check 在起始錨點缺失時由 IndexOf 範圍例外拒絕，其餘走具名失敗；沒有「區塊消失而負向斷言靜默通過」。
- 額外啟動真正 `build\PaneDock.exe --diagnostic`（PID 36408），先確認 main HWND 與非零 live count，再按 PID/class 找到主視窗送正常 WM_CLOSE；**201 ms** 內退出、exit code **0**，診斷序列 **0 → 1 → 0 → 0**，最終 `live_view_count=0`。未強制 kill 或跳過 Destroy，結束後 `Get-Process PaneDock` 無殘留。這次 runtime session 只實現 1 個 view，不宣稱四 pane 實機矩陣已驗。
- 與 HEAD 比對，main 的 **124 支既有頂層函式體逐字一致**，修改的 7 支與移除的 4 支皆在 scope。另驗證 `apply_layout` 只替換 member call、`window_proc` 只替換 brush release。`src/core`、`shell_core`、`file_operations`、`PaneHost`、`PaneTabStrip`、window helpers 與 session schema 零 diff，無新增 host 成員／timer／dependency。
- `git diff --check` 通過。`main.cpp` 4,764 → 4,606 行；純記錄、不作本票驗收指標，也不據此修改 PD-197 的既有驗收落差。

### 留在 main 的 pane 相關行為與人工未驗項目

- popup 目標解碼、singleton 清理、WM_COMMAND 延後分派，以及 control message 的 shutdown／Shell re-entry guard：皆是 app-level 協調。
- folder-context 的 active-pane 切換／focus／main owner，`perform_clipboard_paste` 的 app transfer／取消／close-after-transfer：本票 non-goals，沒有搬入 Pane。
- `NavigationGeometry`／`inset_rect`／layout rectangles、parent-scoped batches、可見性／footer z-order 與整個 DPI/layout pass：跨 pane 的配置與提交時序。
- `tab_strip_proc` 的跨 pane drag／capture／gate、tab hit 路由、tab_display_text、共享 font／tooltip／hover，以及 Group CRUD/rebind、startup Shell callback／SHAutoComplete 接線、全域鍵盤滑鼠路由、session/shutdown：依本票及 PD-196／197 明列邊界保留，不假稱 main 已無 pane 相關程式碼。
- 使用者實機檢查 **1–4 未執行完整操作與視覺矩陣**：四 pane 位址輸入／首次 click、所有 popup 操作、不同 DPI 與裁切／字型色彩比對、menu 開啟中切換／關閉與重入、重開還原仍需人工確認。上述 source／資料／生命週期驗證不等同逐像素或完整互動 PASS；本票不推進 release gate。
