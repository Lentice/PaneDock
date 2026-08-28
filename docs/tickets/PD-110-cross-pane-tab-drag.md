# PD-110 — Pane tab 允許跨 pane 拖曳搬移

Phase 7 · app_shell / core · Depends on: PD-035, PD-050, PD-074

- Source: 使用者要求「pane tab 允許被 drag & drop across different panes」(2026-08-28),經 `/grill-with-docs` 一輪問答確認範圍。
- Origin: PD-035 當初(2026-08-25)明確把範圍限定在「同一個 pane 內」,並在交接區留話「不含跨 pane 搬移 tab……若未來需要,另開新票」——本票就是那張新票,不是重開已否決的方向。
- Priority: MEDIUM——現有同 pane 拖曳排序已經穩定運作,這是延伸而非修 bug。

## 已確認的現況(有程式碼證據)

1. **live `IExplorerBrowser` 綁在 pane 插槽,不是綁在 tab。** `state.explorers[pane_index]` 是每個 pane 位置各一個持久 `ExplorerHost`;`switch_active_tab`(`main.cpp:2359-2380`)切換同一個 pane 內的 active tab 時,只對既有的 `state.explorers[pane_index]` 呼叫 `navigate()`(`:2374-2377`),從未 destroy/recreate。這代表跨 pane 搬移 tab **不需要**新增或移動任何 COM/Shell view 生命週期,只要搬 `TabState` 資料,再對受影響的兩個 pane(如果各自的 active tab 因此改變)各自呼叫既有的 navigate 邏輯即可。
2. **tab id 在整個 Group 內(跨所有 pane)保證唯一。** `unique_tab_id`(`main.cpp:2343-2357`)產生新 id 時掃描 `group.panes` 底下所有 pane 的所有 tab,不是只看單一 pane。跨 pane 搬移不會撞 id。
3. **目前的拖曳是純滑鼠事件,不是 OLE `IDropTarget`。** `tab_strip_proc` 的 `WM_LBUTTONDOWN`(`main.cpp:3094-3125`)在按下時記錄來源、呼叫 `SetCapture(window)`(`:3116`),之後 `WM_MOUSEMOVE`/`WM_LBUTTONUP` 都路由回同一個來源 strip 的 subclass proc,由 `update_tab_drag`/`finish_tab_drag`/`cancel_tab_drag` 處理(`:2794-2853`)。這與 PD-034 的「拖檔案到 tab hover 切換」用的 `IDropTarget`/OLE 拖放模型(`register_tab_drag_hover_targets`,`main.cpp:2385-2415`)是兩條獨立路徑,互不干擾,本票延續走滑鼠事件這條路。
4. **目前一旦游標離開來源 strip 的 client rect 就直接取消拖曳。** `update_tab_drag`(`main.cpp:2823-2853`)的判斷式:`if (!PtInRect(&client, point)) { cancel_tab_drag(state, strip); return; }`(`:2831-2836`)——這正是要改的地方:離開來源 strip 後不該立刻取消,而是要去 hit-test其他可見 pane 的 tab strip。
5. **`AppState::TabDrag`(`main.cpp:414-423`)目前只有單一 `pane_index`(來源與目標共用同一個 pane)與 `target_index`(同一個 strip 內的插入位置)。** 沒有欄位可以表示「目標是另一個 pane」。
6. **`apply_tab_item_size`(`main.cpp:1260-1322` 起)的 reorder/placeholder 計算完全以 `pane_index` 自己的 `state.tab_visuals[pane_index]` 為準,且只在 `state.tab_drag->pane_index == pane_index` 時才計算 placeholder 插入位置(`:1314-1322`,`:1362-1367`)。** 換句話說,目前完全沒有「在別的 pane 的 strip 裡,為一個不屬於它的 tab 保留插入位置」這種計算路徑。
7. **PD-074 的淡化 placeholder 內容繪製也綁死在同一個 pane。** `paint_tab_strip` 的 placeholder 分支(`main.cpp:2968-3000`)畫淡化文字時用的判斷式是 `state.tab_drag->pane_index == pane_index`(`:2987`),且文字來源是 `visuals[state.tab_drag->source_index].text`(`:2996`)——這個 `visuals` 陣列是**當前這個 pane 自己的** tab 視覺陣列,如果來源 tab 屬於另一個 pane,這裡完全抓不到它的文字/圖示。
8. **`finish_tab_drag`(`main.cpp:2803-2821`)目前只呼叫 `panedock::core::reorder_tab(pane, ...)`(`:2816`),這個 core 函式只認識單一 `PaneState`,天生無法搬到另一個 pane。**
9. **core 層已有的既有不變量:pane 永遠至少保留 1 個 tab。** `close_tab`(`src/core/model.cpp:272-289`)關掉最後一個 tab 時不是清空,而是把該 tab 的 `location` 重置成 `default_location`,保留這一個 tab(`:278-281`)。同檔案的 `add_tab`(`:250-256`)、`reorder_tab`(`:258-270`)、`set_active_tab`(`:291-297`)都是純資料操作,沒有 HWND/COM,符合 `src/core` 的既有測試 seam 邊界。
10. **PD-066 決策 2 與 PD-074 決策 1 已經明確否決「跟隨游標的浮動縮圖」**(`docs/tickets/PD-066-drag-reorder-placeholder-gap.md:51`:「那需要 layered window 或即時 blit,複雜度遠高於收益,而且與本專案的純 GDI 繪製路線不符」)。使用者在本票的 grilling 討論中已確認**不 override 這個決定**,改為延伸 PD-074 既有的「在 placeholder 空槽裡淡化畫出內容」機制,讓它能畫在**目標 pane** 的 strip 裡,而不是來源 pane 的 strip 裡。

## Binding constraints — quoted, do not weaken

`AGENTS.md`:

> Keep `src/core` free of HWND, COM and `windows.h`. It is the only automated test seam in this project; every type that leaks into it costs that seam.

> Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs persist as data and are realized on activation. This is what keeps memory bounded.

> Event-driven idle path only. No busy loops, no polling timers.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/tickets/PD-066-drag-reorder-placeholder-gap.md`(引用,不重寫):

> 不做「跟隨游標的浮動縮圖」。那需要 layered window 或即時 blit,複雜度遠高於收益,而且與本專案的純 GDI 繪製路線不符。

`docs/tickets/PD-035-tab-drag-reorder.md`:

> 只支援同一個 pane 內的 tab 順序調整,不支援拖到另一個 pane……不支援跨 pane 搬移 tab(已確認的產品決策 1;若未來需要,另開新票,列入 `docs/tickets.md` 候選,本票不預先設計搬移語意)。

## Files to read and trace first

- `src/app_shell/main.cpp:414-423`——`AppState::TabDrag` 結構,需要擴充成能表示「目標 pane 與來源 pane不同」。
- `src/app_shell/main.cpp:2794-2853`——`cancel_tab_drag`/`finish_tab_drag`/`update_tab_drag` 三個既有生命週期函式。
- `src/app_shell/main.cpp:3074-3174`——`tab_strip_proc`,尤其 `WM_LBUTTONDOWN`(`:3094`)的 `SetCapture` 與 `WM_CAPTURECHANGED`(`:3166`)。
- `src/app_shell/main.cpp:1260-1370`——`apply_tab_item_size` 的 reorder 排序與 placeholder rect 計算。
- `src/app_shell/main.cpp:2968-3000`——`paint_tab_strip` 的 placeholder 淡化內容繪製(PD-074 的既有實作)。
- `src/app_shell/main.cpp:2359-2380`——`switch_active_tab`,跨 pane 搬移後兩側 pane 更新 active tab 要重用的既有 navigate 模式。
- `src/app_shell/main.cpp:2343-2357`——`unique_tab_id`,確認 id 唯一性範圍。
- `src/core/model.h:39-52`、`src/core/model.cpp:250-297`——`PaneState`、`add_tab`/`reorder_tab`/`close_tab`/`set_active_tab`,本票要新增的 core 函式要放在這裡、風格要一致。
- `docs/tickets/PD-074-drag-placeholder-shows-dragged-item.md`——淡化 placeholder 的既有視覺決策全文,本票延伸它而非重做。
- `docs/tickets/PD-034-drag-file-onto-tab-hover-switch.md`(若存在,檔名以實際 `docs/tickets.md` 表格為準)——確認 `IDropTarget` 那條路徑的邊界,避免本票的滑鼠事件路徑與它衝突。

## Fix 方向 / Scope

1. **`AppState::TabDrag` 擴充**:加入來源 pane 之外的目標狀態,例如 `std::optional<std::size_t> target_pane_index`(與既有 `pane_index` 表示的來源 pane 分開)。`target_index` 語意不變,永遠代表「在 `target_pane_index`(若無值則預設等於來源 `pane_index`)那個 strip 裡的插入位置」。
2. **`update_tab_drag` 離開來源 strip 時,不直接取消**:游標離開來源 client rect 後,改成把游標轉成螢幕座標,依序對其餘**可見**的 `state.tab_strips[index]`(排除來源)做 `ScreenToClient` + `PtInRect(client)`,找到命中的 strip 就設定 `target_pane_index = index` 並用該 strip 座標系的 `tab_item_at_point` 算出插入位置;都沒命中才維持/回到取消。游標移回來源 strip 時要能正確把 `target_pane_index` 清回來源 pane(視為同 pane 拖曳排序),不是永遠鎖定第一個命中的目標。
3. **`apply_tab_item_size` 新增「外來拖曳插入」分支**:當某個 `pane_index` 是目前 `tab_drag->target_pane_index` 且不等於 `tab_drag->pane_index`(來源)時,在該 pane 自己的 `widths`/`order`/`placeholder` 計算裡插入一個額外的 placeholder 寬度槽(大小可用來源 tab 原本的寬度或 `kTabMinWidth`,兩者取合理值,不必和來源完全一致),不修改該 pane 真正的 `state.tab_visuals[pane_index]` 內容。來源 pane 自己的 strip 在目標變成別的 pane 後,維持「來源項目原地不畫」(沿用 PD-066/074 既定行為),但不再對自己的 `order` 做同 pane reorder 位移。
4. **`paint_tab_strip` 的 placeholder 淡化文字繪製改成認「誰是目前的目標」,不是「誰是來源」**:判斷式從 `state.tab_drag->pane_index == pane_index` 改成「這個 `pane_index` 是不是目前的目標 pane」;文字/圖示來源一律從 `active_group(state).panes[來源 pane_index]` 依 `tab_drag->tab_id` 查出對應 `TabState`(而不是讀本地 `visuals` 陣列),因為跨 pane 時目標 pane 的 `visuals` 裡本來就沒有這個 tab。同 pane 的既有行為(來源 pane == 目標 pane)結果必須不變,不能因為改寫判斷式而回歸。
5. **新增 core 函式**(建議命名 `move_tab`,放 `src/core/model.h`/`model.cpp`,簽章類似 `bool move_tab(PaneState& source, PaneState& target, const std::string& tab_id, std::size_t target_index, const ShellLocation& default_location)`):
   - 在 `source` 找不到 `tab_id` 就回傳 `false`,不動兩邊。
   - 若 `source.tabs.size() == 1`(搬走的是來源僅剩的最後一個 tab):比照 `close_tab` 既有精神(`model.cpp:278-281`),來源保留 1 個 tab、把它的 `location` 重置成 `default_location`,搬走的是原本那個 `TabState` 的完整內容(含原本的 `location`/`view_mode`/`sort_*`)。若 `source.tabs.size() > 1`:比照 `close_tab` 的 active tab 重新指派規則(`:282-287`)決定來源新的 `active_tab_id`。
   - 把搬出的 `TabState` 插入 `target.tabs` 的 `target_index`(超界則夾到 `target.tabs.size()`),並把 `target.active_tab_id` 設成這個 tab 的 id(依 grilling 確認的決策:被丟到目標 pane 的 tab 在目標裡變成 active)。
   - 為這個函式寫至少一個涵蓋「一般搬移」「搬走來源最後一個 tab」「搬到目標會成為 active tab」三種情境的 runnable 測試,比照現有 `src/core` 測試檔案的既有風格與位置。
6. **`finish_tab_drag` 依 `target_pane_index` 分流**:等於來源 pane 時完全沿用現有 `panedock::core::reorder_tab` 路徑(不得改動既有同 pane 行為);不等於時呼叫新的 `move_tab`,然後:
   - 呼叫既有的 `capture_pane_location(state, 來源 pane_index)`(參考 `switch_active_tab` 在 `main.cpp:2369` 的既有用法)把來源目前 active tab 的最新 `location`/排序等狀態寫回 model,再進行搬移,避免搬走的是過期資料。
   - 若來源 pane 已 realize(`state.realized[來源 pane_index]`)且 active tab 因搬移而改變,對 `state.explorers[來源 pane_index]` 呼叫 `navigate()`(比照 `switch_active_tab:2374-2377`)。
   - 若目標 pane 已 realize,對 `state.explorers[目標 pane_index]` 呼叫 `navigate()` 到被搬入 tab 的 `location`(同樣比照既有 navigate 模式)。
   - 兩個 pane 都呼叫既有的 `refresh_tab_strip`,最後 `save_now(state)`。
7. **有效丟放區只有 tab strip 本身**,不含 explorer 內容區、sidebar 或視窗外——維持純滑鼠事件模型,不新增 `IDropTarget` 註冊,避免與 PD-034 的檔案拖曳路徑混淆。
8. **不限制必須拖到相鄰的 pane**——2x2 或三分割版型下,任一可見 pane 的 strip 都是合法目標,不額外判斷幾何相鄰性。

## Non-goals

- **不做「跟隨游標的浮動縮圖」,不 override PD-066 決策 2 / PD-074 決策 1。** 使用者已在本票的 grilling 討論中明確選擇延伸既有淡化 placeholder 機制;若之後要重新考慮浮動縮圖,需要新的證據與另一次明確的 override 決策,不在本票範圍。
- 不改變同 pane 拖曳排序(來源 pane == 目標 pane)的既有行為、視覺或核准流程——PD-035/050/066/074 的決策全部維持原樣。
- 不支援跨 Group 拖曳 tab——同一時間畫面上只會顯示一個 active Group 的 pane,沒有 UI 介面可以同時操作兩個 Group 的 tab strip,沒有這個情境。
- 不新增或改動任何 `IExplorerBrowser` 的建立/銷毀邏輯——如「已確認的現況」第 1 點所述,搬移只需要對既有 pane 插槽的既有 browser 呼叫 `navigate()`。
- 不在本票修改 PD-034 的 `IDropTarget`/檔案拖曳 hover 切換機制。
- 不新增計時器、輪詢或背景執行緒。
- 不處理「畫面上只有 1 個可見 pane(single_pane 版型)」時的特殊 UI——沒有其他 pane 可以當目標,本來就是拖曳無效果的自然結果,不需要額外設計。

## Acceptance Criteria

1. 在四宮格或三分割版型下,把 pane A 某個 tab 拖到 pane B 的 tab strip 上放開:pane B 的 tabs 裡出現這個 tab、位置符合放開時的插入位置、且它變成 pane B 的 active tab(若 pane B 已 realize,實際 Shell view navigate 到它的路徑);pane A 的這個 tab 消失,active tab 依既有規則重新指派。
2. 對 pane A 只有 1 個 tab 的情境重複第 1 點:放開後 pane A 仍有 1 個 tab,且其 `location` 等同 `default_application_state()`(或既有 `close_tab` 用的同一個 default)所定義的預設路徑。
3. 拖曳中,只要游標停在 pane B 的 tab strip 上,pane B 會即時顯示淡化的插入 placeholder(內容為被拖曳 tab 的顯示文字),放開前用 `PrintWindow(hwnd, hdc, 2)` 截圖可以看到;游標移回 pane A 的 strip,placeholder 消失、恢復成同 pane 排序的既有視覺。
4. 同 pane 拖曳排序(不跨 pane)的既有行為、視覺、測試結果與修改前逐位元一致——用既有 PD-066/074 的驗證方法重跑一次,確認沒有回歸。
5. 拖到 explorer 內容區、sidebar 或視窗外放開:不觸發任何搬移,兩個 pane 的 tabs 與 session 都不變。
6. `cmake --build build`、`ctest --test-dir build --output-on-failure`、`git diff --check` 全數通過,新增的 `move_tab`(或等價命名)core 測試涵蓋一般搬移/搬走最後一個 tab/目標變 active 三種情境且全部通過。
7. 拖曳過程中游標靜止時,10 秒內 PaneDock process CPU delta 為 0(沿用 PD-066 既有的量測方法)。

## Agent Checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
rg -n "TabDrag|update_tab_drag|finish_tab_drag|apply_tab_item_size|move_tab" src\app_shell\main.cpp src\core\model.h src\core\model.cpp
```

跨 pane 拖曳的實機驗證比照 PD-066 交接區記載的既有做法:`SetCursorPos` 分段移動 + `mouse_event(MOUSEEVENTF_LEFTDOWN/LEFTUP)`,在送出 `LEFTUP` 之前用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 截圖確認目標 pane 的淡化 placeholder;真實 child HWND 幾何一律用 `EnumWindows`/`GetWindowRect`/`GetDlgItem` 取得,不得猜測座標。最後務必送出 `LEFTUP`,避免左鍵卡在按下狀態。

## Handoff requirements

- 記錄跨 pane 拖曳截圖(放開前的目標 pane placeholder、放開後兩側 pane 的最終 tabs)與對應的真實 HWND 幾何。
- 記錄 `move_tab` 新增測試的檔案位置與三種情境各自的斷言。
- 記錄同 pane 拖曳排序回歸測試的結果,證明沒有破壞 PD-035/066/074 既有行為。
- 記錄 CPU 靜止量測結果。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-28 — 實作交接

- `src/core/model.h` / `model.cpp` 新增 `move_tab`:一般搬移會依目標 index 插入,來源 active tab 依既有 `close_tab` 規則重選;搬走最後一個 tab 時來源保留原 tab identity、重設成預設 location 並清除不再相符的 history;搬入 tab 會成為目標 pane active tab。
- `tests/unit/core_model_test.cpp::test_move_tab` 覆蓋三項情境:一般搬移後來源 active 改為下一個 tab、搬入項目完整 location 保留且成為目標 active、搬走最後一個 tab 後來源仍有一個預設 location tab。
- `src/app_shell/main.cpp` 延伸既有 mouse-capture 拖曳:以螢幕座標 hit-test 所有可見 pane tab strip;目標 pane 顯示沿用 PD-074 樣式與來源文字的淡化 placeholder;放開後只搬 `TabState`,先 capture 兩側目前 location,再對 active tab 已改變的來源與目標既有 `ExplorerHost` navigate,未新增 COM lifetime、OLE drop target、timer、thread 或浮動視窗。
- 自動驗證:`cmake --build build` 成功(僅既有 missing-field-initializers warnings);`ctest --test-dir build --output-on-failure` 5/5 passed;`git diff --check` 通過。依使用者明確限制,本輪未啟動或操作實機視窗,也未使用 `SetCursorPos`、`mouse_event`、Computer Use 或互動後 `PrintWindow`。
- 使用者手動驗證:在 three-pane / four-pane 版型各將一般 tab 與來源唯一 tab 拖至另一個 pane tab strip 的開頭、中間、尾端,確認目標 placeholder 文字、插入位置、目標 active 與兩側 Shell location;再確認拖回來源 strip 的同 pane 重排、放到 Explorer/sidebar/視窗外不搬移、Esc/原位放開復原。另以 PD-066 方法手動量測游標靜止 10 秒 CPU delta 為 0,並保留拖曳放開前後截圖與真實 HWND 幾何。
