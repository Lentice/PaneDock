# PD-036 — Group 拖拉排序(覆寫 PD-017 決策 5)

Phase 7 · sidebar, app_shell · Depends on: PD-017, PD-028

- Source: 使用者需求(2026-08-25 grilling session)
- Origin: 2026-08-25,使用者列出六項需求逐一核對現況,本項為「規劃 group 可以拖拉排序 代替按鈕上移下移」。
- Priority: LOW——純排序互動改進,`Move Up`/`Move Down` 目前已可用且已驗收,本票是新增另一種輸入方式,不是修 bug。

## 這是一次明確的決策覆寫——先讀這節

`docs/tickets/PD-017-group-sidebar.md` 已確認的產品決策 5:
> 重新排序用兩個方向鍵按鈕(「上移」／「下移」),不做拖放排序。

`docs/tickets.md` §已否決的方向的規則要求「要重開是允許的,但新 ticket 內必須寫出覆寫與新證據」。**這條決策記在 PD-017 自己的文件內,沒有被登記進 `docs/tickets.md` 全域的「已否決的方向」表格**——這是 2026-08-25 稽核 PD-017 時確認過的事實(見下方 Binding constraints 的追加說明)。新證據:使用者在 2026-08-25 直接提出「group 可以拖拉排序 代替按鈕上移下移」的明確需求,這已經構成 PD-017 決策 5 允許的重開條件(「之後如果要換成拖放,只是側邊欄內部的輸入處理改變,`core` 呼叫不用動」——PD-017 文件本身就預留了這條退路)。

## 已確認的產品決策

1. **`Move Up`/`Move Down` 不移除,拖放排序是新增的另一種輸入方式,兩者並存。** PD-028 已經把這兩個動作搬進 Group 列的右鍵 context menu(見 `docs/tickets/PD-028-sidebar-brand-and-group-summary-restyle.md` 決策 2)。純拖放排序對鍵盤操作使用者、或不熟悉拖放手勢的使用者不友善,保留 context menu 裡的按鈕作為替代路徑成本很低(已經存在,不用新增程式碼),沒有理由為了拖放而移除。
2. **拖放排序直接呼叫既有的 `core::reorder_group`,不新增新的 `core` 函式。** 對照 PD-017 文件本身的預留(「呼叫端只是側邊欄內部的輸入處理改變,`core` 呼叫不用動」),`core::reorder_group` 的簽章與行為已經在 PD-017 驗收過,本票只是換一種方式呼叫它。
3. **UI 端做法:在側邊欄 `LISTBOX` 上偵測滑鼠拖曳(比照 PD-035 對 tab strip 採用的同一套模式——`WM_LBUTTONDOWN` 記錄起始 index、移動超過閾值進入拖曳狀態、`LB_ITEMFROMPOINT` 算出目前懸停的目標 index、畫插入指示線、放開時呼叫 `core::reorder_group`),不使用 OLE 拖放。** 跟 PD-035 用同一套「滑鼠事件而非 `IDropTarget`」的理由一致:這是清單內部重排,不是接受外部資料的操作,且 `Sidebar` 的 `LISTBOX` 在 PD-034 已經要註冊為 `IDropTarget` 來接收「檔案懸停切換 Group」——**同一個控制項上,拖放檔案(PD-034,`IDropTarget`)與拖放清單項目本身(本票,滑鼠事件)必須能夠區分**,區分方式是:拖曳的起點如果是滑鼠在 `LISTBOX` 內部按下並立刻移動(內部拖曳),走本票的滑鼠事件路徑;PD-034 的 `IDropTarget::DragEnter` 只會在「外部啟動的 OLE 拖放操作」進入該視窗範圍時觸發,兩套機制天生互不干擾,不需要額外旗標協調——**但兩張票的實作 agent 必須都讀過對方的 ticket,確認沒有把滑鼠事件處理與 `IDropTarget` 回呼混在同一段程式碼裡導致邏輯糾纏。**
4. **拖曳排序期間,若同時有 PD-034 的懸停自動切換 Group 邏輯在跑,兩者不應該同時觸發(見決策 3 的區分方式,理論上不會同時發生,因為前者是拖曳清單項目本身、後者是拖曳外部檔案懸停在清單項目上)。若實作時發現這兩個假設有交互作用,在交接區記錄,不要自行擴大範圍去重新設計協調機制。**

## Binding constraints — quoted, do not go looking for them

`docs/tickets/PD-017-group-sidebar.md` 已確認的產品決策 5(本票明確覆寫的對象,理由與退路引用見上):
> 重新排序用兩個方向鍵按鈕(「上移」／「下移」),不做拖放排序......如果之後要換成拖放,只是側邊欄內部的輸入處理改變,`core` 呼叫不用動。

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`CONTEXT.md`:
> **Group**: A named, saved working context, and the primary abstraction of the product... _Avoid_: workspace, session, profile, project, favourite

## Files to read and trace first

- `src/core/model.h`/`model.cpp` 的 `reorder_group` 簽章與行為(PD-017 已驗收,直接呼叫,不修改)。
- `src/sidebar/sidebar.h`/`.cpp`——`Sidebar` 的 `LISTBOX` 建立方式、`selected_index`/`set_selected_index`、既有的 `WM_CONTEXTMENU`(PD-028 新增)處理,確認新增的拖曳偵測不會跟右鍵選單邏輯衝突(右鍵與左鍵拖曳是不同滑鼠鍵,理論上不衝突,但要讀過確認)。
- `docs/tickets/PD-035-tab-drag-reorder.md`——本票的拖曳狀態機與插入指示線繪製手法直接比照它,兩張票的實作應該視覺與程式碼風格一致(同一個「拖曳排序」互動語言用在兩個不同控制項上)。
- `docs/tickets/PD-034-drag-hover-auto-switch.md`——確認側邊欄 `LISTBOX` 的 `IDropTarget` 註冊方式,新增本票的滑鼠事件處理時避免跟它的訊息處理糾纏在同一段程式碼。
- `docs/tickets/PD-028-sidebar-brand-and-group-summary-restyle.md` 的交接區——側邊欄目前的 `WM_CONTEXTMENU` 實作與 `refresh_sidebar` 對 `Move Up`/`Move Down` grayed 判斷邏輯,確認本票不需要修改這部分(拖放排序是額外路徑,既有 grayed 邏輯不變)。

## Scope

1. `Sidebar`(`src/sidebar/sidebar.h`/`.cpp`)的 `LISTBOX` 訊息處理新增拖曳狀態機:比照 PD-035 對 tab strip 的模式,`WM_LBUTTONDOWN` 記錄起始 index、移動超過 `SM_CXDRAG`/`SM_CYDRAG` 閾值進入拖曳狀態、`LB_ITEMFROMPOINT` 算出目標 index、畫插入指示線(可直接複用 PD-035 新增的插入指示線繪製函式,若該函式已經是通用簽章;若 PD-035 尚未完成或函式綁死在 tab strip 上,本票就近新增一份小函式,不要為了共用而過度抽象——以「先做完哪一張,另一張直接複用」為準,不強求兩票同時完成才能各自動工)。
2. 放開滑鼠且目標 index 與起始不同時,呼叫既有的 `core::reorder_group`(透過 app_shell 現有的 `move_group`/呼叫路徑,或直接對照既有 `Move Up`/`Move Down` 按鈕的呼叫方式,傳入正確的目標 index 而非 ±1)。
3. 成功後 `refresh_sidebar`、`save_now`(比照既有 `move_group` 成功後的既有慣例)。

## Non-goals

- 不移除 `Move Up`/`Move Down`(context menu 裡的既有項目,見已確認的產品決策 1)。
- 不新增 `core` 函式,直接複用 `core::reorder_group`。
- 不使用 OLE 拖放實作本功能(已確認的產品決策 3)。
- 不處理 PD-034 的懸停自動切換 Group 邏輯本身(那是另一張票的範圍,本票只確保兩者不糾纏)。

## Acceptance

1. 在側邊欄 Group 列表內,按住某個 Group 列拖曳到另一個位置放開,順序正確改變。
2. 拖曳過程中顯示插入指示線。
3. 拖曳距離太短時視為一般點擊,原有的「單擊切換 Group」行為不受影響。
4. Context menu 裡的 `Move Up`/`Move Down` 行為與改版前完全一致,兩種排序方式的結果可以互相疊加使用(先拖曳、再用選單微調,或反過來)。
5. 排序後重開程式,順序透過 `session.json` 正確持久化。
6. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
7. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "reorder_group" src
# 預期:core 定義、既有 move_group 呼叫點、新增的拖曳排序呼叫點都命中
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:建立至少 3 個 Group,拖曳排序,確認插入指示線與最終順序正確,
# 確認 context menu 的 Move Up/Down 仍正常運作,重開程式確認順序持久化
```

## Handoff requirements

- 拖曳狀態機是否與 PD-035 共用了程式碼(插入指示線繪製函式等),或各自獨立實作,記錄理由。
- 與 PD-034 的 `IDropTarget` 是否有任何實際觀察到的交互影響(即使結論是「沒有」,也要寫下來confirm過)。
- 若真實桌面測試發現側邊欄同時支援右鍵選單、單擊切換、拖曳排序三種手勢時有任何手感或誤觸問題,記錄下來。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-25 實作交接

- PD-017 決策 5 依本票明確覆寫；`Move Up`／`Move Down` context menu 保留原路徑與命令 id，拖曳只是新增的側邊欄輸入方式。未修改 `core::reorder_group`，放開時以拖曳起點保存的 Group id 與目標 index 直接呼叫既有函式，成功後沿用 `refresh_sidebar`／`save_now`。
- 拖曳狀態機沿用 PD-035 的結構，但沒有抽出跨控制項共用抽象：tab 版的 `TabDrag`／`TCM_HITTEST`／垂直指示線綁定 tab strip，而本票新增控制項專用的 `GroupDrag`、`group_list_proc`、`group_item_at_point` 與水平指示線，差異只有 LISTBOX 的 `LB_ITEMFROMPOINT` 與 Group id。兩者都使用 `max(SM_CXDRAG, SM_CYDRAG)` 閾值、按下後 capture、游標離開控制項即取消、放開才 mutation。
- Group 插入指示線由主視窗 `WM_DRAWITEM` 在既有 `Sidebar::draw_item` 完成後繪製，使用 accent blue `RGB(37, 99, 235)`、依 DPI 縮放的 2px 寬水平線；來源索引大於目標索引時畫於目標列上緣，來源索引小於目標索引時畫於目標列下緣。短距離左鍵仍交給原生 LISTBOX，既有 `LBN_SELCHANGE`／`activate_group` 行為不變。
- subclass 安裝在 `Sidebar::create` 完成並已註冊 PD-034 `IDropTarget` 的同一個 LISTBOX HWND 上，但只處理普通 `WM_LBUTTONDOWN`／`WM_MOUSEMOVE`／`WM_LBUTTONUP`；沒有改動 `RegisterDragDrop`、`DragHoverTarget` 或 `WM_CONTEXTMENU`。因此外部檔案 OLE drag 沒有內部左鍵按下起點，不會建立 Group reorder state；Move Up／Move Down 的既有 context menu 分派也沒有改動。
- 真實桌面驗證未完成：本回合 Computer Use 初始化後兩次 `list_apps()` 都回報 `Computer Use native pipe is unavailable: failed to connect native pipe: 系統找不到指定的檔案。 (os error 2)`。因此沒有宣稱實際拖曳順序、插入線、右鍵選單或與 PD-034 的同時手勢已在桌面上通過，也沒有觀察到誤觸或 OLE 交互問題；結論來自程式碼路徑核對。放開前若 Group id／目標 index 已失效，既有 `reorder_group` 會回傳 `false`，不會改動狀態。
- 最終檢查：Release configure、`cmake --build build` 成功；`ctest --test-dir build --output-on-failure` 為 4/4；`rg -n "reorder_group" src` 命中 core 定義、既有 `move_group` 與新增拖曳呼叫；`git diff --check` 通過。額外 sanity check 中 `WM_QUERYENDSESSION`／`WM_ENDSESSION` 仍在 `main.cpp` 原有關機處理路徑，`rg -n "set_active\(" src` 無命中。PD-035 未被修改。
- 針對 LISTBOX 與 tab control 的行為差異補充：拖曳超過閾值後，`group_list_proc` 不再把 `WM_MOUSEMOVE`／拖曳中的 `WM_LBUTTONUP` 交給 LISTBOX 預設處理，避免原生清單在移動中逐列改選取並重入 `activate_group`；閾值前的左鍵仍走原生點擊流程。
