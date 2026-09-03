# PD-172 — deferred shutdown 訊息在 nested Shell call 內被吃掉，`shutdown_message_queued` 未清除，程式可能永遠關不掉

Phase 7 · core / app_shell · Depends on: PD-140, PD-162

## 來源

2026-09-03 三方稽核（Codex 獨立提出；OpenCode 判定 shutdown 機制「乾淨」，兩者結論不同）。經 fork 逐行核對 `src/core/shutdown.cpp` 與 `src/app_shell/main.cpp` 的實際狀態轉移，判定為 CONFIRMED-NEW，且根因可在原始碼上完整推導（不需 runtime repro 即可確認狀態機邏輯缺口）。

## 背景與現況

三段程式碼構成一個無法自行恢復的狀態：

**1. 訊息被分派但無法作用**（`src/core/shutdown.cpp:57-67`）：

```cpp
case ShutdownEvent::deferred_shutdown_ready:
    if (state_.closing_ || state_.shell_call_depth != 0 ||
        !state_.shutdown_deferred)
        return ShutdownAction::none;          // ← depth != 0 時直接返回，未清除 shutdown_message_queued
    state_.shutdown_deferred = false;
    state_.shutdown_message_queued = false;   // ← 只有成功路徑才會清除
    ...
```

**2. 後續不會重新 post**（`src/core/shutdown.cpp:45-51`）：

```cpp
case ShutdownEvent::shell_call_left:
    if (state_.shell_call_depth == 0) return ShutdownAction::none;
    --state_.shell_call_depth;
    return state_.shell_call_depth == 0 && state_.shutdown_deferred &&
                   !state_.shutdown_message_queued      // ← 永遠是 false，因為上面沒清除
               ? ShutdownAction::defer
               : ShutdownAction::none;
```

**3. 唯一的 post 路徑被跳過**（`src/app_shell/main.cpp:593-610`）：

```cpp
void finish_shell_call(AppState& state) noexcept {
    const auto action = state.shutdown_sequence.step(
        panedock::core::ShutdownEvent::shell_call_left);
    if (action != panedock::core::ShutdownAction::defer ||
        state.shutdown_message_queued || state.main_window == nullptr)
        return;                                          // ← action 永遠不是 defer，直接返回
    ...
    if (PostMessageW(state.main_window, kDeferredShutdownMessage, 0, 0))
```

`kDeferredShutdownMessage` 只由 `finish_shell_call`（:601）與 `main.cpp:4936` 這兩處 post；`window_proc` 的 gate（`main.cpp:5102`）**刻意允許**這個訊息在 `closing_`/`shutdown_deferred` 期間通過，dispatch 到 `main.cpp:5308-5315` 觸發 `deferred_shutdown_ready`。`shutdown_message_queued` 只有三個地方會被清除：上述成功路徑、`deferred_shutdown_queue_failed`（:69-72）、`teardown_started`（:141-148）——後兩者在此情境都不會發生。

**完整失效序列：** 使用者按關閉 → `close_requested` 設 `shutdown_deferred = true` → 最外層 Shell 呼叫結束時 `finish_shell_call` post 出 `kDeferredShutdownMessage`、設 `shutdown_message_queued = true` → **但該訊息在下一個 nested Shell 訊息幫浦（例如 `explorer_host.cpp:380` 那個被 `ShellCallScope` 包住的 modal `TrackPopupMenuEx` 選單迴圈）內被分派**，此時 `shell_call_depth != 0` → reducer 回 `none`、旗標不清 → 訊息已被消耗，佇列裡沒有了 → 之後每一次 `shell_call_left` 都因為 `shutdown_message_queued` 仍為 `true` 而回 `none`，永遠不會重新 post → `shutdown_deferred` 永遠是 `true`，`closing_` 永遠是 `false`，主視窗永遠不會被銷毀。

## 為什麼這是真的問題

`docs/design-spec.md §9.4` 固定了 save → destroy all live views → destroy parent → exit loop 的關閉序列，而本缺陷會讓整個序列**永遠不啟動**：使用者按了關閉，視窗留在畫面上且拒絕一般互動（`PD-140` 的 gate 會擋掉一般訊息），程式看起來「卡死」，只能用工作管理員強制結束——而強制結束又會被 `PD-143`/`PD-025` 的機制記為不乾淨關閉，下次啟動誤報崩潰復原。這正是使用者本次要求稽核的「卡住／UI 無法響應」類別中最嚴重的一種。

## Fix 方向

**最小修正在 `src/core/shutdown.cpp`（`core` 內、無 HWND/COM，是本專案唯一的自動化測試 seam）：**

在 `deferred_shutdown_ready` 因為 `shell_call_depth != 0` 而無法作用時，把 `shutdown_message_queued` 清為 `false`——語意上這代表「這則訊息已經被消耗掉，佇列裡已經沒有待處理的 deferred shutdown 訊息了」。這樣最外層 Shell 呼叫返回時，`shell_call_left` 的 `shutdown_deferred && !shutdown_message_queued` 條件會重新成立，回傳 `defer`，`finish_shell_call` 就會重新 post 一次訊息，關閉序列得以繼續。

必須確認這個改動不會造成重複 post（`finish_shell_call` 自己也檢查 `state.shutdown_message_queued`，且 `deferred_shutdown_queued` 只在 `shutdown_deferred` 為真時設旗標，兩者搭配仍能維持「同時最多一則在途訊息」的既有不變式）。實作者若找到更好的等價修法，可以改用，但必須在交接區寫出為何等價，並保留同一組不變式。

## 綁定限制（引用）

- `docs/design-spec.md §9.4`（關閉序列固定為 save → destroy views → destroy parent → exit loop）。
- `AGENTS.md`：「Keep `src/core` free of HWND, COM and `windows.h`. It is the only automated test seam in this project」——本票的修正正好落在 `core` 內，**必須**附一個 reducer 層級的單元測試。
- `AGENTS.md`：「Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.」
- `AGENTS.md`：「Prefer the smallest working change.」——本票是狀態機的一行級修正，不要重新設計 `PD-162` 的 reducer 架構。

## 檔案與範圍

- `src/core/shutdown.cpp`：`deferred_shutdown_ready`（:57-67）、`shell_call_left`（:45-51）、`deferred_shutdown_queued`（:53-55）、`deferred_shutdown_queue_failed`（:69-72）。
- `src/core/shutdown.h`：`ShutdownState` 欄位定義（確認 `shutdown_message_queued`/`shell_call_depth` 語意註解需不需要一併更新）。
- `src/app_shell/main.cpp`：`finish_shell_call`（:593-610）、`ShellCallScope`（:612-628）、`window_proc` 的訊息 gate（:5097-5103）、`kDeferredShutdownMessage` 的 dispatch（:5308-5315）與另一個 post 點（:4936）。
- `src/explorer_host/explorer_host.cpp`：被 `ShellCallScope` 包住的 modal 選單迴圈（:380 一帶的 `TrackPopupMenuEx`）——這是最容易觸發 nested pump 的實際位置。
- `tests/`：既有的 `panedock_core_shutdown_test`（`build/tests/panedock_core_shutdown_test.exe` 已存在，新測試加在這裡）。
- `docs/tickets/PD-140-shell-call-reentry-shutdown-gate.md`、`PD-162-shutdown-sequence-decision-reducer.md`。

## Scope

1. 修正 `deferred_shutdown_ready` 在 `shell_call_depth != 0` 時的旗標處理，使關閉序列在最外層 Shell 呼叫返回後能自行恢復。
2. 在 `panedock_core_shutdown_test` 新增一個 reducer 測試，重現本票的完整失效序列（close_requested → shell_call_entered → shell_call_left(post) → deferred_shutdown_queued → shell_call_entered → deferred_shutdown_ready(depth!=0) → shell_call_left → **必須**得到 `defer`），確認修正後會重新 post。
3. 確認「同時最多一則在途 deferred shutdown 訊息」的既有不變式沒有被破壞（可在同一個測試裡驗證不會連續兩次 `defer`）。

## Non-goals

- 不重新設計 `PD-162` 的 shutdown reducer 架構或事件列表。
- 不改變 `window_proc` 刻意允許 `kDeferredShutdownMessage` 通過 gate 的既有決策（那是 `PD-140` 的正確設計，不是本缺陷的成因）。
- 不處理 `PD-171`（一般命令在 Shell 重入期間改動 vector）——兩者都源於 `PD-140` 的機制邊界，但缺陷與修法各自獨立。
- 不用 `TerminateProcess`／強制結束當作退路。

## Acceptance Criteria

1. 新增的 reducer 測試在修正前失敗、修正後通過，且明確涵蓋「訊息在 depth != 0 時被消耗」這一步。
2. 既有 `panedock_core_shutdown_test` 的所有案例不回歸。
3. 一般關閉路徑（無 nested Shell call）行為完全不變：仍然是 save → destroy views → destroy parent → exit loop，且 clean marker 行為（`PD-143`）不受影響。
4. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "shutdown_message_queued|shell_call_depth|deferred_shutdown" src/core/shutdown.cpp src/app_shell/main.cpp
```

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-09-03 實作交接

- `src/core/shutdown.cpp` 的 `deferred_shutdown_ready` 先保留既有 `closing_`／非 deferred 狀態的 no-op；當 `shell_call_depth != 0` 時，清除已被 nested Shell pump 消耗的 `shutdown_message_queued` 並保留 `shutdown_deferred`，讓最外層 `shell_call_left` 再次回傳 `defer`。depth 為 0 的既有成功路徑與 save／clean-marker／teardown action 未改變。
- `deferred_shutdown_queued` 仍是唯一把 queued flag 設為 true 的 reducer event，且只有 `shutdown_deferred` 為 true 才會設旗標；`shell_call_left` 只在 depth 回到 0 且 queued flag 為 false 時回傳 `defer`。因此 nested 消耗後只會重新 post 一次，重新 queued 後不會重複產生 defer。
- `tests/unit/core_shutdown_test.cpp` 新增 `test_consumed_deferred_shutdown_message_is_reposted`，完整覆蓋 close → outer Shell leave/defer → queue → nested Shell enter → deferred message consumed at non-zero depth → queued flag cleared → outer leave/defer → re-queue → save；並確認 depth 已為 0 時重複 `shell_call_left` 不再產生第二個 defer。既有 normal close test 同時保留 save → destroy views → destroy window → clean marker 順序驗證。
- 本票未修改 `src/core/shutdown.h`、`src/app_shell/main.cpp` 或 `src/explorer_host/explorer_host.cpp`；既有 `window_proc` 對 `kDeferredShutdownMessage` 的允許、`finish_shell_call` 的 post/fallback 與 Shell teardown 順序均維持不變，未涉及 PD-171、session schema、threading 或強制終止。
- Checks：新增測試在修正前以 `cmake --build build --target panedock_core_shutdown_test` 後執行 `build\\tests\\panedock_core_shutdown_test.exe` 失敗（queued flag 未清除、未回傳 defer）；修正後同一測試 PASS。Release configure/build PASS；`ctest --test-dir build --output-on-failure` elevated 17/17 PASS；`rg -n "shutdown_message_queued|shell_call_depth|deferred_shutdown" src/core/shutdown.cpp src/app_shell/main.cpp` 已執行。未使用 `TerminateProcess`，未做超出本票範圍的實機 Shell-extension re-entry 壓力測試。
