# PD-050 — 在新自繪 tab 條上重接拖曳懸停自動切換與拖曳排序

Phase 7 · app_shell · Depends on: PD-049, PD-034, PD-035

- Source: PD-049 覆寫 `SysTabControl32` 決策後的已知後果,由本票接手修正。
- Origin: PD-049 明確記錄「PD-034/PD-035 的既有行為在本票完成後預期會壞掉」,本票是那句話的後續動作。
- Priority: MEDIUM——功能性回歸,拖放檔案到 tab 自動切換、tab 拖拉排序在 PD-049 之後預期不能用,必須在整批視覺改版收尾前修好,否則是使用者可感知的功能退化。

## 前置狀態(必須先確認 PD-049 已完成才能開始)

本票依賴 PD-049 的自繪 tab 條已經存在並通過驗收。開始實作前,先確認:

```powershell
rg -n "WC_TABCONTROLW" src\app_shell\main.cpp
```

**如果這個指令還找得到 `WC_TABCONTROLW`,代表 PD-049 尚未完成或尚未合併,本票不可開始,回報並停止。**

## 已確認的根因(依 PD-049 交接區的實際記錄,不是猜測——實作前需重新讀取 PD-049 交接區確認細節)

1. `docs/tickets/PD-034-drag-hover-auto-switch.md` 的拖曳懸停自動切換,依賴 `register_tab_drag_hover_targets`(`src/app_shell/main.cpp` 第 1850 行起)與 `TCM_HITTEST`(命中測試找出滑鼠懸停在哪個 tab 上)。PD-049 把 tab 條換成自繪控制項後,`TCM_HITTEST` 不再適用於新控制項的 HWND。
2. `docs/tickets/PD-035-tab-drag-reorder.md` 的拖曳排序,依賴 `AppState::TabDrag`(`src/app_shell/main.cpp` 第 293-301 行)結構與 `TCM_HITTEST`/`TCM_SETCURSEL` 找出拖曳來源/目標 tab 索引、重排後呼叫 `TCM_DELETEALLITEMS`+`TCM_INSERTITEMW` 重建。PD-049 之後這些呼叫全部失效。

## 已確認的產品決策

1. **不改變拖曳懸停自動切換與拖曳排序的使用者可感知行為(驗收標準沿用 PD-034/PD-035 原票),只重新實作底層的命中測試與 tab 索引查找,改用 PD-049 新增的 tab 視覺資料結構(例如 PD-049 交接區記錄的「每個 tab 的顯示矩形清單」)取代 `TCM_HITTEST`。**
2. **`AppState::TabDrag`/`GroupDrag` 等既有拖曳狀態結構盡量原樣保留**,只替換內部用來「從滑鼠座標找出對應 tab 索引」與「觸發 tab 切換/重排」的實作細節,不重新設計拖曳狀態機。
3. **`revoke_drag_hover_targets`(第 336 行起)、`DragHoverTarget`/`make_drag_hover_target`(第 245 行起)等既有的 OLE drop target 註冊機制不變**——這些是掛在 tab 條 HWND 上的 `IDropTarget` 註冊,只要新 tab 條仍是一個有效 HWND,這層機制理論上不需要大改,本票要驗證這個假設是否成立,若發現需要調整(例如新控制項需要重新呼叫 `RegisterDragDrop`),就地修正並記錄。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/tickets/PD-049-custom-tab-strip-control.md`(本票的前置條件與已知後果聲明):
> 不在本票內重新實作拖曳排序(PD-035)、拖曳懸停自動切換(PD-034)……PD-034/035 的行為在本票完成後預期會壞掉(這是已知、刻意接受的暫時狀態),由 PD-050(依賴本票)接手重新接上。

## Files to read and trace first

- **先讀 PD-049 的交接區**,取得新 tab 條的資料結構、視窗類別與訊息處理方式的實際實作細節(本票寫成時 PD-049 尚未實作,無法預先寫死程式碼位置)。
- `src/app_shell/main.cpp` 的 `register_tab_drag_hover_targets`(第 1850 行起)、`make_drag_hover_target`(第 245 行起)。
- `src/app_shell/main.cpp` 的 `AppState::TabDrag`(第 293-301 行)與所有讀寫 `state.tab_drag` 的呼叫點(`rg "tab_drag\b" src\app_shell\main.cpp`)。
- `docs/tickets/PD-034-drag-hover-auto-switch.md`、`docs/tickets/PD-035-tab-drag-reorder.md` 全文——既有驗收標準與已知邊界情況(例如懸停時間閾值、拖曳門檻距離)。

## Scope

1. 找出 PD-049 之後所有仍呼叫 `TCM_HITTEST`/`TCM_SETCURSEL`/`TCN_SELCHANGE` 等失效訊息的拖曳相關程式碼,改用 PD-049 新增的 tab 視覺資料結構重新實作命中測試。
2. 驗證拖放檔案到某個 tab、懸停一段時間後自動切換到該 tab(PD-034 行為)在新控制項上正常運作。
3. 驗證拖曳 tab 標題可以在同一個 pane 內重新排序(PD-035 行為)在新控制項上正常運作。

## Non-goals

- 不改變 PD-034/PD-035 原本定義的驗收標準或互動細節(例如懸停自動切換的時間閾值)。
- 不改變拖曳懸停/排序以外的 tab 條行為(那是 PD-049 的範圍,已完成)。
- 不新增新的拖曳互動能力(例如跨 pane 拖曳 tab)——那不在原本 PD-034/035 的範圍內,本票也不新增。

## Acceptance

1. 拖曳一個檔案到某個未啟用的 tab 標題上並懸停,達到 PD-034 原訂的閾值時間後,該 tab 自動變成 active(與 PD-049 之前的行為一致)。
2. 拖曳某個 tab 標題到同一 pane 內的另一個位置放開,tab 順序正確更新(與 PD-049 之前的行為一致)。
3. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
4. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "TCM_HITTEST|TCM_SETCURSEL|TCN_SELCHANGE" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:(a) 從檔案總管拖曳一個檔案到某 pane 的非 active tab 標題上懸停,
#          確認自動切換到該 tab;
#      (b) 拖曳某個 tab 標題到同 pane 內另一個位置,確認排序正確更新。
# 本環境已具備螢幕截圖與滑鼠點擊/拖曳模擬能力,請盡量實際操作驗證。
```

## Handoff requirements

- 新的命中測試實作方式,與是否需要調整 `RegisterDragDrop`/`IDropTarget` 註冊時機。
- 真實桌面測試(懸停自動切換、拖曳排序)的實際結果。
- 若 PD-049 的資料結構在本票實作過程中發現不夠用(例如缺少某個必要欄位),記錄調整方式(可以就地在 PD-049 的檔案裡新增交接區補充,或在本票交接區記錄並附上具體改動)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
