# PD-145 — startup message loop 前不得同步執行可避免的 Shell chrome lookup

Phase 7 · app_shell startup · Depends on: PD-141, PD-142, PD-144

- Source: 2026-08-30 close/startup audit loop；PD-144 後的 startup caller trace。
- Priority: HIGH——PD-141/142 已讓主 frame 可見並移除 pre-window fixed-label
  lookup，但 `wWinMain` 仍在進入 outer `GetMessageW` loop 前同步執行 fixed
  pinned-label `SHCreateItemFromParsingName` 與 `refresh_tab_strips`。若
  virtual folder、network-backed namespace、Shell extension 或 anti-virus
  卡住，使用者看到 frame 卻無法操作，且 active/deferred pane realization
  尚未排入。

## Goal

在主視窗顯示後立即進入既有 event-driven startup path；把可避免的 startup
  chrome Shell lookup 放入既有一次性 `kDeferredRealizeMessage` 所呼叫的
  `realize_startup_panes`，使 frame 已可見且 outer message loop 已開始後，才
  解析 fixed labels、刷新 tab/navigation chrome，再按既有 active-first 順序
  realize panes。

保留 post-show final text、PD-144 warning routing、PD-140 re-entry/shutdown
gate 與 PostMessage 失敗時的同步 fallback。

## Confirmed root cause and callers

- `src/app_shell/main.cpp:wWinMain` 在 `ShowWindow`／`UpdateWindow` 後、
  `PostMessageW(kDeferredRealizeMessage, ...)` 前，直接呼叫
  `display_text_for_parsing_name(kPinnedFixedParsingNames[index])`。
- 同一 block 接著呼叫 `refresh_tab_strips(state)`；它會經
  `tab_display_text`、`refresh_navigation_chrome` 再解析 active tab 的
  virtual-folder display name。
- 這些呼叫已不需要阻擋主視窗建立；`pinned_fixed_labels` 在解析前只是空字串，
  helper 失敗時也沿用 parsing text fallback。
- `PostMessageW` 成功後才開始 outer loop，因此目前的 block 發生在 frame 雖
  可見但尚未可互動的 startup gap；`PostMessageW` 失敗的同步 fallback 是
  明確的 queue-failure emergency path，保留既有行為並在交接區標記。

## Binding constraints

`docs/design-spec.md §9.2`:

> 單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。

`docs/design-spec.md §9.3`:

> 建立主視窗與側邊欄,套用視窗位置

> 其餘 pane 延後 realize,避免被網路或離線路徑阻塞

`AGENTS.md`:

> Event-driven idle path only. No busy loops, no polling timers。

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and
> internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe。

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> App UI text must be English. No Chinese strings ship in the binary。

## Files to read and trace

- `src/app_shell/main.cpp`: `display_text_for_parsing_name`,
  `refresh_navigation_chrome`, `refresh_tab_strip(s)`,
  `realize_startup_panes`, `kDeferredRealizeMessage`, `wWinMain`, and all
  `startup_frame_only`/`startup_realize_pending` guards。
- `tests/release/startup_frame_order_check.ps1` and `tests/CMakeLists.txt`:
  extend the existing source/lifecycle self-check。
- `src/explorer_host/explorer_host.cpp/.h`: confirm no host lifetime change。
- `docs/design-spec.md §9.2, §9.3, §11` and `docs/development.md` startup/Shell
  rules。
- `docs/tickets/PD-141-startup-frame-before-shell-realize.md`,
  `PD-142-startup-no-prewindow-shell-label-lookup.md`, and
  `PD-144-startup-warning-aggregation.md`。

## Scope

1. Move the fixed-label loop and startup `refresh_tab_strips` out of the
   pre-message-loop `wWinMain` prologue into the existing deferred realization
   path, under `ShellCallScope` and the existing shutdown checks.
2. Keep active-pane-first behavior: startup chrome refresh happens in the
   deferred handler, then `realize_startup_panes` calls the current active pass,
   focus, and remaining-pane pass without adding another message/timer/worker.
3. Preserve the synchronous `PostMessageW` failure fallback; it may run the same
   helper before the loop only when queueing the required event failed.
4. Extend `startup_frame_order_check.ps1` to verify the normal `wWinMain` prologue
   contains no startup chrome lookup and that `realize_startup_panes` owns it.

## Non-goals

- Do not change Shell location resolution, tab text, pinned-menu text, active-first
  order, session persistence, warning wording, or COM lifetime.
- Do not add a second deferred message, polling timer, retry, timeout, worker,
  async runtime, fake Shell host, or `TerminateProcess`.
- Do not claim this makes an individual Shell call interruptible; it only places
  the avoidable work after the visible frame and message loop start.
- Do not change the exceptional synchronous fallback when `PostMessageW` fails.

## Acceptance criteria

1. After `ShowWindow`／`UpdateWindow`, the normal `wWinMain` startup prologue has
   no fixed-label `display_text_for_parsing_name` or `refresh_tab_strips` call before
   posting `kDeferredRealizeMessage` and entering the message loop.
2. The deferred path refreshes the same labels/chrome under the existing
   `ShellCallScope`, then realizes active pane first, focuses it, and realizes the
   remaining visible panes.
3. A close/shutdown re-entry during startup chrome refresh stops continuation and
   never reaches layout/focus on a closing window.
4. Final pinned-menu/tab/navigation text remains the same when Shell calls return;
   PostMessage failure still has a functioning synchronous fallback.
5. Focused self-check, build, CTest and `git diff --check` pass.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/startup_frame_order_check.ps1 -SourcePath src/app_shell/main.cpp
ctest --test-dir build -E panedock_launch_smoke --output-on-failure
git diff --check
```

With a real writable `%LOCALAPPDATA%\PaneDock`, run the existing elevated
`panedock_launch_smoke`; it verifies ordinary startup/close, not a never-returning
Shell provider.

## Handoff requirements

- Record the normal ordering and explicitly distinguish it from the
  `PostMessageW`-failure fallback.
- Record deterministic source/build/runtime smoke results separately from offline,
  slow-disk and anti-virus latency evidence.
- Record any remaining setup failure prompt issue and synchronous session read/save
  boundary for the next audit round.

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-08-30 — implemented

- 將 fixed pinned-label lookup 與 startup `refresh_tab_strips` 移入既有
  `realize_startup_panes` deferred path；同一個 `ShellCallScope` 與 shutdown
  gate 仍保留，active-first、focus、remaining-pane 順序不變。
- 正常 `ShowWindow`／`UpdateWindow` 後的 `wWinMain` prologue 不再執行 startup
  chrome Shell lookup；只有 `PostMessageW(kDeferredRealizeMessage, ...)`
  失敗時才使用原本保留的同步 fallback。
- `startup_frame_order_check.ps1` 新增 deferred ownership、shutdown gate 與
  normal prologue 無直接 chrome lookup 的檢查。focused source check PASS；
  LLVM-MinGW Release build PASS；非 launch CTest 10/10 PASS；
  `git diff --check` PASS；elevated `panedock_launch_smoke` 1/1 PASS。
- 這只縮短 frame 顯示到 deferred startup 的不可操作 gap；不宣稱能中斷
  單一永不返回的 Shell/OS call，也未加入 worker、timeout、timer 或 retry。
- 下一輪仍需評估：DPI awareness／diagnostic mitigation setup failure 目前
  只有 debug event；startup session read／false-marker save 仍是 single-STA
  的同步邊界。離線、慢磁碟與 anti-virus provider latency 未被本次 smoke
  注入或證明。
