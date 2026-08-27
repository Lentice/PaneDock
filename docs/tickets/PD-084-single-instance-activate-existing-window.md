# PD-084 — 限制單一 App 實例;第二次啟動改為喚醒既有視窗

## 來源

使用者直接需求:「only one ap instance allowed. activate the window when 2nd executed. notice the race condition.」

## 背景與現況

`wWinMain`(`src/app_shell/main.cpp:3745`)目前沒有任何單一實例檢查。每次執行 `PaneDock.exe` 都會:

1. 讀取 `%LOCALAPPDATA%\PaneDock` 下的 session(`session_directory()` → `panedock::core::read_session`,`main.cpp:3792-3809`)。
2. 建立一個新的 `kWindowClassName`(`L"PaneDockMainWindow"`,`main.cpp:46`)主視窗(`CreateWindowExW`,`main.cpp:3818`)。
3. 呼叫 `save_now(state)`(`main.cpp:3812`)寫回 session。

`docs/tickets.md` 的「已否決的方向」與「候選」兩節都沒有單一實例相關項目,`docs/design-spec.md` 也沒有提到此限制 — 這是全新需求,不是覆寫既有決策。

## 為什麼這是真的問題(不只是「兩個視窗同時開」而已)

`session.json` 的寫入是「整份 `ApplicationState` 覆寫」(`save_now` → `panedock::core::write_session`,atomic replace,見 `AGENTS.md`「All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace...」)。如果允許兩個實例同時執行:

- 兩個實例各自持有自己記憶體內的 `AppState.application`,互不知道對方的存在。
- 任一實例的操作(切換 Group、開分頁、搬動 pane)觸發 `save_now` 時,會用自己記憶體內的完整快照覆寫整個 `session.json`,包含對方實例後續做的所有變更。
- 結果是「後寫入的實例贏,先寫入的實例的所有變更全部消失」— 這不是幾個欄位衝突,是整份設定檔的最後寫入覆蓋(last-writer-wins on the whole document),使用者會遺失整個工作階段的變更且沒有任何警告。

因此正確做法不是「允許多開,之後再處理存檔衝突」,而是從一開始阻止第二個實例啟動並操作 UI。

## 產品決策

1. **判定方式:具名 kernel mutex,而非 `FindWindow`。** `FindWindow` 依賴視窗類別/標題字串比對,診斷模式的視窗標題是 `L"PaneDock \x2014 Diagnostic Mode"`(`main.cpp:3815-3817`),與正常模式標題不同,用 `FindWindow` 比對標題會漏掉診斷模式視窗、造成兩者可以並存。改用 `CreateMutexW` 建立一個固定名稱的具名 mutex(建議 `L"PaneDock-SingleInstanceMutex"`,或依現有专案命名慣例微調,由實作者決定但需記錄在交接區),用 `GetLastError() == ERROR_ALREADY_EXISTS` 判斷是否已有實例在跑,不依賴視窗標題字串,也不受診斷模式影響(診斷模式與正常模式合計仍只允許一個實例 — 若使用者需要診斷模式與正常模式同時跑作對照,那是不同需求,目前沒有人提出,不在本票範圍)。
2. **第二實例的行為:傳遞訊息給第一實例後立即結束,不寫入 session、不建立視窗。** 第二實例偵測到 mutex 已存在時:
   - 找到第一實例的主視窗(用 `EnumWindows` 或 `FindWindow` 依 `kWindowClassName` 搜尋,兩種正常/診斷模式視窗類別相同,只有標題不同,所以用類別名稱搜尋、不比對標題,可以同時涵蓋兩種模式的第一實例視窗)。
   - 對該視窗呼叫還原(若最小化,`ShowWindow(hwnd, SW_RESTORE)`)並 `SetForegroundWindow(hwnd)` 喚醒到前景 — 專案內已有 `SetForegroundWindow(window)` 的既有呼叫慣例可參考(`main.cpp:2357`, `main.cpp:3498`)。
   - 第二實例本身直接 `return` 結束,**完全不觸碰 session 讀寫、不建立任何視窗**,避免第二實例的存在本身造成任何 side effect。
3. **race condition 的具體處理(使用者明確要求注意):**
   - **啟動時序 race:** `CreateMutexW` 本身是 kernel 層的 atomic 操作 — 「建立 mutex」與「回報是否已存在」是同一個系統呼叫內完成,不需要額外的鎖或重試邏輯,這是 Win32 官方建議的單一實例慣用法,可放心依賴其原子性。
   - **喚醒目標視窗尚未建立完成的 race:** 第一實例從 `CreateMutexW` 成功(mutex 尚未被佔用)到 `CreateWindowExW` 真正建立出 `kWindowClassName` 視窗、進入訊息迴圈之間有一段時間差。若第二實例恰好在這段空窗期啟動,`EnumWindows`/`FindWindow` 會找不到目標視窗。此時第二實例應該**重試搜尋一段有限時間(例如輪詢間隔 + 總逾時上限,由實作者依現有 codebase 慣例挑選合理數值並記錄在交接區)**,找不到就放棄並直接結束 — 不得無限等待、不得忙等(busy loop,違反 `AGENTS.md`「Event-driven idle path only. No busy loops」的精神,即使是啟動期間短暫的輪詢也要有上限與合理間隔,不能是 tight loop)。
   - **兩個實例同時啟動的 race(使用者雙擊兩下、或用腳本同時啟動兩個 process):** 兩者都呼叫 `CreateMutexW`,kernel 保證只有一個會拿到「新建」的結果,另一個會拿到 `ERROR_ALREADY_EXISTS`(即使兩者的呼叫時間點幾乎相同)。拿到 `ERROR_ALREADY_EXISTS` 的那個就是「第二實例」,依決策 2 處理即可,不需要額外協調機制。
   - **正常關閉時 mutex 釋放時機:** mutex handle 應隨 process 結束自動釋放(不需要顯式 `CloseHandle`,但若專案風格偏好顯式釋放資源,可在 `wWinMain` 結尾比照現有 `OleUninitialize()` 的收尾慣例一併釋放 — 由實作者決定,記錄在交接區)。不得在視窗關閉但 process 尚未結束的中間狀態提早釋放 mutex,否則會出現「本來是第一實例,視窗還沒真正結束卻讓第二實例誤判自己可以變成新的第一實例」的視窗。
4. **不處理的 race:** 使用者以系統管理權限或跨 session(例如遠端桌面多重 session、不同使用者帳號)分別啟動 PaneDock 的情境 — 具名 kernel mutex 預設是 per-session 命名空間,不同 Windows session 不會互相偵測到,这符合大多数桌面 App 的預期行為(每個登入 session 各自可以有一個實例),不需要用 `Global\` 前綴跨 session 共享,除非之後有人明確提出跨 session 也要限制單一實例的需求。

## 綁定限制(引用)

- `AGENTS.md`:「Event-driven idle path only. No busy loops, no polling timers.」— 喚醒既有視窗的重試搜尋若真的需要輪詢,間隔與逾時上限都要明確、有限,啟動階段短暫輪詢視為合理的一次性等待,不是常駐 polling timer,但仍不可寫成無節制的 tight loop。
- `AGENTS.md`:「Never persist a PIDL or a COM pointer.」與「All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace...」— 本票不涉及 session 檔案格式變更,第二實例完全不觸碰 session 讀寫,天然符合此限制,無需額外處理。
- `AGENTS.md`:「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」— 喚醒視窗時直接重用既有 `SetForegroundWindow` 呼叫慣例(`main.cpp:2357`, `3498`),不要另外設計一套視窗啟用機制。
- `AGENTS.md`:「No network, no telemetry, no third-party runtime, no services, no drivers, no admin elevation.」— `CreateMutexW`/`EnumWindows`/`SetForegroundWindow` 皆為標準 Win32 API,不違反此限制。

## 檔案與範圍

- `src/app_shell/main.cpp`:
  - `wWinMain`(第 3745 行起)最前面(在 `OleInitialize` 與任何 session/視窗建立動作之前)加入單一實例檢查。
  - 檢查失敗(已有實例)時:找到既有視窗、還原並前景化、直接 `return`(合理的 exit code,不需要是 0,只要與正常結束的行為一致,由實作者依現有 `exit_code` 變數慣例決定並記錄)。
  - `kWindowClassName`(第 46 行)可直接重用,不需要新增額外的類別名稱。

## Scope

1. 在 `wWinMain` 最前面加入具名 mutex 檢查(決策 1)。
2. 第二實例喚醒第一實例視窗並結束的邏輯(決策 2),含啟動期間視窗尚未就緒的有限重試(決策 3)。
3. 若有新增獨立的 helper 函式,放在 `main.cpp` 內既有函式群附近(例如靠近 `wWinMain` 或既有的 `diagnostic_requested` 命令列解析邏輯),不需要新增檔案 — 這是單一檔案、幾十行等級的變更。

## Non-goals

- 不做跨 Windows session 或跨使用者帳號的單一實例限制(決策 4)。
- 不新增任何 IPC 協定(例如透過 `WM_COPYDATA` 傳遞命令列參數給第一實例,如「用第二次啟動時的路徑在既有視窗開新分頁」)— 使用者需求只要求「喚醒既有視窗」,沒有要求傳遞啟動參數,若之後有這個需求要另開票。
- 不變更 `session.json` 的 schema 或讀寫邏輯。
- 不新增設定選項讓使用者關閉此限制 — 使用者需求是明確的「only one ap instance allowed」,沒有提到需要可設定。
- 不處理診斷模式與正常模式「刻意同時執行以便對照」的情境(決策 1 已說明兩者合計仍只算一個實例)。

## Acceptance Criteria

1. 正常啟動一個 `PaneDock.exe` 實例,行為與現在完全一致(讀 session、建視窗、正常運作)。
2. 在第一個實例仍在執行時,第二次啟動 `PaneDock.exe`:第二實例不建立任何視窗、不寫入 session,並使第一實例的視窗回到前景(若原本最小化則還原)。
3. 第二實例結束時不留下任何殘留 process(用工作管理員或 `tasklist` 可確認只剩一個 `PaneDock.exe` process)。
4. 兩個實例的可執行檔以相近時間點(例如批次腳本背靠背啟動)啟動時,最終仍只有一個實例真正運作,不會出現兩個視窗、也不會出現兩者都失敗結束的情況。
5. 第一實例正常關閉後,第三次啟動 `PaneDock.exe` 可以正常成為新的「第一實例」(mutex 已正確釋放,不會被上一輪的殘留狀態誤判為仍有實例在跑)。
6. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。
7. 不影響現有的診斷模式啟動流程(`--diagnostic` 參數解析、`SetProcessMitigationPolicy` 等既有邏輯不受影響)。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

以及手動驗證(單一操作 + 截圖等級,允許實作者自行完成):

- 啟動一個 `build\PaneDock.exe`,確認視窗正常出現。
- 在該視窗仍執行中時,再次啟動 `build\PaneDock.exe`(第二次呼叫),用工作管理員或 `tasklist //FI "IMAGENAME eq PaneDock.exe"` 確認全程只有一個 process 存活,並截圖確認第一個視窗被帶到前景。

> **驗證政策提醒:** 本專案的既定慣例是「單一點擊/單一操作 + 截圖」由 Agent(Codex)自行完成即可;任何需要連續多步驟滑鼠/鍵盤操作的驗證(例如需要反覆切換視窗焦點來確認搶焦點行為的細節)留給使用者在實機上自行驗證,不要嘗試用 computer-use 工具操作,那會搶走使用者當下正在使用的實體滑鼠鍵盤。完成後在交接區明確寫出哪些項目是自己截圖驗證過的、哪些留給使用者。

## Handoff 要求

依照 `docs/tickets.md` 既有票的 交接區 格式(繁體中文),記錄:

- 選用的 mutex 名稱字串,以及選擇理由(若與本票建議值不同)。
- 重試搜尋既有視窗的輪詢間隔與總逾時上限數值,以及選擇理由。
- mutex 釋放時機的實際實作方式(process 結束自動釋放 vs. 顯式 `CloseHandle`)。
- build/ctest 結果。
- 哪些 Acceptance Criteria 項目已用截圖/tasklist 自行驗證,哪些留給使用者用實機驗證。
- 是否有任何與其他票(例如 PD-070 的診斷模式錯誤對話框抑制邏輯)的交互作用需要留意。

## 交接區

（實作完成後由實作者填寫）
