# PD-214 — 一個 `ShellReentryGuard` 擁有 Shell re-entry 的暫存、重播與欠下的關閉

Phase 7 · switching path robustness · Depends on: PD-205, PD-211, PD-212, PD-213

- Source: 2026-09-29 架構審查（improve-codebase-architecture），候選 1。
  使用者已逐項確認設計決策（見 §決策）。
- 目的：純重構加上可測性。**使用者可見行為不得改變。**

## 為什麼

「Shell re-entry」（見 `CONTEXT.md`）是 AGENTS.md 點名的核心風險，但它目前
散在五處，而且沒有任何測試能觀察它的組合行為：

1. `src/app_shell/pane_host.h`：`ShellCall`（RAII，呼叫
   `PaneHost::shell_call_entered/left`），`pane.cpp` 內 21 處使用。
2. `src/app_shell/main.cpp`：另一個 RAII `ShellCallScope`（直接
   `step` 關閉狀態機），`main.cpp` 內約 19 處使用。
3. `src/explorer_host/explorer_host.h/.cpp`：第三個
   `ExplorerHost::ShellCallScope`，透過 `set_shell_call_callback` 轉給 pane
   （`pane.cpp` 內 `explorer_host_.set_shell_call_callback(...)`）。
4. `main.cpp` 的 `finish_shell_call` 同時做三件互不相關的 effect：推進
   `ShutdownSequence`、深度歸零時 `flush_deferred_shell_messages`、以及在
   `ShutdownAction::defer` 時 post `kDeferredShutdownMessage`（post 失敗就同步
   `begin_shutdown`）。
5. `AppState::shell_call_depth` 這個指向 `ShutdownSequence::State` 的
   reference 別名，被 window procedure 直接讀取（`main.cpp` 中
   `defer_shell_reentry_mouse_message`、`main_window_proc` 的 `WM_COMMAND` 延遲、
   `WM_NCDESTROY`、pane window proc 的延遲回傳、tab context menu、`WM_TIMER`、
   `handle_pane_control_message` 的 `WM_COMMAND` 等處；以
   `grep -n "shell_call_depth" src/app_shell/main.cpp` 取得完整清單）。

`tests/unit/test_pane_host.h` 的 `shell_call_entered/left` 是空實作，所以沒有
測試能觀察「深度歸零才重播」「關閉延後到 Shell 呼叫退出」「post 失敗時同步
補做」。PD-205／PD-211／PD-212 的 bug 都發生在這段沒被測到的組合行為裡。

## 決策（使用者已確認，實作者不得重新決定）

| # | 決策 |
|---|---|
| D1 | 一張 ticket 完成。 |
| D2 | **不**納入 `apply_layout` 的 `LayoutPassScope`／`layout_pending`／`kDeferredLayoutMessage`。那是版面套用的自我重入，不是 Shell 呼叫期間的重入，留給後續 Group transition 的 ticket。 |
| D3 | **深度仍由 `core::ShutdownSequence` 持有**。`shutdown_ready_to_resume()` 與 `deferred_shutdown_ready` 的判斷依賴它，且 `tests/unit/core_shutdown_test.cpp` 已有巢狀呼叫測試。`src/core/shutdown.*` 與其測試**不改**。guard 只透過 `step(shell_call_entered/left)` 通知並透過 `state().shell_call_depth` 讀取。 |
| D4 | 新增 unit test 透過 fake effects 驗證 guard；並把 `TestPaneHost` 的空實作換成傳入真的 guard。 |
| D5 | pane 直接持有 guard 的參照（建立／設定 pane 時傳入）。`PaneHost` **刪除** `shell_call_entered()` 與 `shell_call_left()`。 |
| D6 | window procedure 內所有 `shell_call_depth != 0` 改問 guard；刪除 `AppState::shell_call_depth` 別名。 |
| D7 | 名稱：module 為 `ShellReentryGuard`；唯一的 RAII 型別為 `ShellCallScope`，取代 `ShellCall` 與舊的 `main.cpp` `ShellCallScope`。 |
| D8 | effects 走一個小的 virtual interface，兩個 adapter：`main.cpp` 的 Win32 版與測試 fake（與 `PaneHost`／`TestPaneHost` 同一種寫法）。 |

## 約束（引自 AGENTS.md，必須遵守）

- 「**Keep `src/core` free of HWND, COM and `windows.h`.**」`deferred_messages.h`
  使用 HWND／`windows.h`，因此 guard 放在 `src/app_shell`，**不得**進 `src/core`。
- 「**The hazard is reentrancy, never concurrency.** … one thread, no locks, no
  atomics, no `volatile` … A depth counter plus a deferral list
  (`ShellCallScope` / `shell_call_depth`) is the right tool for that; a lock is
  not.」不得加入任何執行緒同步。
- 「re-verify identity (`active()`, `pane_state()`, `is_shutting_down()`) after
  every pump point, and never hold a reference or pointer across one.」本票不改
  任何呼叫點在 scope 結束後的重新驗證邏輯。
- 「Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on
  it … on shutdown destroy all views before the message loop exits.」guard 的
  「post 失敗時同步開始關閉」必須走既有的 `begin_shutdown`，不得另寫關閉路徑。
- 「Prefer the smallest working change. Reuse existing code before adding helpers
  or abstractions.」`hold_deferred_message`／`take_deferred_messages`／
  `deferred_message_replaces_previous` 原樣重用。
- 「Event-driven idle path only. No busy loops, no polling timers.」
- 「New non-trivial logic needs one focused runnable test or self-check.」

## 要讀與追的檔案

- `src/app_shell/pane_host.h`（`ShellCall`、`PaneHost`）
- `src/app_shell/deferred_messages.h`（全檔）
- `src/app_shell/main.cpp`：`AppState`（`shell_call_depth`、
  `deferred_shell_messages`、`shutdown_message_queued`、`end_session_pending`
  別名）、`AppState::shell_call_entered/left`、`flush_deferred_shell_messages`、
  `finish_shell_call`、`ShellCallScope`、`defer_shell_reentry_message`、
  `defer_shell_reentry_mouse_message`、`begin_shutdown`、所有
  `shell_call_depth` 讀取點、所有 `ShellCallScope` 使用點、pane 的
  `set_host` 呼叫點。
- `src/app_shell/pane.h/.cpp`：`set_host`、`host_`、所有 `ShellCall` 使用點、
  `explorer_host_.set_shell_call_callback(...)` 的 callback。
- `src/explorer_host/explorer_host.h/.cpp`：`ExplorerHost::ShellCallScope`、
  `set_shell_call_callback`、`enter_shell_call`／`leave_shell_call`。
- `src/app_shell/pane_tab_strip.h`（第 122 行附近提到 coordinator 的 `ShellCallScope` 的註解）。
- `src/core/shutdown.h/.cpp`（只讀，確認 D3）。
- `tests/unit/test_pane_host.h`、`tests/unit/pane_test.cpp`、
  `tests/unit/deferred_messages_test.cpp`、`tests/unit/core_shutdown_test.cpp`、
  `tests/CMakeLists.txt`。
- `tests/release/shell_reentry_gate_check.ps1`（它可能以字串比對 `main.cpp` 的
  符號名；若本票改名使它失敗，更新它以檢查同樣的不變式，不得刪除檢查）。

## 範圍

### 新檔 `src/app_shell/shell_reentry_guard.h`（必要時加 `.cpp`）

```cpp
namespace panedock::app_shell {

// The only Win32 effects Shell re-entry needs. Main window adapter in
// main.cpp; recording fake in tests.
class ShellReentryEffects {
  public:
    virtual ~ShellReentryEffects() = default;
    virtual bool window_alive(HWND window) noexcept = 0;
    virtual bool post(HWND window, UINT message, WPARAM wparam,
                      LPARAM lparam) noexcept = 0;
    // Post of the deferred shutdown message failed: start shutdown now.
    virtual void begin_shutdown_now() noexcept = 0;
};

class ShellReentryGuard final {
  public:
    ShellReentryGuard(panedock::core::ShutdownSequence &shutdown,
                      ShellReentryEffects &effects) noexcept;

    bool in_shell_call() const noexcept;           // depth != 0
    void enter() noexcept;
    void leave() noexcept;                         // was finish_shell_call
    void hold(HWND target, UINT message, WPARAM wparam, LPARAM lparam,
              bool replaces) noexcept;             // was defer_shell_reentry_message body
    ...
};

class ShellCallScope final { /* RAII over ShellReentryGuard& */ };

}
```

簽名是方向不是規格：實作者可以調整參數，但必須符合 D3、D5、D8，而且對外的
介面要保持這麼小。`leave()` 的行為必須與目前 `finish_shell_call` **逐步相同**：

1. `step(shell_call_left)` 取得 action。
2. 深度為 0 時 `take_deferred_messages` 一次取空；若 `is_shutting_down` 為真
   則丟棄；否則對每則 `window_alive` 的訊息 `post`，失敗只記
   `OutputDebugStringW`。**「是否 shutting down」** 需要一個來源：用
   `ShutdownSequence` 可推導的狀態，或由 effects 提供 `shutting_down()`，
   二選一。選哪個、為什麼，寫成該行的程式碼註解。
3. action 為 `defer` 且未 `shutdown_message_queued` 且 main window 存在時：
   `step(deferred_shutdown_queued)` → post `kDeferredShutdownMessage` →
   失敗則 `step(deferred_shutdown_queue_failed)` 並呼叫
   `begin_shutdown_now()`（adapter 內保留 `IsWindow` 檢查與
   `!end_session_pending` 參數）。

`kDragHoverMessage` 的 `replaces` 判斷留在 `main.cpp` 的呼叫端（PD-212 的
決定：app 專屬常數不進共用 header）。

### `pane_host.h`／`pane.h`／`pane.cpp`

- 刪除 `ShellCall`、`PaneHost::shell_call_entered/left`。
- `Pane` 取得 `ShellReentryGuard&`（或指標，與 `set_host` 同一時機設定），
  21 處改為 `ShellCallScope`。
- `explorer_host_` 的 shell-call callback 改為轉給 guard 的 `enter/leave`。
  `ExplorerHost::ShellCallScope` 與其 callback 機制**保留**：`explorer_host`
  不得依賴 `app_shell`，callback 是它對外的 adapter。

### `main.cpp`

- `AppState` 擁有 guard 與 Win32 effects adapter；刪除
  `AppState::shell_call_entered/left` override、舊 `ShellCallScope`、
  `finish_shell_call`、`flush_deferred_shell_messages`、
  `deferred_shell_messages` 成員（移入 guard）、`shell_call_depth` 別名。
- 所有讀 `shell_call_depth` 的地方改為 `guard.in_shell_call()`。
- `defer_shell_reentry_message`／`defer_shell_reentry_mouse_message` 變成
  guard 的薄呼叫端或直接內聯；不改哪些訊息被延遲。

### 測試

- 新檔 `tests/unit/shell_reentry_guard_test.cpp`（加入 `tests/CMakeLists.txt`），
  用真的 `core::ShutdownSequence` 與 recording fake effects，至少涵蓋：
  1. 巢狀 enter/enter/leave 不重播；最外層 leave 才重播，順序與 hold 順序一致。
  2. 重播時 `window_alive` 為假的訊息被略過。
  3. shutting down 時暫存訊息被丟棄。
  4. 巢狀呼叫中要求關閉 → 最外層 leave 才 post `kDeferredShutdownMessage`，且只 post 一次。
  5. 該 post 失敗 → `begin_shutdown_now()` 被呼叫一次。
  6. `in_shell_call()` 在 enter/leave 前後正確。
- `tests/unit/test_pane_host.h` 刪除空的 `shell_call_entered/left`；
  `pane_test.cpp` 等測試改為給 pane 一個真的 guard（搭配 fake effects）。

## 非目標

- 不改 `src/core/shutdown.*` 與 `core_shutdown_test.cpp`（D3）。
- 不動 `LayoutPassScope` 與版面延後機制（D2）。
- 不移除 `ShutdownState` 的其他 reference 別名（`shutdown_message_queued`、
  `end_session_pending` 等）；那屬於後續關閉流程的 ticket。
- 不改哪些訊息被延遲、不改 PD-212 的取代語意、不加容量上限。
- 不改 `ExplorerHost` 的 callback 介面。
- 不改任何使用者可見行為。

## 驗收條件

1. `grep -rn "class ShellCall\b\|shell_call_entered() noexcept = 0" src` 無結果；
   整個 `src/app_shell` 與 `src/explorer_host` 只剩 `ShellCallScope`（app_shell）與
   `ExplorerHost::ShellCallScope`（explorer_host）兩個 scope 型別。
2. `grep -n "shell_call_depth" src/app_shell/*.cpp src/app_shell/*.h` 只出現在
   `shell_reentry_guard.*`。
3. `PaneHost` 不再有 `shell_call_entered`／`shell_call_left`。
4. `src/core` 沒有新增任何 `windows.h`／HWND 相依。
5. 新測試涵蓋 §測試 的 6 項並通過；既有測試全綠。
6. `tests/release/shell_reentry_gate_check.ps1` 若存在於 ctest，仍通過且仍檢查同樣的不變式。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 交接區

- 未驗證：沒有在真機上重現「慢速 Shell 呼叫期間連點」來觀察重播與延後關閉；
  行為等價只由 `shell_reentry_guard_test` 的 6 個 case、既有 unit test 與
  `panedock_launch_smoke` 支撐。
