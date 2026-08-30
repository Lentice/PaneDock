# PD-144 — startup failure warnings 不得互相覆蓋，deferred Shell failure 必須提示

Phase 7 · app_shell startup · Depends on: PD-130, PD-135, PD-141, PD-142

- Source: 2026-08-30 close/startup audit loop；PD-141/142 後的 startup caller
  trace。
- Priority: HIGH——啟動時 session save failure、tab drag-and-drop failure 與
  deferred `IExplorerBrowser` failure 共用一個 `startup_warning_message`。
  先發生的 warning 會讓後發生的 warning 被覆蓋或被 `.empty()` gate 靜默，
  使用者可能看到「storage problem」卻不知道 pane 是空白，或看到 drag-drop
  問題卻不知道 Shell pane 沒有建立。若 `CreateWindowExW` 失敗，既有
  warning 甚至完全不顯示。

## Goal

讓每個已知 startup failure 都至少有一次清楚的英文提示：

- 同一個 pre-window/recoverable warning queue 內的訊息合併顯示，不互相覆蓋；
- deferred Shell realization failure 無論既有 warning 是否存在，都額外顯示
  既有 Shell warning；
- fatal top-level window creation failure 時，也顯示已收集的 recoverable
  warning（若有）。

保留現有 MessageBox UX、PD-135 的 close guard、PD-140 的 re-entry gate，
不建立 notification framework。

## Confirmed root cause and callers

- `wWinMain` startup `save_now(state)` 失敗時直接賦值
  `startup_warning_message`（`src/app_shell/main.cpp:6095`）。
- `window_proc(WM_CREATE)` 的 frame/drag-drop failure path 也直接賦值或只在
  `.empty()` 時賦值（約 `:5061-5077`）。
- `kDeferredRealizeMessage` 只有在 `startup_warning_message.empty()` 時才
  `MessageBoxW`（約 `:5097`）；PD-141 已把 Shell realization 移到該 deferred
  path，因此這個條件現在會直接吞掉「pane 是空白」的獨立 failure。
- `CreateWindowExW` 的 `window == nullptr` branch 只顯示
  `startup_error_message`（約 `:6129-6135`），不顯示同一 `AppState` 已收集的
  storage warning。

## Binding constraints

`docs/design-spec.md §11`:

> COM 失敗:記錄診斷事件,不得靜默忽略,不得使整個視窗不可用

`docs/design-spec.md §9.3`:

> 其餘 pane 延後 realize,避免被網路或離線路徑阻塞

`AGENTS.md`:

> App UI text must be English. No Chinese strings ship in the binary。

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and
> internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe。

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> New non-trivial logic needs one focused runnable test or self-check.

## Files to read and trace

- `src/app_shell/main.cpp`: `AppState` warning fields, `save_now` startup caller,
  `WM_CREATE`, `kDeferredRealizeMessage`, `wWinMain` fatal branch and all
  `startup_warning_message`/`startup_error_message` references。
- `tests/release/shutdown_state_check.ps1` and
  `tests/release/startup_frame_order_check.ps1`: existing source self-check style。
- `tests/CMakeLists.txt`: confirm existing checks are registered。
- `docs/design-spec.md §9.3, §11` and `docs/development.md` UI/error rules。
- `docs/tickets/PD-130-startup-recoverable-failure-never-blocks-window.md`,
  `PD-135-startup-dialog-close-destroyed-hwnd.md`,
  `PD-141-startup-frame-before-shell-realize.md`, and
  `PD-142-startup-no-prewindow-shell-label-lookup.md`。

## Scope

1. Add one small append operation for recoverable startup warnings; preserve all
   messages instead of overwriting. Use the existing English strings.
2. Use it for startup session-save, frame/drag-drop and any sibling warning path.
3. Remove the deferred Shell failure `.empty()` suppression; show its existing
   warning whenever the deferred realization result fails. Keep the warning in a
   separate MessageBox because it happens after the earlier queue was displayed.
4. In the `window == nullptr` branch, display the collected warning after the fatal
   UI error (owner `nullptr`) before COM/mutex cleanup.
5. Extend one existing runnable source check to cover append/no-suppression/fatal
   warning ordering; no new framework or live Shell fake。

## Non-goals

- Do not change Shell realization order, session schema, persistence semantics,
  startup marker timing, warning wording, or close/shutdown sequencing.
- Do not add a notification framework, tray icon, retry loop, timer, worker,
  timeout, async runtime or `TerminateProcess`.
- Do not show a modal MessageBox inside `WM_CREATE`; warnings remain after
  `CreateWindowExW` returns or in the existing deferred message handler.
- Do not claim source checks prove a real offline/anti-virus Shell failure; record
  live desktop limits separately.

## Acceptance criteria

1. If session save and drag-drop warnings both occur, the post-create warning
   contains both English messages; neither assignment erases the other.
2. If the deferred Shell realization fails while any earlier warning exists, the
   Shell-specific warning still appears once; a blank pane is never silent.
3. If `CreateWindowExW` returns null after a recoverable warning was collected, the
   fatal UI error and the recoverable warning are both shown before exit.
4. A close during any warning MessageBox still follows PD-135 and does not run
   deferred startup work against a destroyed HWND.
5. Focused self-check, build, CTest and `git diff --check` pass; existing wording
   remains English.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/startup_frame_order_check.ps1 -SourcePath src/app_shell/main.cpp
ctest --test-dir build -E panedock_launch_smoke --output-on-failure
git diff --check
```

When a real desktop is available, use the existing elevated launch smoke for
ordinary startup/close. Do not invent a UI automation test for MessageBox text;
manually record those paths if a real failure can be induced safely.

## Handoff requirements

- Record the exact append/display behavior for pre-window warnings, deferred Shell
  failure, and fatal window creation failure.
- Record deterministic source checks separately from real disk/AV/Shell failure
  observations.
- Record that slow/unreturning synchronous startup calls remain bounded by the
  single-STA/spec decision and were not hidden by a timeout or worker.
- List any remaining startup or close issue for the next audit round.

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-08-30 — implemented

- 新增 `append_startup_warning`，startup session-save、frame/Shell failure 與
  tab drag-drop warning 以空行合併，不再覆蓋先前訊息；保留既有英文 wording。
- `kDeferredRealizeMessage` 的 Shell failure 現在無條件顯示既有 warning，
  因而即使先前已顯示 storage/drag warning，空白 pane 仍會另行告知。
- `CreateWindowExW` 返回 null 時，在 fatal UI error 後也顯示已收集的
  recoverable warning；所有 MessageBox 仍在 `WM_CREATE` 外，PD-135 close guard
  與 PD-140 re-entry gate 未變。
- `startup_frame_order_check.ps1` 新增 append path、deferred no-suppression
  與 fatal-branch warning checks。focused source check PASS；LLVM-MinGW
  Release build PASS；非 launch CTest 10/10 PASS；`git diff --check` PASS；
  elevated `panedock_launch_smoke` 1/1 PASS。
- 未刻意注入 disk/AV/Shell failure 或以 UI automation 操作 MessageBox；來源
  檢查不是 runtime failure proof。同步 startup session read/false-marker save
  以及永不返回的 OS call 仍受 single-STA/§9.2 邊界約束。
- 後續 audit 注意：`wWinMain` 在 frame 顯示後、message loop/deferred realize
  前仍會同步做 fixed-label lookup 與 `refresh_tab_strips`；另外若
  `SetProcessDpiAwarenessContext` 或 diagnostic mitigation policy 失敗，目前
  只有 debug event、沒有 user-facing prompt，需評估是否達到重要問題門檻。
