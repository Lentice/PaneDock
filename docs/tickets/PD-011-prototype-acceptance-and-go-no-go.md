# PD-011 — 原型驗收協定執行與 Go/No-Go 判定

Phase 0 · 驗證 · Depends on: PD-009, PD-010

- Source: `AGENTS.md`、`docs/design-spec.md` §9.1／§13、`docs/testing.md`、`docs/performance-baseline.md`、`docs/roadmap.md`
- Origin: 2026-08-20 由 PD-001 拆分。
- Override: 本 ticket 與 PD-007／PD-008／PD-009／PD-010 共同取代 PD-001。PD-001 的 Go/No-Go 職責由本 ticket 承接。
- Priority: **HIGH**——本 ticket 是全案 Go/No-Go 閘門。結論為 No-Go 時,Phase 1 以後的全部 ticket 必須重寫而非調整。

## Goal

對 PD-007～PD-010 建成的原型執行 `docs/testing.md` 的原型驗收協定 step 1–9,逐項取得可記錄的觀測結果,並寫下 **Go 或 No-Go** 的判定與依據。

本 ticket 的產出**不是程式碼,而是一份有證據的可行性判定**。若某個驗收步驟需要極小的補強才能執行(例如加一個診斷輸出),可以做;超出補強範圍的功能缺口應退回對應的 ticket,不要在這裡長出實作。

No-Go 是合法結論。

## 已確認的產品決策

1. 選取狀態還原**不在本 ticket 實作範圍**,只產出「可行 / 可行但風險不可接受 / 不可行」的書面判定,實作歸 PD-002。
2. 閒置資源只需取得一次讀數;正式的量測基準與門檻歸 PD-003。
3. 判定為 No-Go 時,交接區必須寫出是哪一個驗收步驟失敗、失敗的具體症狀,以及 `docs/design-spec.md` §9.1 的 `IShellFolder` fallback 是否仍可行。
4. 判定寫入本 ticket 的交接區,並同步更新 `docs/roadmap.md` 的 Phase 0 狀態。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

`AGENTS.md`:
> Anything a later session needs must live in the repository, not in a scratchpad handoff. Candidate tickets and rejected directions go in `docs/tickets.md`; measured numbers go in `docs/performance-baseline.md` or the ticket's 交接區.

`AGENTS.md`:
> Selection restoration has no public Shell API and is expected to require undocumented `LVM_*` messages. It is best-effort and is the first feature cut if the prototype shows it unstable.

`docs/roadmap.md`:
> Done means every step of the prototype acceptance protocol in `docs/testing.md` has a recorded result, and the Go/No-Go decision is written into the commissioning ticket's 交接區. A No-Go outcome is a legitimate result and redirects to the `IShellFolder` fallback recorded in `docs/design-spec.md` §9.1.

## Files to read and trace first

- `docs/testing.md` 的原型驗收協定全文——本 ticket 逐步照它執行
- `docs/performance-baseline.md`——step 9 的讀數要填回去
- PD-007／PD-008／PD-009／PD-010 的交接區——已知的行為異常與待驗證項
- `docs/design-spec.md` §9.1 的 `IShellFolder` fallback 描述

## Scope

1. 依 `docs/testing.md` 原型驗收協定逐步執行 step 1–9,逐項記錄觀測結果與實際數字。
2. 右鍵選單:確認四個 pane 內都是標準 Windows 選單,且在裝有第三方 shell extension 的機器上可見該 extension 的項目。記錄實測機器上安裝了哪些 extension。
3. 拖放:自 pane 0 拖檔至 pane 3 觸發標準 Windows 行為;與外部應用程式雙向拖放可用。
4. 網路韌性:把四個路徑之一設為已中斷連線的網路路徑,確認 UI 全程保持反應,該 pane 呈現可辨識的錯誤而非凍結。記錄從導覽到出現錯誤的實際秒數。
5. COM 例外:正常導覽期間附加除錯器,確認無未處理的 COM 例外。
6. 選取狀態還原可行性:實地試探所需的 `LVM_*` 途徑,產出書面判定,不做正式實作。
7. 閒置十分鐘後取得 CPU、記憶體、handle 數的一組讀數,填入 `docs/performance-baseline.md` 並註明由本 ticket 量得。
8. 寫下 **Go 或 No-Go** 與依據,更新 `docs/roadmap.md` 的 Phase 0 狀態。

## Non-goals

- 不新增產品功能。超出驗收補強範圍的缺口退回 PD-007～PD-010。
- 不實作選取狀態還原(歸 PD-002)。
- 不建立正式的閒置量測基準與門檻(歸 PD-003)。
- 不開始 Phase 1 的任何實作。
- 不建 CI。
- 不修改已完成 ticket 的文件。

## Acceptance

1. `docs/testing.md` step 1–9 全部有記錄的觀測結果,含實際數字,無「未執行」項。
2. 右鍵選單為標準 Windows 選單,第三方 extension 項目可見(或明確記錄實測機器上沒有安裝任何 extension,並標註此項未被真正驗證)。
3. 跨 pane 與對外雙向拖放的結果均已記錄。
4. 斷線網路路徑下 UI 未凍結,錯誤呈現方式與出現秒數已記錄。
5. 無未處理的 COM 例外,或已記錄具體例外與觸發條件。
6. 選取狀態還原有明確書面判定:可行、可行但風險不可接受、或不可行。
7. 閒置讀數已填入 `docs/performance-baseline.md`,該列註明由本 ticket 量得。
8. 交接區寫出 **Go 或 No-Go** 與依據。
9. `docs/roadmap.md` 的 Phase 0 狀態已更新。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
.\build\PaneDock.exe
# 依 docs/testing.md 原型驗收協定逐步執行 step 1-9,逐項記錄
```

```powershell
# 閒置十分鐘後
Get-Process PaneDock | Select-Object CPU, WorkingSet64, HandleCount
```

```powershell
git diff --check
```

## Handoff requirements

- Go/No-Go 判定與依據。
- step 1–9 的逐項觀測結果,含實際數字。
- 使用的機器規格、Windows build、是否附加除錯器、安裝了哪些第三方 shell extension。
- step 9 的讀數同時填入 `docs/performance-baseline.md`。
- 選取狀態還原的判定,寫成 PD-002 的前提。
- 任何 `IExplorerBrowser` 的實際行為與 Microsoft 文件描述不符之處——這類發現是後續 ticket 最有價值的輸入。
- 若判定 No-Go:哪一步失敗、症狀、以及 §9.1 的 `IShellFolder` fallback 是否仍可行。

## 交接區

<!-- 實作 agent 填寫,append-only -->
