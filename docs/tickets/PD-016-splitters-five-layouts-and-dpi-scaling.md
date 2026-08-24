# PD-016 — 支援全部五種版型、可拖曳分隔線,並修正矩形計算的 DPI 縮放缺口

Phase 2 · app_shell · Depends on: PD-015

- Source: `AGENTS.md`、`docs/design-spec.md` §4.3／§4.4／§4.5／FR-003／FR-004／FR-004a／FR-014／NFR-004、`docs/tickets.md` §候選
- Origin: 2026-08-24,`docs/roadmap.md` Phase 2 清單的「Layout templates applied to real panes, draggable splitters」與「Active-pane indication and focus routing」。同時解決 PD-005 交接區記錄、已寫入 `docs/tickets.md` §候選 的已知落差:「讓 `compute_layout_rects` 接收 DPI 縮放後的最小尺寸／分隔線厚度,取代目前寫死的 96-DPI 基準常數」。
- Priority: MEDIUM——不是 Phase 2 的地基(PD-015 才是),但沒有它,五種版型與分隔線拖曳都不能用,side bar(PD-017)建好之後使用者第一件事就是想切版型跟拖分隔線。

## Goal

在 PD-015 已經把 app_shell 接上 `core::ApplicationState`／`compute_layout_rects` 的基礎上:

1. 讓使用者能切換全部五種版型(目前只有 `left_right`、`four_pane_grid` 兩種在用)。
2. 讓分隔線可以用滑鼠拖曳,即時反映到 `GroupState.divider_ratios` 並在放開時存檔。
3. 修掉 PD-005 記錄的 DPI 缺口:`kMinimumPaneWidth`／`kMinimumPaneHeight`／`kDividerThickness` 目前是寫死的 96-DPI 基準值,在非 96 DPI 的螢幕上會算出視覺上過小或過大的最小尺寸與分隔線厚度。
4. 補上 FR-014 要求的「pane 間移動焦點的鍵盤快速鍵」——目前只有滑鼠點擊可以切換 active pane。

## 已確認的產品決策

1. **`compute_layout_rects` 的簽章擴充,不重寫演算法。** PD-005 交接區已經建議這個方向:「把最小尺寸/分隔線厚度改成函式參數,矩形演算法本身不需要動」。本 ticket 採納這個建議,新增一個重載或加上帶預設值的參數(取捨見 Scope 1),讓呼叫端(app_shell)傳入依目前視窗 DPI 縮放過的值;`core` 內的三個具名常數繼續作為 96-DPI 基準,不砍掉。
2. **DPI 縮放係數用 `GetDpiForWindow(window) / 96.0`**,在 app_shell 內計算,不下放進 `core`(那需要 `windows.h`)。`WM_DPICHANGED` 收到新 DPI 時重新計算並重新套用版型,沿用現有的 `apply_layout` 呼叫點(PD-015 已把它接到 `compute_layout_rects`)。
3. **分隔線的命中測試範圍是 `core::kDividerThickness`(DPI 縮放後)本身,不額外加緩衝區。** 使用者體感上分隔線比較細,但這是刻意的最小實作;如果之後有人回報難以命中,再開票加大熱區,不在這裡預先加。
4. **拖曳中即時重算矩形並呼叫 `set_rect`,但不即時存檔。** 每個 `WM_MOUSEMOVE` 都存檔會在拖曳期間造成高頻磁碟 I/O,牴觸 NFR-001 的閒置資源精神(雖然拖曳不是「閒置」,但沒有理由在同一個使用者手勢內寫檔數十次)。存檔動作放在放開滑鼠鍵(`WM_LBUTTONUP`)那一刻,呼叫 PD-015 建立的 `save_now`。這是 PD-015 决策 4「mutation 後立即存檔」在「連續拖曳」這個情境下的具體化:一次拖曳手勢視為一個 mutation,不是每個中繼影格一個 mutation。
5. **三分割(`three_pane`)的兩個比例語意沿用 PD-005 的既定定義**:`divider_ratios[0]` 控制左右分隔,`divider_ratios[1]` 控制右側上下分隔。拖曳左側分隔線只改 `[0]`,拖曳右側分隔線只改 `[1]`。四宮格同理,`[0]` 是垂直分隔、`[1]` 是水平分隔。
6. **鍵盤 pane 焦點切換使用 `F6`(下一個)／`Shift+F6`(上一個)**,在目前版型下可見的 pane 之間依陣列索引順序循環,不做方向鍵式的空間導覽(例如「往右移」)。理由:`F6` 是 Windows 內建、Explorer 與 Visual Studio 等多窗格應用程式的既有慣例(「移動焦點到視窗的下一個窗格」),使用者不需要重新學習;空間方向導覽在三分割這種不規則版型下語意含糊(例如三分割向下按該去哪個 pane 不明顯),留給有真實需求時再開票加。這條快速鍵**不**透過 `RegisterHotKey`(那是行程層級全域熱鍵,容易跟其他應用衝突且不必要),而是在既有訊息迴圈裡、`ExplorerHost::translate_accelerator` 回傳 `S_FALSE` 之後攔截 `WM_KEYDOWN`,比照 PD-014 建立的攔截模式。
7. **`Ctrl+Shift+L` 熱鍵擴充為五種版型的循環切換**(固定順序:single → left_right → top_bottom → three_pane → four_pane_grid → single),取代 PD-015 沿用的兩版型切換。這是對 PD-015 決策的正式接手,不是覆寫——PD-015 交接區已明確把「擴充到五種版型」列為留給本 ticket 的範圍。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` FR-003:
> 支援且僅支援五種版型:單一、左右、上下、三分割、四宮格。切換版型時,若新版型的 pane 數較少,超出的 pane 之 tab 依序併入保留的 pane;若較多,新增的 pane 以預設 location 開啟一個 tab。

`docs/design-spec.md` FR-004／FR-004a:
> pane 分隔線可拖曳,比例以 0.0–1.0 的相對值儲存於該 Group,視窗縮放時維持比例。
> 視窗過小導致 pane 寬度或高度低於下限時,維持比例但不得產生負值或零尺寸的 pane 矩形。

`docs/design-spec.md` NFR-004:
> Per-Monitor-V2 DPI awareness。視窗跨越不同 DPI 的螢幕時,全部 pane 正確縮放。

`docs/design-spec.md` FR-014:
> 提供 pane 間移動焦點、切換 tab、新增／關閉 tab、上層導覽的快速鍵。快速鍵一律送往 active pane。

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers.

`docs/tickets/PD-005-layout-rect-computation.md` 交接區:
> 發現票券的 DPI 說明稱呼叫端會傳入「已按 DPI 縮放過的最小值常數」,但 scope 所定函式輸入只有 client size、版型與比例,固定常數無法由呼叫端縮放。依本票 scope 保留 96-DPI 基準常數;接線到 Per-Monitor-V2 UI 前,建議另票釐清並讓計算函式接收按 DPI 縮放後的 minimum/divider metrics,無須改變矩形演算法。

## Files to read and trace first

- `docs/tickets/PD-015-app-shell-core-state-wiring.md` 的交接區——本 ticket 接手前必讀,確認 `save_now`、`PaneRect`→`RECT` 轉換函式、目前支援哪兩種版型的實際狀態。
- `src/core/layout.h`／`layout.cpp`——`compute_layout_rects` 現有簽章與 `kMinimumPaneWidth`／`kMinimumPaneHeight`／`kDividerThickness`。
- `src/core/model.h`——`GroupState.divider_ratios`、`divider_ratio_count`、`switch_layout`。
- `src/app_shell/main.cpp`(PD-015 改寫後的版本)——`apply_layout`、`WM_SIZE`、`WM_DPICHANGED`、`WM_PARENTNOTIFY`、`WM_HOTKEY`、訊息迴圈裡 `translate_accelerator` 之後的攔截點。
- `src/explorer_host/explorer_host.h`——`set_rect`、`focus`、`set_active`。
- `docs/tickets/PD-014-explorer-host-ole-and-accelerator-wiring.md` 交接區——訊息迴圈攔截 `WM_KEYDOWN`/`WM_SYSKEYDOWN` 的既有模式,本 ticket 的 `F6` 快速鍵要照同一個模式接。

## Scope

1. `core::layout.h` 新增一個 `compute_layout_rects` 重載(或加參數,取決於實作時哪個對呼叫端更自然),接受最小寬度、最小高度、分隔線厚度三個 `int` 參數;不帶這三個參數的既有簽章繼續存在,內部呼叫新版並帶入現有的 96-DPI 常數,**保證 PD-005 的既有測試不必修改就能繼續通過**。
2. app_shell 新增 DPI 縮放輔助函式:`GetDpiForWindow(window)` 除以 96,四捨五入縮放三個基準常數後傳給新簽章的 `compute_layout_rects`。`WM_CREATE`、`WM_SIZE`、`WM_DPICHANGED` 都改用這個路徑。
3. `Ctrl+Shift+L` 改為五種版型依固定順序循環(見已確認的產品決策 7),每次呼叫 `core::switch_layout` 後照 PD-015 的模式呼叫 `save_now`。
4. 分隔線命中測試:給定目前版型與矩形集合,算出每條分隔線的矩形區域(寬度/高度等於 DPI 縮放後的 `kDividerThickness`,長度覆蓋相鄰兩個 pane),`WM_LBUTTONDOWN` 落在其中時進入拖曳狀態(記錄是哪一條分隔線、對應 `divider_ratios` 的哪個索引)。
5. 拖曳中(`WM_MOUSEMOVE` 且左鍵按住):依滑鼠目前位置換算新比例,寫回 `GroupState.divider_ratios[index]`(夾在 `0.0`–`1.0`,可直接借用 PD-004 `is_valid` 已驗證過的合法範圍語意,但寫入時自行 `std::clamp`,不必每個中繼影格都呼叫 `is_valid`),呼叫 `compute_layout_rects`重新套用矩形。`WM_SETCURSOR` 在游標落在分隔線上時顯示 `IDC_SIZEWE`/`IDC_SIZENS`。
6. `WM_LBUTTONUP` 結束拖曳狀態,呼叫一次 `save_now`。
7. `F6`/`Shift+F6` 的鍵盤 pane 焦點切換:在訊息迴圈裡、`translate_accelerator` 回傳 `S_FALSE` 之後,檢查是否為 `F6` 按下(`VK_F6`),依 `GetKeyState(VK_SHIFT)` 決定方向,在目前版型可見的 pane(依陣列索引)之間循環呼叫既有的 `set_active_pane` 邏輯,呼叫後 `continue`(不再 `TranslateMessage`/`DispatchMessageW`)。
8. 五種版型的 pane 數量變化沿用 PD-015 已接上的 `core::switch_layout`,本 ticket 不需要重寫 tab 搬移邏輯(PD-004 已經測過);只需要確保 app_shell 端在 pane 數量增加時,新出現的 `ExplorerHost` 陣列成員在此刻才第一次 `initialize`(沿用 PD-015 決策 4 的「可見才 realize」),減少的一側則 `set_visible(false)` 但不 `destroy`(PD-015 已定的池化策略)。

## Non-goals

- 不做 tab、不做導覽列(上一頁/下一頁/上層)——那是 Phase 3。
- 不做側邊欄(PD-017)。
- 不做方向鍵式的空間 pane 導覽(見已確認的產品決策 6)。
- 不擴大分隔線的命中熱區或加視覺 hover 效果的打磨——能用滑鼠準確拖到即可,视觉细节不是本 ticket 的重點。
- 不修改 `core::model.h`/`session.h` 的既有型別或不變式。
- 不新增 `RegisterHotKey` 之外或之上的其他全域熱鍵。

## Acceptance

1. `Ctrl+Shift+L` 依固定順序循環全部五種版型,每個版型下顯示的 pane 數量與 `pane_count(layout)` 一致。
2. 每種版型下的分隔線都可以用滑鼠拖曳,放開後矩形保持在拖曳結束時的比例(重新啟動後精確還原,因為已存檔)。
3. 拖曳分隔線時視窗不凍結,拖曳中每一幀都即時反映新矩形(手動觀察流暢度,不需要精確量測 FPS)。
4. 在一台可切換 DPI 縮放比例(例如 100% 與 150%)的螢幕或虛擬機上測試:兩種縮放比例下,最小 pane 尺寸與分隔線厚度在視覺上都合理(不會過小到難以拖曳、也不會過大到浪費空間)。
5. 現有 `panedock_core_layout` 測試不必修改即可繼續通過(新簽章是加法,不是破壞性修改)。
6. `F6`/`Shift+F6` 可在目前版型的全部可見 pane 之間循環移動 active pane 與鍵盤焦點,方向鍵鍵盤操作本身不影響 Shell view 的正常操作(在 pane 內按 `F6` 不會被 Shell view 攔截,因為 `IShellView::TranslateAcceleratorW` 通常不處理 `F6`——若測試發現有 Shell view 攔截了 `F6`,記錄下來,不強行覆蓋)。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
8. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "windows\.h|GetDpiForWindow" src\core
# 預期:無命中——DPI 查詢留在 app_shell
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:Ctrl+Shift+L 走過五種版型;每種版型拖曳分隔線;F6/Shift+F6 循環 active pane;
# 若環境允許,切換螢幕 DPI 縮放後觀察最小尺寸與分隔線厚度是否仍合理
```

## Handoff requirements

- `compute_layout_rects` 新簽章的最終形狀(重載或預設參數,以及參數順序),供之後任何呼叫端(例如未來的自訂 UI 皮膚)參考。
- DPI 縮放係數的計算時機與快取方式(是否每次 `WM_SIZE` 都重新查詢 `GetDpiForWindow`,還是只在 `WM_DPICHANGED` 時更新一次快取值)。
- 分隔線命中測試熱區的實際尺寸公式,供之後如果要加大熱區的 ticket 參考基準。
- `F6` 快速鍵在真實桌面上是否曾被任何已安裝的第三方 shell extension 或 Shell view 本身攔截;若有,記錄下觸發條件。
- 若五種版型循環對使用者來說順序不直覺(例如更期待用側邊欄圖示直接選版型而非循環按熱鍵),記錄下來——這屬於 PD-017 或未來 UI 打磨 ticket 的輸入,不在本 ticket 處理。

## 交接區

<!-- 實作 agent 填寫,append-only -->
