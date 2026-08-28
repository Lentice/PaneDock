# PD-112 — Pinned Locations 管理對話框(移除／排序)

Phase 7 · app_shell · Depends on: PD-111

- Source: 使用者與 assistant 的 grilling session(2026-08-28)。
- Origin: 使用者原文:「快速加入直接加在最下面,在額外的 panel 中可以移除也可以排序」。
- Priority: MEDIUM——PD-111 的 Pinned Locations 選單若沒有移除/排序能力,清單只能無限增長,無法修正加錯的項目。

## 已確認的產品決策

1. **獨立的 Manage 對話框,不在選單本身做右鍵刪除。** Win32 原生 popup menu(`TrackPopupMenu`)不易接右鍵事件——那需要 owner-draw menu,工作量遠大於一個獨立小視窗,不符合最小化範圍。PD-111 的選單最下方已保留 "Manage Pinned Locations..." 入口,本票接上它。
2. **對話框本身不用 `.rc` 的 `DIALOGEX` 資源。** 專案目前完全沒有任何 `DIALOGEX`/`CreateDialog`(`rg -n "DIALOGEX|CreateDialog"` 全專案零命中),所有 UI 都是 `CreateWindowExW` 手刻——比照既有慣例,用 `CreateWindowExW` 建一個小的 owned window(`WS_POPUP`/`WS_CAPTION` 皆可,由實作 agent 決定),不要新增 `.rc` 資源這條路。
3. **可以 Remove,也可以重新排序**(覆寫先前 grilling round 1 「不做排序」的暫定判斷——使用者在確認 round 明確要求兩者都要)。排序方式用「Move Up / Move Down」按鈕即可,不必做拖曳排序(拖曳排序在既有 tab/Group 清單上都是額外一套滑鼠事件邏輯,對一個管理小視窗不成比例地複雜)。
4. **只列出分隔線下方的自訂 Pinned Location**,Desktop/My Computer 這兩個固定項目不出現在管理清單裡、不能被移除或移動——它們是恆定的內建項目。
5. **不做重新命名**(沿用 PD-111 決策 9:一律用系統顯示名稱)。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`AGENTS.md`:
> Every persisted config/setting file must be designed for forward extensibility... A schema change is additive...

`CONTEXT.md`:
> **Pinned Location**: ...App-wide: shared across all Groups and panes, not part of any Group's state...

## Files to read and trace first

- `docs/tickets/PD-111-pinned-locations-menu.md`,尤其其交接區——本票要接上的 `pinned_locations`、去重規則、"Manage Pinned Locations..." 選單項目目前的佔位行為。
- `src/core/model.h` 的 `ApplicationState::pinned_locations`(PD-111 新增)。
- `src/core/session.cpp`——`pinned_locations` 的序列化,確認移除/排序後的寫回路徑。
- `src/app_shell/main.cpp` 現有視窗建立慣例(任一個非對話框的簡單 owned window,若存在的話;否則直接照抄主視窗/side panel 的 `CreateWindowExW` + 訊息迴圈風格,而非引入新框架)。
- `src/app_shell/main.cpp` 的 `display_text_for_parsing_name`——清單裡每一列要顯示的文字來源,同 PD-111。

## Scope

1. `core` 新增(若 PD-111 尚未提供)`remove_pinned_location`/`reorder_pinned_location` 等最小必要操作函式,行為與 `core::reorder_tab` 等既有函式風格一致(輸入索引或 identity,回傳是否成功)。
2. 新增一個 `CreateWindowExW` 建立的管理視窗:一個列表(自訂 Pinned Location,依序,顯示系統顯示名稱)+ Remove / Move Up / Move Down / Close 四個按鈕。
3. PD-111 選單的 "Manage Pinned Locations..." 改為開啟這個視窗(取代之前的佔位行為)。
4. 視窗內操作即時反映到 `ApplicationState::pinned_locations` 並 `save_now`;關閉視窗後,任一 pane 重新開 Pinned Locations 選單能看到最新順序與內容。

## Non-goals

- 不做拖曳排序(決策 3)。
- 不做重新命名(決策 5)。
- 不能移除/移動 Desktop、My Computer(決策 4)。
- 不做多選批次刪除。
- 不新增 `.rc` `DIALOGEX` 資源或任何對話框框架依賴(決策 2)。

## Acceptance

1. 從任一 pane 的 Pinned Locations 選單點 "Manage Pinned Locations...",開啟管理視窗,列出目前所有自訂 Pinned Location(不含 Desktop/My Computer)。
2. 選一項按 Remove,該項從清單消失;重新開任一 pane 的 Pinned Locations 選單,確認該項也從那裡消失。
3. 用 Move Up/Move Down 調整順序,關閉視窗後重新開 Pinned Locations 選單,順序與管理視窗一致。
4. 清單為空時 Remove/Move Up/Move Down 不當機(按鈕停用或操作無效果皆可)。
5. 重啟程式後,移除/排序的結果原樣還原。
6. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
7. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "remove_pinned_location|reorder_pinned_location|Manage Pinned Locations" src\core src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:開 Manage 視窗,Remove/Move Up/Move Down 各驗證一次,關閉後回選單確認同步;
# 重啟程式確認持久化。
```

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 管理視窗的視窗風格選擇(`WS_POPUP` vs 有 caption、是否 modal/`EnableWindow(parent, FALSE)`)與理由。
- `core` 新增函式的最終簽章。
- Remove/Move Up/Move Down 之後清單與選單同步的觸發點(是否需要主動 `refresh`,或下次開選單自然讀到最新資料)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 實作交接（2026-08-28）

- 管理視窗使用 `CreateWindowExW` 建立的單例 owned window：`WS_POPUP | WS_CAPTION | WS_SYSMENU`，搭配 `WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT`；它是 modeless，不呼叫 `EnableWindow(parent, FALSE)`。理由是管理視窗只承載清單操作，不需要額外的 modal message loop，保留既有主視窗的 Shell re-entrancy 與訊息處理路徑；同一時間再次開啟只會 refresh 並把既有視窗帶到前景。未新增 `.rc` 或 `DIALOGEX` 資源。
- `core` 最終簽章為：`bool remove_pinned_location(ApplicationState& application, std::size_t index) noexcept;` 與 `bool reorder_pinned_location(ApplicationState& application, std::size_t source_index, std::size_t target_index) noexcept;`。兩者都以 `pinned_locations` 的 vector index 操作，索引越界回傳 `false`；排序保留既有 `reorder_tab` 的 move/erase/insert 風格。
- Remove／Move Up／Move Down 成功後先更新 `ApplicationState::pinned_locations`，呼叫 `save_now`，再 refresh 管理視窗的 LISTBOX 並保留適當的選取列；`Add Current Folder` 若管理視窗仍開啟，也會在 `save_now` 後 refresh。Pinned Locations 選單沒有常駐快取，每次重新開啟都從共享 application model 重建，因此關閉管理視窗後任一 pane 的選單自然取得最新順序與內容。

#### 使用者手動驗證（本 agent 未執行）

本次依驗證限制未啟動或操作 PaneDock UI，請人工作以下檢查：

1. 執行 `build\\PaneDock.exe`，從任一 pane 開啟 Pinned Locations → `Manage Pinned Locations...`；確認視窗為獨立 owned window，清單只列自訂位置，不列 Desktop／This PC，並有 `Remove`、`Move Up`、`Move Down`、`Close` 四個按鈕。
2. 選取一筆位置按 `Remove`，確認它立即從清單消失；重新從任一 pane 開選單，確認該項目也消失。
3. 建立至少三筆 pinned locations，分別選中間項目按 `Move Up`、`Move Down`，確認清單即時換位；關閉視窗後從另一個 pane 開選單，確認順序一致。
4. 清空清單後確認 `Remove`／`Move Up`／`Move Down` 停用或按下無效果且程式不當機；重新加入一筆後確認按鈕狀態恢復。
5. 關閉程式並重新啟動，確認移除與排序結果原樣還原；驗證結束後以不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉，不使用 `Stop-Process -Force`。
