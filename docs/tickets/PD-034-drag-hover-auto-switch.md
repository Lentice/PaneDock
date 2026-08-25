# PD-034 — 拖曳懸停自動切換(側邊欄 Group 列與 pane 的 tab)

Phase 7 · sidebar, app_shell · Depends on: PD-017, PD-019, PD-028

- Source: 使用者需求(2026-08-25 grilling session),`docs/design-spec.md` FR-007/FR-008(檔案操作透過 Shell)
- Origin: 2026-08-25,使用者列出六項行為/資料模型需求並要求逐一核對現況,本項為「規劃 drag file to tab,hover 一陣子會自動切換 tab」,經追問後使用者明確要求擴大範圍:**同時支援 PaneDock 內部與外部(桌面、其他總管視窗)拖曳來源,並且側邊欄的 Group 列也要有同樣的懸停自動切換效果**——原文:「Both,甚至拖到 group 停留一下子 也應該要可以切換 group」。
- Priority: MEDIUM——是使用者明確提出的功能需求,但非阻塞性 bug,排在 Phase 6 視覺改版與 PD-032 之後。

## 已確認的產品決策

1. **懸停目標涵蓋兩種區域,共用同一套機制:側邊欄 Group 列表(`Sidebar` 的 `LISTBOX`)與每個 pane 的 tab strip(`SysTabControl32`)。** 兩者都要註冊為拖放目標,懸停超過門檻時間時分別呼叫既有的 `activate_group`(Group 列)或 `switch_active_tab`(tab item)。懸停在 pane 的 Shell view 內容區(檔案列表本身)不需要額外處理——那是原生 `IExplorerBrowser` 已經自己是拖放目標,懸停在裡面本來就是「使用者已經找到目的地資料夾」,不需要再切換。
2. **同時支援 PaneDock 內部拖曳(從某個 pane 的 Shell view 拖出)與外部拖曳(從 Windows 桌面、其他檔案總管視窗拖入)。** 兩者在 Win32 拖放模型下走的是同一套 `IDropTarget`/`RegisterDragDrop` 機制,不區分來源,`DragEnter`/`DragOver` 不需要檢查 `IDataObject` 的來源或內容格式即可觸發懸停計時——只要有東西正在被拖曳且游標停在目標矩形內就開始計時,不判斷是否為 `CF_HDROP`(比檢查格式更寬容,但也更簡單;若之後發現有非檔案的拖曳來源觸發誤判,再收斂判斷條件)。
3. **懸停延遲比照 Windows Explorer 資料夾自動展開的既有慣例,取一個固定值 800 毫秒,不做使用者可調整的設定。** 用 `SetTimer`/`KillTimer`(訊息迴圈已有 `WM_TIMER` 前例可循,若沒有則新增)在 `DragEnter` 時啟動計時器、`DragLeave`/`Drop`/游標移出目標矩形時取消,計時到期才觸發切換,計時器 id 用具名常數避免與既有 `kLayoutToggleHotkeyId` 之類的 id 衝突。
4. **懸停切換之後,實際的檔案 drop 動作不由側邊欄或 tab strip 的 `IDropTarget` 處理,一律回傳 `DROPEFFECT_NONE`(不接受 drop),讓使用者放開滑鼠後如果還停在同一個位置什麼都不會發生;要真正完成搬移/複製,使用者必須把游標移到已經切換過去的 pane 的 Shell view 內容區再放開。** `AGENTS.md`「File operations go through Shell `IDataObject` and `IFileOperation`... Never assemble a path string and call the filesystem directly」——如果在側邊欄/tab strip 自己接受 drop 並嘗試搬檔案,等於要重新刻一份 `IFileOperation` 呼叫,而且行為會跟原生 Shell view 自己的拖放語意(複製 vs 搬移、快捷鍵修飾、衝突處理 UI)不一致。只做「懸停切換顯示」,不做「懸停切換後在原地接受 drop」,是唯一不違反這條規則的做法。
5. **Group 列懸停切換呼叫既有的 `activate_group`,tab item 懸停切換呼叫既有的 `switch_active_tab`;兩者的既有行為(保活式 Group 切換、realize-on-activation)完全不變,本票不新增新的切換邏輯,只是多一個觸發來源(懸停計時器到期)。** 對照 `AGENTS.md`「Shell APIs re-enter our message loop during drag... Host-side locking and shutdown sequencing must be reentrancy-safe」——`activate_group` 內部會呼叫 `apply_layout`,可能同步初始化新的 `IExplorerBrowser`;這個呼叫是在拖放操作進行中(`DoDragDrop` 的訊息迴圈重入)觸發的,必須確認不會在同一個呼叫堆疊裡遞迴呼叫 `DoDragDrop` 或造成死結——由於 `activate_group`/`apply_layout` 本身不啟動新的拖放操作,只做視窗排版與 Shell 導覽,理論上安全,但需要在真實桌面上針對「拖曳中途觸發 Group 切換」這個組合情境做過一次真人操作驗證,不能只靠程式碼審查判斷安全。
6. **已否決方向重新確認:本票不涉及 `docs/tickets.md`「端到端 UI 自動化(WinAppDriver／UIAutomation)」的否決範圍**——本票新增的是產品程式碼(`IDropTarget` 實作),不是測試自動化工具,兩者不衝突,不需要在本票討論重開那條否決。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> File operations go through Shell `IDataObject` and `IFileOperation`. Never assemble a path string and call the filesystem directly — that loses virtual items, progress UI and conflict handling.

`AGENTS.md`:
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`:
> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups.

`CONTEXT.md`:
> **realized**: A tab is realized when it holds a live `IExplorerBrowser` instance with an HWND... "Realize" is the verb for the transition; it happens on activation, never in bulk.

`docs/tickets/PD-017-group-sidebar.md` 已確認的產品決策 5(本票不重開這條,拖放懸停切換與「拖放排序」是不同功能,見 PD-036 才是重開排序的票):
> 重新排序用兩個方向鍵按鈕(「上移」／「下移」),不做拖放排序。

## Files to read and trace first

- `src/sidebar/sidebar.h`/`.cpp`——`Sidebar` 目前的 `LISTBOX` 建立方式、`selected_index`/`set_selected_index`,確認新增 `IDropTarget` 要註冊在哪個 HWND 上(`list_box_`)。
- `src/app_shell/main.cpp` 的 `activate_group`、`switch_active_tab`、`AppState::tab_strips`、`window_proc` 現有的訊息處理(確認有無既有 `WM_TIMER` 使用慣例)。
- Win32 `IDropTarget`/`RegisterDragDrop`/`RevokeDragDrop`/`DragEnter`/`DragOver`/`DragLeave`/`Drop` 官方語意——本程式碼庫目前完全沒有自訂 `IDropTarget` 實作(既有的拖放行為全部來自 `IExplorerBrowser` 內部,`OleInitialize` 已經在 `wWinMain` 呼叫過,不需要重複初始化 OLE)。
- `docs/tickets/PD-025-crash-recovery-path.md`/`explorer_host.cpp` 的 COM 生命週期管理風格(`Microsoft::WRL::ComPtr`),新的 `IDropTarget` 實作應該沿用同樣的 COM 物件寫法慣例。

## Scope

1. 新增一個小型 `IDropTarget` 實作(可以是一個內部類別,建構時吃一個「懸停到期時要執行的回呼」與「目標區域內命中測試」兩個參數,供側邊欄與每個 tab strip 共用同一份程式碼,不要各寫一份),`DragEnter`/`DragOver` 內做命中測試(側邊欄用 `LB_ITEMFROMPOINT`,tab strip 用 `TCM_HITTEST`)決定目前懸停在哪一列/哪個 tab item,若與上次不同就重設計時器;`DragLeave` 取消計時器;`Drop` 一律回傳 `DROPEFFECT_NONE` 且不執行任何檔案操作(依決策 4)。
2. 側邊欄的 `Sidebar::create` 對 `list_box_` 呼叫 `RegisterDragDrop`,懸停到期呼叫 `activate_group` 對應的既有邏輯(側邊欄本身不持有 Group 權威狀態,回呼要接到 `app_shell` 層,可透過既有的 `kRenameCommitMessage` 類似的 `WM_APP` 自訂訊息轉發給主視窗處理,或建構時直接吃 `std::function` 回呼,擇一,以最小改動為準)。
3. 每個 pane 的 tab strip 建立時對其 HWND 呼叫 `RegisterDragDrop`,懸停到期呼叫 `switch_active_tab`。
4. `Sidebar`/主視窗銷毀時對應呼叫 `RevokeDragDrop`,比照 `AGENTS.md` 對資源生命週期配對的一貫要求。
5. 懸停計時器門檻常數(800ms)集中定義一處,兩個懸停目標共用。

## Non-goals

- 不做懸停切換後直接在側邊欄/tab strip 完成檔案 drop(已確認的產品決策 4)。
- 不做拖放排序(那是 PD-035/PD-036 的範圍,懸停切換與排序是兩個獨立互動)。
- 不新增使用者可調整懸停延遲的設定選項。
- 不判斷/過濾拖曳來源的 `IDataObject` 格式(已確認的產品決策 2,除非之後有具體誤判案例才收斂)。
- 不修改 `core::activate_group`/`switch_active_tab` 呼叫的既有函式簽章或行為,只新增觸發來源。

## Acceptance

1. 從某個 pane 的 Shell view 拖曳一個檔案,懸停在側邊欄任一個非目前 active 的 Group 列上超過約 800ms,該 Group 被切換為 active,pane 區域正確還原該 Group 的版型與內容(沿用既有 `activate_group` 驗收標準)。
2. 從 Windows 桌面(PaneDock 外部)拖曳一個檔案到 PaneDock 視窗,懸停在某個非目前 active 的 tab item 上超過約 800ms,該 tab 被切換為 active tab,pane 內容正確導覽過去(沿用既有 `switch_active_tab` 驗收標準)。
3. 懸停未達門檻時間就移開游標或放開滑鼠,不觸發任何切換。
4. 在懸停任一目標時直接放開滑鼠(drop),不執行任何檔案複製/搬移,原生 Shell view 的正常拖放(在 pane 內容區直接放開)不受影響、行為與改版前一致。
5. 拖曳操作進行中觸發 Group 切換(情境 1)不會造成當機、卡死或視窗訊息迴圈異常——需要在真實桌面實際操作驗證,不能只靠程式碼審查。
6. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
7. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "RegisterDragDrop|IDropTarget|RevokeDragDrop" src
# 預期:側邊欄與 tab strip 各有一組註冊/註銷
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動(需要真實桌面,無法在無互動環境模擬):從桌面拖一個檔案到 PaneDock 視窗,
# 懸停在 Group 列與 tab item 上驗證 800ms 後自動切換、放開滑鼠不誤觸發檔案操作,
# 並確認拖曳中途觸發的 Group 切換沒有造成當機或卡死
```

## Handoff requirements

- `IDropTarget` 共用實作的最終介面形狀(建構參數、回呼型別)。
- 懸停計時器常數的最終數值與集中定義位置。
- 拖曳中途觸發 `activate_group` 的重入安全性,是否已經在真實桌面驗證過;若尚未驗證,誠實記錄,不得宣稱已驗證。
- 若真實測試發現某些應用程式的拖曳來源(例如某些第三方軟體用非標準 `IDataObject` 格式)導致懸停判斷失準,記錄下來,判斷是否需要日後收斂決策 2 的「不檢查格式」假設。

## 交接區

<!-- 實作 agent 填寫,append-only -->
