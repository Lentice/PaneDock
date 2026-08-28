# PD-105 — 雙擊 pane 分隔線,將該分隔線重設回置中(平分兩側)

Phase 7 · app_shell · Depends on: 無

- Source: 使用者需求(2026-08-28)。
- Origin: 使用者原文:「for panes 分隔線,在上面按兩下會讓分隔線回到中央(平分兩邊)。這樣使用者可以方便的回到平分的狀態,不用自己量測判斷增加困擾。」
- Priority: LOW——純易用性擴充,不影響既有拖曳行為。

## 已確認的現況(有程式碼證據,不是猜測)

主視窗類別註冊時完全沒有設定 `CS_DBLCLKS`(`main.cpp:3996-4006`,`WNDCLASSEXW window_class{}` 從未指定 `.style`)。這是 Windows 訊息系統的既有規則:視窗類別沒有 `CS_DBLCLKS`,系統就**不會**合成 `WM_LBUTTONDBLCLK` 訊息——目前在分隔線上快速點兩下,實際收到的只是兩組獨立的 `WM_LBUTTONDOWN`/`WM_LBUTTONUP`,各自被既有的單擊拖曳邏輯處理(等同兩次「點一下不移動就放開」,視覺上什麼都沒發生),`window_proc` 裡也完全沒有 `WM_LBUTTONDBLCLK` 的 case。這就是雙擊目前沒有任何效果的根本原因。

既有分隔線拖曳邏輯(`main.cpp:3856-3883`)已確立的樣式:

```cpp
case WM_LBUTTONDOWN:
    if (state != nullptr && has_active_group(*state)) {
        state->splitter_drag = splitter_at_point(
            window, active_group(*state), point_from_lparam(lparam));
        if (state->splitter_drag.has_value()) {
            SetCapture(window);
            return 0;
        }
    }
    break;
...
case WM_LBUTTONUP:
    if (state != nullptr && state->splitter_drag.has_value()) {
        update_splitter_drag(window, *state, point_from_lparam(lparam), true);
        state->splitter_drag.reset();
        save_now(*state);
        ReleaseCapture();
        return 0;
    }
    break;
```

「置中」的目標值不需要另外計算或猜測——`core::default_divider_ratios`(`model.cpp:54-56`)已經是現成的正確答案:

```cpp
std::vector<double> default_divider_ratios(LayoutTemplate layout_template) {
    return std::vector<double>(divider_ratio_count(layout_template), 0.5);
}
```

每一種版型、每一條分隔線的預設比例都是 `0.5`(平分兩側),沒有例外——這是新建 Group 時套用的既有初始值(`model.cpp:238`),雙擊要恢復的正是這個值,不是另外定義的新概念。

`apply_layout` 的既有簽章(`main.cpp:1914-1916`):

```cpp
HRESULT apply_layout(HWND window, AppState& state,
                     bool realize_deferred_panes = false,
                     bool recompute_content = true) {
```

`WM_LBUTTONUP` 透過 `update_splitter_drag(..., true)` 最終呼叫的是 `apply_layout(window, state, false, true)`(`main.cpp:2571`)——即帶預設參數的兩參數呼叫 `apply_layout(window, state)`。雙擊重設是低頻率的明確操作(不是逐幀拖曳),直接呼叫兩參數版本 `apply_layout(window, *state)` 即可達到與放開滑鼠時完全相同的「完整重排 + 內容重算」語意,不需要额外參數。

## 已確認的產品決策

1. **雙擊只重設「被點中的那一條」分隔線,不是整個 Group 的所有分隔線。** `three_pane`/`four_pane_grid` 有兩條分隔線(`divider_ratio_count`,`model.cpp:43-51`),`splitter_at_point` 回傳的 `Splitter` 已經帶有精確的 `ratio_index`(`main.cpp:381-385`),只重設命中的那一個索引,與拖曳邏輯已經是「只動被拖的那一條」保持一致的互動語言。
2. **重設動作是瞬間完成的操作,不建立新的拖曳狀態機或計時器。** 不需要 `SetCapture`/`ReleaseCapture`(雙擊不是拖曳),`WM_LBUTTONDBLCLK` case 直接讀值、寫回、`apply_layout`、`save_now`,一次處理完畢。
3. **`CS_DBLCLKS` 加在主視窗類別上,範圍是整個主視窗客戶區,不會影響任何子控制項的雙擊行為。** 側邊欄 `LISTBOX`(`group_list_proc`)、tab 條(`tab_strip_proc`)、版面配置按鈕(`layout_button_proc`)都是各自獨立的子視窗、各自的視窗類別,`CS_DBLCLKS` 只影響用 `kWindowClassName` 這個類別建立的視窗(即主視窗本身),不會外溢到這些子控制項或真正的 Shell 檔案列表(`IExplorerBrowser` 的原生視窗,類別完全不同)。
4. **持久化寫入方式比照既有 `WM_LBUTTONUP` 放開滑鼠的既有寫法,呼叫 `save_now(*state)`**(`main.cpp:3879` 既有寫法),不是 PD-091 的防抖——雙擊跟放開滑鼠一樣是「使用者這個動作本身就是最終結果」的明確操作終點。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

置中目標值直接沿用 `core::default_divider_ratios`/新建 Group 的既有預設值 `0.5`,不新增計算邏輯;重設後的重排與存檔直接沿用既有 `apply_layout`/`save_now` 呼叫方式,不新增輔助函式。

## Files to read and trace first

- `src/app_shell/main.cpp:3996-4006`(`register_window_class`)——`CS_DBLCLKS` 的加入處。
- `src/app_shell/main.cpp:3856-3883`(`WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP` 既有分隔線拖曳三段)——新 `WM_LBUTTONDBLCLK` case 的插入處(建議緊接在 `WM_LBUTTONUP` 之後、`WM_TIMER` 之前)與命中測試/資料寫回的既有寫法比照對象。
- `src/app_shell/main.cpp:1043-1049`(`splitter_at_point`)——命中測試直接複用,不改寫。
- `src/core/model.cpp:43-56`(`divider_ratio_count`/`default_divider_ratios`)——置中目標值 `0.5` 的既有來源與依據。
- `src/app_shell/main.cpp:1914-1916`(`apply_layout` 簽章)——確認兩參數呼叫的預設語意與 `update_splitter_drag` 最終呼叫的四參數版本等價。

## Scope

1. 主視窗類別加上 `CS_DBLCLKS` 樣式。
2. 新增 `WM_LBUTTONDBLCLK` 訊息處理:命中某條分隔線時,將該分隔線的 `divider_ratios[ratio_index]` 設回 `0.5`,呼叫 `apply_layout(window, *state)` 完整重排,再呼叫 `save_now(*state)` 立即持久化。
3. 雙擊落在分隔線以外(其餘客戶區、子控制項)時,行為與修改前完全相同(不吃掉該事件,`break` 讓系統/預設處理繼續)。

## Non-goals

- 不改變單擊拖曳分隔線的既有行為與節流狀態(`PD-097` 若已落地,拖曳期間的節流機制不受本票影響——雙擊是全新的獨立訊息分支,不經過拖曳/節流路徑)。
- 不新增「雙擊重設全部分隔線」的批次操作——只重設被點中的那一條。
- 不影響側邊欄寬度調整(`PD-104`,若已落地)的拖曳邊界——那是不同的分隔線集合,`splitter_at_point`/`divider_ratios` 目前只涵盖 pane 分隔線,本票範圍與其互不重疊。
- 不新增使用者可設定的「雙擊間隔時間」——沿用系統既有的 `GetDoubleClickTime` 預設值(`CS_DBLCLKS` 本身就是靠系統設定判斷,不需要應用程式自行量測)。

## Acceptance Criteria

1. 在任一非 `single` 版型下拖曳分隔線使其偏離中央後,在該分隔線上快速點兩下,分隔線立即回到平分兩側的位置。
2. `three_pane`/`four_pane_grid` 版型下,雙擊其中一條分隔線只重設該條,另一條分隔線的位置不變。
3. 雙擊重設後關閉並重新啟動應用程式,分隔線位置維持在重設後的置中狀態(已持久化)。
4. 在分隔線以外的客戶區雙擊,不觸發任何重設,且不影響該處原本的行為(若原本無行為,雙擊後依然無行為)。
5. 側邊欄 Group 清單、tab 條、版面配置按鈕等子控制項上的雙擊/連續點擊行為與修改前完全相同,不受主視窗新增 `CS_DBLCLKS` 影響。
6. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
7. `git diff --check` 通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "CS_DBLCLKS|WM_LBUTTONDBLCLK|splitter_at_point|default_divider_ratios" src\app_shell\main.cpp src\core\model.cpp
git diff --check
```

**驗證原則(本專案共同約定):只做單次點擊/雙擊 + 截圖的驗證由 Agent 或本人執行**,截圖驗證方法沿用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`。**驗證時務必立即釋放 computer use、盡量縮短測試內容**——完成必要的單次雙擊+截圖確認後立刻用不帶 `/F` 的 `taskkill /PID <pid>` 關閉測試實例,不要讓測試佔用電腦。若某項驗收條件無法用單次動作完成(例如需要先手動拖曳偏移再雙擊的多步驟流程),如實在交接區標記未驗證並說明需要使用者手動驗證的具體步驟。

## Handoff requirements

- `WM_LBUTTONDBLCLK` case 的實際插入位置。
- 雙擊重設的實際截圖驗證結果(拖偏後雙擊、重設後截圖比對)。
- `three_pane`/`four_pane_grid` 兩條分隔線各自獨立重設的驗證結果。
- 跨啟動持久化的驗證結果。
- 未驗證項目與原因(若有)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

- 實作位置：`src/app_shell/main.cpp:3922`，新 `WM_LBUTTONDBLCLK` case 緊接既有 `WM_LBUTTONUP` splitter block、位於 `WM_TIMER` 前；主視窗類別於 `register_window_class` 加入 `CS_DBLCLKS`（目前約 `:4050`）。命中沿用 `splitter_at_point`，只將命中的 `divider_ratios[ratio_index]` 設為 `0.5`，呼叫 `apply_layout(window, *state)` 與 `save_now(*state)`；未命中保留 `break`。
- 單次實機驗證：Release `build/PaneDock.exe` 啟動 PID `11188`，四宮格先將垂直分隔線由中央拖至約 `x=900`，再在該位置雙擊一次；雙擊後畫面觀察到分隔線回到中央。以 native `PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT)`（flag `2`）擷取並檢視成功，輸出：`C:\Users\lenticetsai\AppData\Local\Temp\panedock-pd105-printwindow.bmp`。截圖後立即執行不帶 `/F` 的 `taskkill /PID 11188`；sandbox 初次回報 Access denied，隨即在 elevated context 重送成功，程序已退出。
- 已驗證：`cmake --build build` 通過；`ctest --test-dir build --output-on-failure` 的 5/5 測試通過；`git diff --check` 通過。
- 未驗證、留給使用者：跨重啟持久化；`three_pane` 與 `four_pane_grid` 的兩條分隔線各自獨立重設（本次只測四宮格垂直分隔線）；分隔線外雙擊不被吞掉；側邊欄、tab 條、版面配置按鈕等子控制項的雙擊回歸。請使用者依 Acceptance Criteria 2–5 手動補測。
