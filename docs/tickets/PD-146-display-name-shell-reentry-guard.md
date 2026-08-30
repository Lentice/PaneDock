# PD-146 — 補齊 display-name Shell lookup 的 re-entry shutdown guard

Phase 7 · app_shell lifecycle · Depends on: PD-140, PD-145

- Source: 2026-08-30 close/startup audit loop；PD-145 後的所有 direct Shell
  caller trace。
- Priority: HIGH——PD-140 的 `ShellCallScope` 沒有覆蓋
  `display_text_for_parsing_name()`；它在多個 steady-state UI 路徑直接呼叫
  `SHCreateItemFromParsingName`。若 Shell／AV／virtual-folder provider 在該
  呼叫中 re-enter `WM_CLOSE`，`shell_call_depth` 仍為零，close 可在 helper
  尚未返回時 destroy parent 與 live views，造成 dead HWND 或 COM teardown
  順序錯誤。

## Goal

讓所有 display-name Shell lookup 都經過既有 `ShellCallScope`，使 close／
`WM_ENDSESSION(TRUE)` 在 lookup 期間只記錄 deferred intent，待外層 Shell
call 返回後再沿既有 §9.4 teardown。Startup chrome、tab refresh、paint 中的
placeholder、pinned menu 與 pinned-location manager 都必須共用這個保護。

## Confirmed root cause and callers

- `src/app_shell/main.cpp::display_text_for_parsing_name` 會呼叫
  `SHCreateItemFromParsingName`，並在成功後呼叫
  `IShellItem::GetDisplayName`；目前函式沒有 `AppState` 或 scope。
- 已 trace 的 callers：
  `refresh_navigation_chrome`、`tab_display_text`（由
  `refresh_tab_strip` 與 `paint_tab_strip` 使用）、
  `refresh_pinned_locations_manager`、`refresh_startup_chrome`、
  `show_pinned_locations_menu`。
- `refresh_navigation_chrome`、tab paint、pinned menu/manager 的 steady-state
  caller 不一定位於其他 `ShellCallScope` 內。故只在 startup caller 外層補 scope
  會留下 sibling caller 的同一個 re-entry bug。
- PD-140 的 `window_proc` 只有在 `shell_call_depth != 0` 才會 defer close；
  未受保護的 helper 呼叫期間若 re-enter，`begin_shutdown` 會直接進入
  save／destroy sequence。

## Binding constraints

`docs/design-spec.md §9.2`:

> 單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。

`docs/design-spec.md §9.4`:

> destroy 全部 live `IExplorerBrowser` 必須先於 destroy pane HWND 與主視窗。

`docs/design-spec.md §11`:

> COM 失敗:記錄診斷事件,不得靜默忽略,不得使整個視窗不可用。

`AGENTS.md`:

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and
> internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe。

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> New non-trivial logic needs one focused runnable test or self-check.

App UI text remains English; this ticket adds no UI wording.

## Files to read and trace

- `src/app_shell/main.cpp`: `AppState`、`ShellCallScope`、
  `display_text_for_parsing_name`、`tab_display_text`、all callers listed above、
  `window_proc` close gate、`refresh_startup_chrome`。
- `tests/release/shell_reentry_gate_check.ps1` and `tests/CMakeLists.txt`:
  extend the existing source/lifecycle self-check。
- `docs/design-spec.md §9.2, §9.4, §11`、`docs/development.md` 的 Shell
  re-entry rules、`docs/tickets/PD-140-shell-call-reentry-shutdown-gate.md`、
  `docs/tickets/PD-145-defer-startup-shell-chrome.md`。

## Scope

1. Change `display_text_for_parsing_name` and the minimum related text helper
   signatures so the helper receives `AppState&`.
2. Hold one existing `ShellCallScope` over both Shell calls in the helper; keep
   non-virtual parsing names on the current no-Shell fast path.
3. Update every caller to pass the current state. Do not add per-caller ad hoc
   flags or duplicate Shell lookup logic.
4. Extend `shell_reentry_gate_check.ps1` to prove the shared helper owns the
   scope and all direct callsites route through the state-aware helper.

## Non-goals

- Do not change display-name fallback text, tab/menu wording, or Shell location
  resolution.
- Do not change `ExplorerHost`, COM ownership, startup realization order,
  persistence, schema, or shutdown order.
- Do not add a worker, timeout, retry, timer, async runtime, fake COM seam, or
  process isolation.
- Do not claim a Shell provider that never returns is interruptible; this ticket
  only closes the re-entry window around the call.

## Acceptance criteria

1. Both `SHCreateItemFromParsingName` and `GetDisplayName` in the shared helper
   execute while `ShellCallScope` is alive.
2. No caller invokes a state-less display-name helper; startup, normal tab/nav
   refresh, paint placeholder, pinned menu and manager all route through the
   guarded helper.
3. A close re-entered during the lookup is deferred by the existing gate and
   cannot destroy the parent or live views before the helper returns.
4. Non-virtual parsing names retain the existing direct-text fast path and all
   visible English labels remain unchanged when Shell calls succeed.
5. Focused self-check, build, CTest and `git diff --check` pass.

## Agent checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/shell_reentry_gate_check.ps1
cmake --build build
ctest --test-dir build -E panedock_launch_smoke --output-on-failure
git diff --check
```

With a real desktop, the existing elevated `panedock_launch_smoke` remains the
ordinary startup/close smoke. It does not prove a provider-induced re-entry;
record that limitation separately.

## Handoff requirements

- Record the shared helper signature, both guarded Shell calls, and every caller
  updated to pass `AppState`.
- Record deterministic source/build/runtime smoke results separately from real
  Shell-extension or anti-virus re-entry evidence.
- Record that non-virtual names do not enter the scope and that no new async or
  fake-COM mechanism was introduced.

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-08-30 — implemented

- `display_text_for_parsing_name` 現在接收 `AppState&`，只在 virtual-folder
  parsing name 需要 Shell 時建立既有 `ShellCallScope`；scope 覆蓋
  `SHCreateItemFromParsingName` 與 `IShellItem::GetDisplayName`。
- `tab_display_text` 也傳遞 state；navigation chrome、tab refresh/paint
  placeholder、pinned menu、pinned-location manager 與 startup chrome 全部
  經 state-aware helper，沒有保留 state-less caller。非 `::` 名稱仍走原本的
  直接文字 fast path。
- 擴充既有 `shell_reentry_gate_check.ps1`，檢查 shared helper 的 scope、兩個
  Shell call 與所有 caller routing。focused check PASS；LLVM-MinGW Release
  build PASS；非 launch CTest 10/10 PASS；`git diff --check` PASS；elevated
  `panedock_launch_smoke` 1/1 PASS。
- 本次沒有新增 thread、timer、timeout、fake COM、process isolation 或 UI
  wording；source/runtime smoke 不能證明特定 anti-virus 或第三方 Shell
  extension 的 re-entry latency。下輪繼續 audit 其他 setup failure、同步
  session I/O 與未包住的 OS/COM return path。
