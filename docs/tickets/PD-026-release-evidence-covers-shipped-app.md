# PD-026 — 讓 release evidence 量測目前的完整應用程式,而不是 Phase 0 原型

Phase 5 · diagnostics + tests/release · Depends on: PD-023

- Source: `AGENTS.md`、`docs/design-spec.md` §NFR-001 / §NFR-002 / §AC-003 / §AC-006、`docs/performance-baseline.md`、`docs/tickets/PD-003-idle-resource-baseline.md`、`docs/roadmap.md` Phase 5 第四、五條
- Origin: 2026-08-24,`docs/roadmap.md` Phase 5「`docs/performance-baseline.md` estimates replaced by measurements」與「`tests/release/release_evidence.ps1` producing a PASS」。PD-003 建立腳本時 repo 只有 Phase 0 原型,腳本與 baseline 表格裡有六筆「原型沒有這個東西所以量不到」的註記,在 Phase 1–4 完成後**已經不再成立**。
- Priority: HIGH——PD-027 要跑的就是這支腳本;腳本停留在原型假設,跑出來的 PASS 會是對舊架構的 PASS。

## Goal

把 `tests/release/release_evidence.ps1` 與 `docs/performance-baseline.md` 從「Phase 0 四分割原型」更新到「Phase 1–4 完成後的 PaneDock」,並補上一個最小的執行期診斷輸出,使「20 次版型切換後的 live view 數」這一列從「無法量測」變成可量測。

本 ticket **不執行** `-CollectMeasurements`(那需要真實互動桌面,是 PD-003 至今 `blocked` 的原因,也是 PD-027 的內容),只負責把量測工具本身修對。這是刻意的切分:工具的正確性可以在沒有桌面的情況下驗證,量測不行。

## 已確認的產品決策

1. **fail-closed 契約不動。** blocking 指標仍只有 NFR-001 的兩項(閒置 CPU、閒置磁碟 I/O);未量測 → INCOMPLETE ＋ exit 2;門檻失敗／建置失敗／測試失敗 → exit 1;skipped test 不算證據。本 ticket 不新增 blocking 門檻,也不放寬既有門檻。
2. **記憶體仍然沒有門檻,只有觀測值。** 沿用 PD-003 決策 2:四分割下主導項是 Shell view 與第三方 extension,設絕對門檻會變成量測使用者裝了什麼。
3. **live view 數以應用程式自己輸出的一行文字提供,不用外部探測。** `src/explorer_host/live_view_count.h` 已經維護著這個數字;缺的只是把它送得出來。輸出格式固定為一行 `panedock.live_view_count=<n>\n`,寫到 `STDOUT`,時機為:每次版型套用完成之後、以及關閉序列 destroy 完所有 view 之後。`STDOUT` 無效(一般桌面啟動時本來就無效)時靜默略過。腳本已經在 `Start-Process -RedirectStandardOutput` 下啟動應用程式並收集 stdout,不需要新機制。
4. **這不是遙測、不是 log 檔。** 它只寫到父程序給的 stdout handle,不落地、不聯網、不常駐、不定時。`AGENTS.md` 的 no network / no telemetry 與 idle 零磁碟 I/O 都不受影響——若實作方式會產生檔案或計時器,就是做錯了。
5. **延遲類指標(Group 切換、tab realize、cold start)本輪維持「Not measured」,但理由要改寫。** 目前的理由是「原型沒有 Group／tab」,那句話現在是錯的;新的理由是「量測它需要在產品內加入計時儀器,超出本 ticket 範圍,且三者都沒有 blocking 門檻」。**寫下錯誤的理由比寫下「未量測」更糟**,因為它會讓後來的人以為缺口在別處。
6. **AC-005(還原狀態含無法連線網路路徑時 UI 不凍結)以操作者確認的是／否記錄,不試圖自動量化。** 它的判準本來就是「UI 有沒有凍結」,那是人的觀察。腳本問一次、把答案寫進 evidence 的 non-blocking 區塊即可。**合理的預設值,不是規格明文要求;若使用者之後想要可量化的判準(例如訊息迴圈最大停頓時間)再開 ticket 加儀器。**
7. **不把 `release_evidence.ps1` 註冊成 ctest。** 沿用 PD-003 non-goal:它是發佈前手動閘門,不是單元測試。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §NFR-001:
> 使用者未互動時:CPU 佔用不可量測(取樣期間平均 &lt; 0.1%),磁碟 I/O 為零。此為封閉式發佈門檻——未量測即視為不通過。

`docs/design-spec.md` §NFR-002:
> 記憶體由架構決定,不由語言決定。恰有一個條件保證上界:**只有可見 pane 的 active tab 持有 live `IExplorerBrowser`**。其餘 tab 僅以資料存在。

`docs/design-spec.md` §AC-003:
> 在二分割與四分割版型之間反覆切換,不持續累積 view,不破壞焦點,handle 數不單調成長。

`docs/performance-baseline.md`:
> An unmeasured blocking metric produces **INCOMPLETE** and exit code 2. Absence of evidence is never treated as absence of a problem.

> Any future optimization proposal must show a measured number from this table before it is written as a ticket. Reasoning from deployment size, from runtime baselines, or from another project's figures is not evidence here.

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

> No network, no telemetry, no third-party runtime, no services, no drivers, no admin elevation.

> Code, identifiers, test names and diagnostic event names are English.

> Anything a later session needs must live in the repository, not in a scratchpad handoff. ... measured numbers go in `docs/performance-baseline.md` or the ticket's 交接區.

`docs/tickets.md` §已否決的方向:
> 端到端 UI 自動化(WinAppDriver／UIAutomation)— 對 live Shell view 極易 flaky。要重開需先示範在兩台機器上連續 20 次穩定通過。

## Files to read and trace first

- `tests/release/release_evidence.ps1` 全文——特別是 `-CollectMeasurements` 區塊(約 131–176 行)的 `Read-Host` 提示文字、`$memory` 三種組態、21 次 handle 取樣迴圈,以及 non-blocking context 表格(約 253–277 行)那幾行寫死的「Prototype has no Group／tab」字串。
- `docs/performance-baseline.md` 的 13 列表格——每一列的 Result 與 notes,尤其是提到「the Phase 0 prototype exposes only two- and four-pane layouts」「the prototype has no runtime diagnostic surface」「Prototype has no Group」「Prototype has no tab」的那幾列。
- `src/explorer_host/live_view_count.h` — 既有的計數機制與它的公開函式簽章。**直接重用,不要另建計數器。**
- `src/app_shell/main.cpp` 的 `apply_layout()` 呼叫點與 `wWinMain` 尾端的 `destroy_explorers(state); assert(live_view_count() == 0);` — 決策 3 兩個輸出點就在這兩處附近。
- `src/app_shell/main.cpp` 的 `kLayoutToggleHotkeyId` / `toggle_layout()`(約 1285 行)— 腳本要求操作者按 `Ctrl+Shift+L` 20 次走的就是這條路徑,確認它在 Phase 2 之後仍然存在且仍是同一個快速鍵;**若已改變,腳本的提示文字必須跟著改**,否則操作者會按一個不存在的鍵然後回報「量到了」。
- `docs/tickets/PD-003-idle-resource-baseline.md` 的 交接區——量測方法(`Process.TotalProcessorTime` delta、`GetProcessIoCounters`、`CheckRemoteDebuggerPresent`)與它為何 `blocked`。本 ticket **不解除** PD-003 的 blocked 狀態。
- `docs/tickets/PD-011-prototype-acceptance-and-go-no-go.md` 的 交接區——目前 baseline 表格內兩筆真實讀數(54.3 MB / HandleCount 553)的來源與條件。**它們是 Phase 0 原型的讀數,不是目前 build 的**,重寫 notes 時不要把它們洗成現況數字。

## Scope

1. **應用程式端(最小改動)**:依決策 3 加入 `panedock.live_view_count=<n>` 的 stdout 輸出,兩個時機(版型套用完成後、關閉 destroy 完成後)。以 `GetStdHandle(STD_OUTPUT_HANDLE)` ＋ `WriteFile`(或 `WriteConsoleW` 之外的等價做法)實作,handle 無效或寫入失敗一律靜默略過,不影響任何既有行為。
2. **腳本:記憶體組態**。把「單一 pane 本機資料夾」加回互動流程(Single 版型現在存在,PD-016 已交付五種版型),使 `docs/performance-baseline.md` 的「Resident memory, 1 pane, local folder」列可以被量到。提示文字要明確寫出操作者該做什麼(切到 Single 版型、導覽到一個本機資料夾、等它安定)。
3. **腳本:live view 數**。應用程式關閉後解析收集到的 stdout,取出全部 `panedock.live_view_count=` 讀數,在 non-blocking context 表格報出:切換前的值、20 次切換後的值、以及關閉後的最終值(應為 0)。取不到讀數時照舊報 `Not measured` 並寫出原因——**不得以「應該是 0」填數字**。
4. **腳本:AC-005 responsiveness**。在互動流程尾端加一次 `Read-Host`,請操作者在還原狀態含一個無法連線的網路路徑時確認 UI 是否全程有反應,把答案(含操作者輸入的原文)寫進 non-blocking context(決策 6)。
5. **腳本:清掉原型時代的字串**。non-blocking context 裡寫死的 `Prototype has only two- and four-pane layouts`、`No runtime diagnostic surface exists in the prototype`、`Prototype has no Group`、`Prototype has no tab` 四句,改成與現況相符的敘述(決策 5)。
6. **`docs/performance-baseline.md`**:同步更新受影響列的 notes,標明由 PD-026 改寫;仍為 `Not measured` 的列寫出**現在真正的**原因。既有的兩筆 PD-011 讀數保留原樣並保留其「Phase 0 原型」標註。
7. **一個 runnable self-check**:腳本的解析邏輯(從 stdout 文字取出 live view 讀數)是本 ticket 唯一的非平凡新邏輯。把它抽成腳本內一個可獨立呼叫的函式(例如 `Get-LiveViewCounts([string[]]$Lines)`),並在 `tests/release/` 內加一支極小的 PowerShell self-check,用寫死的假 stdout 文字驗證:正常多筆、零筆、含雜訊行、數字非法。**不需要框架**,`if (...) { throw }` ＋ 非零 exit code 即可。它不需註冊為 ctest(決策 7),但要能單獨執行並在 Agent checks 裡跑一次。

## Non-goals

- **不執行 `-CollectMeasurements`**——需要真實互動桌面,是 PD-027 與使用者的工作。本 ticket 的 agent 若沒有互動桌面,把工具修好、跑不含 measurement 的路徑確認仍是 INCOMPLETE ＋ exit 2,就是完成。
- 不新增或放寬 blocking 門檻(決策 1),不為記憶體設門檻(決策 2)。
- 不在產品內加計時儀器量 Group 切換／tab realize／cold start 延遲(決策 5)。
- 不加 log 檔、不加遙測、不加計時器、不加背景執行緒(決策 4)。
- 不做 UI 自動化、不合成鍵盤滑鼠輸入(`docs/tickets.md` §已否決的方向)。腳本繼續以 `Read-Host` 等待人類動作。
- 不把腳本註冊為 ctest(決策 7)。
- 不解除 PD-003 的 `blocked` 狀態、不修改 PD-003 的文件(`AGENTS.md`:完成的 ticket 文件不得編輯)。
- 不動 `docs/roadmap.md`。

## Acceptance

1. `.\build\PaneDock.exe` 在 stdout 被重導向時,啟動與關閉各至少輸出一行 `panedock.live_view_count=<n>`,關閉後的最後一行為 `panedock.live_view_count=0`;在一般桌面直接雙擊啟動(無 stdout)時行為與本 ticket 之前完全相同,不出現任何主控台視窗。
2. `.\tests\release\release_evidence.ps1`(不含 `-CollectMeasurements`)仍輸出 `INCOMPLETE`、exit code `2`,並產生 `docs/release-evidence.md`。fail-closed 契約未被削弱。
3. `docs/release-evidence.md` 的 non-blocking context 區塊不再出現任何提及 `prototype` 的字串。
4. 腳本的互動流程含:單一 pane 記憶體組態、四 pane 本機、四 pane 混合資源、純文字 vs 縮圖資料夾、21 次 handle 取樣、AC-005 responsiveness 確認、三次 soak。每一項的提示文字都寫清楚操作者要做什麼。
5. live view 解析的 self-check 可單獨執行並通過;餵給它非法輸入時它失敗(不是回傳 0 當作成功)。
6. `docs/performance-baseline.md` 內不再有任何一列以「原型沒有這個功能」作為 `Not measured` 的理由;每一列仍是 `Not measured` 的都有一個現在成立的理由。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過(既有測試數量不減)。
8. `rg -n "windows\.h|HWND|IUnknown" src/core` 無輸出;`git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# live view 輸出的端到端檢查(不需要人類操作)
$out = "$env:TEMP\panedock-viewcount.txt"
$p = Start-Process .\build\PaneDock.exe -PassThru -RedirectStandardOutput $out -RedirectStandardError "$env:TEMP\panedock-viewcount.err.txt"
Start-Sleep -Seconds 5
$p.CloseMainWindow() | Out-Null
$p.WaitForExit(15000) | Out-Null
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Get-Content $out
# 預期:至少兩行 panedock.live_view_count=<n>;正常關閉時最後一行為 =0
```

```powershell
.\tests\release\release_evidence.ps1
"exit code: $LASTEXITCODE"      # 預期:2
rg -n -i "prototype" docs\release-evidence.md   # 預期:non-blocking context 區塊無命中
rg -n "Not measured" docs\performance-baseline.md
```

```powershell
# 解析邏輯的 self-check(檔名依實作而定)
.\tests\release\live_view_count_parse_check.ps1
"exit code: $LASTEXITCODE"      # 預期:0
rg -n "windows\.h|HWND|IUnknown" src/core
rg -n "SetTimer|CreateThread|_beginthread" src   # 預期:無命中
git diff --check
git status
```

## Handoff requirements

- `panedock.live_view_count` 的兩個輸出點在程式碼中的確切位置,以及 stdout handle 無效時的實測行為(雙擊啟動不得跳出主控台視窗——這一條要實際確認,不能只讀程式碼)。
- 腳本改動前後的 `docs/release-evidence.md` 差異摘要,以及不含 measurement 的實際 exit code。
- `docs/performance-baseline.md` 每一列改寫後的理由,逐列列出。
- 確認 `Ctrl+Shift+L` 在目前 build 仍是版型切換的快速鍵(或寫出實際的鍵),以及腳本提示文字是否已對齊。
- self-check 的檔名、涵蓋的 case、以及失敗時的 exit code。
- 明確聲明本輪**沒有**執行 `-CollectMeasurements`、沒有產生任何量測數字、沒有改 PD-003 的狀態。若擅自填入數字,PD-027 的 PASS 就是假的。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-24 實作交接

#### 完成內容與確切輸出位置

- `src/app_shell/main.cpp` 新增 file-local `write_live_view_count() noexcept`：以 `GetStdHandle(STD_OUTPUT_HANDLE)` 取得 stdout，使用 `std::to_chars` 組出固定格式 `panedock.live_view_count=<n>\n`，再以 `WriteFile` 寫出；`nullptr`／`INVALID_HANDLE_VALUE`／格式化失敗／`WriteFile` 失敗都靜默略過，不建立檔案、不開 timer、不開 thread。
- 第一個輸出點在 `apply_layout(HWND, AppState&)`：無 active Group 的成功返回前，以及所有 pane layout/realize 完成後的正常 `S_OK` 返回前。這涵蓋每次版型套用成功路徑，包括 `Ctrl+Shift+L` 經 `toggle_layout()` 呼叫的同一個 `apply_layout`。
- 第二個輸出點在 `destroy_explorers(AppState&)`：所有四個 `ExplorerHost::destroy()` 完成、`state.realized.fill(false)` 之後。WM_CLOSE 與 wWinMain 尾端沿用既有 shutdown 路徑，因此正常 close 會看到 destroy 後的 0（可能因兩條既有 cleanup 呼叫而有重複的 0 行）。
- `kLayoutToggleHotkeyId = 1` 仍由 `RegisterHotKey(window, ..., MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'L')` 註冊，`WM_HOTKEY` 仍呼叫 `toggle_layout()`；release script 的 20 次提示維持 `Ctrl+Shift+L`，與目前 build 對齊。

#### Script 與 baseline

- `tests/release/release_evidence.ps1` 新增 `Get-LiveViewCounts([string[]]$Lines)`；它只解析嚴格的 `panedock.live_view_count=<digits>` 行，無有效行回傳空集合，帶有相同 prefix 但非法數字則 throw，避免把非法輸入當成 0。量測 run 正常關閉後解析 stdout，non-blocking context 報告 `before`、20 次切換後的最後一個 switch sample、以及 `closed` 最終 sample；無讀數或 parse error 報 `Not measured` 並附原因。
- `Complete-PaneDock` 現在回傳收集到的 stdout/stderr 行；只有 `-CollectMeasurements` 的 measurement process 會解析 live counts。未帶旗標時不啟動 measurement／soak process，也不會產生任何量測數字。
- 互動流程新增 Single layout 記憶體組態：提示操作者切到 Single、導覽一個本機資料夾並等待穩定；接著提示 Four Panes local、mixed thumbnails/OneDrive/network、text-only、thumbnail folders。20 次 handle 取樣仍要求人類每次按 `Ctrl+Shift+L`，並新增一次 AC-005 `Read-Host`，將操作者原文寫進 non-blocking context。三次 soak 路徑保持原樣。
- `docs/performance-baseline.md` 改寫每個原先依賴 prototype 的理由：Single layout 現在存在但需互動 desktop 量測；live-view count 已有 stdout surface 但 20-switch 執行仍待 `-CollectMeasurements`；Group/tab/cold-start latency 需產品 timing instrumentation、超出本票且無 blocking threshold；AC-005 改為 operator responsiveness answer、非自動 timing。Idle CPU/disk、mixed memory、handles、thumbnail 等列仍保留現今成立的互動桌面／資源理由；PD-011 的 54.3 MB Phase 0 原始讀數原樣保留。
- 為了讓 parser 可單獨載入，release script 在 dot-source (`.`) 時只定義函式、不執行主流程；一般直接執行仍完整跑原本 fail-closed workflow。新增 `tests/release/live_view_count_parse_check.ps1`，不註冊 CTest。

#### Acceptance 與驗證結果

- Acceptance 1（stdout 啟動與正常關閉各有 live-count，最後為 0；雙擊無 console）：**部分可驗證／正常關閉與雙擊行為未驗證，需真實桌面**。重導 stdout 的 headless run 收到 `panedock.live_view_count=4` 兩行；`CloseMainWindow()` 回傳 `True` 但 15 秒未退出，因本環境的 PowerShell `MainWindowHandle` 指向 `UAC_InputIndicatorOverlayWnd` 而非 PaneDock 主 HWND，最後只終止明確 PID，因此沒有取得可宣稱的 destroy 後 `=0`。未在有互動桌面的雙擊啟動上確認「不出現 console」。
- Acceptance 2（不含 `-CollectMeasurements` 仍 INCOMPLETE／exit 2／產生 evidence）：**PASS**。實際輸出 `INCOMPLETE`、`Evidence: E:\GitHub\PaneDock\docs\release-evidence.md`、exit code `2`；文件已生成且 blocking CPU/disk 都是 `Not measured`／`INCOMPLETE`。
- Acceptance 3（evidence non-blocking context 無 prototype 字串）：**PASS**。`rg -n -i "prototype" docs/release-evidence.md` 無命中；Performance baseline 與 script 受影響行也已清除 prototype 時代理由。
- Acceptance 4（Single、四 pane 組態、20 handle samples、AC-005、三次 soak 的清楚提示）：**PASS（靜態檢查）**。所有提示存在；實際互動步驟未執行，因明確禁止 `-CollectMeasurements`。
- Acceptance 5（parser self-check 及非法輸入非零失敗）：**PASS**。`tests/release/live_view_count_parse_check.ps1` 輸出 `PASSED: live_view_count_parse_check`、exit 0；案例包含多筆正常、零筆、雜訊、非法數字。另以未捕捉的非法輸入直接呼叫 parser，PowerShell exit code 為 `1`。
- Acceptance 6（baseline 無 prototype 缺口理由）：**PASS**。`rg -n -i "prototype" docs/performance-baseline.md docs/release-evidence.md tests/release/release_evidence.ps1` 無輸出；所有 `Not measured` 行都有現況成立的互動／instrumentation／無門檻理由。
- Acceptance 7（build／CTest）：**PASS**。LLVM-MinGW configure/build 成功；CTest 4/4 通過。`live_view_count_parse_check.ps1` 是依決策 7 單獨執行的 self-check，沒有註冊為 CTest。
- Acceptance 8（core boundary／diff check）：**PASS**。`rg -n "windows\.h|HWND|IUnknown" src/core` 無輸出；`git diff --check` 通過。

#### 明確限制與未做事項

- 本輪**沒有執行 `tests/release/release_evidence.ps1 -CollectMeasurements`**，沒有產生或填入任何 idle CPU、disk I/O、memory、handle、live-view 20-switch、thumbnail 或延遲量測數字；`docs/performance-baseline.md` 的既有 54.3 MB 是 PD-011 Phase 0 原始讀數，未被改寫為現況。
- 沒有修改 PD-003 文件或其 blocked 狀態，沒有修改 `docs/tickets.md`／`docs/roadmap.md`，未加入 ctest registration，未 commit。工作開始前既有未追蹤 `.claude/` 未觸碰。

### 2026-08-24 最終檢查補記

- 發現 generated evidence 的 Windows CRLF 會讓 `git diff --check` 將每行 CR 視為 trailing whitespace，已把腳本寫檔改為明確 UTF-8/LF；重新執行不含 `-CollectMeasurements` 的 `release_evidence.ps1` 後仍輸出 `INCOMPLETE`、exit code `2`，再跑 `git diff --check` 通過。
- 最終 parser self-check 再跑一次：`PASSED: live_view_count_parse_check`、exit 0。`rg` 結果：baseline/evidence/script 無 `prototype` 命中；`src/core` 無 `windows.h`／`HWND`／`IUnknown`；`src` 無 `SetTimer`／`CreateThread`／`_beginthread`；均符合預期的無輸出 boundary check。
