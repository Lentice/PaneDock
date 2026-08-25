# PD-051 — 每個 pane 增加狀態列(項目數/選取數,比照 Windows 檔案總管)

Phase 7 · app_shell · Depends on: PD-007, PD-030

- Source: 使用者比對 `docs/panedock-ui-demo-01-refined-quiet-header.html` 目標畫面與實機截圖後回報(2026-08-25)。
- Origin: 「每個 pane 都應該有狀態列,要跟 windows explorer 類似」。
- Priority: MEDIUM——新功能,目標畫面每個 pane 卡片底部有一列「4 items · Details view」的狀態文字,目前完全沒有對應元件。

## 已確認的現況(有程式碼證據,不是猜測)

`src/explorer_host/explorer_host.h`(第 22-72 行)目前只暴露 `navigate`/`navigate_up`/`set_navigation_callback`/`set_navigation_failed_callback`/`set_rect`/`set_visible`/`focus`/`translate_accelerator`/`destroy`/`location`/`navigation_complete`/`navigation_failed`。**沒有任何取得目前資料夾項目數、選取數或目前檢視模式的介面**,`rg "IFolderView" src` 沒有任何符合結果——本專案目前完全沒有取用 `IFolderView`/`IFolderView2` 這個 Shell 介面。

## 已確認的產品決策

1. **狀態列顯示的資訊來源是 `IExplorerBrowser::GetCurrentView(IID_PPV_ARGS(&folder_view))` 取得的 `IFolderView2`,呼叫 `IFolderView2::ItemCount(SVGIO_ALLVIEW, &total)` 取得總項目數、`IFolderView2::ItemCount(SVGIO_SELECTION, &selected)` 取得選取項目數,不是自己走訪檔案系統或重新實作 Explorer 的狀態列邏輯。** 這完全符合 `AGENTS.md`「Reach for the standard library and Win32 before adding a dependency」——這是 Shell 官方公開介面,`IExplorerBrowser` 本來就已經是本專案的核心依賴,`GetCurrentView` 是同一組 COM 介面上的既有方法,不需要新增依賴。
2. **狀態列文字格式比照 Windows 檔案總管的慣例:無選取時顯示「N 個項目」(或英文 `N items`,App UI 文字一律英文,見 `AGENTS.md` 語言規則),有選取時顯示「已選取 N 個,共 M 個」對應的英文格式(`N of M selected`,實際文字由實作 agent 決定,只要語意等同 Windows 檔案總管即可)。**
3. **更新時機:(a) 每次導覽完成(`navigation_complete`,既有的回呼點)重新查詢一次項目數;(b) 選取狀態改變時也要更新——這需要新增一個選取變化的通知來源。** 查證 `IExplorerBrowserEvents`(`src/explorer_host/explorer_host.cpp` 第 57 行既有的 `events_` 成員)目前訂閱哪些事件;若沒有涵蓋選取變化,改用 `IFolderView2::SetGroupBy`/`Advise` 或 `IShellView::GetItemObject`搭配 `IShellFolderViewCB`(`SFVM_SELECTIONCHANGED` 通知)接收選取變化——**實作 agent 需要先查證這兩種機制何者在 `IExplorerBrowser` 宿主情境下實際可行、可靠,並在交接區記錄查證結果與最終採用的機制**,不強制規定確切做法,因為這是本專案第一次接觸這個通知路徑,允許實作過程中發現最初設想的機制行不通而改用替代方案,只要最終行為符合驗收即可。
4. **狀態列是每個 pane 卡片底部的固定高度區塊(比照目標畫面樣式,置於檔案清單下方、pane 卡片邊界內),不是視窗全域共用的單一狀態列。** 四宮格版型下有四個獨立的狀態列,各自反映該 pane 目前 active tab 的狀態。
5. **只有 active tab 的狀態列會顯示即時資訊(因為只有 active tab 持有 live `IExplorerBrowser`,`AGENTS.md` 既有規則)。非 active tab 沒有 live view,狀態列在 tab 切換到該 tab、重新 realize 之後才會有數值——這不是本票的缺陷,是既有 realize-on-activation 架構的自然結果,不需要為了「非 active tab 也要顯示項目數」去提前 realize 或另外查詢,那會違反 `AGENTS.md`「Only the visible pane's active tab holds a live IExplorerBrowser」的既有紅線。**

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`AGENTS.md`:
> Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs persist as data and are realized on activation.

`AGENTS.md`:
> App UI text must be English. No Chinese strings ship in the binary.

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers.
(狀態列更新必須掛在既有的 `navigation_complete` 回呼與 Shell 提供的選取變化通知上,不可以用計時器輪詢 `IFolderView2::ItemCount`。)

## Files to read and trace first

- `src/explorer_host/explorer_host.h`/`.cpp`——本票要新增的 `IFolderView2` 查詢方法與選取變化通知的落腳處。
- `src/explorer_host/explorer_host.cpp` 第 266-280 行附近(`Initialize`/`SetOptions`)——確認 `IExplorerBrowser` 的既有初始化流程,新增 `GetCurrentView` 呼叫的正確時機(必須在 `navigation_complete` 之後,view 才存在)。
- `src/app_shell/main.cpp` 的 `draw_pane_card`(第 1271-1333 行)與 `apply_layout`——本票要新增狀態列 HWND/繪製區塊的排版落腳處,狀態列需要佔用 pane 卡片內的一小段固定高度,連動既有的 `pane_rect`/`explorer_containers` 尺寸計算(縮小 Shell view 的可用高度)。
- `docs/design-spec.md` NFR-005——確認狀態列顯示的資訊屬於 best-effort 還是必要狀態(應屬於 UI 呈現,不是需要還原的持久化狀態,不影響 session 還原範圍)。

## Scope

1. `ExplorerHost` 新增查詢目前資料夾項目數與選取數的方法(透過 `IFolderView2`)。
2. `ExplorerHost` 新增選取變化的通知機制(查證後採用可行方案)。
3. `src/app_shell/main.cpp` 新增每個 pane 的狀態列 UI 元件(HWND 或 owner-draw 區塊),顯示項目數/選取數文字,在導覽完成與選取變化時更新。
4. `apply_layout`/pane 矩形計算,扣除狀態列佔用的高度。

## Non-goals

- 不顯示目前檢視模式(view mode)於狀態列——那是 PD-052 的範圍(view mode 切換按鈕本身已經是最直接的檢視模式指示,不重複顯示)。除非實作 agent認為可以用同一次改動輕鬆一併顯示且不增加額外複雜度,否則不強制要求。
- 不對非 active tab 提前 realize 以取得項目數(見已確認的產品決策 5)。
- 不新增計時器輪詢機制。

## Acceptance

1. 每個 pane 卡片底部顯示一列狀態文字,反映該 pane 目前 active tab 的資料夾項目數。
2. 在檔案清單中選取一或多個項目時,狀態列文字即時更新為「已選取 N 個」對應的英文格式。
3. 取消選取後,狀態列文字恢復為純項目數顯示。
4. 切換 tab 或導覽到新資料夾後,狀態列文字正確更新為新位置的項目數。
5. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
6. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "IFolderView2|GetCurrentView|ItemCount" src\explorer_host\explorer_host.cpp src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:導覽到有多個項目的資料夾,確認狀態列項目數正確;
# 選取檔案清單中的項目,確認狀態列即時顯示選取數;
# 本環境已具備螢幕截圖與滑鼠點擊模擬能力,請盡量實際操作驗證選取變化的即時性。
```

## Handoff requirements

- 選取變化通知最終採用的機制(`IShellFolderViewCB`/`SFVM_SELECTIONCHANGED` 或其他),以及查證過程中排除掉的方案與理由。
- 狀態列 UI 元件的實作方式(HWND 子視窗 vs. `paint_client_background` 內的 owner-draw 區塊)與排版佔用的高度數值。
- 真實桌面測試(項目數、選取數即時更新)的實際結果。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 實作交接

2026-08-25

- 選取通知採用 `IShellView` QueryInterface 到 `IShellFolderView` 後呼叫 `SetCallback`，以自有 `IShellFolderViewCB` 接收 undocumented `SFVM_SELECTIONCHANGED`（值 8）；callback 會轉送原 callback，再通知 `ExplorerHost`。`IExplorerBrowserEvents` 只有導覽事件，沒有選取事件；`IFolderView2` 沒有可用的 selection advise，因此未採用輪詢或假設性的 `IFolderView2::Advise`。
- 狀態列採用每 pane 一個原生 `STATIC` child HWND，固定高度 `24px@96dpi`（以 DPI scaling），放在 Shell view 下方；`apply_layout` 從 Explorer container 高度扣除該高度。文字為 `N items` 或 `N of M selected`。
- `IFolderView2::ItemCount(SVGIO_ALLVIEW/SVGIO_SELECTION)` 在導覽完成與選取通知時查詢；非 active tab 沒有提前 realize。
- 驗證：LLVM-MinGW/Clang + Ninja 建置成功，4/4 CTest 通過，Agent `rg` 與 `git diff --check` 通過。GUI 可啟動；本次環境未完成實際滑鼠選取／截圖驗證，因沒有可用的解鎖桌面互動通道。啟動煙霧測試程序 PID 41692 曾以非強制 `taskkill /PID` 重試，該程序未在本次 shell 權限內回報退出，未使用 `/F`。
