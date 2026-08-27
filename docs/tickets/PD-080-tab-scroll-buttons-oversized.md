# PD-080 — Tab 溢出捲動按鈕(左移/右移)尺寸過大,和真實瀏覽器的緊湊樣式差距明顯

Phase 7 · app_shell · Depends on: PD-073

- Source: 使用者實機截圖回報(2026-08-27)。
- Origin: 使用者原文:「pane tab 瀏覽左移右移的按鈕太大,不好看,應該小小的類似這樣」,並附兩張截圖。
- Priority: MEDIUM——純視覺,沒有功能缺陷(PD-073 已修復可點擊性,本票只調尺寸)。

## 使用者附圖

| 現況(過大) | 使用者期望的參考樣式(緊湊) |
|---|---|
| [PD-080-current-scroll-buttons-too-large.png](assets/PD-080-current-scroll-buttons-too-large.png) | [PD-080-reference-compact-chevron-buttons.png](assets/PD-080-reference-compact-chevron-buttons.png) |

現況截圖中每個 tab 都被撐成獨立矩形,右側兩顆捲動按鈕與整個 tab 一樣高、寬度也接近一個短 tab。參考圖(瀏覽器分頁常見樣式)則是一顆遠比 tab 矮的小圓角按鈕,裡面塞了兩個緊靠在一起的小箭頭圖示。

## 已確認的根因(有程式碼證據,不是猜測)

`src/app_shell/main.cpp`:

1. 第 63 行:`constexpr int kTabScrollButtonWidth = 28;`——這是唯一控制寬度的常數,兩顆按鈕各佔 28px(96 DPI),合計 56px。
2. 第 1164-1174 行(`apply_tab_item_size`):兩顆按鈕的 rect 都是 `{button_left, 0, ..., client.bottom}`——**高度永遠等於整個 tab strip 的高度**,沒有獨立的高度常數,也沒有垂直置中的縮小處理。tab strip 本身的高度由 `kTabHeight`(或等效常數)決定,和一般 tab 幾乎一樣高,所以按鈕自然「和 tab 一樣大」。
3. `draw_tab_scroll_button`(第 2614-2640 行):背景用 `FillRect` 填滿整個傳入的 `rect`(方角,無圓角),前景箭頭用 `half = min(2, min(scaled_value(window,8), min(rect 寬,高)/2))`,箭頭本身只有 96 DPI 下最大 8px 高——箭頭很小,但外框矩形很大且方正,兩者比例失衡,視覺上就是「一個大方塊裡塞一個小箭頭」,和參考圖「按鈕本身就是小巧的形狀」完全不同。

## 已確認的產品決策

1. **捲動按鈕改為視覺上明顯小於 tab 本身、垂直置中的緊湊按鈕**,呼應參考圖的比例(按鈕高度明顯小於 tab strip 高度,寬度也相應收窄,兩個方向鍵可以並排靠在一起甚至共用同一個外框/背景)。具體像素值由實作 agent 決定並在交接區寫明理由,但驗收要求使用者附圖的視覺比例作為基準,不是憑感覺調整。
2. **背景改用圓角(`RoundRect`)而不是方角 `FillRect`**,和 tab 本身(`kTabCornerRadius`)、+ 按鈕的圓角視覺語言一致。
3. **hit-test 矩形可以比視覺矩形大**(例如視覺上是一個 18px 高的小圓角按鈕,但點擊熱區維持接近原本的 28×(tab strip 高度),避免縮小可點擊面積造成手感變差)。這是常見的「視覺尺寸 ≠ 點擊熱區」模式,`AGENTS.md` 沒有禁止,只要求 agent 在交接區明確記錄視覺矩形與熱區矩形的實際差異,不要含糊帶過。
4. **箭頭本身的粗細/大小可以維持現行邏輯或按比例縮小**,但必須與新的按鈕外框比例協調——不能出現「外框變小但箭頭沒跟著等比縮小,擠出外框」的狀況。
5. **不改變捲動的行為邏輯**(`tab_strip_viewport`、`tab_scroll_offsets`、`tab_scroll_max_offsets`、按鈕啟用/停用判斷)。本票純粹是繪製尺寸與外觀,不動 `src/app_shell/tab_overflow.h` 的演算法。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`:
> Read the relevant spec section and trace every caller before touching shared code.

`AGENTS.md`:
> No network, no telemetry, no third-party runtime, no services, no drivers, no admin elevation.

## Files to read and trace first

| 檔案 | 讀什麼 |
|---|---|
| `src/app_shell/main.cpp` 第 63 行 | `kTabScrollButtonWidth` 常數定義。 |
| `src/app_shell/main.cpp` `apply_tab_item_size` 第 1120-1180 行左右 | 兩顆按鈕 rect 的計算來源,高度來自 `client.bottom`。**本票要修改的主戰場之一。** |
| `src/app_shell/main.cpp` `draw_tab_scroll_button` 第 2614-2640 行 | 目前的方角背景 + 手繪箭頭繪製邏輯。**本票要修改的主戰場之二。** |
| `src/app_shell/main.cpp` 第 2742-2751 行(`paint_tab_strip` 內) | 兩顆按鈕的呼叫點與 enabled/disabled 判斷(捲到底/捲到頂)。 |
| `src/app_shell/main.cpp` 第 2826-2833 行附近(`tab_strip_proc` 內) | 點擊時的 `PtInRect` 判斷,若改成「視覺矩形 ≠ 熱區矩形」,這裡要改成用熱區矩形做命中測試,不是視覺矩形。 |
| `src/app_shell/tab_overflow.h` | `tab_strip_viewport` 的 `scroll_button_width` 輸出——只讀,不改演算法,但要確認新的視覺寬度值怎麼傳進去。 |
| `docs/tickets/PD-073-tab-overflow-scroll-buttons.md` | 這兩顆按鈕是 PD-073 新增的,讀它的決策與交接區,確認本票不會回頭破壞可點擊性修復。 |

## 範圍

1. 捲動按鈕的視覺矩形改為明顯小於 tab strip 高度、垂直置中,寬度按比例收窄。
2. 背景改用圓角矩形。
3. 若視覺矩形縮小到影響點擊手感,保留一個較大的熱區矩形供 `PtInRect` 判斷,並在交接區記錄兩者差異。
4. 箭頭大小/粗細與新外框等比例調整。

## 非目標

- 捲動行為、偏移量計算、`tab_strip_viewport` 演算法(PD-073 範圍,不動)。
- tab 本身的圓角/間距/寬度(PD-062 範圍)。
- 「+」新增按鈕的外觀(PD-081 的範圍,不在本票)。
- 側邊欄或 header 版型按鈕。

## 驗收條件

1. 3× 以上 NearestNeighbor 放大截圖,修改前/修改後並排,視覺比例明顯比對成功——按鈕不再與 tab 等高。
2. 捲動按鈕仍可正確點擊,捲到底/捲到頂時 disabled 配色正確顯示(沿用現行 `RGB(190, 197, 209)` / `RGB(90, 102, 122)`)。
3. 若熱區矩形與視覺矩形不同,交接區需明確寫出兩者的實際尺寸與理由。
4. 96 DPI 與一個高 DPI 設定(例如 150%)下各截一組圖,按鈕沒有模糊、裁切或位移。
5. `cmake --build build` 與既有 CTest 全數通過。
6. `git diff --check` 無尾隨空白。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

**建置輸出路徑注意:** 確認目前 build directory 的 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 設定,實際執行檔路徑以該設定為準(先前是 `build\pd062-output\PaneDock.exe`,可能已變動)。

**截圖驗證方法(本環境已驗證可用,請直接沿用):** `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,不要用 `Graphics.CopyFromScreen`;放大用 `InterpolationMode = NearestNeighbor`。

**驗證要快、要最小化。** 只需要一次單擊+截圖證明尺寸縮小的核心視覺主張,不要做多情境比較、不要長時間互動測試。任何需要「持續操作」的驗收項目(例如多步驟捲動測試)請留在交接區標記未驗證,交回使用者親自確認,不要用 computer-use 工具長時間占用實體滑鼠/鍵盤。**computer-use 工具的單次操作完成、截圖存檔後,立刻結束該互動 session,不要停留。**

## Handoff requirements

- 新的按鈕視覺尺寸(寬、高)與 96-DPI 基準值,以及選擇這個尺寸的理由(相對於使用者附圖的比例推算)。
- 視覺矩形與熱區矩形是否不同,以及各自的實際數值。
- 修改前/修改後 3× 放大截圖。
- 96 DPI 與高 DPI 截圖。
- 未驗證項目與原因(若有)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-27 — 實作與使用者實機多輪微調(彙整)

只修改 `src/app_shell/main.cpp` 的 PD-080 視覺繪製路徑(`draw_tab_scroll_button`);`apply_tab_item_size`、`tab_strip_viewport`、offset/clamp、tab geometry、`PtInRect` 判斷與「+」按鈕均未修改。

**最終定案值(96 DPI 基準,經使用者實機多輪比對調整):**

| 項目 | 值 |
|---|---|
| 視覺矩形 | `18×16 px`,`RoundRect` 圓角半徑 `4px` |
| 熱區矩形(`tab_scroll_button_rects`) | 每顆 `20×31 px`(從 PD-073 原本的 28px 收窄) |
| 視覺水平偏移 `visual_offset_x` | `6px`,往「+」按鈕方向靠近 |
| 視覺垂直偏移 `visual_offset_y` | `1px`,往上 |
| 箭頭半尺寸 | `5px` |

**調整過程:** 初版熱區沿用 PD-073 的 28px 並置中繪製 18px 視覺按鈕;使用者實機比對後依序回饋「更靠近 add button、整個區域縮小」(熱區 28→20)、「再右移 2px」×3 次(累加至 6px)、「再下移 2px」後又「上移 1px」(淨位移 1px 上)。每次調整都各自跑過 `cmake --build build` 與 `ctest --test-dir build --output-on-failure`(5/5 PASS)。

**未驗證項目:** 3× 放大截圖對照、150%/200% DPI 實機截圖、disabled 配色與捲到底/捲到頂顯示的多情境驗證——依 single-click + single-screenshot 的驗證政策,這些留給使用者親自於實機確認。既有 before/參考圖仍為 `PD-080-current-scroll-buttons-too-large.png`、`PD-080-reference-compact-chevron-buttons.png`。
