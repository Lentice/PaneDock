# PD-009 — active pane 指示與保活式版型切換

Phase 0 · app_shell · Depends on: PD-008

- Source: `AGENTS.md`、`docs/design-spec.md` §3.2／§9.3／§9.4、`docs/development.md`、`docs/testing.md`
- Origin: 2026-08-20 由 PD-001 拆分。
- Override: 本 ticket 與 PD-007／PD-008／PD-010／PD-011 共同取代 PD-001。
- Priority: **HIGH**——「切換版型時保活 view」是 `AGENTS.md` 明文規定的做法,也是全案最關鍵的崩潰面與資源洩漏面。本 ticket 獨立存在,是為了讓這條路徑能被反覆切換與量測而不與其他工作混淆歸因。

## Goal

驗證「版型切換時保持 `IExplorerBrowser` 存活、只重新 `SetRect`」這條路徑在反覆操作下不洩漏、不失焦、不崩潰。

`AGENTS.md` 已經規定了做法,本 ticket 要拿到它成立的證據。

## 已確認的產品決策

1. 只需要「二分割(左右各半,pane 0 與 pane 1)」與「四宮格」兩種狀態。**這與 `docs/testing.md` step 5 的措辭一致**——PD-001 原本寫「只顯示 pane 0」與四宮格,與驗收協定不符,本 ticket 予以更正。二分割也比單一 pane 更有價值:它同時測到「隱藏 view」與「兩個可見 view 一起 SetRect」。五種版型屬於 Phase 2。
2. 切換為隱藏／顯示與 `SetRect`,**不得** destroy 後重建 pane HWND 或 view。
3. active pane 指示以最低成本呈現,邊框繪製即可。
4. 隱藏 pane 的 view 在本階段保持 live。**這是對 `AGENTS.md`「Only the visible pane's active tab holds a live `IExplorerBrowser`」的一個明示例外**,不是疏漏:本片要測的正是保活路徑會不會洩漏,先把 view 殺掉就測不到。realize-on-activation(§NFR-002)屬於 Phase 3。若本片量到保活四個 view 的資源代價不可接受,寫進交接區,讓 Phase 3 提前處理。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups — that is both the crash surface and a CPU/disk spike from simultaneous folder enumerations.

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`:
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers.

`docs/design-spec.md` §9.2:
> 單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。

## Files to read and trace first

- `src/app_shell/`、`src/explorer_host/`(PD-007／PD-008 建立)
- PD-008 的交接區:多實例下已知的行為異常
- `IExplorerBrowser::SetRect`、`SetFocus`、`ShowWindow`

## Scope

1. 記錄哪個 pane 為 active。點擊 pane 內任一處將其設為 active。
2. active pane 以邊框繪製指示。切換 active 時只重繪受影響的兩個 pane。
3. 一個快速鍵在「二分割」與「四宮格」之間切換。切換時保活全部四個 view,只做顯示／隱藏與 `SetRect`。
4. 切換到二分割時,若 active pane 不在可見的兩個之中,active 轉移到 pane 0;切回四宮格時 active 維持不變。此規則寫進交接區。
5. 一個可執行的 self-check 目標,驗證版型切換的狀態機:任意次數的切換序列之後,live view 計數恆為 4、active pane 索引恆在有效範圍內。狀態機邏輯必須可在不建立真實 COM 物件的情況下被測到。

## Non-goals

- 不做可拖曳分隔線。
- 不做五種版型;只有二分割與四宮格兩種。
- 不做 realize-on-activation。
- 不做位置持久化(歸 PD-010)。
- 不做驗收協定的記錄與 Go/No-Go(歸 PD-011)。
- 不做視覺打磨;邊框以外不加任何樣式。

## Acceptance

1. 點擊任一 pane 後該 pane 成為 active,邊框指示正確,且該 pane 的 Shell view 取得鍵盤焦點。
2. 快速鍵可在二分割與四宮格之間切換,兩種狀態下可見的 view 都保持可用、可導覽。
3. 切換 20 次之後:live view 計數回到 4(基準)、焦點仍落在 active pane、handle 數未單調成長。
4. 切換過程中程式碼路徑內無 view 的 destroy／recreate。
5. 閒置時無計時器、無輪詢;不操作時 CPU 為 0%。
6. 版型切換狀態機 self-check 通過。
7. `rg -n "AddRef|->Release\(\)" src` 無命中。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
.\build\PaneDock.exe
# 手動:切換 20 次,以工作管理員或 Get-Process 記錄 HandleCount 與 WorkingSet 的前後值
Get-Process PaneDock | Select-Object HandleCount, WorkingSet64
```

```powershell
rg -n "AddRef|->Release\(\)" src
git diff --check
```

## Handoff requirements

- 切換 20 次前後的 HandleCount 與 WorkingSet64 實測值。
- 切換單次的體感延遲。
- 焦點在切換後是否曾丟失;若曾丟失,是哪一種切換方向、如何修正。
- 隱藏的 view 在隱藏狀態下是否仍消耗 CPU 或觸發磁碟 I/O——這個觀察會直接影響 Phase 3 的 realize-on-activation 設計。

## 交接區

<!-- 實作 agent 填寫,append-only -->
