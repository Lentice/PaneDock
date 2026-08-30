# PD-124 — 巢狀 modal loop 內關閉時 `WM_QUIT` 被吃掉,外層 message loop 永不退出(程序殘留)

Phase 7 · app_shell / message loop · Depends on: PD-007, PD-032, PD-068, PD-106

- Source: 使用者回報「關閉 AP 但 process 依然存在」(2026-08-30)＋ Codex 報告的調試觀測。
- Origin: 主 UI thread(TID 19300)停在外層 `GetMessageW`,視窗已銷毀,但 `WM_QUIT` 未到達外層 loop;thread 並未卡在 `IExplorerBrowser::Destroy`、clipboard 或 COM。PD-106 已修正其中一種態樣,本票處理其剩餘的、更深的態樣。
- Priority: HIGH——程序殘留會鎖住執行檔、阻礙下次啟動與建置,且佔用已銷毀視窗對應的 Shell 資源;屬於桌面應用「看得到卻關不掉」的核心缺陷。

## 已確認的根因(有程式碼證據)

1. 本程式在 UI thread 上會進入**巢狀 modal loop**:我們自己的 `TrackPopupMenu`(`src/app_shell/main.cpp:3126` view-mode、`:3193` pinned-locations、`:4556` tab context、`:4610` group context),加上 `IExplorerBrowser` 宿主的 shell32 右鍵選單、`IFileOperation` 進度對話框與 OLE 拖放內部迴圈。
2. `WM_CLOSE`(`main.cpp:4996`)目前做的事:`save_now`(`:5004`)+ `destroy_explorers`(`:5005`)→ `DestroyWindow(window)`(`:5009`)→ `PostQuitMessage(0)`(`:5010`)。這些全部發生在「派發 `WM_CLOSE` 的那個 loop」之內。
3. 若派發 `WM_CLOSE` 的是一個**巢狀 modal loop**(例如使用者在 view-mode 選單、tab 右鍵選單或某個 shell context menu 開啟期間關閉視窗,或 Windows 在 `IFileOperation` 進行中送 close),`PostQuitMessage(0)` 的 `WM_QUIT` 會被**該巢狀 loop 的 `GetMessage` 取走並消耗**。巢狀 loop 回傳,控制權回到外層 `wWinMain` 的 `GetMessageW`(`main.cpp:5278`),此時視窗已銷毀、`WM_QUIT` 已不在,外層 loop 永遠 block。這正是 Codex 看到的「主視窗已銷毀、thread 停在外層 `GetMessageW`、非卡在 Destroy」。
4. **與 PD-106 的關係**。PD-106 把 `PostQuitMessage(0)` 從 `WM_DESTROY` 移到 `WM_CLOSE` 的 `DestroyWindow` 返回之後,解決的是「`DestroyWindow` 期間 Shell child teardown 重入、消耗剛排入的 `WM_QUIT`」的態樣。但把 `PostQuitMessage` 放在 `WM_CLOSE` 只保證它對「普通、非巢狀」的 `WM_CLOSE` 有效;當 `WM_CLOSE` 本身是由巢狀 loop 派發時,`WM_QUIT` 仍舊被那個巢狀 loop 吃掉,外層 loop 依然拿不到。本票不是重開已否決方向,而是 PD-106 的未竟後續:單靠「把 quit 放在 `WM_CLOSE`」並不足以保證外層 loop 一定退出。

## Binding constraints — quoted, do not weaken

`docs/design-spec.md §9.4 關閉序列`:

> 1. 擷取現行狀態並原子寫入 session document
> 2. **destroy 全部 live `IExplorerBrowser`**(每個曾 `Initialize` 的都必須 `Destroy`)
> 3. destroy pane HWND
> 4. destroy 主視窗
> 5. 退出訊息迴圈
> 6. `CoUninitialize`
>
> 順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。

`docs/design-spec.md §9.2`:

> 單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。

`AGENTS.md`:

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`:

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`:

> Event-driven idle path only. No busy loops, no polling timers.

## Files to read and trace first

- `src/app_shell/main.cpp:449-460`——`AppState` 新增的 `quit_requested`。
- `src/app_shell/main.cpp:4996-5020`——`WM_CLOSE`／`WM_DESTROY` 的 teardown 與 `PostQuitMessage`。
- `src/app_shell/main.cpp:5278-5340`——外層 `GetMessageW` 迴圈、`TranslateMessage`/`DispatchMessageW` 與退出判定。
- `src/app_shell/main.cpp:3126/3193/4556/4610`——四個 `TrackPopupMenu` 巢狀 modal loop 入口。
- `src/app_shell/main.cpp:5036-5051`——`WM_QUERYENDSESSION`／`WM_ENDSESSION`(保持現狀,見 Residual risks)。
- `docs/tickets/PD-106-graceful-shutdown-shell-teardown.md`——前一次部分修復,及其交接區中的「root cause 更正」。
- `docs/tickets/PD-032-endsession-clean-shutdown-handling.md`、`PD-068-shell-folder-view-setcallback-null-out-param.md`——既有關閉路徑 precedent。

## Fix 方向 / Scope(已實作)

最小 root-cause 修正:讓外層 loop 的退出**不依賴** `WM_QUIT` 是否抵達它,改為以一個旗標決定。

1. 在 `AppState`(`main.cpp:459`)新增 `bool quit_requested{};`,並註明為何不能只用 `PostQuitMessage`。
2. 在 `WM_CLOSE`(`main.cpp:4998`)與 `WM_DESTROY`(`main.cpp:5014`)的開頭置位 `state->quit_requested = true;`。`WM_DESTROY` 置位是收斂任何「主視窗已銷毀」的路徑,不只依賴 `WM_CLOSE`。
3. 外層迴圈在每次 `DispatchMessageW(&message)` 之後(`main.cpp:5337`)檢查 `if (state.quit_requested) break;`,並在迴圈結束後(`main.cpp:5340`)讓 `exit_code = 0`。
4. §9.4 順序完全不變:先保存 session、再 destroy 全部 live browser、再 destroy parent HWND、最後退出迴圈。未新增 thread、hook、timer、polling 或 cleanup short-circuit(`AGENTS.md` 的事件驅動閒置路徑仍成立)。

為何有效:巢狀 modal loop 若在**它的派發**中跑了 `WM_CLOSE` 並吃掉 `WM_QUIT`,控制權最終仍會回到外層 `DispatchMessageW` 之後的迴圈主體,此時 `quit_requested` 已為真,迴圈 break。普通非巢狀 case 則由 `PostQuitMessage` 正常讓 `GetMessageW` 回 0,flags 是 backstop,兩種 `break` 路徑都會讓 process 結束。

## Non-goals

- 不改變 §9.4 的關閉順序,不跳過任何已 `Initialize` browser 的 `Destroy`。
- 不依賴、也不嘗試修正第三方 shell32/OLE `TrackPopupMenu` modal loop 的 `WM_QUIT` 行為——那是 `GetMessage` 的定義,不是本程式的缺陷。
- 不為 `IExplorerBrowser` 新增 fake／抽象測試 seam,不修改 `src/core`。
- 不把 teardown 移到背景執行緒,不引入 async runtime、IPC、service 或 driver。
- 不新增 polling timer 或 busy loop 來「確保」外層 loop 醒來。
- **`WM_ENDSESSION(wparam=TRUE)` 路徑不改**:關機由 OS 終止 process,若在該處置位 `quit_requested` 會破壞「關機被取消後 app 繼續存活」的語意。該路徑維持現狀。
- `--diagnostic` console 的 Ctrl+C / 關閉 console:本 app 不註冊 console control handler,維持現狀(資料遺失風險單獨記載於 Residual risks,非本票範圍)。

## Acceptance Criteria

1. `cmake --build build`、`ctest --test-dir build --output-on-failure` 與 `git diff --check` 全數通過。
2. `rg -n "quit_requested" src/app_shell/main.cpp` 確認旗標在 `WM_CLOSE`／`WM_DESTROY` 置位,外層迴圈在 `DispatchMessageW` 後檢查並 break;`exit_code` 在旗標置位時為 0。
3. 真實、可互動桌面(release build)下,以 `EnumWindows` 找到 visible `PaneDockMainWindow` 並送出一次不帶 `/F` 的 `taskkill /PID`,`PaneDock` 的 PID 與全部頂層 HWND 在 5 秒內同時消失,連續 3 次通過(沿用 PD-106 的驗證協定,證明普通關閉沒退化)。
4. 巢狀 modal loop 情境:在一個 pane 的右鍵選單(view-mode / tab / group context menu)開啟期間送出 `WM_CLOSE`(或對主視窗的 `taskkill`),`PaneDock` 在 5 秒內退出,主視窗與 PID 同時消失;此為本票的決定性情境,不可用「普通關閉通過」替代。
5. 關閉後 `session.json` 的 `clean_shutdown` 為 `true`,既有未知欄位保留與 backup/atomic replace 行為不變。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
rg -n "quit_requested|PostQuitMessage|case WM_CLOSE|case WM_DESTROY|TrackPopupMenu" src/app_shell/main.cpp
```

實機 check 必須使用 Release `build\PaneDock.exe`,以真實 `EnumWindows`/`EnumChildWindows` 幾何與不帶 `/F` 的 `taskkill /PID`;不得以猜測座標、`CopyFromScreen` 或強制終止代替。

## Handoff requirements

- 記錄本票已套用的旗標修正位置、為何它涵蓋 PD-106 未涵蓋的巢狀 modal loop 態樣。
- 記錄 build／CTest／diff 結果,以及是否有真實桌面的普通關閉與「選單開啟期間關閉」的驗證。
- 列出未驗證項目與原因;不得把單次成功或工具退出延遲當作根因。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-30 已實作旗標修正

- 根因定位:見上方「已確認的根因」第 3 點。Codex 原始調試顯示 thread 停在外層 `GetMessageW`、視窗已銷毀、非卡在 `IExplorerBrowser::Destroy`/clipboard/COM——與「巢狀 modal loop 消耗 `WM_QUIT`」完全吻合。
- 已修改 `src/app_shell/main.cpp`(僅此一檔):
  - `AppState` 新增欄位 `bool quit_requested{};`(`:459`,含說明註解)。
  - `WM_CLOSE`(`:4998`)與 `WM_DESTROY`(`:5014`)開頭 `state->quit_requested = true;`。
  - 外層 `GetMessageW` 迴圈在 `DispatchMessageW` 後 `if (state.quit_requested) break;`(`:5337`),迴圈後旗標置位時 `exit_code = 0`(`:5340`)。
- §9.4 關閉順序、`destroy_explorers`、`save_now`、`FlushFileBuffers` 原子寫入與未知欄位保留皆未變;未新增 thread/hook/timer。
- 驗證:`cmake --build build` PASS、`ctest --test-dir build --output-on-failure` 6/6 PASS、`git diff --check` 通過(先前的流程)。
- 尚未在真實桌面對「選單開啟期間關閉」做決定性重現(見 Agent checks 4);普通關閉路徑由本票的旗標為 backstop、`PostQuitMessage` 為主,邏輯上不退化。此項列為待補的實機驗證。
- 與 PD-106 關係:PD-106 處理「`WM_DESTROY` 重入消耗 `WM_QUIT`」並已把 quit 移到 `WM_CLOSE`;本票處理「`WM_CLOSE` 由巢狀 modal loop 派發而消耗 `WM_QUIT`」——前者靠 `WM_CLOSE` 位置,後者靠外層旗標。兩者互補,本票為 PD-106 的後續,非重開已否決方向。
