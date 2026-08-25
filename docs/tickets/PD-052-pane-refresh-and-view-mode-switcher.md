# PD-052 — Pane 增加 refresh 按鈕與檢視樣式切換按鈕,補上 `TabState::view_mode` 的還原缺口

Phase 7 · app_shell · Depends on: PD-020, PD-006

- Source: 使用者比對 `docs/panedock-ui-demo-01-refined-quiet-header.html` 目標畫面與實機截圖後回報(2026-08-25)。
- Origin: 「pane 要增加 refresh 按鈕,以及檢視樣式的按鈕(大圖示、小圖示、清單、詳細……)」。
- Priority: MEDIUM——這不只是新增 UI,還補上一個既有的 spec/schema 承諾與實作之間的落差。

## 已確認的根因(有程式碼證據,不是猜測——這是本票最重要的一段,說明這不是憑空的新需求)

1. **`docs/design-spec.md` 已經把 view mode 列為「必要還原狀態」,不是本票新開的產品決策:**
   - FR-002:「選取 Group 時還原:版型、分隔比例、每個 pane 的 tab 集合、每個 tab 的 Shell location、**view mode**、排序欄位與方向、active pane、每個 pane 的 active tab。」
   - NFR-005:「**必要狀態**(版型、location、tab、**view mode**、排序)必須精確還原,否則視為缺陷。」
   `core::TabState`(`src/core/model.h` 第 25-35 行)也已經有對應欄位:
   ```cpp
   struct TabState final {
       std::string id;
       ShellLocation location;
       std::string view_mode;
       std::string sort_column;
       bool sort_ascending{true};
       ...
   };
   ```
   這個欄位已經跟著 session document 一起序列化/還原(`session.cpp`),但 `rg "view_mode" src\app_shell\main.cpp` **零筆結果**——`app_shell` 從來沒有讀取或寫入這個欄位,也沒有任何 UI 可以改變檢視模式。這代表 spec 承諾的「view mode 必要還原」目前是靜默失效的:欄位有序列化,但永遠是空字串或建立時的預設值,使用者不管怎麼操作都無法真正改變或還原檢視模式。
2. **`ExplorerHost`(`src/explorer_host/explorer_host.h` 第 22-72 行)沒有任何 refresh 或檢視模式相關方法**——沒有 `refresh()`,沒有 `set_view_mode()`/`get_view_mode()`。

## 已確認的產品決策

1. **檢視模式透過 `IExplorerBrowser::GetCurrentView(IID_PPV_ARGS(&folder_view))` 取得 `IFolderView2`,呼叫 `IFolderView2::SetCurrentViewMode(FVM_ICON/FVM_SMALLICON/FVM_LIST/FVM_DETAILS/FVM_TILE/...)`/`GetCurrentViewMode` 切換與查詢,不是自己重新實作檢視樣式渲染。** 符合 `AGENTS.md`「Reach for the standard library and Win32 before adding a dependency」;`IFolderView2` 是 Shell 官方公開介面。
2. **UI 提供至少四種檢視模式對應使用者原文列出的:大圖示(`FVM_ICON`)、小圖示(`FVM_SMALLICON`)、清單(`FVM_LIST`)、詳細(`FVM_DETAILS`)。** 額外模式(`FVM_TILE`/`FVM_CONTENT` 等)是否加入由實作 agent 決定,不強制。
3. **`core::TabState::view_mode` 儲存 `IFolderView2` 的檢視模式列舉值(建議儲存為字串化的列舉名稱,例如 `"FVM_DETAILS"`,而不是原始整數,理由是 PD-013 的設定檔可擴展性慣例——字串比魔術數字更容易在未來的 schema 版本中安全解讀)。導覽完成或使用者手動切換檢視模式時寫回這個欄位;Group/tab 還原時讀取這個欄位並呼叫 `SetCurrentViewMode` 還原。**
4. **Refresh 按鈕呼叫既有的 `ExplorerHost::navigate(location_)` 用目前的位置重新導覽一次即可(不需要新的 COM 呼叫),但實作 agent 必須先確認這樣呼叫不會被「位置沒變就不作為」的既有短路邏輯擋掉(需要追 `navigate` 內部實作確認)。若確認擋住,才新增一個明確的 `refresh()` 方法繞過短路。**
5. **Refresh 與檢視模式切換按鈕的位置與視覺,比照目標畫面(`docs/panedock-ui-demo-01-refined-quiet-header.html`)放在每個 pane 的導覽列(既有的 back/forward/up/address 那一排,PD-031 已經圖示化),不需要新增一整排工具列。**

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` FR-002(已存在的必要還原範圍,本票補上實作,不是新決策):
> 選取 Group 時還原:版型、分隔比例、每個 pane 的 tab 集合、每個 tab 的 Shell location、view mode、排序欄位與方向、active pane、每個 pane 的 active tab。

`docs/design-spec.md` NFR-005:
> 必要狀態(版型、location、tab、view mode、排序)必須精確還原,否則視為缺陷。

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`docs/tickets/PD-013-config-file-extensibility-convention.md`(既有的設定檔可擴展性慣例,`view_mode` 的字串化儲存方式需遵守):
> 每個持久化的設定/組態檔案都要為未來擴充而設計。

## Files to read and trace first

- `src/core/model.h` 第 25-35 行(`TabState::view_mode`)——已存在的欄位,本票要真正讀寫它。
- `src/core/session.cpp`——確認 `view_mode` 目前如何序列化/反序列化(理論上已經跟著既有的 `TabState` 序列化邏輯走,需要確認實際情況)。
- `src/explorer_host/explorer_host.h`/`.cpp`——本票要新增的 `refresh()`/`set_view_mode()`/`get_view_mode()` 方法落腳處;`navigate()` 的既有實作(確認是否有「位置相同就不作為」的短路)。
- `src/app_shell/main.cpp` 的導覽列相關程式碼(`draw_navigation_icon_button`、`back_buttons`/`forward_buttons`/`up_buttons` 陣列,PD-031/043 已經涵蓋的區塊)——本票要新增 refresh 與檢視模式按鈕的排版落腳處,沿用同一套 DPI 縮放與圖示繪製慣例。
- `docs/tickets/PD-020-address-bar-and-navigation-buttons.md`——既有導覽按鈕的建立慣例。

## Scope

1. `ExplorerHost` 新增 `refresh()`、`set_view_mode(FOLDERVIEWMODE)`、`get_view_mode()`(或等效介面,由實作 agent 決定精確簽章)。
2. `core::TabState::view_mode` 在導覽完成/使用者切換檢視模式時寫入,Group/tab 還原時讀取並套用。
3. `src/app_shell/main.cpp` 每個 pane 的導覽列新增 refresh 按鈕與檢視模式切換按鈕(至少大圖示/小圖示/清單/詳細四種)。

## Non-goals

- 不在狀態列(PD-051)重複顯示檢視模式文字,除非能以極小改動一併完成。
- 不支援 `FVM_CONTENT`/`FVM_TILE` 以外的所有 `IFolderView2` 檢視模式(只需覆蓋使用者原文列出的四種,額外的由實作 agent 自行決定是否加)。
- 不改變既有的排序欄位/方向(`sort_column`/`sort_ascending`)還原邏輯——那不在本票範圍,若發現同樣有還原缺口,另開票不在本票內處理。

## Acceptance

1. 每個 pane 的導覽列有一個 refresh 按鈕,點擊後重新載入目前資料夾內容(即使內容沒有實際變化,操作本身要能執行,不能被短路吃掉)。
2. 每個 pane 的導覽列有檢視模式切換按鈕,可以在大圖示/小圖示/清單/詳細之間切換,且點擊後 Shell view 的實際呈現正確改變。
3. 切換檢視模式後儲存 Group(`save_now`/既有的 session 存檔路徑),重新啟動應用程式或切換 Group 再切回來,該 tab 的檢視模式正確還原為切換前選擇的模式。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "view_mode|SetCurrentViewMode|GetCurrentViewMode|FVM_" src\core\model.h src\core\session.cpp src\explorer_host\explorer_host.cpp src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:點擊 refresh 按鈕確認重新載入;切換四種檢視模式確認 Shell view 實際改變;
# 切換檢視模式後切換 Group 再切回來(或重啟程式),確認檢視模式正確還原。
# 本環境已具備螢幕截圖與滑鼠點擊模擬能力,請盡量實際操作驗證。
```

## Handoff requirements

- `ExplorerHost` 新增方法的最終簽章。
- `view_mode` 字串化列舉值的具體對應表(哪個字串對應哪個 `FOLDERVIEWMODE`)。
- `navigate()` 是否原本就有「位置相同不作為」的短路、refresh 按鈕最終如何繞過(或確認不需要繞過)。
- 真實桌面測試(refresh、四種檢視模式切換、還原)的實際結果。

## 交接區

<!-- 實作 agent 填寫,append-only -->
