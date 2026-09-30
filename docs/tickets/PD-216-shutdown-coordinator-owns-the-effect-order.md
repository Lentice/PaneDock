# PD-216 — `ShutdownCoordinator` 擁有 shutdown 的 effect 順序

Phase 7 · switching path robustness · Depends on: PD-203, PD-214, PD-215

- Source: 2026-09-29 架構審查，候選 3。純重構加上可測性，使用者可見行為不變。

## Overrides

- **覆寫 2026-09-03 F2 的結論**（`docs/tickets.md`「不採用 core ShutdownAction log 方案，
  不新增第二套順序真相」）：本票不是加一份 log，而是把順序本身搬進 core——
  `run_shutdown_action`／``grep -nw "run_shutdown_action|finish_shutdown|begin_shutdown|shutdown_sequence" src``／`begin_shutdown`／`complete_deferred_close`／
  `run_end_session_shutdown` 的分派與順序只剩 `core::ShutdownCoordinator` 一份，
  effect 本體經 `ShutdownEffects` seam 留在 `main.cpp`。因此順序仍只有一個真相，
  而且由 `core_shutdown_coordinator_test` 執行，不再只靠 regex。
- **不重開「移除 `AppState` 的 shutdown reference alias」（2026-09-03 已否決）**：
  alias 保留，只改成 `const bool&`。`ShutdownSequence::state()` 只剩 const 版本，
  reducer 成為唯一寫入者。

## 約束（引自 AGENTS.md）

- 「Keep `src/core` free of HWND, COM and `windows.h`.」——seam 只用 `bool`。
- 「Never destroy a parent HWND while a view is alive; on shutdown destroy all views
  before the message loop exits.」——coordinator 只在 `views_destroyed` 回
  `destroy_window` 後才呼叫 `destroy_window()`；view 端的六步順序仍在
  `AppShutdownEffects::destroy_views`，由 `shutdown_state_check.ps1` 有序掃描。
- 「The hazard is reentrancy, never concurrency.」——save-failure prompt 是 modal，
  巢狀的 end_session 在它回來後由 reducer 決定勝出；不加鎖。

## 範圍

- `src/core/shutdown_coordinator.h/.cpp`：`ShutdownEffects`、`TransferChoice`、
  `ShutdownCoordinator`（`request_close`、`deferred_shutdown_ready`、`drag_changed`、
  `complete_deferred_close`、`transfer_chosen`、`end_session_confirmed`、`run`、
  `sequence()`、`state()`、`is_shutting_down()`）。
- `main.cpp`：`AppShutdownEffects` adapter；`AppState::shutdown_coordinator` 取代
  `shutdown_sequence`；`ShellReentryGuard` 建在 `shutdown_coordinator.sequence()` 上。
  `WM_DESTROY` 的 fallback save 與 `finalize_process` 不動。
- release checks 改指向 adapter；已由單元測試執行的順序規則從 regex 移除。

## 非目標

- 不改 reducer 的任何轉移。
- 不改 `ShellReentryGuard` 自己 post deferred shutdown 的路徑（與 coordinator 的
  `defer` 分支重複，見交接區）。
- 不動 `WM_DESTROY` 與 `finalize_process`。

## 驗收條件

1. `grep -nw "run_shutdown_action|finish_shutdown|begin_shutdown|shutdown_sequence" src`\|finish_shutdown\|begin_shutdown\|shutdown_sequence" src`
   無結果。
2. `core_shutdown_coordinator_test` 通過：正常關閉順序、Shell 呼叫中不 post、post 失敗
   重試、無視窗時停止、save 失敗 keep-open／明確 No、prompt 中的 session end 勝出、
   傳輸中關閉、drag 延後、session end 先寫 checkpoint 且在 Shell 呼叫中／關閉進行中
   不拆除。
3. 既有測試全綠（含 `panedock_launch_smoke`）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 交接區

- 未驗證：沒有在真機上跑 save 失敗 prompt、傳輸中關閉與真正的 OS 登出／重開機；
  只由單元測試與 `panedock_launch_smoke` 的正常關閉支撐。
- 既有行為（未改）：deferred shutdown 的 post 若持續失敗而視窗仍活著，
  `request_close` 會一直遞迴重試。原本的 ``grep -nw "run_shutdown_action|finish_shutdown|begin_shutdown|shutdown_sequence" src`` 相同。
- `ShellReentryGuard::leave` 與 `ShutdownCoordinator::run(defer)` 各有一份
  「queued → post → 失敗則 queue_failed 並重新關閉」。合併需要改 PD-214 的 effects
  seam，留作後續候選。
