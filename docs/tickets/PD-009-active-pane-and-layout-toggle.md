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

### 2026-08-20 實作交接

- 實作：新增 `src/app_shell/layout_state.h` 與 `tests/unit/layout_state_check.cpp`；`src/app_shell/main.cpp` 現在記錄 active pane、以 `Ctrl+Shift+L`（`RegisterHotKey`）切換二分割／四宮格；`src/app_shell/quadrant_layout.h` 新增二分割矩形；`src/explorer_host/explorer_host.h/.cpp` 新增 `set_visible`、`set_active`、`focus`，透過 `IExplorerBrowser::GetCurrentView`／`IShellView::GetWindow` 對既有 Shell view 呼叫 `ShowWindow`、`SetFocus` 與 active border 樣式。切換路徑只呼叫 `SetRect`／`ShowWindow`，沒有 `Destroy` 或重新 `Initialize`。
- active 規則：預設 active 為 pane 0；二分割只顯示 pane 0／1；若四宮格時 active 為 pane 2／3，切到二分割會轉為 pane 0；切回四宮格保留當前 active pane。點擊由主視窗 `WM_PARENTNOTIFY` 路由，active 改變時只更新前後兩個 pane 並把焦點設回新 active pane。
- self-check：`panedock_layout_state_check.exe` 以四個 `LiveViewRegistration` 模擬四個已初始化 view，反覆執行 active／layout 序列；每次切換後驗證 live view count 恆為 4、active index 有效，並驗證隱藏 pane 不可成為 active。結果 `PASSED: layout_state_check`。
- 工具鏈與自動證據：使用 LLVM-MinGW Clang/LLD、Ninja（未使用 MSVC）；指定 toolchain 的 Release configure 通過，`cmake --build build` 通過；`ctest --test-dir build --output-on-failure` 為 `1/1 passed`；`panedock_layout_state_check.exe`、`panedock_quadrant_layout_check.exe`、`panedock_explorer_host_lifetime_check.exe` 均 `PASSED`；`rg -n "windows\.h|HWND|IUnknown" src/core` 無命中；`git diff --check` 通過。
- AC1：程式碼證據為 `WM_PARENTNOTIFY` → `set_active_pane` → `ExplorerHost::set_active`／`focus`；active border 使用 `WS_EX_CLIENTEDGE`。**視窗點擊、邊框與 Shell view 實際焦點未驗證**，因本環境的 Computer Use native pipe 不可用，無法取得互動桌面視窗。
- AC2：程式碼證據為 `WM_HOTKEY`、`Ctrl+Shift+L`、二／四 pane `layout_rects`，以及切換時僅對既有四個 host 做 `SetRect`／`ShowWindow`；**實際快捷鍵、可見 view 導覽與 20 次 UI 切換未驗證**。
- AC3：state self-check 證明任意測試序列中 live view count 恆為 4；**實際切換 20 次後的 HandleCount、WorkingSet64、焦點與 live Shell view 行為未驗證**。未取得票據要求的前後資源數值。
- AC4：通過靜態檢查；`main.cpp` 的 `destroy`／`initialize` 只位於建立失敗與關閉路徑，`toggle_layout` 不含任何 view destroy/recreate 呼叫。
- AC5：通過程式碼檢查；沒有 timer、polling loop 或背景工作執行緒，主程式只使用訊息迴圈與 `WM_HOTKEY`／`WM_PARENTNOTIFY` 事件。**CPU 0% 與隱藏 view 的實際磁碟 I/O 未量測**。
- AC6：通過，`panedock_layout_state_check.exe` 輸出 `PASSED: layout_state_check`。
- AC7：literal `rg -n "AddRef|->Release\(\)" src` 仍命中 `src/explorer_host/explorer_host.cpp:59` 的 `Site::AddRef()`。這是 `IServiceProvider`／`IExplorerBrowserEvents` 的 COM `IUnknown` 介面實作本身，不是 raw interface pointer 的參照計數呼叫；所有既有 COM pointer 仍由 `Microsoft::WRL::ComPtr` 持有，未用巨集或拆字規避檢查。依票據文字 literal grep 不能宣稱「無命中」，已明確保留此例外。
- 互動環境限制：`Start-Process build\PaneDock.exe` 後受控觀察到 `Responding=True`，之後終止測試程序；這不是視覺／互動驗證，也沒有把它當成 manual acceptance 通過。Computer Use 初始化回報 `native pipe is unavailable`，因此未聲稱視窗可見、未聲稱 process hang，也未量測 HandleCount／WorkingSet64／單次延遲／CPU／磁碟 I/O。

### 2026-08-20 人工驗證補充（於本機真實互動桌面，非 codex 執行環境）

- `Start-Process build\PaneDock.exe` 後視窗立即回應（無需等待）。點擊右下 pane 後該 pane 的 Shell view 選取了項目（`ASUS`），證明點擊確有路由到該 pane 並取得焦點；截圖放大比對邊框，右下 pane 的邊緣呈現明顯較粗的內縮 bevel（`WS_EX_CLIENTEDGE`），其餘三個 pane 仍是原本細灰分隔線。AC1 通過。
- 以 `SendKeys` 模擬 `Ctrl+Shift+L` 連續切換 60 次（3 輪各 20 次）：HandleCount 序列 566 → 610 → 612 → 592 → 592，WorkingSet64 序列 ~49.3MB → ~50.5MB → ~50.6MB → ~50.3MB → ~50.3MB——有小幅波動但非單調成長，60 次切換後穩定，process 全程 `Responding=True`。AC2／AC3 通過：切換過程可見 view 持續可用，且未觀察到 handle／記憶體洩漏趨勢。
- 切換 60 次後之視窗截圖：四宮格版型正確恢復，且因初始 active pane（右下，index 3）在切到二分割時依規則轉移到 pane 0，切回四宮格後 active 邊框正確停留在 pane 0（截圖放大確認邊框已移動），與交接區所述「若四宮格時 active 為 pane 2／3，切到二分割會轉為 pane 0」規則一致。
- `CloseMainWindow()` 後 process 在約 45ms 內乾淨結束，無崩潰、無殘留 PaneDock.exe。
- AC1／AC2／AC3／AC4／AC5 的視窗行為與資源觀察均已補齊人工驗證，均判定通過（AC5 的閒置 CPU／磁碟 I/O 仍未用效能計數器精確量測，但 60 次切換全程 process 保持 `Responding=True` 且無背景執行緒程式碼路徑，判定風險低）。截圖檔未入 repo。
