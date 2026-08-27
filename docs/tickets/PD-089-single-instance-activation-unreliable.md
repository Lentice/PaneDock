# PD-089 — PD-084 的單一實例喚醒在常見情境下會靜默失敗:`SetForegroundWindow` 被拒絕、輪詢逾時無回饋(覆寫/修正 PD-084 的已知缺口)

## 來源

2026-08-27 三方稽核(Claude / Codex / OpenCode)。**三份報告全部獨立指出同一張剛完成的票(PD-084)有真實的可靠性缺陷**,是本次稽核中收斂程度最高的發現。Claude 的報告額外指出最關鍵的技術細節:`SetForegroundWindow` 在呼叫方不擁有前景權限時會被 Windows **靜默拒絕**(不報錯、不丟例外,就是不生效)。

本票**不是**要否決 PD-084 的既有決策(具名 mutex、不做跨 session 限制等決策維持不變),而是修正 PD-084 交接區已知未驗證、且稽核確認為真實 bug 的兩個子問題。

## 背景與現況

`src/app_shell/main.cpp:3834-3864` 一帶(`find_existing_main_window` / `activate_existing_main_window`,由 PD-084 引入):

1. 第二個實例偵測到 mutex 已存在後,呼叫 `find_existing_main_window()`:以 `FindWindowW(kWindowClassName, nullptr)` 輪詢,間隔 `Sleep(50)`,總逾時 5000ms。
2. 找到視窗後呼叫 `activate_existing_main_window()`:視需要 `ShowWindow(SW_RESTORE)`,再呼叫 `SetForegroundWindow`。

兩個具體缺陷:

- **`SetForegroundWindow` 常見情境下無效。** Windows 前景鎖定規則(見 MS Learn `SetForegroundWindow`/`AllowSetForegroundWindow` 文件)限制:一個 process 若不是目前擁有前景焦點的 process、也沒有透過使用者互動觸發呼叫,`SetForegroundWindow` 通常只會讓目標視窗在工作列閃爍,**不會**真的把它帶到前景——這正是「使用者雙擊桌面圖示,PaneDock 已在背景執行」這個最常見的使用情境。PD-084 自己的交接區已經誠實記錄「未取得截圖」「留給使用者在互動桌面驗證」,本次稽核確認這不是驗證不足,而是程式邏輯本身在此情境下大機率不會生效。
- **輪詢逾時是靜默失敗,沒有任何回饋。** 若第一個實例啟動較慢(大型 `session.json`、慢速網路磁碟機、shell extension 載入慢),5 秒逾時後第二個實例直接結束,使用者雙擊圖示卻什麼事都沒發生,且沒有任何錯誤訊息或日誌可供排查。

## Fix 方向

### 1. 前景喚醒:改用「讓第一實例喚醒自己」而不是「第二實例喚醒第一實例」

`SetForegroundWindow` 的限制是針對「呼叫方」,一個 process 呼叫 `SetForegroundWindow` 讓**自己**的視窗前景化,在多數情況下比另一個 process 幫它呼叫更容易成功;業界常見的可靠作法是搭配 `AttachThreadInput` 暫時附著兩個執行緒的輸入狀態、或暫時調整 `SPI_SETFOREGROUNDLOCKTIMEOUT`,呼叫後復原。實作者應查閱 MS Learn 的 `SetForegroundWindow`/`AllowSetForegroundWindow`/`AttachThreadInput` 文件,選擇一個可靠、範圍最小的技巧,並在交接區記錄選用的機制與理由(依既有慣例引用 API 文件出處)。

具體流程建議:第二實例找到第一實例的視窗後,不直接呼叫 `SetForegroundWindow`,而是 `PostMessageW` 一個私有訊息(例如 `WM_APP` 自訂編號)給第一實例的視窗;第一實例在自己的 `WndProc` 收到這個訊息時,在**自己的執行緒**內執行「若最小化則 `SW_RESTORE`,並用可靠技巧把自己帶到前景」。第一實例把自己帶到前景,比第二實例幫第一實例呼叫,更符合 Windows 的前景權限模型。

### 2. 輪詢逾時:維持有限等待,但態度上仍算「一次性啟動等待」而非常駐輪詢

`AGENTS.md` 的「Event-driven idle path only」規範的是**執行期間**的閒置行為,PD-084 交接區已經把這段輪詢定調為「啟動空窗期的一次性有限等待」,本票不推翻這個定調。但既然三份稽核都點名這段輪詢,建議的改善方向(非強制,實作者可評估):讓第一實例在 `CreateWindowExW` 成功後,立刻 `SetEvent` 一個具名事件(例如 `L"PaneDock-MainWindowReadyEvent"`),第二實例改成 `WaitForSingleObject` 這個事件(仍設一樣的 5 秒上限),取代盲目的 `Sleep(50)` 輪詢——這是事件驅動而非輪詢,且能在第一實例真正就緒的當下立刻喚醒第二實例,不需要等到下一次輪詢間隔。若實作者評估這個改動的複雜度不划算,至少要確保逾時後有留下可追蹤的訊號(例如既有的診斷/日誌機制,若專案目前沒有日誌機制則不需新增,只需在交接區明確記錄逾時情境下的實際行為)。

## 綁定限制(引用)

- `AGENTS.md`:「Event-driven idle path only. No busy loops, no polling timers.」—— 見上方 fix 方向 2 的討論。
- `AGENTS.md`:「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」—— 不要為了這個修正引入訊息佇列、IPC library 之類的重量級機制,`PostMessageW`/具名事件都是既有 Win32 API,足以解決問題。
- `AGENTS.md`:「No network, no telemetry, no third-party runtime, no services, no drivers, no admin elevation.」—— 本票所有建議手段皆為標準 Win32 API,不違反此限制。
- 本票**不覆寫** PD-084 的決策 1(具名 mutex 判定方式)、決策 4(不做跨 session 限制)——這些維持原樣。本票覆寫/修正的是決策 2 中「喚醒既有視窗」的**實作機制**(原本是第二實例直接呼叫 `SetForegroundWindow`,現在改為訊息轉發讓第一實例喚醒自己)。

## 檔案與範圍

- `src/app_shell/main.cpp`:
  - `find_existing_main_window()`(約 `:3834-3851` 一帶)
  - `activate_existing_main_window()`(約 `:3853-3864` 一帶)
  - `wWinMain` 中呼叫上述兩者的位置
  - `WndProc`(需新增處理第一實例收到「請把自己帶到前景」訊息的分支)

## Scope

1. 定義一個私有 `WM_APP` 自訂訊息(例如 `WM_APP_ACTIVATE_EXISTING_INSTANCE`),第二實例找到第一實例視窗後改為 `PostMessageW` 這個訊息,而不是直接呼叫 `SetForegroundWindow`。
2. 第一實例的 `WndProc` 處理該訊息:視需要 `ShowWindow(SW_RESTORE)`,並用可靠的技巧把自己帶到前景(依上方 fix 方向 1 選定的機制實作)。
3.(可選,依實作者評估)把 `find_existing_main_window` 的 `Sleep` 輪詢改為具名事件 `WaitForSingleObject`,第一實例在主視窗建立成功後 `SetEvent`。若評估後不做,需在交接區說明理由。

## Non-goals

- 不變更 PD-084 已定案的 mutex 名稱、判定方式、決策 4(不做跨 session 限制)。
- 不新增任何命令列參數轉發給第一實例(維持 PD-084 的 Non-goals:不做 IPC 傳遞啟動參數)。
- 不新增設定選項讓使用者關閉單一實例限制。

## Acceptance Criteria

1. 第一個 `PaneDock.exe` 實例執行中且視窗**不在前景**(例如被其他應用程式視窗遮蔽/切換到其他應用程式)時,第二次啟動 `PaneDock.exe`,第一實例的視窗必須確實被帶到前景(可視覺確認,不是只有工作列閃爍)。
2. 第一實例最小化時,第二次啟動能正確還原並前景化。
3. 第一實例啟動較慢(可用人為方式模擬,例如刻意放大 session.json 或在 debugger 中下中斷點延後 `CreateWindowExW`)時,第二實例的等待行為維持有限逾時,不會無限期等待;若選擇維持輪詢機制,逾時值與 PD-084 一致(5000ms)或說明調整理由。
4. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。
5. 不影響正常單一實例啟動(第一個實例)的既有行為。
6. 不影響診斷模式(`--diagnostic`)第二次啟動時「直接依單一實例規則結束」的既有行為(PD-084 決策 1)。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

以及手動驗證(單一操作 + 截圖等級,允許實作者自行完成):

- 啟動 `build\PaneDock.exe`,切換到其他應用程式視窗使其失去前景焦點,再次啟動 `build\PaneDock.exe`,截圖確認第一個視窗確實回到前景(不是只有工作列圖示閃爍)。

> **驗證政策提醒:** 單一點擊/單一操作 + 截圖由 Agent 自行完成即可;需要反覆切換視窗焦點、模擬啟動延遲等連續多步驟驗證,留給使用者在實機上驗證,不要用 computer-use 工具連續操作。完成後在交接區寫清楚哪些是自己驗證過的、哪些留給使用者,特別是「視窗真的被帶到前景」這一項務必誠實記錄實際驗證方式,不要用「process 存活數量正確」代替「視窗前景化成功」的證據(PD-084 交接區已經犯過這個混淆,本票要修正的正是這個缺口本身)。

## 交接區

### 2026-08-27 實作交接

- `src/app_shell/main.cpp` 新增私有 `WM_APP + 50` 訊息。第二實例找到第一實例後，先以 `AllowSetForegroundWindow` 將前景權限交給第一實例，再以 `PostMessageW` 要求它自行喚醒；第二實例不再直接呼叫 `SetForegroundWindow`。第一實例在自己的 `WndProc` 中必要時 `ShowWindow(SW_RESTORE)`，暫時以 `AttachThreadInput` 附著目前前景視窗的輸入執行緒，呼叫 `SetForegroundWindow` 後立即解除附著。沒有修改全域 `SPI_SETFOREGROUNDLOCKTIMEOUT`。
- 選用的 Win32 API 依據：[SetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow) 說明呼叫者的前景限制；[AllowSetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-allowsetforegroundwindow) 允許有權限的第二實例把權限轉交第一實例；[AttachThreadInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-attachthreadinput) 提供第一實例執行緒的最小範圍補強，避免改動使用者的全域前景鎖定設定。
- Mutex 名稱仍為 `L"PaneDock-SingleInstanceMutex"`，未改動 PD-084 決策；它固定、由正常／診斷模式共用，且不依賴視窗標題。第二實例仍在 mutex 已存在時直接結束，不進入 OLE、session 或新視窗建立流程。
- 視窗搜尋仍使用 50 ms 間隔、5000 ms 總逾時的啟動空窗期有限等待；這是既有 PD-084 的數值，且不屬於常駐 idle polling。未採用可選的具名 ready event，因目前一次性有限等待已滿足範圍，新增 event 會增加另一個具名同步物件與建立時序；逾時現在會以既有 `OutputDebugStringW` 留下 `timed out waiting for existing main window` 訊號。若日後量測到啟動常超過 5 秒或需要更精確的 ready 時點，再改用具名 event。
- Mutex handle 的實作仍是顯式 `CloseHandle`：第二實例在喚醒請求後釋放自己的 handle；第一實例從 `wWinMain` 持有至所有 view 清理與 `OleUninitialize()` 完成後才釋放，沒有在視窗關閉中途提前釋放。若 process 異常結束，Windows 仍會自動回收 handle。

#### Agent checks

```text
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release：PASS。
cmake --build build：PASS；LLVM-MinGW 編譯並連結 PaneDock.exe 成功。
ctest --test-dir build --output-on-failure：PASS；5/5 tests passed。
git diff --check：PASS；無 whitespace error。
```

#### Focused runtime self-check

- 以 Release `build\PaneDock.exe` 背靠背啟動兩次：第一 PID `46016`、第二 PID `42300`。第二實例在 7 秒內退出，`Get-Process -Name PaneDock` 回報只剩第一個 PID；這驗證了第二實例不持續存活，且有限等待路徑可返回。
- sandbox 中第一個 process 的 `MainWindowHandle` 為 0，無法取得互動桌面的前景視窗或截圖；`tasklist` 未取得證據，程序數量是以 `Get-Process` 檢查。優雅終止訊號因沒有可用視窗 handle 未能完成，最後只對已確認的 self-check PID 做了必要的 `/F` 清理；這不是產品流程的驗證結果。

#### Acceptance evidence

| # | 結果 | 證據 |
|---|---|---|
| 1 | 留給使用者 | 未在互動桌面切換到其他程式，也沒有截圖；因此未宣稱第一視窗確實前景化。 |
| 2 | 留給使用者 | 未取得可操作的最小化視窗 handle，未驗證 `SW_RESTORE` 的實際畫面結果。 |
| 3 | 部分驗證 | 程式碼仍為 50 ms／5000 ms 有限等待；runtime self-check 中第二實例已在 7 秒內結束，但沒有用 debugger 或大型 session 量測精確逾時行為。 |
| 4 | 通過 | Release configure、build 與 CTest 5/5 全部成功。 |
| 5 | 部分驗證 | 第一實例 process 能啟動，且正常單一實例的 session／視窗建立程式碼未改動；sandbox 沒有可見畫面，未做互動桌面確認。 |
| 6 | 程式碼驗證，實機留給使用者 | mutex 檢查仍早於 `--diagnostic` 解析，第二次診斷啟動會直接依單一實例規則結束；`diagnostic_requested`、`SetProcessMitigationPolicy` 與 PD-070 的 `SetErrorMode` 路徑未改動。 |

未新增 `core` 單元測試：這段行為依賴 Win32 kernel mutex、跨 process message 與真實 top-level window，無法透過既有 `core` seam 驗證；本次以建置、CTest 與上述程序層 self-check 驗證可自動驗證的部分，視窗真正前景化／最小化還原留給使用者在互動桌面完成。
