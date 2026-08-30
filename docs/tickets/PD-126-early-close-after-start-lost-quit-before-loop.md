# PD-126 — 啟動後立即關閉(UI 剛出現)會在啟動 MessageBox 的 modal loop 內吃掉 `WM_QUIT`,外層 loop 空佇列時永遠阻塞

Phase 7 · app_shell / message loop startup · Depends on: PD-124, PD-125

- Source: 「關閉 AP 但 process 依然存在」稽核迴圈第三輪的發現(2026-08-30),使用者明確要求「纳入 UI 出現後馬上就關閉」的情境。
- Origin: PK-124 修的是「外層 loop 已在跑、`WM_CLOSE` 由執行中的巢狀 modal loop 派發、吃掉 `WM_QUIT」;本票處理時間更早的變體——`WM_CLOSE` 發生在**外層 message loop 尚未開始**時,`WM_QUIT` 被啟動階段的 modal loop 吃掉,外層 loop 啟動後又因佇列為空而永遠 block。
- Priority: HIGH——與原回報「看得到卻關不掉」同類;啟動後極短時間內關閉(尤其開機時有 recovery／不乾淨關閉警示)時,程序殘留。

## 協定:正常啟動後立即關閉 vs 有啟動 MessageBox 時關閉

先釐清「UI 剛出現後馬上關閉」的兩種子情況,以免把本已安全的路徑誤判為 bug:

### 正常啟動(無 recovery／無不乾淨關閉)
1. `WM_CLOSE`(使用者按 X / Alt+F4)在訊息迴圈(`wWinMain` 的 `GetMessageW`)啟動後才會被派發;它本身就是佇列中的訊息,`GetMessageW` 會取回,走 `WM_CLOSE` 路徑 → `closing_`(PD-125)＋`quit_requested`(PD-124)→ 外層 loop break → 退出。再者,`startup_realize_pending`(若有未 realiize 的可見 pane)會 `PostMessage(kDeferredRealizeMessage)`(`main.cpp:5276`),也是佇列中的一個喚醒。
2. 因此正常啟動後立即關閉**在 PD-124／PD-125 後已是安全**;本票不是修它。

### 有啟動 MessageBox(recovery 或 !clean_shutdown)時關閉
1. 啟動序列:`CreateWindowExW` → `ShowWindow` → `UpdateWindow` → 視 `recovered_from_corruption`／`clean_shutdown` 跳出 **`MessageBoxW`(`main.cpp:5253`、`:5261`、`:5269`)** → post deferred realize(`:5276`)—> 進入外層 message loop(`:5289`)。
2. `MessageBoxW` 在本 app 自己的 UI thread 上執行一個 **nested modal loop**。若在該 MessageBox 顯示期間關閉(例如對主視窗送 `WM_CLOSE`／`taskkill`,`WM_CLOSE` 對 disabled 主視窗仍會進佇列並被該 modal loop 派發):
   - `WM_CLOSE` handler(PD-125):`closing_`／`quit_requested` 置位,`destroy_explorers`、`DestroyWindow`、`PostQuitMessage(0)`。
   - `MessageBoxW` 的 modal loop 的 `GetMessage` 取走那顆 `WM_QUIT` 並回傳 → `MessageBoxW` 返回。**`WM_QUIT` 已被啟動階段的 modal loop 消耗**。
3. 之後 `wWinMain` 繼續:`PostMessage(deferred)`(若 `startup_realize_pending` 為真會有一個喚醒訊息;若為假則無),然後進入外層 `GetMessageW` loop。
4. 若此時佇列為空(例如 `startup_realize_pending` 為假、且 `WM_CLOSE` 已派發),外層 `GetMessageW` 會 **block**。PD-124 的 `quit_requested` 旗標只在 `DispatchMessageW` **之後**檢查——但該檢查永遠不會跑到,因為 `GetMessageW` 在 loop 主體執行前就 block。`quit_requested` 已經 true 也喚不醒它 → 程序殘留。

這是 PD-124 未涵蓋的時間窗:旗標的檢查點在「進入 `GetMessageW` 之前」缺失,而 `GetMessageW` 本身會無限期 block。

## Binding constraints — quoted, do not weaken

`AGENTS.md`:

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`:

> Event-driven idle path only. No busy loops, no polling timers.

`docs/design-spec.md §9.2`:

> 單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。

## Files to read and trace first

- `src/app_shell/main.cpp:5237-5291`——`CreateWindowExW` / `ShowWindow`／`UpdateWindow`／`MessageBoxW`／post deferred realize / 進入外層 loop。
- `src/app_shell/main.cpp:5291-5361`——外層 `GetMessageW` loop 與 `quit_requested` 檢查點(修正後版本)。
- `src/app_shell/main.cpp:5001-5065`——`WM_CLOSE`／`WM_DESTROY`／`WM_QUERYENDSESSION`／`WM_ENDSESSION`(PD-124／PD-125 修正後)。
- `docs/tickets/PD-124-*-nested-modal-loop-close-lost-quit-message.md`、`PD-125-*-close-sequence-reentrancy-and-endsession-exit.md`——前一輪的旗標與 `closing_` 守衛。

## Fix 方向 / Scope(已實作)

把「外層 loop 退出」的檢查從只在 `DispatchMessageW` 之後,提前到**每次迭代進 `GetMessageW` 之前**(`main.cpp:5292-5294`)。

1. 把 `while ((result = GetMessageW(...)) > 0)` 改成 `for (;;)`:
   - 進迴圈先 `if (state.quit_requested) break;`——就算啟動階段 `WM_QUIT` 已被 MessageBox modal loop 消耗、佇列為空,先檢查旗標就直接退出,不會 block。
   - `result = GetMessageW(&message, nullptr, 0, 0); if (result <= 0) break;`(保留 WM_QUIT → 0、錯誤 → -1 的原始語意)。
   - 保留 `DispatchMessageW` 之後的 `if (state.quit_requested) break;`(PD-124,處理「執行中巢狀 modal loop」的情況)。
2. 修改後 `flag` 三處檢查行為不變;`exit_code` 仍為 `result < 0 ? 1 : message.wParam`,並在 `quit_requested` 時強制 0(`main.cpp:5360-5361`)。
3. 無新增 thread、hook、timer、polling;`Event-driven idle` 原則不變(`GetMessageW` 仍在無訊息時 block,只有 `quit_requested` 需退出時才跳過它)。

為何有效:當 `WM_QUIT` 被啟動 MessageBox 的 modal loop 消耗後,外層 loop 一進來就在 `GetMessageW` 之前看到 `quit_requested=true` 而 break;若在執行中被巢狀 loop 消耗(`PD-124`),則在 `DispatchMessageW` 後 break。兩種時間窗都覆蓋。

## Non-goals

- 不把 `GetMessageW` 改成 `PeekMessage` 或任何輪詢;閒置時仍「無訊息即 block」。
- 不新增 async runtime、背景執行緒、IPC 或 timer。
- 不為啟動 MessageBox 新建「關閉確認」或 barrier;其處於 `recovered_from_corruption`／`!clean_shutdown` 時的既有顯示行為不變。
- 不修改 `WM_CLOSE`／`WM_ENDSESSION` 的 teardown 內容(PD-125 已負責其順序與冪等)。
- 不改變正常啟動後立即關閉的行為——該路徑本已安全,本票只覆蓋「啟動 MessageBox 期間收到關閉」的缺陷時間窗。

## Acceptance Criteria

1. `cmake --build build`、`ctest --test-dir build --output-on-failure` 與 `git diff --check` 全數通過。
2. `rg -n "quit_requested|GetMessageW" src/app_shell/main.cpp` 確認:旗標在外層 loop **進 `GetMessageW` 前**與 `DispatchMessageW` 後各檢查一次;`result <= 0` 處理 WM_QUIT／錯誤。
3. 正常啟動後關閉(Release `build\PaneDock.exe`、`EnumWindows` 找 visible `PaneDockMainWindow`、送一次不帶 `/F` 的 `taskkill /PID`)⇒ PID 與全部頂層 HWND 在 5 秒內同時消失,連續 3 次。
4. 啟動 MessageBox 情境:以 `clean_shutdown=false` 的 `session.json`(或先產出一次不乾淨關閉狀態)啟動,出現警告 MessageBox 期間對主視窗送 `WM_CLOSE`／`taskkill`;`PaneDock` 在 5 秒內退出,不殘留。(若無法產出可靠的不乾淨關閉狀態,改為先以一次性 driver 直接對主 HWND 送 `PostMessage(WM_CLOSE)` 於 MessageBox 顯示期間驗證。)
5. 關閉後 `session.json` 的 `clean_shutdown` 為 `true`(若此為正常關閉),未知欄位保留與 backup/atomic replace 行為不變。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
rg -n "quit_requested|GetMessageW|for \(;;\)" src/app_shell/main.cpp
```

實機 check 使用 Release `build\PaneDock.exe`、真實 `EnumWindows`/`EnumChildWindows` 幾何與不帶 `/F` 的 `taskkill /PID`;不得以猜測座標、`CopyFromScreen` 或強制終止代替。

## Handoff requirements

- 記錄啟動 MessageBox modal loop 消耗 `WM_QUIT` 的機制、以及旗標提前到 `GetMessageW` 之前的位置。
- 記錄 build／CTest／diff 結果與是否有真實桌面的「啟動後立即關閉」／「啟動 MessageBox 期間關閉」驗證。
- 列出未驗證項目與原因;不得把單次成功或工具退出延遲當作根因。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-30 已實作外層 loop 提前檢查 `quit_requested`

- 根因:見上方「有啟動 MessageBox 時關閉」;PD-124 的旗標只檢查在 `DispatchMessageW` 之後,啟動階段 MessageBox 的 modal loop 消耗 `WM_QUIT` 後,外層 `GetMessageW` 在佇列為空時會 block,旗標檢查永遠不執行。
- 已修改 `src/app_shell/main.cpp`(僅此一檔):外層 loop 由 `while ((result = GetMessageW(...)) > 0)` 改為 `for (;;)`,進迴圈先 `if (state.quit_requested) break;`,再 `result = GetMessageW(...); if (result <= 0) break;`,保留 `DispatchMessageW` 後的 break(`main.cpp:5291-5361`)。
- 正常啟動後立即關閉本就安全(WM_CLOSE 為佇列訊息,或有 deferred realize 喚醒),未修改其行為;本票只覆蓋「啟動 MessageBox 期間收到 WM_CLOSE 且佇列轉空」的缺陷時間窗。
- §9.4 順序、teardown 內容、無 thread/hook/timer/polling,皆未變。
- 驗證:`cmake --build build` PASS、`ctest --test-dir build --output-on-failure` 6/6 PASS、`git diff --check` 通過。
- 與 PD-124 關係:PD-124 處理「執行中巢狀 modal loop 消耗 WM_QUIT」(檢查點在 `DispatchMessageW` 後);本票處理「外層 loop 啟動前、MessageBox modal loop 消耗 WM_QUIT」(檢查點在 `GetMessageW` 前)。兩者補足外層 loop 對 `WM_QUIT` 被消耗的所有時間窗。
- 待補實機驗證:Release 桌面下「正常啟動後立即關閉」與「啟動 MessageBox 期間關閉」各一次,確認 5 秒內 exit。
