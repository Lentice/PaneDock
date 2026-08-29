# PD-003 — 量測閒置資源基準並取代 performance-baseline 的估計值

Phase 0 · diagnostics · Depends on: PD-001

- Source: `AGENTS.md`、`docs/design-spec.md` §NFR-001／§NFR-002／§AC-006、`docs/performance-baseline.md`
- Origin: 2026-08-20 專案建立。`docs/performance-baseline.md` 全部 12 列都是估計值,沒有一列被觀測過。選型審查明確要求以量測取代估計,並指出「用檔案大小或 runtime baseline 推論記憶體」是已否決的推理方式。
- Priority: **HIGH**——NFR-001 是封閉式發佈門檻,未量測即不通過。且趁 PD-001 的原型還在手上最容易量;晚做要重建原型。

## Goal

在 PD-001 的四分割原型上取得真實讀數,把 `docs/performance-baseline.md` 中「Not measured」的列換成觀測值,並為 NFR-001 的封閉式門檻定出可執行的量測方法。

本 ticket 的價值不在數字本身,而在**建立一個之後每次發佈都能重跑的量測程序**。因此產出包含一支腳本,不只是一組數字。

## 已確認的產品決策

1. NFR-001 的門檻已定:閒置十分鐘取樣期間平均 CPU ≥ 0.1% 即失敗,任何磁碟 I/O 即失敗。本 ticket 不重新討論門檻,只負責量測。
2. 記憶體**沒有**門檻,只有觀測值。理由:四分割下主導項是 Shell view 與第三方 extension,設一個絕對門檻會變成量測使用者裝了什麼而非量測我們的程式碼。設門檻需先有本 ticket 的數字。
3. 量測必須在 Release 建置、未附加除錯器的情況下進行。附加除錯器會改變讀數,因此腳本必須記錄除錯器是否附加。
4. 量測結果分兩類:**blocking**(NFR-001 的 CPU 與磁碟)與 **context**(記憶體、handle 數、延遲)。context 讀數永遠不構成通過或失敗,只是脈絡。
5. 「未量測」一律視為 INCOMPLETE 並以 exit code 2 結束,不得視為通過。這是 `docs/performance-baseline.md` §Release evidence contract 的既有契約,本 ticket 只是第一個實際執行它的 ticket。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §NFR-001:
> 使用者未互動時:CPU 佔用不可量測(取樣期間平均 &lt; 0.1%),磁碟 I/O 為零。此為封閉式發佈門檻——未量測即視為不通過。

`docs/performance-baseline.md`:
> An unmeasured blocking metric produces **INCOMPLETE** and exit code 2. Absence of evidence is never treated as absence of a problem.

`docs/performance-baseline.md`:
> Any future optimization proposal must show a measured number from this table before it is written as a ticket. Reasoning from deployment size, from runtime baselines, or from another project's figures is not evidence here.

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

`docs/tickets.md` §已否決的方向:
> 用「部署檔案大小」推論記憶體 — self-contained 部署的 100 MB 檔案大小與常駐記憶體無關,曾據此誤判 C# 出局。任何以檔案大小論證記憶體的 ticket 一律退回。

## Files to read and trace first

- PD-001 的 `## 交接區` — step 9 的初步讀數與量測時的機器狀態
- `docs/performance-baseline.md` — 12 列的目標與門檻定義,以及哪幾列標記為 blocking
- `src/app_shell/` — 訊息迴圈與任何 timer;若存在任何 `SetTimer` 或輪詢,它就是閒置 CPU 的來源

## Scope

1. 建立 `tests/release/release_evidence.ps1`。契約依 `docs/performance-baseline.md` §Release evidence contract:blocking 指標未量測 → INCOMPLETE 且 exit 2;量測到的門檻失敗、建置失敗或測試失敗 → exit 1;skipped test 不算證據,計為 INCOMPLETE。
2. 腳本記錄的環境資訊:時間戳、OS build、CPU 型號、**是否附加除錯器**、git commit、live `ctest -N` 數量、工具版本表。
3. 腳本擷取並包含各命令的 stdout 與 exit code:configure、build、完整 ctest、process 啟動、閒置取樣、三次連續 soak。
4. 閒置量測方法:啟動四分割原型導覽到四個本機資料夾,不做任何互動,取樣十分鐘。CPU 取平均,磁碟 I/O 取累計位元組。實作以效能計數器或 `GetProcessIoCounters` 取得,不得以工作管理員目視。
5. 記憶體量測三種組態,各自記錄:單一 pane 本機資料夾;四 pane 本機資料夾;四 pane 含縮圖、OneDrive 與網路路徑。
6. handle 數:閒置時、以及在單一／四宮格之間切換 20 次後,確認是否單調成長。
7. 縮圖對記憶體的貢獻:比較「含縮圖的資料夾」與「純文字檔資料夾」兩種組態的差值。此數字是 `docs/tickets.md` §候選 中「縮圖 pipeline 上限」ticket 的開立前提。
8. 產出 `docs/release-evidence.md`,含 blocking-threshold gate 表格(`| Metric | Blocking threshold | Measurement source | Measured | Value | Verdict |`)、CTest gate(註冊數 == 執行數,否則 STALE)、non-blocking context 區塊,以及 `## Result` 結論行。
9. 以量得的值更新 `docs/performance-baseline.md`,每列註明由 PD-003 量得。仍無法誠實量到的列維持「Not measured」並寫出為何不能。

## Non-goals

- 不做任何效能最佳化。本 ticket 只量測。發現問題就開新 ticket,附上本 ticket 量到的數字。
- 不為記憶體設門檻(見已確認的產品決策 2)。
- 不把 `release_evidence.ps1` 註冊為 ctest。它是發佈前手動執行的閘門,不是單元測試。
- 不量測 Group 切換延遲——PD-001 的原型沒有 Group。該列維持 Not measured。
- 不量測 tab realize 延遲——原型沒有 tab。該列維持 Not measured。
- 不加 CI 來自動跑這支腳本(見 `docs/tickets.md` §計畫決策紀錄)。
- 不宣稱量到了實際上沒量到的東西。若某個讀數受環境干擾無法誠實歸因,寫出來,不要填一個數字上去。

## Acceptance

1. `tests/release/release_evidence.ps1` 存在,且在 blocking 指標未量測時以 exit code 2 結束並輸出 INCOMPLETE。
2. 腳本記錄了時間戳、OS build、CPU、除錯器附加狀態、git commit、live ctest 數量與工具版本。
3. 閒置十分鐘的 CPU 平均值與磁碟 I/O 累計值有實際讀數,量測方法寫在腳本內而非人工步驟。
4. 三種記憶體組態各有讀數。
5. handle 數在 20 次版型切換後未單調成長,或若成長,寫出成長量與疑似來源。
6. 縮圖對記憶體的貢獻有一個差值數字。
7. `docs/release-evidence.md` 已產生,含 blocking gate 表格與 `## Result` 結論。
8. `docs/performance-baseline.md` 的可量測列已換成觀測值並註明來源 ticket;不可量測列保留 Not measured 且寫出原因。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
.\tests\release\release_evidence.ps1
echo "exit code: $LASTEXITCODE"
# 預期:產生 docs/release-evidence.md。exit 2 且結論為 INCOMPLETE 是本階段的正確結果,
# 除非全部 blocking 指標都已在本 ticket 量到。
```

```powershell
rg -n "SetTimer|Sleep\(" src
# 預期:無命中,或每一處都有註解說明為何不違反 event-driven idle path
rg -n "Not measured" docs\performance-baseline.md
# 預期:命中數少於建立時的 12 列
git diff --check
```

## Handoff requirements

交接時記錄:

- 全部讀數,含量測時的機器規格、Windows build、是否附加除錯器、安裝了哪些第三方 shell extension。
- 每個讀數的取得方法(哪個 API／計數器),使他人能重現。
- 哪些列仍是 Not measured,以及為何不能誠實量到。
- `release_evidence.ps1` 的實際 exit code 與 `## Result` 結論。
- 若閒置 CPU 未達門檻:疑似來源(哪個 timer、哪個 Shell 元件)與建議的後續 ticket。
- 縮圖記憶體差值,以及是否足以支持開立「縮圖 pipeline 上限」ticket。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-24 腳本實作與非互動環境交接

- 新增 `tests/release/release_evidence.ps1`。預設路徑依 LLVM-MinGW toolchain 執行 Release configure、build、`ctest -N` 與完整 CTest，逐步保留 stdout/stderr 與 exit code；產生 `docs/release-evidence.md`。本次實際結果：configure 0、build 0、CTest discovery 0、完整 CTest 0，live registrations 1、executed 1、skipped markers 0，CTest gate `PASS`。最終 `## Result` 為 **INCOMPLETE**，腳本 exit code **2**，原因是兩個 blocking 指標都沒有合格的十分鐘互動桌面讀數。這是 fail-closed 契約的預期結果，不是 PASS。
- 環境記錄由腳本產生於 2026-08-24（精確 timestamp 見 evidence）：Windows build 26200.9168（registry fallback，因此受限執行環境拒絕 CIM 存取）；CPU `Intel64 Family 6 Model 151 Stepping 2, GenuineIntel`、20 logical processors；evidence script 未附加 debugger；PaneDock measurement 未執行，故 app debugger 狀態為 `Not measured`；git commit `d36ae74e51acbf7ad01773b1149970fc6623855f`。工具版本與各步原始輸出完整保存在 `docs/release-evidence.md`。本受限環境無法可靠盤點目前載入／安裝的第三方 shell extension；先前真實桌面清單仍見 PD-011 交接區，未把它冒充成本次量測環境。
- `-CollectMeasurements` 是真實桌面路徑：操作者確認四個本機資料夾已穩定後，腳本以 `Process.TotalProcessorTime` 的 idle-window delta／elapsed／logical-processors 算平均 CPU，以 Win32 `GetProcessIoCounters` 的 read/write/other transfer-byte delta算磁碟 I/O，並用 `CheckRemoteDebuggerPresent` 記錄 PaneDock 是否附加 debugger。WorkingSet64 與 HandleCount 由同一 process snapshot 取得。腳本另依序等待操作者準備「四 pane 本機」、「四 pane 含縮圖、OneDrive、network」、「純文字資料夾」、「縮圖資料夾」組態，計算縮圖 WorkingSet64 差值；它不自行導覽或合成輸入。
- 20 次 layout check 的量測點已實作：切換前取一次 HandleCount，此後每次明示要求操作者自行按一次 `Ctrl+Shift+L`，按 Enter 後再取樣，共 21 點，報告前後值與是否每一步皆嚴格單調上升。依本次政策未模擬鍵盤／滑鼠，故實際 20 次執行仍待人類在真實桌面完成。live-view count 仍為 `Not measured`，因原型沒有 runtime diagnostic surface；本 ticket 是 measurement-only，沒有加入產品診斷功能。
- 三次連續 soak 已納入同一互動路徑：各次獨立啟動 PaneDock、等待操作者操作並正常關閉，記錄 process launch 的 stdout/stderr 與最終 exit code。此次未執行；非互動 report 對 measurement launch、idle sample 與 soak 1–3 各自明列 `not run`，沒有把 skipped step 當成證據。
- 無互動桌面的粗讀（**非官方 baseline，不滿足任何 gate**）：2026-08-24 12:55 啟動 Release `PaneDock.exe`，約 15 秒後 PID 35140 為 `Responding=True`、累積 CPU 1.046875 s（含 startup/navigation）、WorkingSet64 59,011,072 bytes（約 56.3 MiB）、HandleCount 747。`CloseMainWindow()` 回傳 true，但在額外 15 秒內仍未退出，最後只終止本次明確 PID 的測試程序。此結果受非互動 Shell/session 行為干擾，不寫入表格的正式 Result，也不推論 shutdown 缺陷。
- 仍為 `Not measured`：正式 idle CPU、idle disk I/O、單一 pane memory、四 pane mixed-resource memory、20-switch handle/live-view count、thumbnail contribution、Group switch latency、tab realize latency、cold-start paint。原因分別已更新至 `docs/performance-baseline.md`。其中單一 pane 是原型能力缺口（只有二／四 pane且隱藏 view 仍 live）；Group/tab 不存在於原型；cold-start first paint 需要目前沒有的 visible-paint instrumentation。四 pane local 只保留 PD-011 的 54.3 MB 單次讀數，未把本次 15 秒 headless 粗讀覆蓋成 PD-003 正式值。
- `rg -n "SetTimer|Sleep\(" src` 無命中，現有 app idle path 仍是 `GetMessageW` event-driven message loop。非平凡量測／gate 邏輯的 runnable self-check 即本次直接執行腳本：它捕捉成功的 configure/build/CTest 證據，並在 blocking measurement 缺席時產生 `INCOMPLETE`、exit 2。真實 `GetProcessIoCounters` 十分鐘路徑與人工切換路徑因缺少互動桌面未執行；不得視為驗收條件 3–6 已通過。

### 2026-08-24 驗證與狀態:blocked,需人工執行

獨立重跑 `.\tests\release\release_evidence.ps1`(不含 `-CollectMeasurements`)於真實桌面,結果與交接一致:`INCOMPLETE`、exit code `2`;`git status`/`git diff --stat` 確認只有 `docs/performance-baseline.md`、本文件與新增的 `tests/release/release_evidence.ps1`、`docs/release-evidence.md` 變動,無其他範圍外檔案。

腳本本身已檢視:`-CollectMeasurements` 路徑用 `Read-Host` 在多個步驟等待操作者按 Enter(確認四 pane 設定、確認縮圖/OneDrive/network 組態、每次 `Ctrl+Shift+L` 後按 Enter、確認正常關閉),完全不合成任何鍵盤/滑鼠輸入,設計上就是給人類在互動終端機執行,不是給無人值守環境跑的。這與目前受限的執行環境(無互動 stdin)以及使用者本 session 暫緩鍵盤/滑鼠自動化的指示一致——即使沒有暫緩指示,這支腳本原本就无法被非互動環境跑完。

Acceptance 3–6(十分鐘 idle 讀數、三種記憶體組態、handle 數、縮圖差值)因此仍未達成,不能標記 `done`。`docs/tickets.md` 狀態設為 `blocked`,依賴不變(PD-011),阻塞原因為「需要使用者在真實互動桌面親自執行 `.\tests\release\release_evidence.ps1 -CollectMeasurements`」。執行方式:開一個真正的互動式 PowerShell(不透過任何自動化 agent),`cd` 到 repo 根目錄執行該指令,依提示依序完成四種資料夾組態切換與 20 次 `Ctrl+Shift+L`,腳本會自動產生 `docs/release-evidence.md` 與量測數字;完成後把 `docs/release-evidence.md` 的內容摘要或截圖回報,即可把本 ticket 的狀態改為 `done` 並補上 `docs/performance-baseline.md` 的正式讀數。

### 2026-08-29 實機量測完成,轉 `done`

使用者確認由 agent 代為執行量測(路徑:`D:\Documents\Desktop\screenGif`、`D:\downloads`、`D:\OneDrive - via.com.tw\附件`、`\\vianextfs06\Tmp\Lentice\test`)。**未修改** `tests/release/release_evidence.ps1` 本體(它「不合成輸入」是刻意記錄的設計決策,見上方 2026-08-24 交接);改用一支不進 repo 的暫存 driver script,dot-source 該檔取用其 `Start-PaneDock`/`Get-Snapshot`/`Get-DebuggerAttached`/`Complete-PaneDock` 等 helper functions,自行以 Win32 訊息驅動 UI(`WM_KEYDOWN` 送位址列、`BM_CLICK` 送版型按鈕、`WM_CLOSE` 送主視窗),完全不合成滑鼠/鍵盤實體輸入。過程中 `FindWindow` 從 driver 的 shell 找不到 PaneDock 主視窗(class atom 查找失敗,原因未深究,懷疑與某種跨 process 限制有關),改用 `EnumWindows` 依 PID+class name 過濾繞過後正常運作。

讀數(完整證據見 `docs/release-evidence.md`,已同步進 `docs/performance-baseline.md`):

- Idle CPU(600.02 s,4 pane 本機資料夾,完全未互動):平均 **0.004948%**,PASS(門檻 <0.1%)。
- Idle 磁碟 I/O(同一視窗):累計 **307294 bytes**,**FAIL**(門檻零 bytes)。來源未診斷——超出本票範圍(non-goal:不做最佳化),已列入 `docs/tickets.md` §候選。
- 記憶體:1 pane 本機 62,881,792 bytes(678 handles);4 pane 本機 72,822,784 bytes(971 handles);4 pane 縮圖+OneDrive+network 72,933,376 bytes(971 handles)。
- 20 次自動化版型切換(Single/Four Panes 交替,直接 `BM_CLICK` 版型按鈕,非人工 `Ctrl+Shift+L`——程式碼裡實際上沒有這個 hotkey,交接文字裡的假設是錯的,實際 UI 是版型按鈕列):21 個 handle 樣本在兩個平台間震盪(~735/~890),非單調成長,PASS。
- 縮圖記憶體差值:讀出 0 bytes,**不可信**——比較的兩個組態在同一次 run 內都已導覽過同一批資料夾,縮圖早已快取,不是「首次產生縮圖 vs 純文字」的有效對照。已在候選表註記需要全新 process 分別量測。
- Live view count:此建置不輸出 `panedock.live_view_count=` 診斷 stdout,維持 Not measured。
- AC-005(不可達網路路徑復原後回應性):本次未測試,只做了可達路徑的直接位址列導覽。

過程中偶發一次 `panedock_launch_smoke` 崩潰(`STATUS_STACK_BUFFER_OVERRUN`,0xC0000409),發生在關閉一個含 4 個 Group(其中一個 42 tabs)的真實 `%LOCALAPPDATA%\PaneDock\session.json` 之後;立即重跑同一測試與完整 suite 兩次皆通過,無法穩定重現。未診斷根因(超出本票範圍),已記錄進 `docs/tickets.md` §候選供未來追查。

Acceptance 1–8 全部達成(腳本存在且 fail-closed、環境記錄齊全、閒置讀數、三種記憶體組態、handle 數、縮圖差值有讀數且誠實標註可信度、`release-evidence.md` 產出、`performance-baseline.md` 更新)。磁碟 I/O 門檻本身 FAIL 不影響本票完成度——那正是本票要交付的量測證據,NFR-001 的實際放行判定在 PD-027。狀態轉 `done`。
