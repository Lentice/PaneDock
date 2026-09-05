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

（實作者填寫：`handle_pane_command` 逐分支歸屬；繪製輔助函式的逐支歸屬；剩餘 `pane_index` 的逐行清單與理由；`main.cpp` 最終行數；全檔複驗的結論摘要與新開的票號。）
