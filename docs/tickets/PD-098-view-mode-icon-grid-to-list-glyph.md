# PD-098 — 檢視模式按鈕圖示由 4 方格(GridView)改為清單樣式(List)圖示

Phase 7 · app_shell · Depends on: PD-075

- Source: 使用者需求(2026-08-28),附兩張參考圖:目前圖示(2×2 灰色方格,GridView 樣式)與目標圖示(三條橫線,每條左側各有一個小方塊,清單/List 樣式)。
- Origin: 使用者原文:「change view size icon from [4 方格圖] to [清單圖]. should follow nav buttons styles.」
- Priority: LOW——純圖示樣式調整,不影響功能。

## 背景與現況

每個 pane 導覽列的檢視模式按鈕(`kViewModeButtonIdBase`,`main.cpp:129`)與另外四個導覽按鈕(上一頁/下一頁/上層/重新整理)共用同一套繪製函式 `draw_navigation_icon_button`(`main.cpp:840-869`),按 `glyph_kind` 參數(0-4)索引:

- 主要路徑:`kNavigationGlyphs`(`main.cpp:114-115`)是 `Segoe MDL2 Assets` 字型的 5 個字元碼,檢視模式對應索引 4、目前值 `L'U+E80A'`——這是 MDL2 的 **GridView**(2×2 方格)字符,也就是使用者截圖中目前的圖示。
- Fallback 路徑(字型不可用時):`draw_navigation_fallback_glyph`(`main.cpp:782-838`)的 `case 4`(`:824-831`)手繪「四個小方塊」,視覺上與 GridView 字符一致。

使用者要求把這個圖示換成清單樣式(三條橫線,每條左側各有一個小方塊/項目符號),並要求新圖示「follow nav buttons styles」——沿用現有四個導覽按鈕的繪製機制與視覺規格,不要另外發明新的繪製路徑或不同的尺寸/顏色邏輯。

檢視模式的下拉選單(`TrackPopupMenu`,`main.cpp:2487`/`3741`)是純文字原生選單,沒有 `MIIM_BITMAP`/icon bitmap,不在本票範圍內——本票只改變**觸發下拉選單的那顆按鈕本身**的圖示。

## Fix 方向

1. `kNavigationGlyphs[4]`(`main.cpp:115`)的字元碼由 `U+E80A`(GridView)改成清單樣式的 `Segoe MDL2 Assets` 字符,候選為 `U+E71D`(MDL2 的 **List** 字符:三條橫線各配一個項目符號,是微軟官方圖示集裡最接近使用者截圖描述的字符)——實作者須用本專案既有截圖驗證流程(`PrintWindow` + 放大比對)實際核對渲染結果與使用者提供的目標圖示是否一致,若 `U+E71D` 渲染出來不吻合,換一個更接近的 MDL2 字符(例如同集合裡其他 List/BulletedList 變體),並在交接區記錄最終選用的字元碼與比對依據。
2. `draw_navigation_fallback_glyph` 的 `case 4`(`main.cpp:824-831`)手繪邏輯,由「四個小方塊」改成對應的清單樣式(例如三條等距橫線,可選擇是否加上左側小方塊/項目符號以貼近目標圖示),沿用同一個 `case` 分支、同一組 `color`/`pen_width`/`half` 變數,不新增參數或函式簽章變化。
3. 圖示本身的繪製機制、尺寸(`kNavigationGlyphSize`)、顏色(`disabled`/一般態的 `color` 計算)、hover 背景(`draw_navigation_icon_button` 的 `hovered` 分支)全部沿用不變——這正是使用者「should follow nav buttons styles」的要求,本票不得為這一顆按鈕加特殊邏輯。

## 綁定限制(引用)

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

只需要換一個字元碼常數與一段 fallback 手繪邏輯,不需要新的圖示子系統。

`docs/tickets/PD-075-unify-pane-chrome-icon-style.md` 的既有決策(本票沿用,不覆寫):
> 統一到 `Segoe MDL2 Assets` 字型字符……那個學費不要付第二次。

本票新圖示必須維持「字型字符為主、手繪為 fallback」的既有雙軌設計,不倒退回純手繪。

## 檔案與範圍

- `src/app_shell/main.cpp`:
  - `kNavigationGlyphs`(`:114-115`)——索引 4 的字元碼修改處。
  - `draw_navigation_fallback_glyph` 的 `case 4`(`:824-831`)——fallback 手繪圖形修改處。

## Scope

1. 檢視模式按鈕的主要圖示(字型字符)由 GridView(`U+E80A`)改為清單樣式字符。
2. 對應的 fallback 手繪圖形(字型不可用時)同步改為清單樣式,不再畫四方格。
3. 圖示的尺寸、顏色、hover/disabled 視覺狀態、按鈕本身的點擊行為(開啟下拉選單)完全不變。

## Non-goals

- 不改變檢視模式下拉選單(`TrackPopupMenu`)本身的文字項目或加入圖示。
- 不改變其餘四個導覽按鈕(上一頁/下一頁/上層/重新整理)的圖示。
- 不改變按鈕的尺寸、位置或點擊/鍵盤行為。
- 不新增圖示資源檔或點陣圖——維持既有的字型字符 + GDI 手繪雙軌做法。

## Acceptance Criteria

1. 四個 pane 的檢視模式按鈕圖示均改為清單樣式,不再顯示 2×2 方格。
2. 新圖示與其餘四個導覽按鈕的視覺風格一致(同尺寸、同顏色邏輯、同 hover 背景),放大截圖比對可確認風格統一。
3. 新圖示的實際渲染結果經過使用者提供的目標圖示比對確認吻合(或記錄選用的最接近字符與比對結果)。
4. Fallback 路徑(可用臨時停用/清空圖示字型的方式驗證,或程式碼審查確認)不再繪出四方格。
5. disabled/hover 狀態不受影響。
6. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
7. `git diff --check` 通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kNavigationGlyphs|draw_navigation_fallback_glyph|E80A|E71D" src\app_shell\main.cpp
git diff --check
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,放大用 `InterpolationMode.NearestNeighbor`,單次啟動+單次截圖即可完成驗證,不需要連續互動。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 最終選用的 `Segoe MDL2 Assets` 字元碼與選用理由(與使用者目標圖示的比對依據)。
- Fallback 手繪圖形的最終樣式描述。
- 放大截圖比對結果(新圖示 vs. 其餘導覽按鈕風格一致性)。
- 未驗證項目與原因(若有)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
