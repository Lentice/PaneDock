# PD-217 — `run_group_transition` 在 core 執行 Group 切換的步驟

Phase 7 · switching path robustness · Depends on: PD-206, PD-215

- Source: 2026-09-29 架構審查，候選 4。純重構加上可測性，使用者可見行為不變。

## 為什麼

`plan_group_transition` 有測試，但實際出過錯的是執行它的迴圈：每步之前的
`is_shutting_down()` 提早返回、導覽失敗時在 layout 前中止、每步重新查 active Group、
PD-206 capture suppression 在每個出口與 save 之前解除。這些都在 `main.cpp` 的
`perform_group_transition`，沒有測試。`active_group_changed` 也由四個呼叫端各自斷言，
可以從 model 推導卻沒有。

## 約束（引自 AGENTS.md）

- 「Keep `src/core` free of HWND, COM and `windows.h`.」——effects seam 只用 `bool`。
- 「re-verify identity (`active()`, `pane_state()`, `is_shutting_down()`) after every
  pump point」——runner 每步前查 `is_shutting_down()`，需要 Group 的步驟前重查
  `has_active_group()`。
- 「Group switching keeps live views alive and re-navigates them.」——步驟內容不變。

## 範圍

- `core/group_transition.h/.cpp`：`GroupTransitionEffects`、
  `run_group_transition(transition, previous_active_group_id, active_group_id, effects)`。
  suppression 由 runner 保證在每個出口恰好解除一次，且在 save 之前。
- `main.cpp`：`perform_group_transition` 的簽名改為收 `previous_active_group_id`；
  內含一個 local adapter，持有 `LocationCaptureSuppression`。四個呼叫端傳入變更前的 id。

## 非目標

- `apply_layout` 的 `LayoutPassScope` 延後機制不併入 `ShellReentryGuard`（見交接區）。
- 不改 plan 本身、`activate_group` 的同 Group 捷徑。

## 驗收條件

1. `core_group_transition` 新增並通過：完整順序且 release 在 save 之前；步驟中 shutdown
   停止其餘並仍 release；導覽失敗在 layout 前中止；shutdown 中開始只 release；
   changed 由 id 推導；中途 Group 消失時略過 navigate/focus。
2. 既有測試全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 交接區

- 未驗證：沒有在真機上讓 Group 切換中途關閉或導覽失敗；只由單元測試支撐。
- 未處理：`apply_layout` 的 `LayoutPassScope` 是第三套重入延後機制。審查建議把它併入
  `ShellReentryGuard`，但這要改 layout 在重入中被要求時的語意，另開票。
