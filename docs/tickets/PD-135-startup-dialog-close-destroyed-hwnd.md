# PD-135 — 啟動警告 MessageBox 的 modal loop 內被關閉：後續啟動序（剩餘訊息框＋deferred realize post）對已銷毀 HWND 執行

Phase 7 · app_shell startup · Depends on: PD-126, PD-127, PD-130

- Source: 2026-08-30 startup／close audit loop。使用者要求 audit「AP startup／AP close」的 race condition／跨情境，稽核確認。
- Priority: HIGH——「啟動後立即關閉」在 PD-126 只處理了「外層 message loop 空佇列永不退出」，卻沒處理「啟動序本身在窗口已銷毀後繼續跑」；這是同一個關閉時機的不同缺陷，且後續 `PostMessageW(kDeferredRealizeMessage)` 可能落到被 reuse 的 HWND。

## 已確認的根因（有程式碼證據）

`wWinMain`（`src/app_shell/main.cpp:5761`-`5802`）在視窗建立後、進入主 message loop 前，串列顯示**多個**啟動 MessageBox，最後 `PostMessageW` deferred realize：

```cpp
ShowWindow(window, ...); UpdateWindow(window);
if (recovered && source == backup) MessageBoxW(window, ...);   // modal loop A
if (recovered && source == default_state) MessageBoxW(window, ...); // modal loop B
if (!clean_shutdown) MessageBoxW(window, ...);                  // modal loop C
if (!startup_warning_message.empty()) MessageBoxW(window, ...); // modal loop D
if (state.startup_realize_pending)
    PostMessageW(window, kDeferredRealizeMessage, 0, generation);
```

`MessageBoxW` 的 nested modal pump 會 dispatch 同 thread 其他視窗（含主視窗）的訊息。若在 A/B/C/D **任一** modal loop 內主視窗收到 `WM_CLOSE`（使用者 Alt+F4／系統關機）或 `WM_ENDSESSION(TRUE)`，`window_proc` 的 `begin_shutdown`（`:4438`）會執行：`destroy_explorers` → `DestroyWindow(window)` → `WM_DESTROY`（`:5473` 設 `quit_requested=true`）→ 此 MessageBox 因 owner 視窗被銷毀而自動收起並返回。

此時控制回到 `wWinMain`，**檔頭仍持有一個已銷毀的 `window` 值**，而後續「剩下的」啟動序繼續執行：
- 下一個啟動 MessageBox（C/D）以 `window`（已銷毀）為 owner 呼叫 `MessageBoxW` —— owner 已是無效 HWND。
- `PostMessageW(window, kDeferredRealizeMessage, ...)`（`:5792`）post 到已銷毀（甚至可能已被 reuse 成另一個視窗）的 HWND —— 若該 handle 被系統 reuse，此訊息會落到錯誤的視窗，可能對他視窗執行 pane realize 或觸發未定義行為。

PD-126 修補的缺口是「外層 loop 的 `quit_requested` 檢查」與「loop 前空佇列退出」（`:5811`），並沒有在這些 MessageBox／post 之間加「是否仍在啟動」的 guard。PD-127 修的是 `apply_layout` 在 teardown pump 內的重入守衛；PD-130 引入 `startup_warning_message` 的顯示點，但同樣沒有在此串列間 guard。

## Binding constraints — quoted, do not weaken

`AGENTS.md`：

> Group switching keeps live views alive and re-navigates them. … on shutdown destroy all views before the message loop exits.

`AGENTS.md`：

> App UI text must be English. No Chinese strings ship in the binary.

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/design-spec.md` §9.4：關閉序列（view destroy 先於 parent HWND destroy）的既有順序不可破壞。

## 要讀取與 trace 的檔案

- `src/app_shell/main.cpp:4438`（`begin_shutdown`：`closing_`/`quit_requested` 的設定）、`:5462`-`5472`（`WM_CLOSE` → `begin_shutdown`）、`:5473`-`5489`（`WM_DESTROY`）、`:5497`-`5515`（`WM_QUERYENDSESSION`/`WM_ENDSESSION`）。
- `src/app_shell/main.cpp:5761`-`5802`（本次修改的啟動 MessageBox 串列＋deferred realize post）。
- PD-126／PD-127／PD-130 的文件，確認已覆蓋邊界（loop 退出、teardown 重入、可復原失敗提示），本票補的是「啟動序在關閉後繼續跑」。

## Fix 方向 / Scope

讓啟動序在「已開始關閉」時立即停止，不再對已銷毀／可能被 reuse 的 HWND 顯示後續訊息框或 post deferred realize。

1. 引入一個「是否仍應繼續啟動序」的檢查：`state.closing_ || state.quit_requested`（這兩個 flag 由 `begin_shutdown`／`WM_DESTROY` 設為 true，涵蓋 `WM_CLOSE` 與 `WM_ENDSESSION` 兩種觸發）。
2. 用一個 `bool proceed` 串接各啟動 MessageBox；每次 `MessageBoxW` 返回後更新 `proceed = !state.closing_ && !state.quit_requested`。任一 MessageBox 的 modal loop 內觸發關閉 → 後續 MessageBox 全部跳過。
3. `PostMessageW(kDeferredRealizeMessage, ...)` 以 `proceed && state.startup_realize_pending` 為條件，並在 `proceed == false` 時清掉 `startup_realize_pending`（`begin_shutdown` 已會清，此處是防 `WM_CREATE` 之後、`begin_shutdown` 之外的極早關閉路徑）。

優先考慮「最小改動」：不抽共同 helper（避免改變既有訊息字串與順序），只加 `proceed` 串接；若發現跨函式重複再抽。

## Non-goals

- 不改各啟動 MessageBox 的既有字串／順序／條件（recovery、unclean-shutdown、warning 三個來源的語意不變）。
- 不把這三個啟動 MessageBox 改為非 modal 或延後顯示（user 需要在進入互動前知道問題）。
- 不改 `begin_shutdown`／`WM_DESTROY` 的既有 teardown 順序。
- 不改 `src/core`。
- 不新增 thread。

## Acceptance Criteria

1. `cmake --build build`、`ctest --test-dir build --output-on-failure`、`git diff --check` 全數通過，`panedock_launch_smoke` 亦通過。
2. `rg -n "proceed|quit_requested|closing_" src/app_shell/main.cpp`：確認啟動 MessageBox 序列以 `proceed` 串接；`PostMessageW(kDeferredRealizeMessage)` 受 `proceed` guard。
3. 人工（或與 `panedock_launch_smoke` 等價的 script）驗證：在帶「不乾淨關閉」標記＋既有 recovery 的 session 啟動，於警示 MessageBox 顯示期間 Alt+F4 主視窗；app 應正常退場、不應再彈出後續警告框、不應對已銷毀視窗 post deferred realize（無錯誤視窗殘留）。若無法以 script 驅動，於交接區記錄。

## Agent checks

```powershell
rg -n "proceed|PostMessageW\(window, kDeferredRealizeMessage" src/app_shell/main.cpp
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

## Handoff requirements

- 記錄 `proceed` flag 的引入、它與 `begin_shutdown`/`WM_DESTROY` 的 `closing_`/`quit_requested` 的關係，以及為何這補上了 PD-126 未涵蓋的「啟動序繼續跑」。
- 記錄 build／CTest／launch smoke／diff 結果，以及是否實際模擬「啟動警示期間關閉」驗證過不再 post 到銷毀 HWND。
- 任何未驗證項目均須標明。

## 交接區

<!-- 實作 agent append-only -->
