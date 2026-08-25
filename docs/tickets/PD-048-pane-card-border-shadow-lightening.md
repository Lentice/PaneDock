# PD-048 — Pane 卡片外框/陰影顏色偏重,需比照目標畫面調淡

Phase 6 · app_shell · Depends on: PD-045

- Source: 使用者比對 `docs/panedock-ui-demo-01-refined-quiet-header.html` 目標畫面與實機截圖後回報(2026-08-25)。
- Origin: 「每個 pane 按鈕與 address 列的樣式要跟目標類似,下方的檔案區框線要像目標那樣淡淡的不是黑色那麼明顯」。
- Priority: LOW——純視覺對比度調整,不影響功能。

## 已確認的根因(有程式碼證據,不是猜測)

1. **「pane 按鈕與 address 列的樣式」已經由 PD-031/PD-043/PD-044 完成**(圖示化導覽按鈕、圓角網址欄背景、子資料夾自動完成),不是本票範圍。使用者這句話裡真正還有落差的是後半句「下方的檔案區框線」。
2. **`draw_pane_card`(`src/app_shell/main.cpp` 第 1271-1333 行)的外框與陰影顏色比目標畫面深:**
   ```cpp
   HBRUSH shadow_brush = CreateSolidBrush(RGB(205, 211, 219));   // 第 1297 行,陰影
   ...
   const COLORREF border_color =
       is_active ? RGB(37, 99, 235) : RGB(223, 229, 236);        // 第 1321-1322 行,外框
   ```
   inactive 外框 `RGB(223,229,236)` 本身數值上不算深,但陰影 `RGB(205,211,219)` 偏灰且不透明(`draw_pane_card` 決策 1 明確排除 `AlphaBlend`/`GradientFill`,是實心 offset RoundRect),兩者相鄰時視覺上疊加成一圈比目標畫面「幾乎看不見」的邊框更明顯的灰邊,尤其在 4 宮格版型下每個 pane 之間的間距較窄,鄰接的陰影/外框視覺上容易連成一片,讓使用者感覺「框線很重、接近黑色」。
   Active pane 的 2px 藍色外框(`RGB(37,99,235)`)是刻意的強調色(PD-033 決策),不是本票要調淡的對象——使用者原文「下方的檔案區框線」是相對 inactive pane、以及卡片與檔案清單交界處的觀感描述,不是要求拿掉 active 指示。

## 已確認的產品決策

1. **只調整 inactive pane 的外框顏色與陰影顏色,兩者都調淡(數值更接近白/淺灰,例如外框改用比目前更淺的中性灰、陰影改用更淺且可考慮縮小 offset 或降低不透明視覺重量)**,不改變 active pane 的藍色強調外框(PD-033 決策維持不變)。
2. **陰影不引入 `AlphaBlend` 半透明繪製**(延續 `draw_pane_card` 現有決策 1:「no AlphaBlend/GradientFill」),只透過選用更淺的純色達到「看起來很淡」的視覺效果,維持最小改動與既有繪製手法一致。
3. **`border_width`(1px inactive / 2px active,第 1319-1320 行)維持不變**,本票只調顏色不調粗細——粗細已經在 PD-045 修正過上下一致性,不應該在本票裡再動。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`src/app_shell/main.cpp`(`draw_pane_card` 既有註解,本票延續其繪製手法,不重寫):
> Card visuals: white body, shadow one step darker (product decision 3)…Shadow is a flat offset RoundRect, not a real blur (product decision 1 — no AlphaBlend/GradientFill).

`docs/tickets/PD-033-active-pane-flat-border-indicator.md`(active pane 的藍色外框,本票不改動):
> Active pane 指示改為扁平彩色外框。

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `draw_pane_card`(第 1271-1333 行)——本票要修改的顏色常數所在。
- `docs/panedock-ui-demo-01-refined-quiet-header.html`——目標畫面的 `.pane` 卡片邊框/陰影實際 CSS 色值,作為調色參考基準(可用瀏覽器或直接讀 HTML/CSS 原始碼取得確切色碼)。
- `docs/tickets/PD-045-pane-card-bottom-border-clipped.md` 交接區——確認外框粗細一致性的既有修正,本票不動粗細只動顏色。

## Scope

1. `draw_pane_card` 的 `shadow_brush` 顏色(第 1297 行)調淡。
2. `draw_pane_card` 的 inactive `border_color`(第 1322 行)調淡。

## Non-goals

- 不改變 active pane 的藍色外框顏色或粗細(PD-033 決策維持)。
- 不改變外框粗細(`border_width`,PD-045 已修正一致性)。
- 不引入半透明/漸層繪製手法。
- 不改動 pane 按鈕、address 列樣式(已由 PD-031/043/044 完成)。

## Acceptance

1. Inactive pane 的外框與陰影視覺上明顯比目前更淡,與目標畫面 `docs/panedock-ui-demo-01-refined-quiet-header.html` 的觀感接近(接近看不見但仍能分辨卡片邊界)。
2. 4 宮格版型下,相鄰 pane 之間不再因為陰影/外框疊加而感覺框線很重。
3. Active pane 的藍色外框視覺不受影響,仍然清楚可辨。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "draw_pane_card" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:切換到四宮格版型,目視比對 inactive pane 的外框/陰影是否明顯變淡,
# active pane 的藍色外框是否維持清楚可辨
```

## Handoff requirements

- 最終採用的外框/陰影色值,以及是否有參考目標畫面 HTML/CSS 的確切色碼。
- 若真實桌面測試(本環境已具備螢幕截圖能力,見 CLAUDE 對話紀錄)發現調淡後在深色系統主題或高對比模式下可辨識度過低,記錄下來並說明因應方式。

## 交接區

<!-- 實作 agent 填寫,append-only -->
