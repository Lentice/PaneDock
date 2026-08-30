# PD-139 — single-instance relay 對 stale/closing/hung HWND 靜默退出

Phase 7 · app_shell startup relay · Depends on: PD-084, PD-129, PD-131

- Source: 2026-08-30 close/startup audit loop。
- Priority: HIGH——第二次啟動只要 `FindWindowW` 找到一個尚未消失的 main HWND，就 `PostMessageW` 後立即 `return 0`。若該 HWND 正在 teardown、UI thread 卡在 Shell/AV/disk work、或訊息未被處理，第二次啟動沒有自己的 UI，也沒有提示。`CreateMutexW`／`OpenMutexW` 的非預期錯誤路徑也可能只寫 debug 後靜默結束。

## Goal

Make the second-launch decision observable and bounded:

1. Relay only when the existing window synchronously acknowledges that it is usable.
2. Treat a closing, destroyed, or unresponsive window as “keep waiting”; if the mutex remains held until the deadline, show the existing warning.
3. If the previous instance releases the mutex, reacquire it with the existing race check and launch exactly one fresh instance.
4. Show an English startup error for lock-inspection/lock-creation failures instead of silently returning success.

## Confirmed root cause and callers

- `relay_or_wait_for_existing_instance` is the sole caller from `wWinMain`; its current `FindWindowW` branch calls `PostMessageW` and returns `true` even when `PostMessageW` returns false.
- `PostMessageW` success means only that the message was queued. It does not prove the receiver processed it or that `AppState::closing_` is false. A window remains discoverable while `finish_shutdown` is destroying Shell views and before the mutex handle is closed.
- The activation handler currently always returns `0` after calling `activate_main_window_on_own_thread`, so the relay has no health/closing acknowledgement.
- A failed `OpenMutexW` is currently treated as “mutex released” regardless of `GetLastError`; a failed reacquire `CreateMutexW` logs and returns `true`, producing no UI for an indeterminate lock state.

## Binding constraints

`AGENTS.md`:

> Host-side locking and shutdown sequencing must be reentrancy-safe.

> Do not push branches, publish releases, or modify anything outside this repository without explicit approval.

> Event-driven idle path only. No busy loops, no polling timers.

> App UI text must be English. New non-trivial logic needs one focused runnable test or self-check.

The existing short startup wait is an interaction-time bounded relay, not an idle timer. Do not add a service, lock file, second process, or forced termination.

## Files to read and trace

- `src/app_shell/main.cpp`: `kActivateExistingInstanceMessage`, activation case in `window_proc`, `relay_or_wait_for_existing_instance`, `wWinMain`, `AppState::closing_`/`quit_requested`.
- `docs/tickets/PD-084-single-instance-activate-existing-window.md`, `docs/tickets/PD-129-single-instance-startup-race-relay.md`, and `docs/tickets/PD-131-single-instance-reacquire-race.md`.
- `tests/CMakeLists.txt`: register the focused source/self-check.

## Scope

1. Change the activation message handler to return a distinct “closing/unusable” result while retaining the existing same-thread foreground activation.
2. Replace the fire-and-forget activation post with bounded `SendMessageTimeoutW`; only an acknowledged usable result returns from the relay.
3. Continue the existing finite loop after timeout, send failure, or closing acknowledgement; preserve the existing fresh-mutex race check.
4. Distinguish “mutex object does not exist” from other `OpenMutexW` failures and show an English error for indeterminate lock state or failed fresh `CreateMutexW`.
5. Add one focused runnable source/self-check for the handshake and error branches; do not create a fake HWND/COM test seam.

## Non-goals

- Do not change the mutex name, single-instance guarantee, five-second deadline, foreground policy, session schema, or shutdown order.
- Do not force-kill or auto-restart an unresponsive previous instance.
- Do not make startup polling persistent or add a background thread/timer.
- Do not change the existing user-facing “already running but not responding” warning except where needed to reach it reliably.

## Acceptance criteria

1. An existing healthy window receives the activation request and the second process exits without creating a window or touching session data.
2. An existing window with `closing_`/`quit_requested`, a failed send, or a timeout does not cause the second process to silently exit; it waits for mutex release or shows the warning at the deadline.
3. Two waiters still use the PD-131 `ERROR_ALREADY_EXISTS` reacquire check and cannot both fall through to startup.
4. `OpenMutexW`/fresh `CreateMutexW` errors other than the normal “object absent” case show an English startup error and return nonzero/handled failure.
5. Focused self-check, build, CTest, and `git diff --check` pass.

## Agent checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/single_instance_relay_check.ps1
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
rg -n "SendMessageTimeoutW|kActivateExistingInstanceMessage|closing_|ERROR_FILE_NOT_FOUND|ERROR_ALREADY_EXISTS" src/app_shell/main.cpp
```

## Handoff requirements

- Record the handshake return values and timeout budget.
- Record how normal mutex disappearance remains the only fresh-launch path.
- Record live single-instance/closing-race validation separately from source self-check; do not call source matching runtime proof.

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-08-30 — implemented

- activation message 現在對 `closing_`/`quit_requested` 回傳 unusable；relay 改用 250 ms `SendMessageTimeoutW` handshake，只有明確的 healthy acknowledgement 才讓第二個 process 正常退出。
- timeout、send failure、closing acknowledgement 會繼續原本 5 秒 bounded wait；mutex 消失後仍經 PD-131 的 `ERROR_ALREADY_EXISTS` reacquire guard 才 fall through。`OpenMutexW` 非 `ERROR_FILE_NOT_FOUND` 與 fresh `CreateMutexW` failure 會顯示英文 error。
- 新增 `single_instance_relay_check.ps1` 並註冊 CTest；self-check、build、排除 live launch smoke 的完整 8/8 CTest（其中 8 個均 deterministic/source check）與 `git diff --check` 通過。
- 尚未取得真實「既有 instance 正在 Shell teardown 時第二次啟動」桌面 stress evidence；本票未宣稱該 runtime 情境已驗收。未新增 process、thread、timer、IPC 或 force kill。
