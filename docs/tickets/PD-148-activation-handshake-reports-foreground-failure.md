# PD-148 — activation handshake 將 `SetForegroundWindow` 拒絕誤判為成功

Phase 7 · app_shell startup relay · Depends on: PD-139

## Goal

讓第二次啟動只在既有 PaneDock 視窗真的接受 activation request 時安靜結束。若 Windows 因前景鎖定規則拒絕 `SetForegroundWindow()`，既有 instance 必須回報 handshake failure，第二 instance 繼續既有的有限等待，最後顯示既有 warning，而不是無 UI、無提示地退出。

本票修正 PD-139 的實作缺口，不改變 PD-139 已定案的前景化技巧、mutex、等待上限或單一實例政策。

## Confirmed root cause and caller trace

- `src/app_shell/main.cpp:5944-5961` 的 `activate_main_window_on_own_thread(HWND)` 目前回傳 `void`。它會記錄 `SetForegroundWindow()` 失敗，但呼叫端拿不到結果。
- `src/app_shell/main.cpp:5185-5189` 的 `kActivateExistingInstanceMessage` handler 因此固定回傳 `0`；只有 closing/quit 的早期分支回傳 `1`。
- `src/app_shell/main.cpp:5969-5990` 的 `relay_or_wait_for_existing_instance()` 將 `activation_result == 0` 視為 usable acknowledgement。當 `SetForegroundWindow()` 被拒絕時，第二 instance 仍取得 `0`，立即 return，沒有自己的 UI，也沒有 warning。
- `activate_main_window_on_own_thread()` 的唯一 caller 是上述 activation message handler；relay 不直接呼叫它，因此在 shared boundary 傳遞結果即可一次修正所有 caller。

## Binding constraints

`docs/design-spec.md`:

> §9.3 啟動序列：讀取 session、建立主視窗與側邊欄，套用視窗位置，然後 realize Shell pane。

> §9.4 關閉序列：destroy 全部 live `IExplorerBrowser`、destroy pane HWND、destroy 主視窗、退出訊息迴圈、`CoUninitialize`；順序不可調換。

> §11：COM 失敗要記錄診斷事件，不得靜默忽略，也不得使整個視窗不可用。

`docs/development.md`:

> Shell APIs re-enter our message loop. Any host-side lock, and any "close the view then wait for its event" sequence, must be reentrancy-safe.

> Make the smallest change that satisfies the acceptance criteria. Reuse before adding.

> Add one focused runnable test or self-check for new non-trivial logic.

`AGENTS.md`:

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> Event-driven idle path only. No busy loops, no polling timers.

> App UI text must be English.

> No network, no telemetry, no third-party runtime, no services, no drivers, no admin elevation.

## Files to read and trace

- `docs/design-spec.md` §9.3, §9.4, §11。
- `docs/development.md` COM lifetime、error handling、change workflow。
- `docs/tickets.md` 「已否決的方向」、PD-084、PD-089、PD-129、PD-131、PD-139。
- `src/app_shell/main.cpp`：`activate_main_window_on_own_thread`、activation message handler、`relay_or_wait_for_existing_instance`、`wWinMain`。
- `tests/release/single_instance_relay_check.ps1`：既有 handshake source/self-check。

## Scope

1. 將 `activate_main_window_on_own_thread(HWND)` 改為回傳 `bool`，沿用現有 `ShowWindow`、`AttachThreadInput`、`SetForegroundWindow` 流程；其回傳值只反映 `SetForegroundWindow` 的 Win32 結果。
2. 讓 activation message handler 在 activation 成功時回傳 `0`，在 `SetForegroundWindow` 失敗時回傳非零值。既有 closing/quit failure 分支維持非零。
3. 保持 relay 的 `SendMessageTimeoutW`、250 ms handshake timeout、5 秒 bounded wait、mutex reacquire race guard 與既有 warning 不變；activation failure 應自然流入既有 timeout/warning 路徑。
4. 擴充 `tests/release/single_instance_relay_check.ps1`，驗證 activation function 的結果確實回傳給 handler。

## Non-goals

- 不改變 mutex 名稱、單一 instance 保證、relay 等待上限、`AllowSetForegroundWindow` 或 `AttachThreadInput` 策略。
- 不直接由第二 instance 呼叫 `SetForegroundWindow`。
- 不新增 IPC、thread、timer、service、dependency 或 admin elevation。
- 不修改 session schema、Shell view teardown 順序或其他 close/startup 行為。

## Acceptance criteria

1. `SetForegroundWindow()` 回傳成功時，activation handler 回傳 `0`，第二 instance 依既有規則結束且不建立第二個主視窗。
2. `SetForegroundWindow()` 回傳失敗時，activation handler 回傳非零；第二 instance 不把它當成成功 relay，繼續有限等待並在 mutex 持續存在時顯示既有 warning。
3. closing/quit handler failure、`SendMessageTimeoutW` timeout/send failure、mutex disappearance 與 PD-131 reacquire race 行為不變。
4. 所有新增或修改的 UI 字串維持 English；本票不新增字串。
5. Agent checks 全部通過，且不影響既有 `panedock_launch_smoke`。

## Agent checks

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/single_instance_relay_check.ps1
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

## 交接區

<!-- append-only；狀態只看 docs/tickets.md 的 Ticket 總覽表 -->

### 2026-08-30 — implemented

- `activate_main_window_on_own_thread()` 現在回傳 `SetForegroundWindow()` 的 BOOL 結果；activation message handler 成功回傳 `0`，失敗回傳 `1`。既有 relay 只接受 `activation_result == 0` 的規則因此會在前景化失敗時繼續 bounded wait，最後顯示既有 warning。
- 未改變 `AllowSetForegroundWindow`、`AttachThreadInput`、250 ms handshake timeout、5 秒有限等待、mutex reacquire race guard、single-instance policy 或任何 UI 字串。
- focused source self-check 已驗證 function signature、handler forwarding 與 result return；沒有新增 fake HWND/COM seam。

#### Agent checks

```text
powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/single_instance_relay_check.ps1：PASS。
cmake --build build：PASS。
ctest --test-dir build -E panedock_launch_smoke --output-on-failure：PASS，10/10。
ctest --test-dir build --output-on-failure（提升權限，含實際 %LOCALAPPDATA% launch smoke）：PASS，11/11。
git diff --check：PASS。
```

#### Runtime evidence

- 實際使用者 `%LOCALAPPDATA%\PaneDock\session.json` 已完成正常 launch/graceful-close smoke。
- 以實際 session 先確認 `clean_shutdown=true`，force-kill 第一個 PaneDock 後確認 durable marker 為 `false`；再啟動、關閉 recovery warning 並 graceful close，確認 marker 恢復 `true`。
- 兩個實際 PaneDock 程序背靠背啟動：第二個在 10 秒內 relay 結束，第一個仍存活，之後第一個 graceful close 成功。
- 目前互動桌面沒有刻意製造 `SetForegroundWindow()` 回傳 `FALSE` 的穩定條件；該分支由 source self-check 驗證結果傳遞，實際 Windows 前景鎖定拒絕時的使用者提示仍保留給實機手動驗證。
- force-kill 後既有 `tests/release/launch_smoke.ps1` 會因 recovery warning 是 modal dialog 而讓 `CloseMainWindow()` 回傳 false；這是 smoke script 未處理 recovery dialog 的限制，不是產品 close failure。本次已手動 dismiss 該 warning 後正常關閉並完成 marker restoration，未修改該非本票測試工具。
