# PD-142 — 移除 startup frame 顯示前重複的 virtual-folder label Shell lookup

Phase 7 · app_shell startup · Depends on: PD-141

- Source: 2026-08-30 close/startup audit loop；PD-141 實作後的 caller trace。
- Priority: HIGH——PD-141 已將 active `IExplorerBrowser` realization 延後到
  frame 顯示後，但 `wWinMain` 仍在 `CreateWindowExW` 前呼叫
  `display_text_for_parsing_name`。virtual-folder 的
  `SHCreateItemFromParsingName` 若被慢速 Shell extension、離線 provider、
  anti-virus 或卡住的磁碟拖住，仍可讓使用者看不到任何 UI；同一批 label
  在 frame 顯示後又被解析一次。

## Goal

讓所有 startup-only fixed pinned label 的 Shell lookup 都發生在主視窗
`ShowWindow`／`UpdateWindow` 之後。保留 PD-141 的 post-show label refresh、
最終英文顯示文字與既有 `ShellCallScope`／shutdown gate，不新增任何非同步
機制。

## Confirmed root cause and flow

- `src/app_shell/main.cpp:wWinMain` 在載入 session 後、`CreateWindowExW` 前，
  以 `display_text_for_parsing_name(kPinnedFixedParsingNames[index])` 填入
  `state.pinned_fixed_labels`。
- 該 helper 對 `::` parsing name 呼叫 `SHCreateItemFromParsingName`；這是
  真實 Shell/extension 呼叫，不是純字串轉換。
- PD-141 已在 `ShowWindow`／`UpdateWindow` 後保留同一個 label fill，並呼叫
  `refresh_tab_strips`；因此 pre-window loop 是重複工作與可見 frame 的阻塞點，
  不再有必要的 consumer。
- `pinned_fixed_labels` 只供 pinned menu 顯示；在 post-show fill 前不需要有
  有效 label。失敗時 helper 已回退為 parsing text，既有結果不變。

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

> App UI text must be English. No Chinese strings ship in the binary。

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace

- `src/app_shell/main.cpp`: `AppState::pinned_fixed_labels`,
  `display_text_for_parsing_name`, all `pinned_fixed_labels` consumers,
  `wWinMain` around session load / `CreateWindowExW` / `ShowWindow`, and
  `ShellCallScope` shutdown checks。
- `src/explorer_host/explorer_host.cpp/.h`: confirm this ticket does not alter
  `IExplorerBrowser` lifetime or host behavior。
- `docs/design-spec.md §9.2, §9.3, §11` and `docs/development.md` startup/Shell
  rules。
- `docs/tickets/PD-141-startup-frame-before-shell-realize.md`: implemented frame
  ordering and its recorded limitation。
- `tests/release/startup_frame_order_check.ps1` and `tests/CMakeLists.txt`:
  extend the existing focused source check; do not add a second test framework.

## Scope

1. Delete only the pre-`CreateWindowExW` fixed-label fill in `wWinMain`.
2. Keep the existing post-`UpdateWindow` fixed-label fill and
   `refresh_tab_strips` under the existing Shell re-entry/shutdown guard.
3. Extend `startup_frame_order_check.ps1` to fail if a fixed-label Shell lookup
   appears before the top-level window creation and to verify the surviving lookup
   is after `UpdateWindow`.

## Non-goals

- Do not move `read_session`, startup `clean_shutdown=false` save, or placement
  calculation; those remain synchronous by the single-STA/spec boundary.
- Do not change `display_text_for_parsing_name`, pinned-menu text, tab text,
  `IExplorerBrowser`, COM initialization, or session schema.
- Do not add a worker, timeout, retry, timer, polling loop, fake Shell host, or
  `TerminateProcess`.
- Do not aggregate startup warnings; the separate deferred Shell failure prompt
  issue remains an audit item after this ticket.

## Acceptance criteria

1. No `display_text_for_parsing_name(kPinnedFixedParsingNames[index])` call occurs
   before `CreateWindowExW` returns.
2. The existing post-show lookup remains after `ShowWindow` and `UpdateWindow`,
   before startup proceeds to deferred realization; pinned menu labels still use
   the same resolved text or parsing-name fallback.
3. No Shell host lifetime or shutdown ordering changes; PD-140 re-entry checks
   remain intact.
4. Focused source check, build, CTest and `git diff --check` pass.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/startup_frame_order_check.ps1 -SourcePath src/app_shell/main.cpp
ctest --test-dir build -E panedock_launch_smoke --output-on-failure
git diff --check
```

Runtime smoke, when a real desktop and writable `%LOCALAPPDATA%` are available:

```powershell
ctest --test-dir build -R "^panedock_launch_smoke$" --output-on-failure
```

This proves ordinary frame creation and graceful close only; it does not prove
an offline/anti-virus provider will return from its Shell call.

## Handoff requirements

- Record the pre-window lookup removal and the exact post-show ordering.
- Record that the focused check detects a future lookup moved back before
  `CreateWindowExW`; do not call source matching a runtime latency proof.
- Record build, CTest, diff and elevated launch-smoke results separately.
- Record the remaining warning aggregation and synchronous pre-window session
  read/marker-save boundaries for the next audit round.

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-08-30 — implemented

- 刪除 `wWinMain` 在 `CreateWindowExW` 前填入 `pinned_fixed_labels` 的
  `display_text_for_parsing_name` loop；唯一 startup fixed-label lookup 保留在
  `ShowWindow`／`UpdateWindow` 與 `startup_frame_only = false` 之後，接著刷新
  tab chrome，再排入 deferred realization。
- `startup_frame_order_check.ps1` 新增位置檢查：若 fixed-label lookup 回到
  top-level `CreateWindowExW` 前，或早於 `UpdateWindow`，測試會失敗；既有
  frame-only、active-first、remaining-pane ordering checks 仍保留。
- 已執行：focused source check PASS；LLVM-MinGW Release build PASS；
  `ctest --test-dir build -E panedock_launch_smoke --output-on-failure`
  10/10 PASS；`git diff --check` PASS；提升權限後實際
  `ctest --test-dir build -R "^panedock_launch_smoke$" --output-on-failure`
  1/1 PASS（`%LOCALAPPDATA%\PaneDock`、主窗建立、graceful close、code 0）。
- 這些 source/runtime smoke 結果不等於 offline provider、slow disk 或
  anti-virus 阻塞 call 的 latency/interruptibility 證據；依 §9.2，
  `read_session` 與 startup marker save 仍是同步 single-STA 邊界。
- 後續 audit 仍需處理：多個 startup warning 可能互相覆蓋，deferred Shell
  failure 在已有 warning 時可能不提示；normal close 在 Shell teardown 前
  寫入 `clean_shutdown=true` 也可能讓 teardown 中 force kill 被誤判為 clean。
