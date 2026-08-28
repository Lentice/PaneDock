# PD-102 — Tab「+」新增按鈕加上圓角外框,並修正「+」字符置中

Phase 7 · app_shell · Depends on: PD-081

- Source: 使用者需求(2026-08-28)。
- Origin: 使用者原文:「for pane add button, centered the '+' in the button. make button 外框圓角」;實作前使用者追加更正:「For PD-102 我希望 pane add button 圓框但平常是沒有外框的(框線大小0) 只有 onhover 變色才看的出來」——本票下方決策已依更正版本撰寫,不是原始要求的「永遠可見外框」。
- Priority: LOW——純視覺樣式調整,不影響功能。

## 已確認的現況(有程式碼證據,不是猜測)

`paint_tab_strip`(`main.cpp:2940-2972`)目前繪製「+」新增按鈕的方式:

1. **完全沒有外框**——只有 hover 時用 `FillRect` 填一個方形背景(`:2944-2951`,`RGB(236, 240, 244)`,方角,不是 `RoundRect`);非 hover 時整顆按鈕沒有任何背景/外框,視覺上只是浮在 tab 條背景色上的一個「+」字符。
2. **「+」字符的置中從未被實際驗證過**——`DrawTextW` 使用 `DT_CENTER | DT_VCENTER`(`:2968-2969`),但繪製矩形 `plus_rect` 是先複製 `add`(`:2940`,即 `state.tab_add_rects[pane_index]`),再整體垂直位移 `-1px`(`:2966-2967`,PD-081 加入,commit 訊息「Nudge tab add glyph up 1px」)。根據 `docs/tickets/PD-081-tab-add-button-glyph-notch.md` 自己的交接區記載,那次修改後的實際渲染結果**從未被截圖確認**(唯一一次驗證嘗試因 `GetDlgItem` 找不到 tab strip HWND而失敗)。換句話說,現在這顆按鈕的置中效果,從 PD-081 至今沒有人實際看過。

同一個檔案裡已經有兩個成熟、可直接沿用的圓角外框繪製先例:

- `draw_tab_scroll_button`(`:2780-2826`)——tab 條左右捲動按鈕,**永遠可見**的 `RoundRect` 外框(`RGB(226, 232, 240)`,`scaled_value(window, 1)` 寬),搭配 hover/normal 兩種填色(`RGB(236, 240, 244)` / `RGB(255, 255, 255)`)。
- Tab 本身的繪製(`:2865-2883`)——`radius = scaled_value(window, kTabCornerRadius)`(`kTabCornerRadius = 6`,`:94`)、`border_width = scaled_value(window, 1)`(`:2844`,`paint_tab_strip` 一開始就算好,兩處都直接沿用同一個區域變數,不用另外算)。

## 已確認的產品決策

> **2026-08-28 更正(取代原本的決策 1/4,實作前生效):** 使用者實機看過本票最初依照 `draw_tab_scroll_button` 寫的「外框永遠可見」設計後,明確要求改為**平常(非 hover)完全不畫外框/填色,框線視覺上寬度為 0,只有滑鼠移入(hover)時才透過變色顯示出圓角外框**。使用者原文:「For PD-102 我希望 pane add button 圓框但平常是沒有外框的(框線大小0) 只有 onhover 變色才看的出來」。以下決策 1、4 已依此更正,決策 2、3、5 不受影響。

1. **外框只在 hover 時才可見,非 hover 時完全不畫外框也不填色**——比照本票修改前的既有行為(修改前非 hover 態本來就「無背景/無外框」,只有 hover 時才有方形 `FillRect`),本票只是把「hover 時才顯示」的既有互動語言從方形改成圓角,不是新增一個「永遠可見」的樣式。實作上最小改動的做法是:非 hover 態直接跳過繪製(比照現況,`:2944-2951` 原本就是「hover 才 `FillRect`」的 if 判斷,本票只需把 hover 分支內的繪製從 `FillRect` 換成 `RoundRect` 外框+填色,非 hover 分支維持原樣不畫),不需要用「寬度 0 的筆」這種特例寫法。
2. **外框繪製沿用 `draw_tab_scroll_button` 的既有寫法與色值**(`RoundRect` + `CreatePen(PS_SOLID, border_width, RGB(226, 232, 240))`),`radius`/`border_width` 沿用 `paint_tab_strip` 已經算好的區域變數(`:2843-2844`),不重新計算、不新增函式簽章。**只在 hover 時執行這段繪製**(見決策 1)。
3. **外框/填色的矩形範圍採用現有的 `hover`(即 `add` 內縮 `add_inset` 之後的矩形,`:2941-2943`)而非整個 `add` 熱區矩形**——`add` 是完整點擊熱區(比照 tab 高度),`hover` 才是視覺上「這顆按鈕看起來多大」的既有內縮矩形。沿用既有的 `hover` 矩形當作外框/填色的視覺邊界,而不是另外設計一個新尺寸,是最小改動;點擊熱區(`add`)完全不變,不影響任何既有的 hit-test 邏輯(`:3036`、`:3050`)。
4. **正常態(非 hover)維持無背景、無外框**(修改前既有行為,本票不改變這一點)——hover 態填色沿用既有色值 `RGB(236, 240, 244)`,只是形狀從方形 `FillRect` 改成與外框一致的 `RoundRect`。**不新增「正常態白色填色」**——`draw_tab_scroll_button` 的「正常態也填色」是它自己的既有樣式,不是本票要跟著套用的規則;本票的「+」按鈕平常維持「看不出按鈕邊界,只有一個字符浮在 tab 條背景上」的既有觀感,只有 hover 才「浮現」圓角外框與填色。
5. **「+」字符置中問題,先用本專案既有的 `PrintWindow` 截圖驗證方法實際確認目前效果,再決定是否保留/移除/調整 PD-081 加入的 `-1px` 垂直位移。** 不得未經視覺驗證就假設現狀是對的或錯的——這正是使用者回報的問題本身。若截圖顯示置中後仍有偏移,調整 `OffsetRect` 的位移量或直接移除;若截圖顯示已經置中(`-1px` nudge 恰好抵銷字型 baseline 的視覺偏移),保留不動並在交接區記錄佐證截圖。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

外框繪製沿用 `draw_tab_scroll_button` 的既有 `RoundRect`/`CreatePen` 寫法與 `paint_tab_strip` 已算好的 `radius`/`border_width` 變數,不新增繪製函式或參數。

`docs/tickets/PD-081-tab-add-button-glyph-notch.md`(本票沿用其字型字符渲染方式,不覆寫):
> 由手繪改為 `DrawTextW` 字型字符渲染,解決十字交叉處的渲染缺口。

本票只調整外框與置中位移量,不改變 PD-081 已確立的「用字型字符畫『+』」這個渲染方式本身。

## Files to read and trace first

- `src/app_shell/main.cpp:2940-2972`——「+」按鈕目前的填色與文字繪製邏輯,本票主要修改處。
- `src/app_shell/main.cpp:2780-2826`(`draw_tab_scroll_button`)——外框繪製直接沿用的既有寫法。
- `src/app_shell/main.cpp:2843-2844`——`paint_tab_strip` 已算好的 `radius`/`border_width` 區域變數,外框繪製直接引用,不重算。
- `src/app_shell/main.cpp:2966-2967`——PD-081 加入的 `-1px` 垂直位移,置中驗證與可能調整處。
- `docs/tickets/PD-081-tab-add-button-glyph-notch.md`——交接區記載的「從未截圖驗證」限制,直接沿用其對「+」字符渲染的既有結論(字型字符本身沒問題,只是位置/外框未驗證)。

## Scope

1. Hover 態的填色從方形 `FillRect` 改為圓角 `RoundRect`(填色 + 外框),沿用 `draw_tab_scroll_button` 的色值與寫法(填色 `RGB(236, 240, 244)`、外框 `RGB(226, 232, 240)`)。
2. 正常態(非 hover)維持修改前的既有行為——不畫任何背景或外框,只有「+」字符本身。
3. 「+」字符的垂直位移量,經截圖驗證後保留、調整或移除。

## Non-goals

- 不改變「+」按鈕的點擊熱區(`add`/`state.tab_add_rects`)大小或位置。
- 不改變「+」按鈕觸發的行為(新增分頁)。
- 不改變字型字符本身的渲染方式(PD-081 已確立,不重寫成手繪)。
- 不改變 tab 本身、tab 捲動按鈕的既有樣式(本票只動「+」按鈕)。
- 不新增 disabled 狀態(「+」按鈕目前沒有 disabled 態,本票不新增)。

## Acceptance Criteria

1. 「+」按鈕正常態(非 hover)看不到任何外框或背景,只有「+」字符本身;只有滑鼠移入(hover)時才顯示圓角外框與填色。
2. Hover 態外框的圓角視覺風格與 tab 本身、tab 捲動按鈕放大截圖並排比對一致(同一套視覺語言,不是自創樣式),但**只在 hover 時出現**這一點與 tab 本身/tab 捲動按鈕(兩者外框永遠可見)不同,這是使用者對「+」按鈕的明確要求,不是不一致的缺陷。
3. 「+」字符在放大截圖中確認置中於按鈕可視範圍內(水平與垂直皆置中,不偏移)。
4. 「+」按鈕的點擊行為(新增分頁)與熱區範圍與修改前完全相同。
5. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
6. `git diff --check` 通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "tab_add_rects|plus_rect|draw_tab_scroll_button|kTabCornerRadius" src\app_shell\main.cpp
git diff --check
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,放大用 `InterpolationMode.NearestNeighbor`,單次啟動 + 單次截圖即可完成驗證,不需要連續互動。**務必先找到正確的 tab strip 子視窗 HWND 再截圖**——PD-081 上一次驗證失敗正是因為 `GetDlgItem(main, 200)` 找錯視窗,本票必須避免重蹈覆轍(可用 `EnumChildWindows` 列舉並比對視窗類別名稱/位置來定位正確的 tab strip HWND)。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 外框/填色最終採用的色值與矩形範圍(確認沿用 `hover` 變數或有調整,並說明理由)。
- 「+」字符置中的實際截圖驗證結果,以及垂直位移量的最終決定(保留 `-1px`/調整/移除)與依據。
- 放大截圖比對結果(新外框 vs. tab 本身/tab 捲動按鈕的視覺一致性)。
- 本次是否成功定位到正確的 tab strip HWND 並完成截圖(若沿用 PD-081 失敗的方法,記錄改用的定位方式)。
- 未驗證項目與原因(若有)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-28 實作交接

- hover 外框/填色使用既有 `hover` 矩形：由 `add` 以 `scaled_value(window, 5)` 內縮；`add` 熱區本身沒有改動。hover 填色為 `RGB(236, 240, 244)`，外框為 `RGB(226, 232, 240)`，並沿用 `paint_tab_strip` 已算好的 `radius` 與 `border_width`，以 `RoundRect` 繪製。非 hover 分支完全不繪製背景或外框。
- 實際以 Release diagnostic 啟動後，用 `EnumChildWindows` 找到 `Static`、Id 200 的 tab strip，矩形為 `[242,83,1069,114]`；以主視窗 `PrintWindow(hwnd, hdc, 2)` 取得畫面，再依該子視窗矩形精確裁切並以 4x `InterpolationMode.NearestNeighbor` 放大。現有 `-1px` 版本的放大結果顯示「+」筆畫中心仍比 add 可視矩形低約 1px；改為 `-2px` 後，筆畫中心落在矩形中心約半像素內，因此最終保留字型 `DrawTextW`，將位移調整為 `-scaled_value(window, 2)`。最終截圖輸出於 `C:\Users\lenticetsai\AppData\Local\Temp\panedock-pd102-final-hover-strip-4x.png`。
- 放大畫面確認 tab 本身與 tab 捲動按鈕使用相同的圓角/細框視覺語言；最終 PrintWindow 畫面為非 hover 狀態，因此 add 按鈕沒有常態外框，符合更正後設計。使用者另提供的 hover 截圖可看到 hover 填色與外框；本次程式碼也維持與既有 scroll button 相同的色值與 `RoundRect` 路徑。
- 已成功用 `EnumChildWindows` 依 class/位置定位正確 tab strip，沒有使用 PD-081 失敗的 `GetDlgItem` 查找方式；主視窗 `PrintWindow` 成功，子視窗直接 `PrintWindow` 不作為依據。測試只讀取既有 session，沒有建立 throwaway group/tab，因此不需還原 session backup。
- 未以滑鼠點擊新增分頁，以免改動使用者 session；`WM_LBUTTONDOWN` 的 `add` hit-test 區域未修改。未另外覆蓋 150% DPI 或一般模式互動流程；本票的實機截圖確認與 Release build/CTest 已完成。
