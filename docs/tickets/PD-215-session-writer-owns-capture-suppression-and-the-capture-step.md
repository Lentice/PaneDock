# PD-215 — `SessionWriter` 擁有擷取抑制與擷取步驟

Phase 7 · switching path robustness · Depends on: PD-201, PD-206, PD-214

- Source: 2026-09-29 架構審查，候選 2。純重構加上可測性，使用者可見行為不變。

## Overrides

- **覆寫 PD-201 的界線**：`session_writer.h` 原本明寫「刻意不擁有」兩件事——
  擷取 live pane location，以及 Group 切換的擷取 guard。本票把 guard 與「寫之前
  要不要擷取」收進 `SessionWriter`；**讀 live Shell view 仍不是它的事**，經
  `LiveLocationCapture` seam 交給 `main.cpp`。理由：「抑制期間被拒的 save 仍然
  欠著」是一條關於「何時寫」的規則，PD-201 把它留在 coordinator，結果它散在
  `save_now`、`capture_locations`、`LocationCaptureSuppression`、
  `perform_group_transition` 的釋放順序、以及 `PaneHost::location_capture_suppressed`
  五處，且沒有測試。shutdown reducer 事件仍留在 coordinator。

## 為什麼

- `save_now` 在抑制期間回 `false`，但**沒有任何東西記得它欠一次**。
  `perform_group_transition` 必須在 `save_session` 前先 `suppression.reset()`，否則
  `arm_timer` 失敗時的 fallback save 會被默默丟掉；transition 若提早 return
  （`is_shutting_down()`、導覽失敗），或 `WM_TIMER` 在抑制期間觸發（它不在延遲清單
  上，會先 `cancel_timer` 再被拒），被拒的 save 要等到下一次 model 變更才會寫。
- 抑制旗標經 `PaneHost` 洩漏進 pane。

## 約束（引自 AGENTS.md）

- 「All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace …」——
  寫檔路徑（`SessionWriter::write`）不改。
- 「Event-driven idle path only. No busy loops, no polling timers.」——重新排程只在
  抑制解除且確實有被拒的 save 時發生一次，走既有 debounce timer。
- 「The hazard is reentrancy, never concurrency.」——抑制是巢狀深度計數，不加鎖。
- 「Keep `src/core` free of HWND, COM and `windows.h`.」——全部留在 `src/app_shell`。

## 範圍

`src/app_shell/session_writer.h/.cpp`：

- `LiveLocationCapture`（virtual `capture_live_locations()`）：app adapter 在
  `main.cpp`，測試用計數 fake。
- `LocationCaptureGate`：唯讀的 `suppressed()`，只有 `SessionWriter` 能改。
- `SessionWriter::CaptureSuppression`：RAII、可巢狀；最外層釋放時若有被拒的 save，
  呼叫 `schedule()`。
- `bind(application, capture)`、`set_timer_owner(HWND)`、`schedule()`、
  `save_now(clean_shutdown, force_during_capture_suppression)`、
  `capture_live_locations()`、`capture_gate()`、`timer_armed()`。
- `save_now` 語意與原 `main.cpp` 版本逐步相同：抑制中且未 force → 拒絕（並記下欠著）；
  否則 `mark_dirty` → 擷取（抑制中不擷取）→ `write`。

`main.cpp`：`save_now`／`schedule_session_save`／`capture_locations` 保留為一行轉呼叫——
它們是 `tests/release/shutdown_state_check.ps1` 用來管制「哪些路徑可以同步寫」的
呼叫點詞彙。刪除 `suppress_location_capture`、`LocationCaptureSuppression` 類別（改為
alias）、`AppState::location_capture_suppressed`。

`pane`：`PaneHost::location_capture_suppressed` 刪除；pane 經 `set_capture_gate` 直接
拿到 gate（與 PD-214 的 guard 同一時機設定）。

## 非目標

- 不改 end-session 路徑直接呼叫 `write(...)` 的做法。
- 不改 `WM_TIMER` 分支（仍在 Shell 呼叫中略過、先 cancel 再 save）。
- 不改 debounce 常數與 `arm_timer` 的上限規則。
- 不動 shutdown reducer 與其他 `ShutdownState` 別名（候選 3）。

## 驗收條件

1. `grep -rn "suppress_location_capture\|location_capture_suppressed" src tests` 無結果。
2. `session_writer_test` 新增並通過：save_now 擷取後寫入 bound model；抑制中被拒且仍
   dirty；force 寫入但不擷取；巢狀抑制只在最外層解除；被拒的 save 在解除時重新
   arm timer；沒被拒時解除不 arm。
3. 既有測試全綠；release checks 檢查同樣的不變式。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 交接區

- 未驗證：沒有在真機上讓 Group 切換中途觸發 save 來觀察重新排程；只由
  `session_writer_test` 與既有 release checks 支撐。
