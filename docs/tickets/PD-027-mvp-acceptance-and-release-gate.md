# PD-027 — MVP 驗收清單在四種必要環境上的執行與發佈閘門判定

Phase 5 · 驗證 ticket(不寫產品程式碼) · Depends on: PD-024, PD-025, PD-026

- Source: `AGENTS.md`、`docs/design-spec.md` §13(AC-001～AC-006)/ §12.4 / §NFR-001、`docs/testing.md` §MVP acceptance checklist / §Required test environments、`docs/performance-baseline.md` §Release evidence contract、`docs/roadmap.md` Phase 5 第三、五條
- Origin: 2026-08-24,`docs/roadmap.md` Phase 5「Full MVP acceptance checklist executed on every required environment」與「`tests/release/release_evidence.ps1` producing a PASS」。
- Priority: HIGH——這是 Phase 5 的完成判定,也是全案是否可發佈的唯一依據。

## Goal

把 `docs/testing.md` 的 MVP 驗收清單(13 項)乘上必要測試環境(4 種),寫成一份**逐項、逐環境、可照著做**的協定;執行它;把每一格的實際結果記進交接區;然後在真實互動桌面上執行 `.\tests\release\release_evidence.ps1 -CollectMeasurements`,以它的 `## Result` 作為發佈閘門的判定。

比照 PD-011 與 PD-023:**這是驗證 ticket,預期沒有 `src/` 改動**。發現缺口就開新 ticket,不在本 ticket 內順手修。

## 已確認的產品決策

1. **閘門判定只看 `docs/release-evidence.md` 的 `## Result`。** PASS 才算過。INCOMPLETE 是「還沒量」,不是「大概沒問題」;`docs/performance-baseline.md` 的契約寫得很清楚:未量測即不通過。任何以「看起來很順」代替讀數的判定一律無效。
2. **四種必要環境不是四種「最好也有」。** `docs/testing.md` 已經列名:無第三方 extension 的乾淨 Windows 11 x64、裝有第三方 extension(至少一個雲端同步用戶端與一個壓縮軟體)的機器、兩種不同 DPI 的多螢幕、可隨時中斷的對應網路磁碟機。**同一台實體機器可以同時滿足多個環境條件**(例如多螢幕機器上再裝 extension),協定要允許這件事,但交接區必須寫清楚哪一格是在哪台機器上得到的。
3. **不是每一項都要在每一種環境跑一遍。** 13 × 4 = 52 格裡多數是重複勞動。分配原則寫死在協定裡:AC-006 閒置資源與 NFR-004 DPI 各只在對應環境跑;AC-001／AC-003 在乾淨機與 extension 機各跑一次(extension 是最可能的崩潰來源);AC-005 與 FR-009 在網路磁碟機環境跑;其餘在任一環境跑一次即可。**協定必須明列每一項指定的環境,不留給執行者臨場判斷。**
4. **本專案不做鍵盤／滑鼠自動化。** 沿用 PD-011、PD-020～PD-023 一路的立場,以及 `docs/tickets.md` §已否決的方向對 WinAppDriver／UIAutomation 的否決。本 ticket 絕大多數項目需要真實互動桌面,由使用者親自執行;執行 agent 負責把協定寫好、跑完它跑得動的部分(建置、CTest、不含 measurement 的 evidence 腳本、程序層級檢查)、並把使用者回報的結果整理進交接區。**不得猜測或編造任何未實際執行的格子**——本 ticket 的產出就是證據本身。
5. **每一格三選一:`PASS` / `FAIL` / `未驗證,需真實桌面`。** 不接受空白、「應該可以」、「程式碼看起來正確」。`FAIL` 必附重現步驟與後續 ticket 編號。
6. **診斷模式(PD-024)在 extension 環境要跑一次對照。** 同一台機器、同一個操作,一般模式與 `--diagnostic` 各一次。這是 NFR-006 診斷模式唯一有意義的驗收方式,也順便驗證了它真的抑制得到 extension。
7. **Phase 5 的 roadmap 完成段落由本 ticket 的執行者撰寫,且只有在閘門 PASS 時才寫。** 比照 Phase 1／2／3／4 段落的既有寫法:列出交付 ticket、寫出實際狀態、把未完成的部分誠實寫成待辦而不是省略。**INCOMPLETE 或有未解決的 `FAIL` 時不得撰寫完成段落。**

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §NFR-001:
> 使用者未互動時:CPU 佔用不可量測(取樣期間平均 &lt; 0.1%),磁碟 I/O 為零。此為封閉式發佈門檻——未量測即視為不通過。

`docs/design-spec.md` §12.4:
> 以 `docs/testing.md` 的原型驗收清單執行,至少涵蓋一台裝有第三方 shell extension 的機器,以及一個已儲存但無法連線的網路路徑。

`docs/design-spec.md` §AC-003:
> 在二分割與四分割版型之間反覆切換,不持續累積 view,不破壞焦點,handle 數不單調成長。

`docs/design-spec.md` §AC-004:
> 關閉再開啟後,版型與全部 tab 的 location 精確還原。

`docs/design-spec.md` §AC-005:
> 還原狀態中含一個無法連線的網路路徑時,UI 保持反應。

`docs/design-spec.md` §NFR-005:
> **必要狀態**(版型、location、tab、view mode、排序)必須精確還原,否則視為缺陷。**best-effort 狀態**(選取項目、捲動位置、欄寬)不保證。

`docs/performance-baseline.md`:
> An unmeasured blocking metric produces **INCOMPLETE** and exit code 2. Absence of evidence is never treated as absence of a problem.

> A skipped test is not evidence.

`AGENTS.md`:
> Do not push branches, publish releases, or modify anything outside this repository without explicit approval.

> Never edit a completed ticket's document — that rule protects its scope, decisions and 交接區, which are the historical record.

> Anything a later session needs must live in the repository, not in a scratchpad handoff.

`docs/development.md` §Build configuration:
> Release for every gate measurement.

## Files to read and trace first

- `docs/testing.md` §MVP acceptance checklist(13 項)、§Required test environments(4 種)、§Prototype acceptance protocol(Phase 0 的寫法範本)、§Shell file operations acceptance protocol(PD-023 建立的 A–D 協定,格式範本)。
- `docs/tickets/PD-011-prototype-acceptance-and-go-no-go.md` 的 交接區——Phase 0 已經在真實桌面驗過哪些項目、哪一項(protocol step 5,版型切換 handle 數)明確**沒有**執行。那一項至今未關,本 ticket 是它的最後機會。
- `docs/tickets/PD-023-shell-file-operations-acceptance.md` 的 交接區——A–D 全部標為「未驗證,需真實桌面」。**FR-007／FR-009 那兩格不得引用 PD-023 當作 PASS**,它們仍待執行。
- `docs/tickets/PD-003-idle-resource-baseline.md` 的 交接區——`-CollectMeasurements` 的實際操作步驟與它 `blocked` 的原因。本 ticket 的量測執行同時解除 PD-003 的阻塞。
- `docs/tickets/PD-024-diagnostic-mode-suppressing-shell-extensions.md`、`docs/tickets/PD-025-crash-recovery-path.md`、`docs/tickets/PD-026-release-evidence-covers-shipped-app.md` 的 交接區——三者各自留下的「未驗證,需真實桌面」項目,本 ticket 的執行順便把它們收掉。
- `tests/release/release_evidence.ps1`(PD-026 更新後的版本)——`-CollectMeasurements` 會依序要求哪些操作。**先讀完再開始跑**,中途才發現要準備 OneDrive 資料夾或 USB 磁碟區會浪費一次十分鐘取樣。
- `docs/roadmap.md` Phase 1～4 的完成段落——決策 7 要比照的寫法。

## Scope

1. 在 `docs/testing.md` 新增一節 `MVP acceptance run (Phase 5)`,把 13 項 × 指定環境展開成一張逐項表格,每一項寫明:操作步驟、預期結果、指定執行環境(決策 3)、以及要記錄什麼。格式比照既有的 Phase 4 協定。表格至少涵蓋:
   - AC-001 四分割穩定性(乾淨機 ＋ extension 機)
   - AC-002 跨 pane 拖放、AC-002b 對外拖放
   - AC-003 版型切換 20 次的 view 數／焦點／handle 數(乾淨機 ＋ extension 機;這也是 PD-011 protocol step 5 的補做)
   - AC-004 重啟後版型與全部 tab location 精確還原(含多 tab、多 Group)
   - AC-005 還原狀態含無法連線網路路徑時 UI 有反應(網路磁碟機環境)
   - AC-006 閒置十分鐘資源符合 NFR-001(由 evidence 腳本量,不用目視工作管理員)
   - FR-001 Group 建立／重新命名／複製／刪除／重新排序
   - FR-003 五種版型
   - FR-005 每種版型下的 tab 新增／關閉／切換
   - FR-007 複製／移動／刪除／重新命名(可引用 PD-023 的 A–B 協定,但要實際跑)
   - FR-009 網路磁碟機、USB 磁碟區、OneDrive 佔位檔(網路磁碟機環境)
   - FR-013 損壞 session document 的復原(照 PD-025 Acceptance 3–5 的步驟)
   - NFR-004 混合 DPI 正確縮放(多螢幕環境)
   - 加上決策 6 的一般模式 vs `--diagnostic` 對照(extension 機)
2. 執行該協定,逐格記錄 `PASS` / `FAIL` / `未驗證,需真實桌面`(決策 5),並記錄每一格所在的機器與環境條件。
3. 在真實互動桌面、Release 建置、未附加除錯器的情況下執行 `.\tests\release\release_evidence.ps1 -CollectMeasurements`,把產生的 `docs/release-evidence.md` 提交進 repo,並把量得的數字回填 `docs/performance-baseline.md`(逐列註明由本 ticket 量得)。
4. 每一個 `FAIL` 開一張後續 ticket(取當時 `docs/tickets.md` 總覽表最大號 +1,確認 `docs/tickets/` 尚無該號檔案),附重現步驟;在本 ticket 交接區留下指標。
5. 閘門 PASS 時,依決策 7 撰寫 `docs/roadmap.md` 的 Phase 5 完成段落。**這是本 ticket 唯一被授權修改 `docs/roadmap.md` 的情況。**

## Non-goals

- 不寫任何 `src/` 產品程式碼;不在本 ticket 內修任何發現的缺口(決策 4、Scope 4)。
- 不做 UI 自動化、不合成鍵盤滑鼠輸入(`docs/tickets.md` §已否決的方向)。
- 不放寬、不調整任何 NFR-001 門檻。量不過就是量不過,開 ticket 修程式碼,不改門檻。
- 不編輯任何已完成 ticket 的文件(`AGENTS.md`);要引用就引用,不要改。
- 不 push branch、不發佈 release、不做打包／安裝程式——`AGENTS.md` 的 Safety boundaries,且 Spec 未要求。
- 不在閘門非 PASS 時撰寫 Phase 5 完成段落(決策 7)。
- 不為記憶體或延遲新增門檻。

## Acceptance

1. `docs/testing.md` 新增了 `MVP acceptance run (Phase 5)` 協定,13 項全部展開,每一項都標明指定環境,且格式與既有協定一致。
2. 交接區內每一格都有一筆明確結果(三選一),含機器與環境條件。**沒有任何一格是空白或含糊的。**
3. 四種必要環境各自至少有一格實際執行紀錄,或明確寫出該環境不可得的原因與受影響的項目。
4. 每一個 `FAIL` 都有對應的新 ticket(檔案已建立、`docs/tickets.md` 總覽表已加一列),或在交接區寫出為什麼不需要開票。
5. `docs/release-evidence.md` 已由 `-CollectMeasurements` 產生並提交,`## Result` 的結論與腳本 exit code 一併記入交接區。
6. `docs/performance-baseline.md` 的可量測列已換成本次讀數並註明來源 ticket;仍為 `Not measured` 的列寫出現在成立的原因。
7. 閘門為 PASS 時:`docs/roadmap.md` 有 Phase 5 完成段落,列出 PD-024～PD-027 與任何後續修補 ticket。閘門非 PASS 時:交接區寫出**還差哪幾個具體讀數或哪幾格**,以及誰要做什麼才能收掉。
8. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過(本 ticket 預期不改 `src/`,結果應與執行前完全相同)。
9. `git diff --check` 通過;`git status` 顯示的改動只有 `docs/*`(以及必要時新開的 ticket 檔)。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 不含 measurement 的一次基準跑(agent 可執行,確認腳本本身健康)
.\tests\release\release_evidence.ps1
"exit code: $LASTEXITCODE"     # 預期:2 / INCOMPLETE —— 這不是閘門結果,只是腳本健康檢查
```

```powershell
# 兩種模式的程序層級冒煙(agent 可執行)
foreach ($a in @('', '--diagnostic')) {
    $p = Start-Process .\build\PaneDock.exe -ArgumentList $a -PassThru
    Start-Sleep -Seconds 3
    $p.Refresh()
    "{0,-12} Responding={1} Title='{2}' Handles={3}" -f ($(if ($a) { $a } else { '(none)' })), $p.Responding, $p.MainWindowTitle, $p.HandleCount
    Stop-Process -Id $p.Id -Force
}
git diff --check
git status   # 預期:只有 docs/* 改動,沒有 src/*
```

```powershell
# 閘門本體 —— 需要真實互動桌面,由使用者親自在互動式 PowerShell 執行,不透過任何 agent:
#   cd <repo>
#   .\tests\release\release_evidence.ps1 -CollectMeasurements
# 依提示完成各種 pane 組態、20 次 Ctrl+Shift+L、AC-005 確認與三次 soak。
# 完成後把 docs/release-evidence.md 的 ## Result 與 exit code 回報。
```

## Handoff requirements

- 逐格結果表(項目 × 環境),含每台機器的 Windows build、螢幕與 DPI 設定、已安裝的第三方 shell extension 清單、涉及的磁碟機代號與類型。
- 一般模式 vs `--diagnostic` 的右鍵選單對照結果(決策 6)。
- `release_evidence.ps1 -CollectMeasurements` 的完整結論:`## Result`、exit code、每個 blocking 指標的實際數值、量測時是否附加除錯器。
- 明確列出「引用既有 ticket 的歷史證據而本次未重跑」的項目與理由(比照 PD-023 的做法);PD-023 的 A–D 目前全是「未驗證」,**不得被當成歷史 PASS 引用**。
- 每一個 `FAIL` 對應的新 ticket 編號。
- 閘門非 PASS 時:還缺哪些讀數、缺的原因、以及取得它們的具體步驟。
- **沒有互動桌面的執行 agent**:把協定寫完、跑完可自動執行的部分、把全部需要桌面的格子標成「未驗證,需真實桌面」交回,並在交接區寫明使用者要怎麼一步步跑完閘門。這是可接受的完成方式;猜測結果不是,填一個沒量到的數字更不是。

## 交接區

<!-- 驗收 agent 填寫,append-only -->
