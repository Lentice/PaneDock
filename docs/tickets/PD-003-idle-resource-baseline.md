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
