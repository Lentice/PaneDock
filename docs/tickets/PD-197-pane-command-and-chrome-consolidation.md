# PD-197 — pane 命令、chrome 與導覽完成的收尾

Phase 7 · architecture · Depends on: PD-192, PD-193, PD-194, PD-195, PD-196

- Source: 2026-09-05 使用者要求「main 中不要有屬於 pane 的獨立功能」。本票是整串的收尾與驗收票。
- Priority: MEDIUM。前六張全部完成後才有意義；提前做會反覆改同一批函式。

## Outcome

1. 前六張票留下的三個過渡點全部結清。
2. `handle_pane_command`／`draw_pane_control`／`refresh_status_bar`／`refresh_navigation_chrome` 收進 `Pane`。
3. 對 `main.cpp` 做一次全檔複驗，把「還有哪些屬於 pane 的東西」的答案寫進 `docs/tickets.md`，作為這條界線往後的判準。

## 要結清的三個過渡點

| 來源 | 過渡做法 | 本票的處置 |
|---|---|---|
| PD-192 Scope 3 | `handle_navigation_complete` 拆成 `Pane::record_navigation_result()`（已搬）＋約 8 行留在 `main.cpp`（因為要呼叫當時尚未搬的 `apply_pane_view_mode`／`apply_pane_sort`／`refresh_tab_strip`） | 三者已分別由 PD-193／PD-196 搬進 `Pane`，合併成完整的 `void Pane::navigation_complete(NavigationGeneration, const core::ShellLocation&)`，`main.cpp` 的殘留函式刪除 |
| PD-194 Scope 3 | `PaneHost::tab_strip_needs_refresh(Pane&)` | PD-196 已刪除；本票驗證它確實不存在 |
| PD-193／PD-195 | `TrackPopupMenu` 的 owner HWND 若當時決定保留主視窗參數 | 依兩張票交接區的記錄，統一處置並記錄最終決定 |

## 要搬的函式（2026-09-05 工作樹行號；前六張票完成後行號會位移，以函式名為準）

| 目前 | 行數 | 搬成 |
|---|---|---|
| `handle_pane_command(AppState&, std::size_t, int id)` `:4642-4684` | 43 | `bool Pane::handle_command(int id)` |
| `draw_pane_control(const DRAWITEMSTRUCT&, AppState&, ...)` `:4797-4824` | 28 | `bool Pane::draw_control(const DRAWITEMSTRUCT&)` |
| `refresh_status_bar(Pane&, AppState&)` `:1682-1716` | 35 | `void Pane::refresh_status_bar()` |
| `refresh_navigation_chrome(Pane&, AppState&)` `:1585-1598` | 14 | `void Pane::refresh_navigation_chrome()` |

`refresh_status_bar` 是 PD-190 明確標記為「因為要讀 Shell item count 而需要協調層服務」的那一支——`PaneHost::is_shutting_down()` 與 `ShellCall`（PD-191）已把該理由消掉。

## Scope

1. 依上表搬移，機械替換同 PD-192。
2. `handle_pane_command` 的分支在 PD-192～PD-196 之後已幾乎全是 `Pane` 成員呼叫；搬進去之後應是一個純 `switch`。若仍有分支需要協調層（例如開啟 app 層級對話框），**保留該分支在 `main.cpp` 的 `handle_global_command`**，不要為它在 `PaneHost` 新增成員。逐分支的歸屬決定寫進交接區。
3. `draw_pane_control` 依賴 `main.cpp` 的繪製輔助函式（`draw_navigation_icon_button` `:1260`、`draw_status_bar` `:1349`、`fill_rounded_rect` `:867`、`navigation_icon_font` `:1125` 等）。這些**只服務 pane 的子控制項**，一併搬進 `pane.cpp` 的匿名 namespace；若其中有函式同時被 sidebar／header 使用（`draw_sidebar_action_button` `:1321` 明顯是 sidebar 的，`fill_rounded_rect` 可能共用），則把共用的那幾支抽到 `src/app_shell/window_helpers.{h,cpp}`（該檔已存在）。逐支歸屬清單寫進交接區。
4. **全檔複驗**：搬完後對 `main.cpp` 做一次唯讀盤點，回答「還有哪些屬於 pane 的東西沒搬」。結果寫成 `docs/tickets.md` 的一則新條目，格式比照 2026-09-04 的「全檔盤點」條目：列出仍留在協調層的每一類與其理由。若發現真正該搬而本系列漏掉的，開新票，不要塞進本票。
5. 更新 `docs/tickets.md` §候選 的 tab strip 那一列（PD-196 已開票，加刪除線並註明），以及 2026-09-03 模組契約 (1) 的現況（PD-191 已覆寫其中一半）。

## Non-goals

- **不動**這些協調層的東西（本系列已確立的界線，寫在這裡作為往後的判準）：
  - 跨 pane／全域協調：tab 跨 pane 拖曳、splitter 拖曳、active pane 切換、`pane_at_point`、版型矩形（`layout_rects`／`pane_area`／`pane_content_area`）、`apply_layout`、`paint_client_background` 的全視窗迴圈、`layout_sidebar`／`layout_header`。
  - Group 域：`activate_group`／`add_group`／`duplicate_group`／`delete_group`／`move_group`／`rebind_panes`／`unique_group_id`／group list 的 proc 與拖曳。
  - app 生命週期：`wWinMain`、`window_proc`、shutdown 序列、single-instance、session 存檔、啟動通知、DPI 變更分派。
  - `core` 的 pane 域純函式——唯一的自動化測試接縫，`Pane` 只呼叫不吸收。
  - `shell_core`／`file_operations`——經確認零 pane-scoped 狀態，設計上就是 pane-agnostic。
  - singleton 狀態：`tab_context_menu_pane`／`tab_context_menu_tab_id`／`suppress_location_capture`／`owner_draw_hovered_button`。
- 不做效能最佳化、不改繪製演算法。
- 不趁機改任何視覺。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> **A `pane_index` parameter is a signal that the function belongs to that pane.** … The coordinator owns what spans panes — cross-pane tab drag, splitter drag, active-pane selection, layout rects, whole-window painting; `Pane` owns what is its own.

> Read the relevant spec section and trace every caller before touching shared code. A guard in the shared function is a smaller diff than a guard in every caller, and patching only the path the ticket names leaves sibling callers broken.

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

> Anything a later session needs must live in the repository, not in a scratchpad handoff.

## Acceptance criteria

1. 上表四支皆為 `Pane` 成員。
2. `Pane::navigation_complete` 是完整的一支，`main.cpp` 無殘留的 `handle_navigation_complete`。
3. `PaneHost::tab_strip_needs_refresh` 不存在。
4. `grep -n 'std::size_t pane_index' src/app_shell/main.cpp` 的結果**只剩**跨 pane 協調函式（`pane_at_point` 的回傳、`tab_strip_index`、拖曳三支、版型迴圈）。逐行清單寫進交接區並說明每一行為什麼合理。
5. `main.cpp` 從 5,975 行降到 3,300 行以下。
6. `docs/tickets.md` 已加入本系列的全檔複驗條目。
7. 零行為、零視覺變更。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

全部既有測試必須通過，包含 `panedock_launch_smoke`。

優雅關閉檢查（`AGENTS.md` 的 `IExplorerBrowser::Destroy` 規則）：

```powershell
$p = Start-Process build\PaneDock.exe -PassThru
Start-Sleep 3
$p.CloseMainWindow() | Out-Null
$p.WaitForExit(5000)
$p.HasExited   # 必須為 True，且無殘留行程
```

閒置檢查（NFR-001）：啟動後靜置 60 秒，工作管理員 CPU 為 0%、無磁碟 I/O。本系列不應引入任何 timer 或輪詢。

## 使用者實機檢查

本票是整串的驗收，清單較長：

1. 四種版型（1／2／3／4 pane）各切一輪，pane 內容正確。
2. 每個 pane 的六個導覽按鈕（上一頁／下一頁／上一層／重新整理／檢視模式／釘選）各點一次。
3. 資料夾內容選單按鈕（folder context）→ 原生 Shell 選單出現。
4. 狀態列的項目數量／選取數量在四個 pane 各自正確更新。
5. 切換 active pane → 只有作用中的 pane 顯示焦點外框。
6. 建立／複製／刪除／重排 Group，每次之後四個 pane 都正確重綁。
7. 拖曳 splitter 調整 pane 寬度。
8. 跨 pane 拖曳 tab、同 pane 重排 tab。
9. 把視窗拖到不同 DPI 的螢幕。
10. 複製一批檔案到另一個 pane（`IFileOperation` 進度中重入）。
11. 在檔案操作進行中按關閉視窗 → 延後關閉的對話框行為正確。
12. 關閉 App → 重開 → 四個 pane 的資料夾、tab、檢視模式、排序、捲動位置全部還原。
13. 關閉時無殘留行程、無 `IExplorerBrowser` 洩漏（`live_view_count` 診斷檔為 0）。

## 交接區

### 實作與三個過渡點

- 四支指定函式已收進 `Pane`：`handle_command(int)`、`draw_control(const DRAWITEMSTRUCT&)`、`refresh_status_bar()`、`refresh_navigation_chrome()`。`handle_pane_control_message` 的直接 caller 一併收窄為 `Pane&`；它仍在協調層維護原 shutdown/re-entry gate，不新增 PaneHost 服務。
- `record_navigation_result` 與協調層 `handle_navigation_complete` 合併為完整 `Pane::navigation_complete(NavigationGeneration, const core::ShellLocation&)`；順序仍為 generation/Group/tab identity 檢查 → history/location 記錄 → view mode → shutdown check → sort → shutdown check → strip refresh → debounce save。兩個舊函式均不存在。
- `PaneHost::tab_strip_needs_refresh` 確認不存在。PD-196 額外建立的 `refresh_navigation_chrome(Pane&)` 過渡 bridge 亦刪除，strip 直接呼叫 Pane 成員。PaneHost 現為 13 支服務（不含解構子），tracker 已同步。
- PD-193／PD-195 已把 view-mode／pinned popup 的 `TrackPopupMenu` owner 改為 pane 自己的 HWND；本票維持此決定。兩者皆 `TPM_RETURNCMD`，仍向 parent main window 發 `WM_COMMAND`，不改 ID、項目、位置、勾選或主視窗的延後分派。原生 folder-context 仍以主視窗作 owner，保留與 view site／全域焦點的現有契約；不將這種 Shell 選單混同為上述產品 popup。

### command 逐分支歸屬

| 分支 | 最終歸屬與理由 |
|---|---|
| Back | `Pane::handle_command` → `navigate_history(true)`，只碰本 pane 歷史。 |
| Forward | 同上 → `navigate_history(false)`。 |
| Up | 同上 → `navigate_up()`。 |
| Refresh | 同上 → `refresh_view()`。 |
| View | 同上，讀自己的 view button 螢幕矩形，呼叫 `show_view_mode_menu()`。 |
| Pinned | 同上，讀自己的 pinned button 螢幕矩形，呼叫 `show_pinned_locations_menu()`。 |
| Folder context | 依 Scope 2 留在 `handle_global_command`；直接 caller 傳 `Pane* source_pane`，只此分支使用。原 bound/realized/visible 檢查、`set_active_pane`、shutdown/active-index 複驗、focus、ShellCallScope、main owner 與 anchor 全部保留。它協調全域 active-pane，不能為移入 Pane 增加 host 服務。 |
| tab strip／address／未知 ID | Pane 回 `false`，原本沒有對應動作；外層保留已解碼控制通知的消費方式。 |

原已在 `handle_global_command` 的 view-mode/pinned 選項、Manage 對話框與 tab context command 未趁本票移動；局部選項行為的後續歸屬見 PD-198。

### 繪製 helper 逐支歸屬

| 函式／資源 | 最終歸屬 |
|---|---|
| `navigation_icon_font` | `pane.cpp` 匿名 namespace；原共用靜態 Segoe MDL2 font，不改每次建立條件或 glyph size。 |
| `release_navigation_icon_font` | `Pane` static 成員；協調層在原 `WM_DPICHANGED`／`WM_DESTROY` 兩處呼叫，維持一次全域 cache 釋放，不變成四份字型。 |
| `draw_navigation_font_glyph` | `pane.cpp` 匿名 namespace；函式逐字不變。 |
| `draw_navigation_fallback_glyph` | 同上，七種 fallback 的筆畫、幾何及資源清理逐字不變。 |
| `draw_navigation_icon_button` | 同上，函式逐字不變。協調層把既有 singleton hover 資訊 OR 進本次 `DRAWITEMSTRUCT` 副本的 `ODS_HOTLIGHT`，Pane 不取得或保存 hovered-button singleton。 |
| `draw_status_bar` | 同上，三段文字、分隔線、font、footer reserve 逐字不變。 |
| `scaled_value` | `window_helpers.{h,cpp}`；main 與新搬移的 pane 繪製共用，公式原封不動。 |
| `fill_rounded_rect`（main 版本） | 留在 main，caller 只有 header 的 `draw_layout_segment_background` 與 sidebar 的 `draw_sidebar_action_button`，沒有 pane caller。 |
| `fill_rounded_rect`（pane 既有版本） | 留在 `pane.cpp`；這是使用 stock `DC_BRUSH`／`DC_PEN` 的另一個實作，與 main 的 Create/Delete 版本不同。依「不改繪製演算法」不合併這兩支同名函式。 |
| `draw_sidebar_action_button`／layout/brand helpers | 留在協調層，服務 sidebar/header，非 pane 子控制項。 |

glyph、footer 色票及 pane-only metrics 搬入 pane 匿名 namespace；layout 仍需的 4/8/12px spacing、24px footer height、3px inset 保留原值。未建立新 palette、dependency、timer 或 cache policy。

### `main.cpp` 剩餘 `std::size_t pane_index` 完整逐行清單

以下為本票最終檔案的 `rg -n 'std::size_t pane_index' src/app_shell/main.cpp`，共 11 行；未用改名來隱藏命中。

| 行 | 所在位置 | 歸屬理由 |
|---|---|---|
| 485 | `AppState::TabDrag::pane_index` | 記錄跨 pane 拖曳來源，singleton coordinator state。 |
| 1792 | `apply_layout::LayoutFailure::pane_index` | 全體 layout pass 的首個失敗 pane 診斷；不是 pane-parallel 常駐資料。 |
| 2413 | `AppState::tab_drag_layout` | 比對來源及目標 pane，計算跨 pane placeholder；PD-191／196 明列保留。 |
| 2461 | `register_tab_drag_hover_targets` | 四 pane 註冊迴圈，共用 app drag lifetime 與 timer/message 分派。 |
| 2647 | `close_tab_at_point` | 從全視窗座標找目標 pane，再委派 close；PD-196 明列保留。 |
| 2802 | `tab_strip_paint_state` | 協調全域 active-pane 與跨 pane dragged/placeholder 資訊，以短期值交給 strip paint。 |
| 3282 | `perform_clipboard_paste` | app transfer/cancel/close-after-transfer 與 Shell-call gate；合理留在協調層，但超出票面 Acceptance 4 的列舉。 |
| 3561 | `handle_global_command` view-mode 解碼 | 全域 popup ID → pane 路由；局部選項處理已列入 PD-198，不在本票擴大實作。 |
| 3573 | `handle_global_command` pinned 解碼 | 同上，且 Manage 使用 app-level 對話框；局部導覽／pin 行為列入 PD-198。 |
| 4022 | `window_proc`／`kDragHoverMessage` | app timer ID → pane target，協調延後 OLE hover 執行。 |
| 4188 | `window_proc`／`WM_TIMER` | 同上，事件啟動的一次性 hover timer 分派，不是閒置 polling。 |

### 全檔複驗、行數與驗收落差

- `docs/tickets.md` 已新增整串的全檔複驗條目，列出跨 pane／Group／全域 chrome／startup／shutdown／session／DPI／file operation／singleton 的完整分類與理由；更新 tab-strip 候選現況與 2026-09-03 模組契約 (1) 的 PaneHost 例外。
- 真正漏列的局部函式為 EDIT proc、位址列 brush/色彩、container region、folder-context font，以及 popup 選項與批次 tab-close／tab context menu 建構。依 Scope 4 開 **PD-198**，不修改這些既有函式，也不搬 non-goals。
- **最終 4,764 行**；開工 5,213 行，**淨減 449**。相對系列原始 5,975 行共減 1,211 行。**Acceptance 5 的「3,300 行以下」未達**；指定 scope 沒有足夠可搬內容，沒有刪除註解／壓行／跨範圍切檔湊數。
- **Acceptance 4 的「只剩跨 pane」字面條件亦未完全達成**：paste 屬必要的 app-operation 協調，popup 局部行為則是盤點後另開票的遺漏。完整逐行證據如上；不以合理歸屬說明假充 grep 限制通過。驗收取捨仍待使用者確認。
- 五支主要 pane drawing helper（含 font 建立）的函式逐字比對一致。另 21 支協調函式（Group CRUD/rebind、active-pane、layout header/sidebar、跨 pane drag、context menu、paste、`wWinMain`、shutdown 等）與開工版本逐字一致；`apply_layout` 僅機械替換 navigation/status calls，`window_proc` 僅替換 font cleanup 呼叫。這是無意圖行為／視覺變更的程式碼證據，不能替代實機視覺 PASS。

### 檢查結果（2026-09-05）

- 依 AGENTS 設定 `E:\Dev\LLVM-MinGW\bin`／`E:\Dev\Ninja` PATH，指定 Release configure 與 build 通過，無編譯警告。
- 完整 `ctest --test-dir build --output-on-failure` **24/24 通過**；最後一次 7.24 秒，`panedock_launch_smoke` 1.99 秒。使用正常可寫 session 的 Win32 執行環境；並未宣稱把 `SHGetKnownFolderPath` 的實際 session 路徑改到 build（僅改 LOCALAPPDATA environment 不會做到這件事）。
- 新增 focused `test_navigation_completion_rejects_stale_results_and_refreshes_chrome`：舊 generation／不同 Group／shutdown 不寫入；正常完成更新 model/history/strip 並排程存檔；history suppression 不增加歷史；coordinator-only／未知 command 回傳 false。它依赖 PaneHost/Win32，沿用現有 `panedock_pane` 測試，不污染 core。
- PD-196 的刷新中途 tab 清單變更測試，從觀察已刪除 hook 的呼叫次數改為真實 hidden child address HWND 的文字：失敗刷新保留舊地址，正常刷新更新地址。沒有移除其既有 mutation/label assertions。
- `address_bar_failure_check` 改追 `Pane::refresh_navigation_chrome`；`shell_reentry_gate_check` 補追已搬的 display lookup／item counts guard。記憶體中移除 ShellCall 或 binding check 的兩次 mutation 均被正確拒絕，未修改產品檔案。
- 正常模式啟動後等待 10 秒，再取樣 **60.0158449 秒**：CPU 增量 **0.015625 秒**，按邏輯處理器數正規化為 **0.00130174%**；Read/Write/Other transfer bytes 增量皆 **0**。符合本票 60 秒 NFR-001 spot check；非工作管理員人工讀值，也非 10 分鐘 release gate 或四 pane 實機矩陣。
- 該正常模式 process（PID 4796）`CloseMainWindow` 後 `WaitForExit(5000)` 成功，`HasExited=True`、exit code 0。Diagnostic 輸出曾達 3 個 live views，正常關閉後最後兩筆皆為 `panedock.live_view_count=0`，未強制終止或繞過 Destroy。
- Runtime probe 的限制：第一次 diagnostic 的 Process wrapper 在退出後回報空 ExitCode，雖 `HasExited=True`／live count 0，仍使暫存腳本錯判；先持有 Process handle 後重跑，另一次 `CloseMainWindow()` 未關閉實際 `PaneDockMainWindow`。依既有 smoke 的 PID/class 定位補送 `WM_CLOSE`，5 秒內退出、exit code 0、live count 0，無殘留。不把這兩次 probe 問題隱藏為首次全部通過，亦未因此改動 shutdown。
- `git diff --check` 通過；`src/core`、`shell_core`、`file_operations`、`tab_overflow.h` 與 session schema 零 diff。舊 navigation helper、record helper 與兩個過渡刷新 hook 對 src/tests 搜尋皆無命中。
- 「使用者實機檢查」1–13 **未執行完整清單，需真實桌面人工驗證**；尤其多版型／四 pane 按鈕、拖曳、不同 DPI、檔案操作進度重入及視覺比對。只驗證上述啟動／關閉／live-view 診斷與資料測試，不代替全部實機驗收。
