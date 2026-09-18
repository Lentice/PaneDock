# PD-210 — 同步失敗的導覽必須把 `latest_navigation_generation_` 還給仍在飛的請求

Phase 7 · switching path robustness · Depends on: PD-199, PD-207

- Source: 2026-09-18 第二輪切換路徑稽核（Claude finding 2，CONFIRMED）。作者已
  在原始碼層面複驗機制成立。Codex 的 finding 3 從另一個角度指到同一個根源
  （`IExplorerBrowserEvents` 沒有 request token，歸屬靠順序猜）。
- Priority: HIGH——後果是一個 pane 進入**永久性的錯誤狀態**：View mode 選單按了
  靜默無反應、狀態列凍結、該 pane 的 view mode 與 sort 不再寫進 session。
  這是本輪稽核唯一會留下持續性錯誤狀態的一項。

## 根本原因

`ExplorerHost::latest_navigation_generation_` 只會單調上升
（`src/explorer_host/explorer_host.cpp:268-269`，`enqueue_navigation` 裡的
`std::max`）。`fail_enqueued_navigation`（`:273-287`）撤掉自己那筆 queue
record 之後，**不把 `latest_` 降回還在飛的那一筆**：

```cpp
void ExplorerHost::fail_enqueued_navigation(
    NavigationGeneration generation) noexcept {
    for (auto request = navigation_requests_.rbegin();
         request != navigation_requests_.rend(); ++request) {
        if (request->generation != generation) continue;
        navigation_requests_.erase(std::next(request).base());
        break;
    }
    report_navigation_failed(generation);
}
```

`navigation_complete`（`:993-995`）只有在 `generation == latest_` 時才推進
`completed_navigation_generation_`：

```cpp
    // Every exit below still ends this navigation. Leave the generation
    // recorded, or view-mode/sort calls keep returning E_PENDING forever.
    if (generation == latest_navigation_generation_)
        completed_navigation_generation_ = generation;
```

那句註解正是這個失效模式的另一半——作者知道「generation 沒被記錄就會永久
`E_PENDING`」，但只修了「完成」這個方向，沒修「同步失敗」這個方向。

### 可觸發的事件序列（已完整追過）

1. Group 切換對 pane 0 發出 gen N 到一個慢速 SMB 路徑。
   `BrowseToObject` 回 `S_OK`、導覽非同步進行中。
   `navigation_requests_ == [N]`，`latest_ == N`。
2. 使用者立刻切 tab（或再切一次 Group），pane 0 發出 gen N+1 到一個**不可達**
   位置 → `SHCreateItemFromParsingName`（`:623-627`）在 1000 ms
   `dwTickCountDeadline` 後失敗 → `fail_enqueued_navigation(N+1)`：N+1 的
   record 被撤掉，但 `latest_` **留在 N+1**。queue 仍是 `[N]`。
3. gen N 完成 → `navigation_complete` → `take_navigation_generation()` 回 N →
   `:994` 的 `generation == latest_` 為**假** → `completed_` 永遠停在 N-1。
   N+1 已被撤回，不會再有任何完成事件帶著它。

### 後果（該 pane，直到下一次成功導覽為止）

- `item_counts`（`:818`，PD-207 加的 in-flight guard）永久回 `E_PENDING` →
  `Pane::refresh_status_bar`（`pane.cpp:652`）永久早退，狀態列凍在舊計數。
- `set_view_mode`／`set_sort`（`:670`、`:703`）永久回 `E_PENDING` →
  **使用者按 View mode 選單完全沒反應，且靜默失敗**。
- `get_view_mode`／`get_sort` 永久 `E_PENDING`，而
  `Pane::capture_view_mode`／`capture_sort`（`pane.cpp:924`、`:944`）把
  `E_PENDING` 當 `FAILED` 直接 return → 該 pane 的 view mode 與 sort
  **不再被寫進 session**。
- `report_navigation_failed`（`:1042`）為 N+1 顯示了 error overlay，而
  `navigation_complete` 對 stale generation 是在 `error_overlay_.hide()`
  （`:1027`）**之前**就 return → overlay 蓋在一個其實已成功載入的 view 上。

## 單執行緒前提（本票據以簡化的依據）

`ExplorerHost` 的所有 generation 記帳都只在 UI 執行緒發生：`navigate`／
`enqueue_navigation`／`fail_enqueued_navigation` 來自我們的訊息處理，
`navigation_complete`／`navigation_failed`／`navigation_pending` 來自
`IExplorerBrowserEvents`，而該介面是 STA-bound、由同一個執行緒的訊息迴圈派送。
因此：

- **不需要鎖、不需要原子、不需要記憶體序**。本修法是三個純量的同執行緒更新。
- 唯一要考慮的是**重入**（Shell 呼叫抽我們的迴圈），而重入在同執行緒下是
  巢狀而非交錯：每一次更新在下一個 pump 點之前都是完整的。
- 因此「還原 `latest_` 與撤 record 之間被別人看到中間狀態」這個 case
  **不存在**，不要為它寫防護。詳見 PD-213。

## 要讀與追的檔案

- `src/explorer_host/explorer_host.h:150-155`：四個 generation 欄位的角色
  （`next_`／`latest_`／`completed_`／`prepared_`）與
  `NavigationRequestRecord`。
- `src/explorer_host/explorer_host.cpp`：`begin_navigation`、
  `enqueue_navigation`（`:257`）、`fail_enqueued_navigation`（`:273`）、
  `take_navigation_generation`（`:290`）、`navigation_pending`（`:302`）、
  `navigate`（`:609-640`，特別是 `:623` 的同步失敗分支）、
  `set_view_mode`／`get_view_mode`／`set_sort`／`get_sort` 的 `E_PENDING`
  guard（`:670`、`:689`、`:703`、`:733`）、`item_counts`（`:818`）、
  `navigation_complete`（`:956-1000`）、
  `report_navigation_failed`（`:1035-1045`）。
- `src/app_shell/pane.cpp`：`navigate_to`（`:790`）、
  `navigation_request_is_current`（`:803`）、`refresh_status_bar`（`:645`）、
  `capture_view_mode`（`:924`）、`capture_sort`（`:944`）。

## 範圍

`src/explorer_host/explorer_host.cpp`，`fail_enqueued_navigation`（`:273`）：

撤掉 record 之後、呼叫 `report_navigation_failed` 之前，若
`navigation_requests_` 非空，把 `latest_navigation_generation_` 還原成
剩下最後一筆 record 的 generation：

```cpp
    if (!navigation_requests_.empty())
        latest_navigation_generation_ = navigation_requests_.back().generation;
```

語意仍然是「最新的 in-flight 請求」。`navigation_requests_` 是 FIFO 且
generation 單調遞增，所以 `back()` 就是最新的一筆。

這一行同時修掉 overlay 誤顯示：`report_navigation_failed`（`:1035`）的
`generation != latest_navigation_generation_` 早退在此時會成立，N+1 的
error overlay 不再被顯示——它本來就不該顯示，因為 N 仍在飛且會成功。

`navigation_requests_` 為空時**不動** `latest_`：那代表沒有任何 in-flight
請求，`latest_` 保持在失敗的那一筆是正確的（error overlay 應該顯示，
`completed_` 追不上也是正確的——確實沒有任何導覽成功）。

## 非目標

- 不改 `IExplorerBrowserEvents` 的歸屬機制（FIFO + `pending_notified`）。
  Codex finding 3 與 Claude finding 5 都指向「Shell callback 沒有 request
  token」這個根源，但兩人都標為 PLAUSIBLE／需實機 trace。那是另一張票，
  觸發條件見 `docs/tickets.md` §候選。
- 不改 `take_navigation_generation()` 在 queue 空時鑄新 generation 的行為
  （那是 view 內雙擊資料夾的正常路徑）。
- 不引入鎖、原子或任何執行緒安全機制（見上方單執行緒前提）。
- 不改 `E_PENDING` 這個慣用法本身，也不改它的四個既有使用者。

## 驗收條件

1. 一筆導覽同步失敗、而另一筆更早的導覽仍在飛時，`latest_` 回到仍在飛的那一筆
   的 generation。
2. 該較早的導覽完成後 `completed_ == latest_`，因此 `item_counts`／
   `set_view_mode`／`set_sort`／`get_view_mode`／`get_sort` 恢復正常，不再
   永久回 `E_PENDING`。
3. 同步失敗且**沒有**其他 in-flight 請求時，行為與現狀相同（overlay 顯示、
   `completed_` 不推進）。
4. 被 supersede 的失敗不再顯示 error overlay。
5. 既有測試全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`docs/testing.md` 明訂 `explorer_host` 無自動化測試網（行為由 `shell32`
定義），所以這個 generation 記帳的狀態機需要一個**不含 COM 的**聚焦自檢：
把 `next_`／`latest_`／`completed_` 與 `navigation_requests_` 的轉移抽成一個
純函式或小型 struct（可放 `src/core/navigation.h`，它已經是這個概念的家且
無 HWND／COM），對「enqueue N → enqueue N+1 → fail N+1 → complete N」這條
序列斷言 `completed_ == latest_`。若判斷抽取會動到 `explorer_host` 的
COM 契約而風險過高，則在交接區寫明原因並改以 `panedock_launch_smoke`
作為不退化證據。

## 交接區

2026-09-18 實作完成。

- **範圍擴大（已評估且刻意）**：ticket 的 Agent checks 要求把 generation 記帳
  抽成不含 COM 的可測單元。實際做了——新增
  `panedock::core::NavigationLedger`（`src/core/navigation.h`），接管原本散落在
  `ExplorerHost` 的 `navigation_requests_`／`next_`／`latest_`／`completed_`
  四個成員（`prepared_navigation_generation_` 不屬於這個狀態機，留在原處）。
  `ExplorerHost` 的六個相關函式改為委派，四處
  `completed_ != latest_` 的判斷收斂為 `navigation_ledger_.in_flight()`，
  三處 `generation == latest_` 收斂為 `is_latest()`。
  `NavigationRequestRecord` 從 header 移除，`<deque>` 也一併移除。
  這不只是為了測試：原本 `std::max(latest_, generation)` 的邏輯在三處各寫一份，
  現在只有一份。
- 本票的實際修正在 `NavigationLedger::withdraw()`：撤掉 record 後，若
  `requests_` 非空就把 `latest_` 還原成 `requests_.back().generation`。
  佇列為空時不動 `latest_`。
- **實作時發現 ticket 遺漏的一項耦合（重要）**：`report_navigation_failed`
  原本把「顯示 error overlay」與「呼叫 `navigation_failed_callback_`」擋在
  同一個 `is_latest(generation)` 閘門後面。修好 `latest_` 之後，被 supersede
  的同步失敗就不再是 latest，於是**連回呼也一起被吞掉**——
  `tests/unit/explorer_host_lifetime_check.cpp:129-130` 立刻抓到
  （「a navigate() that reports failure in its HRESULT must raise exactly one
  failure callback, carrying its OWN generation」）。那個測試是對的：
  `Pane::navigation_failed` 負責釋放 history suppression 與回滾 PD-204 的
  model 移動，吞掉回呼會讓該 pane 的 back/forward 永久卡住。
  因此把兩者解耦：overlay 仍只在 `is_latest` 時顯示（那是本票想修的
  「overlay 蓋在即將成功的 view 上」），回呼則只要 `parent_ != nullptr`
  就一律發出——`Pane::navigation_failed` 本來就有自己的
  `navigation_request_is_current` 過濾。
- 測試：`tests/unit/core_navigation_test.cpp` 新增五個 case。已用**反證**確認
  有效：把 `withdraw()` 的還原那一行改成 `if (false)` 後
  `panedock_core_navigation` 失敗，改回即通過。
- `ctest`：34/34 通過。

### 仍未解決、留給後續

`IExplorerBrowserEvents` 沒有 request token，`NavigationLedger` 的 FIFO 歸屬
仍然是靠「發出順序」猜的。Codex finding 3 與 Claude finding 5 從兩個方向指到
這個根源（被 supersede 的導覽若沒有終止事件就出現 queue hole；
`take()` 在 queue 空時鑄新 generation 而 `Pane` 會無條件收養）。兩者都標
PLAUSIBLE，需要實機 event trace 才能證實。已登記於 `docs/tickets.md` §候選。
`NavigationLedger` 現在是那張票的落點——它已經是這個狀態機唯一的家，且可測。
