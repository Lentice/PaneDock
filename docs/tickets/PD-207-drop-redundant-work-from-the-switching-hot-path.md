# PD-207 — 移除切換熱路徑上的冗餘 Shell 呼叫與重繪

Phase 7 · switching path performance · Depends on: PD-206

- Source: 2026-09-18 tab／Group 切換路徑稽核（Claude finding 4／5／6／8，
  Codex finding 3）。兩份稽核對「display name 每次重查」一項一致。
- Priority: MEDIUM——每一項都是可證實的冗餘工作，且每一次多餘的 Shell 呼叫都會
  抽訊息迴圈，放大 PD-205 修掉的那個重入面。

## 四項冗餘（皆已複驗）

### 1. 導覽未完成就 `refresh_status_bar()`，讀到上一個 Group 的計數

`apply_layout`（`src/app_shell/main.cpp:1804`）以 `recompute_content = true`
進來時，對每個可見 pane 呼叫 `state.panes[index].refresh_status_bar()`。
`Pane::refresh_status_bar`（`src/app_shell/pane.cpp:640`）是同步 Shell 呼叫
（`host().item_counts(counts)`，`pane.cpp:645`）。

`perform_group_transition`（`:1943-1980`）的順序是
`refresh_tab_strips` → `navigate_realized_panes` → `apply_layout`，所以
`apply_layout` 執行時導覽尚未完成：讀到的是**上一個 Group 的資料夾計數**，
會先閃現錯誤數字；等 `navigation_complete` → `selection_changed_callback`
（`main.cpp:1776` 附近註冊）回來又重算一次。四個 pane 就是四次浪費的同步
Shell 呼叫。

### 2. `apply_item_size()` 每個 pane 跑兩次

`PaneTabStrip::refresh()`（`pane_tab_strip.cpp:220`）已呼叫
`apply_item_size(true)`。`apply_layout`（`main.cpp:1842`）的條件是
`changed_panes[index] || recompute_content`；同版型的 Group 切換中
`changed_panes` 全為 `false`，但 `recompute_content` 為 `true`，於是每個 pane
再跑第二次。

### 3. 每個 layout pass 無條件整窗 `InvalidateRect`

`apply_layout` 結尾（`main.cpp:1868`）`InvalidateRect(window, nullptr, FALSE)`
無條件執行，連 `kDeferredLayoutMessage` 那種幾何完全沒動的 no-op pass 也會
觸發整窗重繪。

### 4. 虛擬資料夾顯示名稱每次重整都重新向 Shell 查詢

`display_text_for_parsing_name`（`main.cpp:619`）對 `::` 開頭的 parsing name
每次都開一個 `ShellCallScope` 去問 Shell。一次 Group 切換的
`refresh_tab_strips` 會對「每個 pane 的每個 tab」各查一次
（`pane_tab_strip.cpp:212`），加上 address bar 一次
（`pane.cpp:618-637`）。`::{GUID}` 的顯示名稱在一個 process 生命週期內實務上
不變，`state.pinned_fixed_labels` 已經是同一個概念的手工特例。

## 要讀與追的檔案

- `src/app_shell/main.cpp`：`display_text_for_parsing_name`（`:619`）、
  `AppState::tab_display_text` override、`pinned_fixed_labels`（`:524`）、
  `refresh_startup_chrome`（`:1877`）、`apply_layout`（`:1590-1875`，
  特別是 `:1804`、`:1842`、`:1868`）、`perform_group_transition`（`:1939`）。
- `src/app_shell/pane.cpp`：`refresh_status_bar`（`:640`）、
  `refresh_navigation_chrome`（`:618`）、`navigation_complete`（`:830-843`）、
  `finish_tab_change`（`:1153`）、`pending_navigation()`（`pane.h:82`）。
- `src/app_shell/pane_tab_strip.cpp`：`refresh()`（`:192-222`）。
- `src/core/navigation.h`：`NavigationRequest` 的欄位。

## 範圍

1. **`Pane::refresh_status_bar()`（`pane.cpp:640`）開頭加 guard**：本 pane 有
   未完成導覽時直接 `return`。判斷依據是 `pending_navigation_`（generation／
   group_id／tab_id 齊備）與 `ExplorerHost` 的已完成 generation 比較——沿用
   `navigation_request_is_current`（`pane.cpp:807` 附近）既有的比較方式，
   不要新增平行狀態。
   guard 放在**共用函式內**而非 `apply_layout` 呼叫點：`AGENTS.md` 明示
   「A guard in the shared function is a smaller diff than a guard in every
   caller」，且 `selection_changed_callback` 也是同一個 callee。
   導覽完成後 `navigation_complete`（`pane.cpp:830`）既有的
   `selection_changed_callback` 路徑會補上正確數字，所以沒有「狀態列永久空白」
   的風險——若複驗發現 `navigation_complete` 沒有觸發狀態列更新，則在該處
   補一次 `refresh_status_bar()`。

2. **`main.cpp:1842` 的條件收斂為 `changed_panes[index]`**。內容變更由
   `PaneTabStrip::refresh()` 負責。

3. **`main.cpp:1868` 改為條件式**：僅在 `!committed` 或有任一
   `changed_panes[index]` 為真時整窗 invalidate。（`committed` 為 false 表示
   `DeferWindowPos` 批次失敗、已改走逐一 `SetWindowPos`，此時仍需整窗重繪。）

4. **`display_text_for_parsing_name`（`main.cpp:619`）加 memo**：在 `AppState`
   上放 `std::map<std::wstring, std::wstring> display_name_cache;`，命中就
   不進 Shell。查詢結果為空字串時**不快取**（Shell 暫時失敗不該被記住）。
   這是 process 生命週期內的快取，不持久化、不需要 schema 版本。

## 非目標

- 不改導覽本身的排程與阻塞行為——那是 PD-209 的範圍。
- 不合併 `finish_tab_change` 與 `navigation_complete` 的兩次
  `tab_strip_ui().refresh()`（Claude finding 8）：第一次是選取態的即時回饋，
  有其道理；範圍 4 的 memo 修掉後其成本已消失，**刻意不單獨改**。
- 不動 `refresh_tab_strips` 對未綁定 pane 的走訪（Claude finding 5 前半）：
  `PaneTabStrip::refresh()` 的 `pane_state() == nullptr` 分支只做
  `set_tabs({})` + `apply_item_size()` + `refresh_navigation_chrome()`，
  沒有 Shell 呼叫；加早退 guard 買到的極少，先不做。
- 不快取非 `::` 開頭的路徑（那條路徑本來就不進 Shell）。

## 驗收條件

1. Group 切換期間，導覽未完成的 pane 不再發出 `item_counts()` 呼叫；導覽完成
   後狀態列顯示正確計數。
2. 同版型 Group 切換中每個 pane 的 `apply_item_size()` 只跑一次。
3. 幾何未變且批次提交成功的 layout pass 不再整窗 invalidate；視窗 resize 與
   版型切換的重繪行為不退化。
4. 同一個 `::{GUID}` parsing name 在一個 process 生命週期內只向 Shell 查一次
   顯示名稱。
5. 既有測試全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

範圍 1 的 guard 需要一個聚焦自檢。若無法在不引入 HWND／COM 的前提下測到，
在交接區寫明原因，並以 `panedock_launch_smoke` 作為不退化證據。

## 交接區

2026-09-18 實作完成。

- **範圍 1 的修法改得更小**：ticket 原本要在 `Pane::refresh_status_bar()` 加
  pending-navigation guard，但 `ExplorerHost` 已經有一模一樣的不變量——
  `completed_navigation_generation_ != latest_navigation_generation_`，
  `set_view_mode`／`get_view_mode`／`set_sort`／`get_sort`
  （`explorer_host.cpp:670,689,703,733`）四處都用它回 `E_PENDING`。因此 guard
  加在 `ExplorerHost::item_counts()` 開頭（同樣回 `E_PENDING`），而不是在
  `Pane` 側新增平行狀態。這是更上游的共用函式，`selection_changed_callback`
  與 `apply_layout` 兩條呼叫路徑一次都蓋到，且完全不需要新欄位。
- `Pane::refresh_status_bar()` 只加一行：`hr == E_PENDING` 時直接 return，
  保留上一次的計數（而非清空）。
- `Pane::navigation_complete()` 結尾新增一次 `refresh_status_bar()`：因為
  in-flight 期間的呼叫被拒絕了，不能假設導覽落地後一定會有
  selection-changed 回呼補上正確數字。
- 範圍 2：`main.cpp` 的 `apply_item_size` 條件由
  `changed_panes[index] || recompute_content` 收斂為 `changed_panes[index]`。
- 範圍 3：結尾的 `InvalidateRect(window, nullptr, FALSE)` 改為僅在
  `!committed` 或任一 `changed_panes` 為真時執行。
- 範圍 4：`AppState::display_name_cache`（`std::map<std::wstring, std::wstring>`）
  memo `::` 開頭 parsing name 的顯示名稱；空字串不快取。新增 `#include <map>`。
- `item_counts_cache_` 與本修法不衝突：`E_PENDING` 在讀快取之前就返回，而
  `explorer_host.cpp:972` 在導覽完成時 `reset()` 快取。
- `ctest`：33/33 通過。導覽 in-flight 的行為由 `E_PENDING` 這條既有慣用法承載，
  沒有為它新增單元測試——`docs/testing.md` 明訂 `explorer_host` 無自動化測試網，
  而該不變量的四個既有使用者也是同樣的處境。
