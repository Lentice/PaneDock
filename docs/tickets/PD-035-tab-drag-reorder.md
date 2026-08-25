# PD-035 — Tab 拖拉排序(同一 pane 內)

Phase 7 · core, app_shell · Depends on: PD-019

- Source: 使用者需求(2026-08-25 grilling session)
- Origin: 2026-08-25,使用者列出六項需求逐一核對現況,本項為「規劃 tab 可以拖拉排序」,經追問後使用者確認範圍**只到同一個 pane 內**,不含跨 pane 搬移 tab(「同一個 pane 內」,使用者原文回答 Q2)。
- Priority: LOW——純排序功能,沒有現有行為會因此改變,風險集中在新增的拖曳輸入處理正確性。

## 已確認的產品決策

1. **只支援同一個 pane 內的 tab 順序調整,不支援拖到另一個 pane。** 使用者已明確確認範圍。拖曳到別的 pane 的 tab strip 上不觸發任何排序或搬移動作(那個互動屬於 PD-034 的懸停切換,兩者不衝突:PD-034 的懸停切換來源是「檔案」的拖曳,本票的拖曳來源是「tab 項目本身」,兩種拖曳在 `SysTabControl32` 上要能區分——本票用滑鼠在 tab strip 內部按下並移動的既有滑鼠事件處理,不透過 `IDropTarget`/OLE 拖放模型,天生就不會跟外部檔案拖曳混淆)。
2. **`core` 新增 `bool reorder_tab(PaneState& pane, const std::string& tab_id, std::size_t target_index) noexcept`,語意比照既有 `core::reorder_group`(`src/core/model.h`/`.cpp`)。** 找不到 `tab_id`、`target_index` 超出範圍時回傳 `false` 且不改動狀態;成功時把該 tab 移到 `target_index`,其餘 tab 保持相對順序,不改變 `active_tab_id`(移動的是順序,不是哪個 tab 是 active)。
3. **UI 端用滑鼠拖曳偵測,比照側邊欄既有 rename 的「就地」風格保持最小化,不使用 OLE 拖放。** 在 tab strip 的既有滑鼠訊息路徑(`WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP`)裡加入:按下時記錄起始 tab index、移動超過一個小閾值(例如 `GetSystemMetrics(SM_CXDRAG)`)才進入「拖曳中」狀態、拖曳中即時用 `TCM_HITTEST` 算出目前游標下的 tab index 並畫一條插入指示線(比照 `docs/tickets/PD-017-group-sidebar.md` 決策 5 提到的「插入指示線繪製」風格,但這裡套用在 tab strip 而非側邊欄 `ListBox`)、放開滑鼠時若目標 index 與起始不同才呼叫 `core::reorder_tab`。**不使用 `IDropTarget`/`DoDragDrop`**——tab 項目重排是 UI 內部操作,不涉及 `IDataObject`,用原生滑鼠事件比走 OLE 拖放模型省事,且不會跟 PD-034 的 `IDropTarget` 產生同一個控制項上兩套拖放機制打架的風險。
4. **拖曳中的插入指示線只在同一個 pane 的 tab strip 範圍內顯示;游標移出該 tab strip 的範圍時視為取消拖曳(放開滑鼠不觸發排序),不做「拖出去再拖回來」的容錯範圍擴大。** 保持最小可用邏輯,YAGNI。
5. **拖曳新增/新增分頁的 `+` 項目本身不可被拖曳、也不可被排序目標蓋過。** `+` 永遠留在最後一格(既有 `refresh_tab_strip` 已經保證這件事),`WM_LBUTTONDOWN` 命中 `+` 項目時直接視為既有「新增分頁」點擊行為,不進入拖曳偵測狀態。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`CONTEXT.md`:
> **tab**: One navigable location within a pane. A tab owns its location, view mode, sort order and navigation history.
> _Avoid_: page, document

`docs/tickets/PD-019-tab-strip-and-realize-on-activation.md`(既有 tab strip 建立方式,本票在其上擴充,不重寫):
> 左鍵點 tab control 仍會沿用既有 `WM_PARENTNOTIFY/WM_LBUTTONDOWN -> pane_at_point -> set_active_pane`,使該 pane 成為 active pane;同一次點擊再由 tab control 發出 `TCN_SELCHANGE` 切 tab。

## Files to read and trace first

- `src/core/model.h`/`model.cpp`——`PaneState.tabs`(`std::vector<TabState>`)、既有 `add_tab`/`close_tab`/`set_active_tab` 的錯誤處理慣例(全部回傳 `bool`,失敗時原狀態不變)、`docs/tickets/PD-017-group-sidebar.md` 裡 `core::reorder_group` 的簽章與實作風格,`reorder_tab` 要對齊同樣的慣例。
- `src/app_shell/main.cpp` 的 `refresh_tab_strip`、`switch_active_tab`、`AppState::tab_strips`、既有 tab strip 的 `WM_NOTIFY`/`TCN_SELCHANGE` 處理與 `WM_LBUTTONDOWN`/`pane_at_point` 現有邏輯——確認新增的拖曳偵測不會跟既有的「點擊切換 pane + tab」邏輯衝突(拖曳開始的第一次 `WM_LBUTTONDOWN` 目前會先觸發 `set_active_pane`,這個行為要保留,只是額外疊加拖曳狀態機)。
- `docs/tickets/PD-017-group-sidebar.md` 決策 5 與其交接區——側邊欄本來考慮過拖放排序但選了按鈕,如果交接區有記錄任何插入指示線的實作細節可以參考,若沒有則本票是本程式碼庫第一個拖曳排序 UI,需要自己設計。

## Scope

1. `core::model.h`/`.cpp` 新增 `bool reorder_tab(PaneState& pane, const std::string& tab_id, std::size_t target_index) noexcept`,語意與錯誤處理比照決策 2。
2. `tests/unit/core_model_test.cpp` 新增 `reorder_tab` 的 focused test:成功移動、`tab_id` 不存在、`target_index` 超出範圍三種情況,比照既有 `set_active_tab`/`set_active_pane` 的測試風格。
3. `main.cpp` 的 tab strip 訊息處理新增拖曳狀態機(依決策 3、4、5),新增一個小型繪製函式畫插入指示線(一條垂直短線,顏色可沿用既有 accent 藍)。
4. 拖曳成功放開時呼叫 `core::reorder_tab`,成功後 `refresh_tab_strip`(重新排列顯示順序)、`save_now`。

## Non-goals

- 不支援跨 pane 搬移 tab(已確認的產品決策 1;若未來需要,另開新票,列入 `docs/tickets.md` 候選,本票不預先設計搬移語意)。
- 不使用 OLE 拖放(`IDropTarget`/`DoDragDrop`)實作這個功能(已確認的產品決策 3)。
- 不改變 `active_tab_id` 的判斷邏輯或既有 tab 切換行為。
- 不改變 `+` 新增分頁項目的既有行為。

## Acceptance

1. 在同一個 pane 的 tab strip 內,按住某個 tab 拖曳到另一個位置放開,tab 順序正確改變,`active_tab_id` 不受影響(除非被拖曳的剛好是 active tab,active 狀態仍然跟著該 tab,只是顯示位置變了)。
2. 拖曳過程中顯示插入指示線,指示線位置正確反映放開後會插入的位置。
3. 拖曳距離太短(未超過 `SM_CXDRAG` 閾值)時視為一般點擊,原有的「點擊切換 active tab」行為不受影響。
4. 拖曳 `+` 項目本身不會發生排序,點擊 `+` 仍正常新增分頁。
5. 排序後重開程式,順序透過 `session.json` 正確持久化(因為 `reorder_tab` 成功後有 `save_now`)。
6. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過(含新增的 `reorder_tab` 測試)。
7. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "reorder_tab" src tests
# 預期:core 定義、app_shell 呼叫點、test 案例都命中
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:同一 pane 內建立至少 3 個 tab,拖曳排序,確認插入指示線與最終順序正確,
# 重開程式確認順序持久化,確認拖到別的 pane 的 tab strip 上沒有反應
```

## Handoff requirements

- `reorder_tab` 最終簽章與測試覆蓋的邊界案例清單。
- 拖曳狀態機的具體實作方式(閾值常數、插入指示線繪製函式)與是否跟既有 `WM_LBUTTONDOWN`/`pane_at_point`/`set_active_pane` 呼叫順序有交互影響,若有,記錄下來。
- 若真實桌面測試發現拖曳時 `TCM_HITTEST` 在 tab 數量變動時(拖曳中同時有其他操作觸發 `refresh_tab_strip`)有競態問題,記錄下來。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-25 實作交接

- 核心新增簽章：`bool reorder_tab(PaneState& pane, const std::string& tab_id, std::size_t target_index) noexcept`。找到 tab 且目標索引小於 `pane.tabs.size()` 才移動；tab id 不存在或索引超出範圍時回傳 `false` 且保留原狀態。`active_tab_id` 不會被改寫。`test_reorder_tab` 覆蓋成功移動、active id 保留、不存在的 `tab_id` 與超出範圍的 `target_index`。
- UI 以每個 `tab_strips[index]` 的 `tab_strip_proc` subclass 處理普通 `WM_LBUTTONDOWN`／`WM_MOUSEMOVE`／`WM_LBUTTONUP`，沒有新增或改動 PD-034 的 `IDropTarget`／OLE 註冊。按下先記錄 tab id、起點與 pane；位移超過 `max(SM_CXDRAG, SM_CYDRAG)` 後才進入拖曳狀態，並以 `TCM_HITTEST` 取得同一 strip 的目標。游標離開 strip、命中 `+` 或放開時沒有合法目標都取消，不會跨 pane 排序。
- 插入指示線由 `draw_tab_insertion_indicator` 在 owner-draw tab item 後繪製，使用既有 accent blue `RGB(37, 99, 235)`、依 DPI 縮放的 2px 寬垂直線；來源索引大於目標索引時畫於目標左緣，來源索引小於目標索引時畫於目標右緣。放開且目標不同於來源才呼叫 `core::reorder_tab`，成功後 `refresh_tab_strip` 與 `save_now`。
- 第一次左鍵仍交給 `DefSubclassProc`，既有主視窗 `WM_PARENTNOTIFY` 的 `pane_at_point`／`set_active_pane` 路徑保持不變；因此短距離點擊仍是原有切換行為。PD-034 的外部檔案 OLE drag 沒有 tab 內 `WM_LBUTTONDOWN` 起點，不會建立本票的內部拖曳狀態，兩者可共存。
- Computer Use 初始化後兩次 `list_apps()` 都遇到相同原生管線錯誤：`Computer Use native pipe is unavailable: failed to connect native pipe: 系統找不到指定的檔案。 (os error 2)`。因此本回合沒有宣稱完成真實滑鼠拖曳、插入線、跨 pane 取消或重開持久化的桌面驗收；也沒有觀察到 `TCM_HITTEST` 與 tab 數量變動的競態。程式在放開時仍重新驗證 active Group、pane、tab id 與目標索引，若狀態已失效則 core 會安全回傳 `false`。
- Agent checks：Release configure 成功；`cmake --build build` 成功；`ctest --test-dir build --output-on-failure` 為 4/4；`rg -n "reorder_tab" src tests` 命中 core 宣告／定義、app 呼叫與 test；`git diff --check` 通過。工作樹保留未提交變更，PD-036 未修改。
