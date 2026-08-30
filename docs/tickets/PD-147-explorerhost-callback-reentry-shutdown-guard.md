# PD-147 — `ExplorerHost` Shell callback 未進入 app re-entry gate，close 可在 callback 內 teardown

Phase 7 · explorer_host / app_shell lifecycle · Depends on: PD-140, PD-146

- Source: 2026-08-30 close/startup audit loop；PD-146 後重新 trace 所有
  `ExplorerHost` Shell-originated callback。
- Priority: HIGH——`Site::OnNavigationComplete`、`Site::OnNavigationFailed` 與
  `ViewCallback::MessageSFVCB` 可在 app 沒有外層 `ShellCallScope` 時由 Shell
  回呼。它們仍會執行 COM/view 操作、Shell callback chain 或 app callback；若
  這些操作泵出 STA 訊息，`WM_CLOSE` 會看到 depth=0，直接 destroy live view
  與 parent，callback 返回後再使用已 teardown 的 host/COM 狀態。

## Goal

讓所有由 Shell 進入 `ExplorerHost` 的 callback，在 callback 內容與其同步
callback chain 期間都使用 app 現有的 re-entry gate。close／confirmed
`WM_ENDSESSION(TRUE)` 必須等 callback 完成、最外層 gate 返回後，才走既有
§9.4 teardown；不改變 callback 的可見行為或 COM ownership。

## Confirmed root cause and callers

- `src/explorer_host/explorer_host.cpp::ExplorerHost::navigation_complete`
  會呼叫 `view_window`、`IExplorerBrowser::GetCurrentView`、
  `IShellView::QueryInterface`、`IShellFolderView::SetCallback`、
  `SHCreateItemFromIDList`、`IShellItem::GetDisplayName`，最後呼叫 app 的
  `navigation_callback_` 與 `selection_changed()`。
- `Site::OnNavigationComplete` 是 Shell 事件入口；在非同步導覽完成時不一定
  位於 app 既有 `ShellCallScope` 內。同步 `BrowseToObject` 已有外層 scope，
  但不能涵蓋 sibling 的非同步事件入口。
- `ViewCallback::MessageSFVCB` 會先呼叫 Shell 提供的
  `previous_->MessageSFVCB`，再呼叫 host 的 selection callback；這整段同樣
  沒有 app scope。
- `navigation_failed()` 雖主要更新 Win32 error panel，但會建立 child HWND
  並呼叫 app callback；若重入 close，host callback 可能在 parent/view 被
  destroy 後繼續執行，故同一個 callback guard 必須覆蓋它。
- PD-140 的 `window_proc` 只在 `shell_call_depth != 0` 時 defer close；PD-146
  只修正 app 內 display-name helper，沒有改 `ExplorerHost` callback 入口。

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

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it,
> or the instance leaks. Never destroy a parent HWND while a hosted view is alive.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> New non-trivial logic needs one focused runnable test or self-check.

App UI text remains English; this ticket adds no UI wording.

## Files to read and trace

- `src/explorer_host/explorer_host.h/.cpp`: `Site`, `ViewCallback`,
  `navigation_complete`, `navigation_failed`, `destroy` and callback storage。
- `src/app_shell/main.cpp`: `AppState`, `ShellCallScope`、其 deferred-shutdown
  completion logic、`apply_layout` host initialization、`window_proc` close gate。
- `tests/release/shell_reentry_gate_check.ps1` and `tests/CMakeLists.txt`:
  extend the existing source/lifecycle self-check。
- `docs/design-spec.md §9.2, §9.4, §11`、`docs/development.md` 的 COM lifetime
  與 Shell re-entry 規則、`docs/tickets/PD-140-shell-call-reentry-shutdown-gate.md`、
  `docs/tickets/PD-146-display-name-shell-reentry-guard.md`。

## Scope

1. Add one minimal non-owning callback hook from `ExplorerHost` to the app's
   existing Shell-call depth gate. It must not add a fake COM abstraction,
   worker, timer, or new shutdown state machine。
2. Use a small RAII scope around `ViewCallback::MessageSFVCB`,
   `ExplorerHost::navigation_complete`, and `ExplorerHost::navigation_failed`.
   The scope must cover Shell calls and synchronous app/Shell callback chains。
3. Install the hook before each host `initialize` so the first synchronous and
   later asynchronous events use the same gate. Keep it valid across host
   destroy/re-realize while the owning `AppState` is alive。
4. Reuse PD-140's existing enter/leave/deferred-message behavior; do not copy
   close logic into `explorer_host`。
5. Extend the existing shell re-entry self-check to prove the host callback
   guard and app hook wiring.

## Non-goals

- Do not change `IExplorerBrowser` event ordering, callback ownership,
  `SetCallback` restoration, navigation semantics, or error-panel wording。
- Do not add a timeout, worker thread, async runtime, polling loop, process
  isolation, fake COM object, or direct process termination。
- Do not claim that a Shell extension or anti-virus provider that never returns
  is interruptible; the guard only prevents close teardown from racing a
  callback that is still executing。
- Do not change the §9.4 save → view destroy → HWND destroy → loop exit order。

## Acceptance criteria

1. `navigation_complete`, `navigation_failed`, and `ViewCallback::MessageSFVCB`
   enter the same app-provided gate before any host/Shell work and leave it after
   all synchronous callback work returns。
2. The app installs the hook before `ExplorerHost::initialize`; re-realizing a
   destroyed pane does not leave a stale or missing gate。
3. A close re-entered from any covered host callback is deferred until the
   callback scope returns, so the parent HWND and live browser are not destroyed
   mid-callback。
4. Existing synchronous navigation remains safe through nested scopes, and all
   existing build/CTest behavior remains intact。
5. Focused self-check, Release build, CTest, `git diff --check`, and elevated
   ordinary launch smoke pass。

## Agent checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/shell_reentry_gate_check.ps1
cmake --build build
ctest --test-dir build -E panedock_launch_smoke --output-on-failure
git diff --check
ctest --test-dir build -R "^panedock_launch_smoke$" --output-on-failure
```

The final command needs a real writable `%LOCALAPPDATA%\PaneDock`; it must only
target the process started by the test. Source checks and ordinary launch smoke
do not prove a provider-induced callback re-entry or a never-returning AV hook。

## Handoff requirements

- Record the hook shape, every covered callback, and the app enter/leave reuse。
- Record that the hook is installed before `initialize` and remains valid across
  destroy/re-realize while `AppState` owns the host array。
- Separate deterministic source/build/runtime smoke from real Shell-extension
  or anti-virus re-entry evidence。

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-08-30 — implemented

- `ExplorerHost` 新增 non-owning `ShellCallCallback` 與 nested RAII
  `ShellCallScope`；app 在每次 `initialize` 前把它接到既有
  `shell_call_depth`/`finish_shell_call`。callback 設定不在 `destroy()` 清除，
  因此同一個 `AppState` 內的 destroy/re-realize 不會出現缺 hook 或 scope
  destructor 無法 leave 的窗口。
- `ViewCallback::MessageSFVCB` 現在以 scope 包住 Shell 提供的
  `previous_->MessageSFVCB` 與 selection callback；
  `ExplorerHost::navigation_complete`、`navigation_failed` 也在最外層先
  進入 scope，覆蓋 view/COM 操作與同步 app callback。同步
  `BrowseToObject` 的既有 app scope 仍保留，會自然形成 nested depth。
- 擴充既有 `shell_reentry_gate_check.ps1`，同時檢查 header callback 形狀、
  app wiring、共用 leave 邏輯，以及三個 host callback scope。focused check
  PASS；LLVM-MinGW Release build PASS；非 launch CTest 10/10 PASS；獨立
  `explorer_host_lifetime_check.exe` PASS；`git diff --check` PASS；elevated
  `panedock_launch_smoke` 1/1 PASS。
- source/build/ordinary launch smoke 不等於真實第三方 Shell extension 或
  anti-virus re-entry stress；本票仍不承諾永不返回的 provider 可被中斷，
  只保證 close 不會在 covered callback 尚未返回時啟動 teardown。
