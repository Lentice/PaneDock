# PD-141 — startup 先顯示可互動 frame，再進行 active/其餘 Shell realization

Phase 7 · app_shell startup · Depends on: PD-093, PD-109, PD-130, PD-140

- Source: 2026-08-30 close/startup audit loop。
- Priority: HIGH——`WM_CREATE` 目前在 `CreateWindowExW` 返回前同步呼叫
  `apply_layout`，active pane 的 `IExplorerBrowser::Initialize`、首次
  `BrowseToObject` 或 virtual-folder label lookup 若被慢速磁碟、離線路徑、
  Shell extension 或 anti-virus 卡住，使用者看不到主視窗；第二個 instance
  也只能看到 mutex 而看不到可啟用的 HWND。

## Goal

讓 startup 先建立並顯示主視窗、側邊欄與 pane chrome 的 frame，再由既有
  event-driven deferred message 進行 Shell realization。Shell realization 的
  順序仍是 active pane first，其餘可見 pane later；任何 failure 都保留在
  既有的 recoverable warning 路徑，不因等待或單一壞 location 讓整窗沒有 UI。

## Confirmed root cause and startup flow

- `src/app_shell/main.cpp::window_proc(WM_CREATE)` 設定
  `startup_realize_pending` 後呼叫 `apply_layout(window, *state)`。
- `apply_layout` 在 pending 狀態仍以 `index == active` 允許
  `ExplorerHost::initialize()`；`initialize()` 內包含 COM 建立、
  `IExplorerBrowser::Initialize`、`Advise` 與首次 `navigate()`。
- `wWinMain` 只有在 `CreateWindowExW` 返回後才呼叫 `ShowWindow`／`UpdateWindow`。
  因此 active pane 的任何 Shell slow path 都發生在使用者可見 UI 之前。
- `WM_CREATE` 前的 `display_text_for_parsing_name` 也可能對 virtual-folder
  parsing name 呼叫 `SHCreateItemFromParsingName`;這不是必要的 startup gate，
  應移到 window 可見後，避免把 tab/pinned label 解析混進 half-visible create。
- `read_session` 與 PD-025 要求的 startup `clean_shutdown=false` marker save
  仍在建立主視窗前；單一 STA 且 spec 禁止自建 worker/async runtime，因此本票
  不假裝能中斷一個卡死的 filesystem/Shell call。該邊界必須留在交接區，不能
  用 timeout、busy loop 或 background thread 掩蓋。

## Binding constraints

`docs/design-spec.md §9.2`:

> 單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。

`docs/design-spec.md §9.3`:

> 建立主視窗與側邊欄,套用視窗位置

> 套用 active Group 的版型,先 realize active pane 的 active tab

> 其餘 pane 延後 realize,避免被網路或離線路徑阻塞

`AGENTS.md`:

> Event-driven idle path only. No busy loops, no polling timers。

> Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs
> persist as data and are realized on activation。

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and
> internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe。

> App UI text must be English. No Chinese strings ship in the binary。

## Files to read and trace

- `src/app_shell/main.cpp`: `AppState` startup flags, `apply_layout`,
  `window_proc(WM_CREATE)`, `kDeferredRealizeMessage`, `wWinMain`, startup
  warning/error branches, `display_text_for_parsing_name`, and all callers of
  `refresh_tab_strips`/`display_text_for_parsing_name` touched by this change。
- `src/explorer_host/explorer_host.cpp/.h`: `initialize`, `navigate`, and
  lifetime contract; do not alter the host contract unless a direct continuation
  bug is found.
- `src/core/session.cpp/.h`: read result and startup clean-marker contract; no
  schema change in this ticket。
- `docs/design-spec.md §9.2, §9.3, §11` and `docs/development.md` startup/Shell
  rules。
- `docs/tickets/PD-093-startup-eagerly-realizes-all-panes-violates-deferred-realize-spec.md`,
  `PD-109-normal-mode-launch-shows-no-main-window.md`,
  `PD-130-startup-recoverable-failure-never-blocks-window.md`,
  `PD-135-startup-dialog-close-destroyed-hwnd.md`, and
  `PD-140-shell-call-reentry-shutdown-gate.md`。
- `tests/CMakeLists.txt` and existing release/source self-check scripts。

## Scope

1. Add the smallest state/branch needed for `WM_CREATE` to lay out and create
   chrome without calling `ExplorerHost::initialize` or another avoidable Shell
   label lookup before `CreateWindowExW` returns.
2. After the top-level window has been shown, use the existing one-shot
   `kDeferredRealizeMessage` path to realize the active pane first, focus it, then
   realize the remaining visible panes. Reuse `apply_layout`; do not add a second
   timer, worker, polling loop, or fake COM seam.
3. Preserve the existing `startup_realize_generation` invalidation and PD-140
   deferred-shutdown checks. If close or confirmed system shutdown arrives during
   the active or deferred realization, stop the current continuation and let the
   existing shutdown path own teardown.
4. Move startup-only virtual-folder display-name lookups (fixed pinned labels and
   tab/navigation chrome if they are currently evaluated in `WM_CREATE`) to after
   the window is visible. Keep the same final text and English UI behavior.
5. Add one focused runnable source/lifecycle self-check for the frame-before-Shell
   ordering and active-first deferred continuation.

## Non-goals

- Do not move Shell work to a worker thread, add an async runtime, add a timeout or
  retry loop, or call `TerminateProcess`.
- Do not change session schema, `read_session`, `write_session`, clean marker
  semantics, atomic replacement, or backup behavior.
- Do not realize inactive tabs or hidden panes, and do not change the steady-state
  one-live-view-per-visible-pane rule.
- Do not redesign startup dialogs or combine every possible warning into a new
  notification framework; retain the existing English MessageBox paths. A separate
  warning aggregation issue may be ticketed after this lifecycle fix.
- Do not alter `src/core` or create an `IExplorerBrowser` fake.
- Do not claim that a synchronous OS/AV/filesystem call that never returns is
  interruptible; record that boundary separately from the fixed no-visible-frame
  bug.

## Acceptance criteria

1. `WM_CREATE` returns without calling `ExplorerHost::initialize` for the active
   pane; `ShowWindow`/`UpdateWindow` occur before the first startup realization
   message invokes Shell initialization.
2. The first deferred realization pass initializes the active pane first, then
   remaining visible panes, and the final steady state matches the current layout
   and location behavior.
3. A close/shutdown re-entry during either pass cannot continue into callbacks,
   focus, navigation, or another layout after PD-140's deferred gate is set.
4. Virtual-folder label lookup is not an avoidable pre-window startup blocker;
   normal labels and pinned menu labels retain their existing final text.
5. Build, focused self-check, CTest, and `git diff --check` pass. The handoff
   distinguishes deterministic source checks from real offline/AV latency evidence.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/startup_frame_order_check.ps1
ctest --test-dir build -E panedock_launch_smoke --output-on-failure
git diff --check
```

Runtime acceptance, when a real desktop is available, should use Release normal
and `--diagnostic` launches with a saved unreachable/slow location and record
first visible-window time separately from deferred pane completion. Do not use a
forced kill as proof of graceful behavior.

## Handoff requirements

- List the exact `WM_CREATE`/`wWinMain` ordering and the active-first/deferred pass.
- Record whether normal and diagnostic real-desktop slow/offline-path checks were
  run; do not call source matching a runtime proof.
- State that pre-window `read_session`/startup marker save remain synchronous by
  the single-STA/spec boundary, and that no timeout/worker/polling mechanism was
  added.
- Record build, focused check, CTest and diff results, plus any remaining startup
  warning/aggregation issue for the next audit round.

## 交接區

<!-- 實作 agent 填寫, append-only -->

- 實作結果：`WM_CREATE` 只建立 child controls、sidebar、tab chrome 與幾何；
  `apply_layout` 受 `startup_frame_only` gate 保護，不會在
  `CreateWindowExW` 返回前呼叫 `ExplorerHost::initialize`。`wWinMain` 先呼叫
  `ShowWindow`／`UpdateWindow`，再解除 gate、補 fixed pinned labels 與 tab
  chrome；既有 `kDeferredRealizeMessage` 透過
  `realize_startup_panes` 先 realize active pane、focus，再 realize 其餘可見
  panes。
- PD-140 的 Shell re-entry/shutdown gate 保留在 sidebar 建立、label lookup、
  focus 與兩次 layout pass；若 close 或 Windows shutdown 在 Shell call 內重入，
  會停止後續 startup continuation。
- 已執行：LLVM-MinGW Release configure/build、
  `startup_frame_order_check.ps1`、`ctest --test-dir build -E
  panedock_launch_smoke --output-on-failure`（10/10）、提升權限後的
  `ctest --test-dir build -R "^panedock_launch_smoke$" --output-on-failure`
  （1/1，實際存取 `%LOCALAPPDATA%\PaneDock`，graceful close/code 0）、
  `git diff --check`，均通過。
- 尚未有真實 offline/slow-disk/anti-virus latency 測量；本票只修正 Shell
  slow path 阻塞可見 frame 的順序，不宣稱同步 OS call 可被中斷。依單一 STA
  與 §9.2，startup 前的 `read_session`／`clean_shutdown=false` marker save
  仍同步執行，沒有加入 worker、timeout、retry 或 polling。
- 後續 audit 注意：startup warning 目前仍可能在不同 failure path 互相覆蓋，
  且 deferred Shell failure 在已有 startup warning 時不再額外提示；另有
  normal close 在 Shell teardown 前寫入 `clean_shutdown=true` 的 force-kill
  視窗，需另開 ticket 評估，未混入本票。
