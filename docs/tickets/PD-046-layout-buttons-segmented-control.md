# PD-046 — 版面配置按鈕群改為視覺相連的分段控制(segmented control)

Phase 6 · app_shell · Depends on: PD-029, PD-039

- Source: 使用者實機操作 `.\build\PaneDock.exe` 後回報,附截圖(2026-08-25)。
- Origin: 「右上角 layout 按鈕應該連在一起,並且需要有 win32 tooltip」。
- Priority: LOW——純視覺,不影響功能。

## 現況(有程式碼證據)

`src/app_shell/main.cpp` 的 `layout_header`(第 1039 行起)目前用固定 `gap`(96-DPI 基準 4px)把 5 個版面配置按鈕逐一排開(第 1074-1081 行,`x += button_width + gap`),每個按鈕是獨立的 `BS_AUTORADIOBUTTON | BS_OWNERDRAW` `BUTTON` 控制項,視覺上是 5 個彼此分開的方塊,不是連在一起的分段控制(segmented control)。

**Tooltip 現況查證:** PD-039 已經在 `WM_CREATE` 建立 `state->layout_tooltip`(`TOOLTIPS_CLASSW`,`TTS_ALWAYSTIP`),對 5 個版面配置按鈕與 more-actions 按鈕都用 `TTF_IDISHWND | TTF_SUBCLASS` 註冊了 `TOOLINFOW`(第 2391-2421 行)。**程式碼證據顯示 tooltip 機制已經存在**,但因為本專案環境一直沒有 Computer Use 或其他互動能力可以實際懸停滑鼠驗證,PD-039 當時只做到非互動煙霧測試,沒有人真正確認過懸停後 tooltip 會不會顯示。使用者這次的回報可能代表:(a) tooltip 其實沒有真的顯示,PD-039 的實作有本票要抓出來的 bug;或 (b) tooltip 目前是好的,使用者只是在重申需求、沒有特別測試懸停。**本票的 Scope 明確包含「確認 tooltip 在真人懸停下確實會顯示」這一項,不可以只憑程式碼存在就假設沒問題。**

## 已確認的產品決策

1. **5 個版面配置按鈕改為視覺上相連的分段控制:去掉按鈕之間的 `gap`(改為 0 或 1px 分隔線),整個按鈕群外圍加一個共用的圓角外框(比照 `draw_navigation_bar_background`/`kAddressBarBackgroundRadius` 的圓角視覺語言),按鈕之間用 1px 淺灰分隔線區隔(而不是留白 gap)。** more-actions 佔位按鈕維持現狀,**不**併入這個分段控制群組(它在設計稿裡本來就是獨立於版面配置按鈕群、右側單獨的圖示按鈕,見 PD-029 決策)。
2. **視覺上的「相連」透過在 `layout_header` 排版計算把 `gap` 從 4px 改為 1px(分隔線寬度),並新增一個背景繪製函式(比照 `draw_navigation_bar_background`)在 5 個按鈕的共同外框畫一個圓角矩形背景,按鈕本身的 `WM_DRAWITEM`(`draw_layout_button`)背景改為透明或跟外框背景同色,讓外框的圓角在群組左右兩端可見,中間按鈕不需要各自畫圓角。** 具體做法(讓群組外框圓角只出現在最左/最右按鈕的外側)可比照 `draw_navigation_bar_background` 的圓角繪製方式:先畫一個涵蓋全部 5 個按鈕範圍的圓角矩形背景,再讓每個按鈕的 `WM_DRAWITEM` 只在自己選中/hover 狀態時畫一個較小的內縮反白,不重複畫外框。
3. **每個按鈕仍然是獨立的 `BUTTON` HWND(不合併成單一自繪控制項),因為 PD-039 的 tooltip 註冊、既有的 `BM_SETCHECK`/`EnableWindow`/`WM_COMMAND` 邏輯全部是按 HWND 走的,合併成單一控制項會需要重寫這些既有機制,超出「純視覺相連」這個需求的範圍(YAGNI)。** 只改變排版間距與背景繪製方式,不改變控制項架構。
4. **確認 tooltip 在真人懸停下確實顯示,若發現有 bug 就地修正(不另開票)。** 如果視覺相連的改動導致按鈕的可視範圍或 z-order 有變化而影響 tooltip 的 `TTF_IDISHWND` 命中判定,一併在本票修正。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Don't add features, refactor, or introduce abstractions beyond what the task requires.

`docs/tickets/PD-039-layout-label-removal-and-tooltip.md`(既有 tooltip 註冊方式,本票延用,不重寫):
> 建立一個 tooltip 控制項(`CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, ...)`),對 5 個 `state->layout_buttons` 與 `state->more_actions_button` 各自 `TTM_ADDTOOLW` 註冊對應文字。

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `layout_header`(第 1039-1097 行)——按鈕排版計算,本票要修改 `gap` 與新增外框背景繪製的呼叫位置。
- `src/app_shell/main.cpp` 的 `draw_layout_button`(第 542 行起)——確認目前每個按鈕的背景/選中狀態繪製方式,改動時要保留選中態(`BST_CHECKED`)的視覺區分,只調整外框/間距部分。
- `src/app_shell/main.cpp` 的 `draw_navigation_bar_background`(第 1292 行起)——可參考的既有圓角背景繪製手法。
- `src/app_shell/main.cpp` 第 2391-2421 行(PD-039 的 tooltip 註冊)——確認目前寫法,本票不重寫註冊邏輯,只在真人測試後視情況修正 bug。

## Scope

1. `layout_header` 的按鈕間距從 4px gap 改為分隔線寬度(建議 1px),整體按鈕群外圍加一個共用圓角背景(仿 `draw_navigation_bar_background`)。
2. `draw_layout_button` 調整背景繪製,配合新的分段控制視覺(選中態/hover 態的視覺區分方式可依實作 agent 判斷,只要與相連的外觀協調)。
3. 驗證(依決策 4)tooltip 在懸停下確實顯示;若發現問題,就地修正 PD-039 遺留的程式碼。

## Non-goals

- 不把 5 個按鈕合併成單一自繪控制項(已確認的產品決策 3)。
- 不改變 more-actions 佔位按鈕的獨立性或位置。
- 不改變版面配置切換的既有邏輯(`BM_SETCHECK`/`WM_COMMAND`/`set_layout` 等)。

## Acceptance

1. 5 個版面配置按鈕視覺上呈現一個相連的分段控制(共用外框圓角,按鈕之間是細分隔線而非留白間隙),more-actions 按鈕維持獨立在旁邊。
2. 選中的版面配置在分段控制內有清楚的視覺區分(比照現有 `BST_CHECKED` 高亮邏輯,調整成適合相連外觀的呈現方式)。
3. **真人懸停驗證**:滑鼠停留在任一版面配置按鈕或 more-actions 按鈕上,確實顯示對應的 tooltip 文字(這是本票的關鍵驗收項,不能只靠程式碼審查判斷完成)。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "layout_header|draw_layout_button" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動(關鍵驗收,必須完成,不能只跑 build/test 就宣稱完成):
# 目視確認 5 個版面配置按鈕視覺相連;
# 滑鼠懸停在每個按鈕上確認 tooltip 實際顯示;
# 點擊切換各種版面配置,確認選中狀態視覺正確、既有功能不受影響
```

## Handoff requirements

- 最終採用的視覺相連實作方式(分隔線寬度、外框圓角半徑、選中態呈現方式)。
- **Tooltip 真人懸停驗證的實際結果**:確實看到顯示,還是發現了 bug——若有 bug,記錄根因與修正方式。若本次環境仍然沒有互動能力可以驗證,誠實記錄這個限制,不要宣稱驗證通過。
- 若真實桌面測試發現分段控制的視覺與設計稿(`docs/panedock-ui-prototype.html`)有落差,記錄下來並說明因應方式。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 實作交接

2026-08-25：完成版面配置按鈕的 segmented control 視覺。五個獨立按鈕以 1px 分隔線相連，群組共用 `RGB(251,252,253)` 圓角背景與 `kAddressBarBackgroundRadius` 的 DPI-scaled 外框；按鈕內容內縮 1px，保留 `BST_CHECKED` 的藍色選中態。more-actions 維持獨立按鈕與原本 4px 間距，沒有改動切換邏輯或 HWND 架構。

Tooltip 註冊檢查：PD-039 的五個版面配置按鈕與 more-actions `TTF_IDISHWND | TTF_SUBCLASS` 註冊仍完整；本環境無法取得互動桌面控制能力，未能真人懸停觀察 tooltip，因此只完成程式碼與非互動 smoke/build/test 驗證，沒有宣稱真人驗收通過。

非互動 smoke check 可成功啟動 `build\\PaneDock.exe` 並以 `taskkill /PID` 優雅關閉；CMake/Ninja build 與 4 個 ctest 全數通過。因互動能力不可用，未能比較實機畫面與 `docs/panedock-ui-prototype.html` 的視覺差異。
