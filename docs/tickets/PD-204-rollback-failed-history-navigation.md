# PD-204 — back/forward 導航失敗時回滾 model，避免保存從未顯示過的位置

Phase 7 · pane navigation correctness · Depends on: PD-203

- Source: 2026-09-14 PD-203 的獨立 code review（Codex）在稽核 end-session
  checkpoint 時發現。既有缺陷，非 PD-203 引入，但 PD-203 讓它從「被遮蔽」
  變成「可觸發」。
- Priority: MEDIUM——觸發窗口窄，但後果是 session 還原到使用者從未看過的
  資料夾，違反 Group 的核心承諾「一鍵還原整組工作情境」。

## 根本原因

`panedock::core::navigate_tab_back` / `navigate_tab_forward`
（`src/core/model.cpp:90,96`）在**向 Shell 發出導航請求之前**就已改寫
`tab.location` 與 `tab.history_index`：

```cpp
bool navigate_tab_back(TabState& tab) noexcept {
    if (!can_navigate_tab_back(tab)) return false;
    tab.location = tab.history[--tab.history_index];
    return true;
}
```

`Pane::navigate_history`（`src/app_shell/pane.cpp:853`）先呼叫上述函式，才
`explorer_host_.navigate(tab->location, generation)`。導航失敗時
`Pane::navigation_failed`（`:844`）只呼叫 `set_suppress_history(false)` 與
`refresh_navigation_buttons()`，**不回滾 model**。

因此 model 可以停在一個 view 從未成功顯示的位置。失敗有同步與非同步兩條路徑，
兩條都沒有回滾：

- 同步：`pane.cpp:870`，`navigate()` 直接回傳失敗 HRESULT。
- 非同步：`explorer_host.cpp:286` → `report_navigation_failed` →
  `navigation_failed_callback_` → `main.cpp:1776` → `Pane::navigation_failed`。

## 為什麼現在才需要修

`save_now` → `capture_locations` → `Pane::capture_location()` 會讀 live view
的實際資料夾，把錯誤的 model 值蓋掉。這遮蔽了缺陷：debounce 存檔與正常關閉
都會自我修正。

PD-203 讓 OS session end 的 durable checkpoint **刻意跳過 `capture_locations`**
（那正是會卡住的跨行程 Shell 呼叫），於是這條路徑上不再有修正機會。一次失敗的
back/forward 之後、下一次成功導航之前收到 `WM_ENDSESSION`，錯誤位置就會被寫進
`session.json`。

## 範圍

`src/core/model.h` / `model.cpp`：

1. 新增純函式
   `bool restore_tab_history_index(TabState& tab, std::size_t index) noexcept;`
   —— `navigate_tab_back/forward` 的反向操作。`index >= history.size()` 時
   回傳 `false` 且不改動 tab（不夾取、不當作 0）。`history` 內容不變，所以
   還原索引即可同時還原 `location`。
   放在 `src/core` 是因為它是本專案唯一的自動化測試接縫
   （`docs/testing.md`），且此函式不需要 HWND/COM。

`src/app_shell/pane.h` / `pane.cpp`：

2. 新增成員
   `std::optional<std::pair<NavigationGeneration, std::size_t>> history_rollback_;`
   **必須帶 generation**：若只存索引，一次已完成的 history 移動之後，另一個
   來源（address bar、`navigate_up`）的導航失敗會錯誤地回滾歷史。
3. `Pane::navigate_history` 在呼叫 `navigate_tab_back/forward` **之前**記下
   `tab->history_index`，`moved` 為真且 `begin_navigation()` 取得 generation
   之後設定 `history_rollback_`。
4. `Pane::navigation_failed` 在 generation 相符時呼叫
   `restore_tab_history_index`，成功則 `tab_strip_ui().refresh()`；無論是否
   回滾都 `reset()`。
5. `Pane::navigation_complete` 一開始就 `history_rollback_.reset()` —— 導航
   成功即解除待回滾狀態。

`tests/unit/core_model_test.cpp`：

6. 新增 `test_restore_tab_history_index()`：back 後還原、forward 後還原、
   越界回傳 false 且完全不改動 tab、空 history 回傳 false。

## 非目標

- 不改 `navigate_tab_back/forward` 成「先問 Shell 再改 model」。導航是非同步
  的，UI（tab 標題、導航按鈕）需要立即反映意圖；把樂觀更新改成悲觀更新會牽動
  整條導航路徑，遠超過這張票。回滾是較小的正確解。
- 不回滾 `navigate_up` 或 address bar 導航：那兩條路徑不預先改寫 model，
  `record_navigation` 只在 `navigation_complete` 內執行。
- 不改 PD-203 的 checkpoint 行為。跳過 `capture_locations` 的決定不變。
- 不在失敗回滾後排程 session save：回滾後的 model 等同已持久化的狀態，
  沒有新的髒資料。

## 驗收條件

1. `panedock_core_model` 涵蓋 `restore_tab_history_index` 的四種情形並通過。
2. `ctest --test-dir build --output-on-failure` 全數通過。
3. 手動：在某個 pane 導航兩層後按上一頁，若目標資料夾已被刪除／離線而導航
   失敗，tab 標題與 address bar 必須停在原本的資料夾，不得顯示失敗的目標。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

邊界檢查需反向驗證：把 `index >= tab.history.size()` 改成 `index >`，
`panedock_core_model` 必須失敗（實測為 SEGFAULT）；改回後必須通過。

## 交接區

- 回滾帶 generation 是必要的，不是防禦性編碼。`navigation_request_is_current`
  只擋掉「過期的失敗回報」，擋不掉「`history_rollback_` 本身過期」——
  一次成功的 back 之後接一次失敗的 address bar 導航，若不比對 generation
  就會把歷史錯誤地倒回去。
- 這個缺陷能存活到現在，是因為 `capture_locations` 每次存檔都在無聲修正它。
  日後若再有「跳過 live capture」的最佳化，要先確認同樣的遮蔽關係不存在。
