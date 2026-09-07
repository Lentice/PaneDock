# Audit: PaneDock（第二輪：sidebar 與 tab strip 手勢流程）

## Summary

本輪針對前一份稽核標為 unreached 的 sidebar 與 tab strip 手勢流程。Sidebar 的
reorder／rename／drag 狀態機逐項核對後沒有缺陷；tab strip 有一個真實的越界讀取，
根因是「模型已改、strip 快取未更新」的視窗會被 Shell 呼叫的訊息泵打開。

- 最大架構疑慮：`PaneTabStrip` 的 `tab_visuals_`、版面 `tab_rects` 與
  `PaneState::tabs` 是三份靠手動同步的長度，繪製時以前者索引後者。
- 最大正確性疑慮：同上。

## Findings

### [High] Tab strip 繪製以快取長度索引即時模型，在 Shell 呼叫泵訊息時越界讀取（已修正）

- **Location**: `src/app_shell/pane_tab_strip.cpp` 的 `paint_contents` 分頁迴圈
  （修正前以 `visuals.size()` 為界並讀 `pane.tabs[index]`）。
- **Mechanism**: `finish_tab_drag`（`src/app_shell/main.cpp:2454`）先對來源 strip
  呼叫 `apply_item_size()`，其中 `InvalidateRect` 排入一則 WM_PAINT；接著
  `core::move_tab`（`src/core/model.cpp:340`）在來源 pane 有 ≥2 個分頁時 `erase`
  掉該分頁；然後 `main.cpp:2489` 在 `ShellCallScope` 內呼叫 `navigate_to`，
  `IExplorerBrowser` 會泵我們的訊息迴圈。此時 `tab_strip_ui().refresh()`
  （`main.cpp:2503`）尚未執行，`tab_visuals_` 仍是搬移前的數量。`tab_strip_proc`
  的再進入閘門只延後滑鼠按鍵訊息（`main.cpp:565`），WM_PAINT 直接放行到
  `strip.paint(...)`（`main.cpp:2600`）。
- **Failure**: 來源 pane 有 3 個分頁，把其中一個拖到另一個 pane → `tabs` 變成 2、
  `tab_visuals_` 仍是 3 → 泵出的 WM_PAINT 讓 `paint_contents` 讀 `pane.tabs[2]`，
  對 2 元素的 `std::vector` 越界讀取；輕則畫出垃圾的 active 狀態，重則存取已釋放
  的記憶體而當掉。
- **No guard**: `paint_contents` 只檢查 `pane_state == nullptr`，沒有比對長度；
  `tab_rects` 與 `visuals` 同長所以擋不下；`child_message_blocked_while_closing`
  與 `defer_shell_reentry_mouse_message` 都不涵蓋 WM_PAINT。
- **Direction / applied**: 新增純函式
  `panedock::app_shell::drawable_tab_count(visual_count, model_tab_count, rect_count)`
  取三者最小值，`paint_contents` 以它為迴圈界限。同一個 guard 一併涵蓋同根因的另外
  兩條路徑：`Pane::finish_tab_change`（`src/app_shell/pane.cpp:1069`）在 `close_tab`
  縮短 `tabs` 後、`refresh()` 之前同樣先泵了 `navigate_to`；以及
  `PaneTabStrip::refresh` 偵測到模型於 `tab_display_text` 再進入期間變動時直接
  return，讓舊 `tab_visuals_` 留著。未改變任何同步時機，只讓不同步時少畫一格。

## Observations

- `Sidebar` 的 reorder 狀態機（PD-057 的延後 capture、`finish_drag` 早於
  `take_reorder_request`）與 `core::reorder_source_index` 對 `reorder_group` 的投影
  一致，本輪逐項核對後沒有發現缺陷。
- `Sidebar::draw_item` 在第一次 `set_rect` 之前 `dpi_` 為 0，所有
  `MulDiv(..., 0, 96)` 內縮歸零；只影響建立後、版面套用前的第一幀，未列為缺陷。
- rename 提交以 `selected_index()` 而非 `begin_rename()` 當下的 group id 解析目標；
  編輯器失焦即取消，本輪找不到讓兩者分歧的路徑。
- `finish_tab_drag` 在 `is_shutting_down()` 時於兩次 `navigate_to` 之後直接 return，
  跳過兩個 `refresh()`；關閉中沒有可觀察後果，未列為缺陷。

## Coverage

- **Read**: 6 plan files + 0 reserve + 0 evidence-overrun（`sidebar.cpp` 全檔；
  `model.cpp` 的 `reorder_group`／`reorder_source_index`／`move_tab`；
  `pane_tab_strip.cpp` 全檔；`main.cpp` 的 sidebar／tab-drag／paint／DPI／WM_TIMER
  區段；`pane.cpp` 的 tab 變更與命令區段；`pane_host.h` 的 `ShellCall`）
- **Flows traced**: sidebar group reorder／rename／選取；跨 pane 與同 pane 的 tab
  拖曳至 session 儲存；tab 新增／關閉至 Shell 導覽；WM_DPICHANGED 重新佈局
- **Ruled out**: sidebar drag 的 capture 時序與 `reorder_source_index` 投影；
  `item_at_point`／`draw_item` 的索引（皆已對 `groups_.size()` 設限）；DPI 變更
- **Unreached**: `pinned_locations_dialog.cpp`、`transfer_close_dialog.cpp`、
  `startup_notification.cpp`、`file_operations.cpp`、`core/session.cpp` 的載入路徑
  （前輪已涵蓋 session，本輪預算用於前輪標為 unreached 的手勢流程）；混合 DPI
  多螢幕與 idle 量測需實機
- **Verification performed**: LLVM-MinGW Release build 成功；
  `ctest --test-dir build --output-on-failure -E '^panedock_launch_smoke$'`
  **26/26 通過**，含新增的 `drawable_tab_count` 斷言。
  這個 High 是讀碼推導的越界，**沒有** sanitizer 或偵錯器的執行期證據；
  `panedock_launch_smoke` 一如前輪未執行（使用 repo 外實際 session，清理含
  `Stop-Process -Force`）。跨 pane 拖曳的桌面驗收也未進行。
