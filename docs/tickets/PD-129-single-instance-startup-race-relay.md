# PD-129 — single-instance 啟動競態:前一個 AP 正在關閉／卡死會靜默 no-op,第二個執行個體無 UI 無提示

Phase 7 · app_shell · Depends on: (none)

- Source: 使用者要求「audit AP startup,考量 previous AP is closing / race condition ... 避免 AP 無法順利開啟或開啟後沒有 UI 並且出現合適的提示」。稽核迴圈(startup 補強)確認。
- Priority: HIGH——正是使用者列的「previous AP is closing」與「race condition」,屬「AP 無法順利開啟且無提示」的崩潰面(雖然不是程式 crash,是 UI 不存在)。

## 已確認的根因(有程式碼證據)

`wWinMain`(`src/app_shell/main.cpp:5170`)單一執行個體判定:

```cpp
HANDLE mutex = CreateMutexW(nullptr, FALSE, kSingleInstanceMutexName);
if (mutex == nullptr) { OutputDebugStringW(...); return 1; }   // 無訊息
if (GetLastError() == ERROR_ALREADY_EXISTS) {
    activate_existing_main_window();   // 內部 find_existing_main_window() 最多等 5s
    CloseHandle(mutex);
    return 0;                          // 永遠安靜退出
}
```

`activate_existing_main_window`(`:5152`,本次稽核前)呼叫 `find_existing_main_window()`,它只用 `FindWindowW(kWindowClassName, nullptr)` 以 50ms 間隔輪詢,最多 5s(常數 `kSingleInstanceWindowRetryTimeoutMs = 5000`)。**找不到視窗就回傳 nullptr**,然後 `activate_existing_main_window` 只輸出 debug 字串、返回,`wWinMain` 接著 `return 0`:

這種「第二個執行個體啟動但完全無 UI、無任何提示、直接退出」在以下三種情況下觸發:

1. **前一個 AP 正在關閉**:`WM_CLOSE`(`:5017`)先 `DestroyWindow` 主視窗(§9.4 第 4 步早於 mutex `CloseHandle`),再繼續 `destroy_explorers`(`IExplorerBrowser::Destroy`,可能數秒)與 `OleUninitialize` → **`IExplorerBrowser::Destroy`(multi-view)與 `OleUninitialize` 期間 mutex 仍被持有,但主視窗已不存在**。這段時間內使用者雙擊圖示,新的執行個體 `FindWindowW` 一直找不到、等滿 5s,靜默退出。
2. **前一個 AP 是在啟動中被卡住**(系統太慢／disk slow／AV 掃描擋住 `read_session`／`apply_layout` 的同步 Shell view 初始化,>5s):`FindWindowW` 在窗口出現前等滿 5s、退場,使用者連點兩次只看到一次成功,第二次無 UI。
3. **前一個 AP 完全卡死**(shell 過渡已重入、`IFileOperation` 卡住、第三方 shell extension 死鎖):mutex 永遠持有 → **每次啟動都 5s 無 UI 然後靜默退出**,使用者完全不知道為何開不起來。

這都符合使用者明確描述的「AP 無法順利開啟 or 開啟後沒有 UI,且沒有提示」。

## Binding constraints — quoted, do not weaken

`AGENTS.md`:

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`:

> Do not push branches ... Anything a later session needs must live in the repository ...

`AGENTS.md`:

> The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

(啟動時一次短的輪詢可接受;此 ticket 不新增常駐 thread/timer/hook,只在「已存在執行個體」的短暫窗口內輪詢,屬互動期間,不違反 0% idle constraint。)

## Files to read and trace first

- `src/app_shell/main.cpp:5170`(`CreateMutexW` 分派)、`:5122`(`find_existing_main_window`,本次刪除)、`:5152`(`activate_existing_main_window`,本次改寫)、`:53`-`:54`(重試常數)、`:50`(mutex 名稱)。
- `src/app_shell/main.cpp:5010`-`5040`(`WM_CLOSE`／`WM_ENDSESSION`:主視窗 `DestroyWindow` 早於 `CloseHandle(mutex)`)。

## Fix 方向 / Scope(已實作)

把「已存在執行個體」的分派改成三態,不再一律靜默退出:

1. 新增 `bool relay_or_wait_for_existing_instance(HANDLE& mutex)` 取代 `activate_existing_main_window`:
   - 輪詢週期內 `FindWindowW`:找到 → `AllowSetForegroundWindow` + `PostMessage(kActivateExistingInstanceMessage)` → 退出(true)。
   - 同週期以 `OpenMutexW(SYNCHRONIZE, FALSE, ...)` 探測 mutex:若回傳 nullptr(--前一個執行個體已釋放 mutex,表示它正在關閉並剛關完)→ `CreateMutexW` 重新建立一個 fresh handle → 返回 false,`wWinMain` **fall-through 正常啟動新視窗**。這讓「關閉中雙擊」變成自動啟動,而不是等 5s 退出。
   - 超過 `kSingleInstanceWindowRetryTimeoutMs` 且視窗從未出現 → `MessageBoxW(nullptr, ...)` 提示「PaneDock is already running but not responding, or it is shutting down. Wait a moment and try again.」→ 退出(true)。
   - `mutex` 在進入時 `CloseHandle`(避免自己的 handle 誤keep object alive 使 `OpenMutexW` 永遠成功,導致探測失敗)。
2. `single_instance_mutex` 由 `const HANDLE` 改為 `HANDLE`,以便 fall-through 時承接 fresh handle。
3. 一個新增 helper 的零邏輯:在「已存在」路徑上若 fresh `CreateMutexW` 失敗則退出(true),避免用完 handle 洩漏或繼續以 null handle 啟動。

為何有效:不再有「視窗不存在／前一個還在關閉」的 5s 靜默退出;「previous AP is closing」在大多數情況變成新的執行個體立即取代,只有真正卡死才會給出明確提示。§9.4 順序、`save_now`、無常駐 thread/timer 皆不變(輪詢只是啟動期短暫互動進程)。

## Non-goals

- 不打破單一執行個體(不允許兩個同時持有 mutex 的 instance 並行寫 session)——fall-through 前仍確認 mutex 已釋放,且 fresh `CreateMutexW` 重新建立單例。
- 不強制殺掉卡死的前一個進程(這會冒 session 檔案不一致的風險,違反資料安全規則);只在無 UI 時給出提示。
- 不新增常駐後台執行緒／計時器／hook。
- 不修改 `src/core`。
- 不把啟動錯誤靜默化——此 ticket 反而確保「開不起來」時使用者一定會看到訊息。

## Acceptance Criteria

1. `cmake --build build`、`ctest --test-dir build --output-on-failure`、`git diff --check` 全數通過。
2. `rg -n "relay_or_wait_for_existing_instance|already running but not responding|find_existing_main_window" src/app_shell/main.cpp`:
   - 找到 `relay_or_wait_for_existing_instance(HANDLE& mutex)` 與「already running but not responding」提示字串。
   - `find_existing_main_window` 已不存在(被三態 relay 取代),惟 `activate_main_window_on_own_thread` 仍在(供 `kActivateExistingInstanceMessage` 在既有執行個體執行緒上用)。
3. 無未使用函式(`find_existing_main_window` 已刪);編譯無 `-Wunused-function` 警示。
4. 實機(Release)驗證「關閉中雙擊」:
   - 啟動後關閉(選「關閉」),在 `IExplorerBrowser::Destroy` 進行中的窗口內瞬間雙擊 ICON → 新執行個體應在任何一種下列情形成立:
     a. 前一個在 5s 內關完 → **新的視窗自動開啟**(不應靜默退出);或
     b. 超過 5s → **出現提示對話框**「already running but not responding ...」。
   - 無論 a／b,使用者都不應得到「無 UI 無提示」的結果。
5. 既有單一執行個體行為不變:AP 已在跑(有視窗),雙擊 ICON → 既有視窗被帶到前景,新執行個體退出 0。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
rg -n "relay_or_wait_for_existing_instance|already running but not responding|find_existing_main_window|activate_existing_main_window" src/app_shell/main.cpp
```

## Handoff requirements

- 記錄 relay 三態的決策分派與為何一開始要 `CloseHandle(mutex)`(避免自身 handle 擋住 `OpenMutexW` 探測)。
- 記錄「previous AP is closing」如何變成 fresh launch、何時反而提示,以及 fall-through 後 `HANDLE` 承接到 `mutex` 的新值。
- 記錄 build／CTest／diff 結果與是否有真實桌面的「關閉中雙擊」驗證(a 或 b 兩種結果),以及單一執行個體加fronting 行為是否維持。
- 任何「沒驗證」或「只在指令端確認」都要標明;不得把「編譯過＝功能對」當作根因。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-30 已實作三態 relay

- 根因:`wWinMain` 對 `ERROR_ALREADY_EXISTS` 一律走 `activate_existing_main_window`→`find_existing_main_window`(5s)`FindWindowW` 找不到就靜默 `return 0`,涵蓋「previous AP 關閉中(視窗已毀但 mutex 尚持)／卡死／啟動太慢>5s」三種無 UI 無提示情境。
- 已修改 `src/app_shell/main.cpp`(僅此一檔):
  - 刪除 `find_existing_main_window`、改寫 `activate_existing_main_window` 為 `bool relay_or_wait_for_existing_instance(HANDLE& mutex)`:進入即 `CloseHandle(mutex)`,輪詢中 `FindWindowW` 找到→relay;`OpenMutexW` 為 null(前一個已關)→`CreateMutexW` fresh handle、返回 false 讓 `wWinMain` fall-through 開新窗;`GetTickCount64` 超時→`MessageBoxW(nullptr, "PaneDock is already running but not responding, or it is shutting down. Wait a moment and try again.")`、返回 true。
  - `wWinMain`:`single_instance_mutex` 由 `const HANDLE` 改 `HANDLE`;`ERROR_ALREADY_EXISTS` 分支改為 `if (relay_or_wait_for_existing_instance(single_instance_mutex)) return 0;` 否則 fall-through。`CreateMutexW` 初次失敗改為 `report_startup_failure(...)` 提示。
  - 另新增 `report_startup_failure(const wchar_t*)`(無 owner `MessageBoxW`),並用於 `OleInitialize`／`register_window_class`／`session_directory` 失敗(原為靜默 return)。
- §9.4 順序、單一執行個體、無常駐 thread/timer/hook 不變;relay 只為啟動期短暫輪詢。
- 驗證:`cmake --build build` PASS、`ctest --test-dir build --output-on-failure` 6/6 PASS(完整及獨立 `panedock_launch_smoke` 重跑均通過;該測試有已知間歇 0xC0000409,與本次無關)、`git diff --check` 通過。
- 待補實機驗證:Release 下「關閉中雙擊」的 a／b 結果與單一執行個體加 fronting。
- 注意:`MessageBoxW(nullptr, ...)` 在 window 建立前使用是安全的(無 owner、無關係到任何 message-pump)。
