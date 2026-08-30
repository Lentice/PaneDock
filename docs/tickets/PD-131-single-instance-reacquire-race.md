# PD-131 — single-instance mutex 重新取得競態可啟動兩個 PaneDock

Phase 7 · app_shell startup correctness · Depends on: PD-084, PD-129

- Source: 2026-08-30 startup／close audit loop。
- Priority: HIGH——兩個等待中的啟動程序可同時越過 PD-129 的 reacquire 分支，之後各自整份覆寫 `session.json`，造成使用者 Group／tab 狀態遺失。

## Goal

當前一個 PaneDock 正在關閉、mutex 剛釋放，而兩個新啟動程序同時等待時，只允許真正建立 named mutex 的程序繼續啟動；競態輸家回到既有 relay/wait 流程，不得建立第二個 UI 或觸碰 session。

## 已確認的根因

`src/app_shell/main.cpp:5567` 的 `relay_or_wait_for_existing_instance` 先以 `OpenMutexW` 探測舊 mutex。探測回傳 null 後呼叫 `CreateMutexW`，只檢查 handle 是否為 null，沒有檢查成功 handle 搭配的 `GetLastError() == ERROR_ALREADY_EXISTS`。

兩個等待程序 A/B 可同時看到 `OpenMutexW == nullptr`：A 先建立 mutex；B 的 `CreateMutexW` 仍回傳有效 handle，但 `GetLastError` 是 `ERROR_ALREADY_EXISTS`。現碼讓 A/B 都 `return false`，兩者都進入 `read_session`、建立 UI 並寫入同一份 session。`wWinMain` 是唯一 caller，沒有後續 guard。

## Binding constraints

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.

> Event-driven idle path only. No busy loops, no polling timers.

`docs/development.md`：

> Make the smallest change that satisfies the acceptance criteria. Reuse before adding.

## Files and callers

- `src/app_shell/main.cpp`: `relay_or_wait_for_existing_instance`, its sole caller `wWinMain`, `kSingleInstanceMutexName` and retry constants。
- `docs/tickets/PD-084-single-instance-activate-existing-window.md`、`PD-129-single-instance-startup-race-relay.md`: single-instance decisions。
- `tests/release/launch_smoke.ps1`: existing launch/close smoke boundary。

## Scope

1. 在 reacquire 的 `CreateMutexW` 後立即讀取 `GetLastError()`。
2. 若 handle 有效但結果為 `ERROR_ALREADY_EXISTS`，關閉該 handle、清空 caller handle，回到有限 wait loop；不得 fall through 啟動。
3. 真正新建 mutex 時維持 PD-129 的 fresh launch；API 失敗維持可見錯誤，不新增 thread、timer、IPC、lock file 或 dependency。

## Non-goals

- 不改 mutex 名稱、session schema、activation UX 或 5 秒 timeout。
- 不把 startup wait 做成常駐 polling。
- 不嘗試從 force-killed process 回收 kernel object；Windows 已自動釋放 process handles。

## Acceptance / Agent checks

1. `CreateMutexW` reacquire 分支明確處理 `ERROR_ALREADY_EXISTS`，且只在真正建立 mutex 時回傳 fresh launch。
2. `cmake --build build`、`ctest --test-dir build --output-on-failure`、`git diff --check` 通過。
3. source check：

```powershell
rg -n "CreateMutexW|ERROR_ALREADY_EXISTS|relay_or_wait_for_existing_instance" src/app_shell/main.cpp
```

4. 真實競態難以由 deterministic core test 製造；以 Release app 做多次「舊 instance 關閉時同時啟動兩次」檢查，結果只能有一個 `PaneDockMainWindow`。若無互動桌面，交接區必須標記未驗證，不得把 build 當 runtime PASS。

## 交接區

<!-- 實作 agent append-only -->

### 2026-08-30 — implemented

- `relay_or_wait_for_existing_instance` 在 reacquire `CreateMutexW` 成功後檢查 `ERROR_ALREADY_EXISTS`；競態輸家關閉 handle 並回到既有有限 wait loop，只有真正建立 mutex 的程序能進入 startup/session flow。
- 沒有新增 helper、thread、timer、dependency 或 session format；修正只在 shared relay 的 7 行內。
- `cmake --build build` PASS；CTest 6/6 PASS（含 launch smoke）；`git diff --check` 與 source check PASS。
- 尚未取得可控制三個 process 精確交錯的真實桌面 runtime 證據；此分支的機制由 Win32 `CreateMutexW`/`GetLastError` 契約與唯一 caller trace 驗證，未宣稱 runtime stress PASS。
