# PD-002 — 判定選取狀態還原是否可行,並決定該需求去留

Phase 0 · explorer_host · Depends on: PD-001

- Source: `AGENTS.md`、`docs/design-spec.md` §NFR-005、`docs/testing.md` §Prototype acceptance protocol step 8
- Origin: 2026-08-20 選型審查。兩份獨立審查都指出「還原選取狀態」是 handoff 文件沒提到的隱形大坑:`IExplorerBrowser` 沒有公開 API 可讀寫 pane 內的選取,實務上要對 view 內部的 `SysListView32` 發未公開的 `LVM_*` 訊息。
- Priority: **MEDIUM**——結論可能砍掉一個需求。越早知道越好,但必須有 PD-001 的原型才能試。

## Goal

回答一個問題:PaneDock 能不能還原一個 tab 內的選取項目,而且風險可接受?

三種合法結論,任一種都算完成:

1. **可行**——找到穩定的做法,寫下做法與其版本相依性,`NFR-005` 維持現狀。
2. **可行但風險不可接受**——做得到,但依賴未公開行為且跨 Windows 版本會斷。需求降級或砍除。
3. **不可行**——做不到。需求砍除。

本 ticket 刻意**不承諾實作**。它的產出是判定與依據。若結論為「可行」,實作另開 ticket。

## 已確認的產品決策

1. 選取狀態屬於 `docs/design-spec.md` §NFR-005 的 **best-effort 狀態**,不是必要狀態。砍掉它不影響 MVP 是否可交付。
2. 捲動位置與欄寬同屬 best-effort,順便一起判定,但不得為了它們擴大本 ticket 範圍。
3. 若結論是砍除,必須同時更新 `docs/design-spec.md` §NFR-005 與 §3.2,並在 `docs/tickets.md` §已否決的方向 新增一列(含重開條件)。**這是本 ticket 唯一被授權修改 spec 的情況**,依 §1「先更新 Spec,再調整受影響的 ticket」的規則執行。
4. 不接受「用一堆 `LVM_*` 訊息硬幹然後不寫下版本風險」這種交付。判定必須包含在哪些 Windows build 上實測過。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §NFR-005:
> **必要狀態**(版型、location、tab、view mode、排序)必須精確還原,否則視為缺陷。**best-effort 狀態**(選取項目、捲動位置、欄寬)不保證。

`AGENTS.md`:
> Selection restoration has no public Shell API and is expected to require undocumented `LVM_*` messages. It is best-effort and is the first feature cut if the prototype shows it unstable.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/tickets.md` §Agent 交付規則:
> 每個 ticket 只負責一個主要成果,避免跨 ticket 的隱性工作。

## Files to read and trace first

- PD-001 的 `## 交接區` — step 8 的初步判定與觀察到的 view 內部結構
- `src/explorer_host/` — PD-001 建立的 view 宿主型別與 site 物件
- 外部契約:`IFolderView2::GetSelectedItem` / `Items` / `SelectItem`、`IShellView::GetItemObject`、`SVGIO_SELECTION`、`FindWindowEx` 對 `SysListView32` 的定位、`LVM_GETNEXTITEM` / `LVM_SETITEMSTATE`

## Scope

1. 先窮盡**公開 API** 的可能性:`IFolderView2::GetSelectedItem`、`IFolderView2::SelectItem`、`IShellView::GetItemObject(SVGIO_SELECTION)`。逐一實測能否讀出選取、能否寫回選取。這一步必須先做完並記錄結果,才允許進入下一步。
2. 若公開 API 不足,才評估未公開路徑:以 `FindWindowEx` 定位 view 內的 `SysListView32`,以 `LVM_*` 訊息讀寫選取狀態。
3. 對每一條可行路徑,測試以下情境並記錄:
   - 一般本機資料夾
   - 項目數量大的資料夾(數千個檔案)
   - 虛擬命名空間(本機、控制台之類沒有一般檔案系統路徑的位置)
   - OneDrive 佔位檔所在資料夾
   - 切換 view mode 後(詳細資料／大圖示)
4. 在至少兩個不同的 Windows build 上實測(Windows 10 22H2 與 Windows 11),記錄行為差異。
5. 評估 realize 時序:選取的還原必須發生在 view 完成導覽之後。判定是否能可靠地知道「導覽已完成」——若不能,選取還原就不可靠,這本身就是判定依據。
6. 寫出判定與依據。若結論為砍除,執行「已確認的產品決策」第 3 點的文件更新。

## Non-goals

- 不實作正式的選取還原功能。本 ticket 只判定,實作另開 ticket。
- 不判定捲動位置與欄寬以外的其他 best-effort 狀態。
- 不為了讓選取還原可行而改動 `docs/design-spec.md` §9.1 的模組邊界或 `core` 的 COM-free 規則。
- 不引入任何抽象層或 wrapper 以「方便將來替換」。
- 不處理多選跨 pane 的情境——選取是 tab 的狀態,不是全域狀態。
- 不做效能最佳化。

## Acceptance

1. 公開 API 路徑的實測結果完整記錄:哪些能讀、哪些能寫、哪些回傳失敗或空值。
2. 若評估了未公開路徑,記錄其在 Scope 3 全部五種情境下的行為。
3. 在兩個 Windows build 上都有實測紀錄,差異寫出。
4. 導覽完成時序的可靠性有明確判定。
5. 產出三種結論之一,並寫出依據。
6. 若結論為砍除:`docs/design-spec.md` §NFR-005 與 §3.2 已更新,`docs/tickets.md` §已否決的方向 已新增一列且含重開條件。
7. 若結論為可行:做法、其版本相依性與殘餘風險已寫下,足以讓後續 ticket 直接實作。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
.\build\PaneDock.exe
# 依 Scope 3 的五種情境逐一實測讀取與寫回選取,記錄每次的 HRESULT 與觀測結果
```

```powershell
rg -n "LVM_|FindWindowEx" src
# 若結論為砍除,預期無命中——探測用程式碼不留在 repo
git diff --check
```

## Handoff requirements

交接時記錄:

- 三種結論之一,以及依據。
- 公開 API 的逐一實測結果,含 HRESULT。
- 未公開路徑(若評估了)在五種情境下的行為。
- 實測的兩個 Windows build 版本號與行為差異。
- 導覽完成時序的判定。
- 若砍除:已更新的文件位置,以及新增的已否決方向那一列的重開條件。
- 若可行:做法摘要、版本相依性、殘餘風險,以及建議的後續 ticket 範圍。

## 交接區

<!-- 實作 agent 填寫,append-only -->
