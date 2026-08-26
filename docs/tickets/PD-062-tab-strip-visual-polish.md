# PD-062 — Tab 改為圓角外框,調整 padding/gap/列高,「+」按鈕加大加粗

Phase 7 · app_shell · Depends on: PD-049, PD-055

- Source: 使用者實機截圖與目標畫面比對後回報(2026-08-26)。
- Origin: 使用者原文第 9 項「pane tabs 應該用圓弧外框,且 padding 應該加上 2px,tabs 之間的 gap 應該可以減 2px,允許適當的增加 tabs 列的高度」、第 12 項「pane tab 的 add 按鈕 '+' 太小太醜,應該畫稍微大一點粗一點」。
- Priority: MEDIUM——純視覺,但 tab 是每個 pane 最上方的常駐元素,方角 + 過小的「+」讓整個 pane 看起來不精緻。

## 已確認的根因(有程式碼證據,不是猜測)

`src/app_shell/main.cpp` 的 `paint_tab_strip`(第 2350-2385 行):

1. **Tab 是方角。** 背景用 `FillRect`、外框用 `FrameRect`,兩者都只能畫矩形:
   ```cpp
   HBRUSH fill = CreateSolidBrush(active ? RGB(226,232,240) : RGB(244,246,248));
   FillRect(dc, &rect, fill);
   FrameRect(dc, &rect, GetSysColorBrush(COLOR_ACTIVEBORDER));
   ```
   與已經圓角化的 pane 卡片(`draw_pane_card`,radius 10px@96dpi)、圓角的位址列 pill(`draw_navigation_bar_background`)並列時,方角 tab 顯得格格不入。
   另外 `FrameRect(dc, &rect, GetSysColorBrush(COLOR_ACTIVEBORDER))` 用的是**系統色**,不是本專案的色票,顏色會隨使用者主題跳動,與周邊元素不協調。
2. **Padding / gap 由單一個 `inset` 同時控制,無法分開調。**
   ```cpp
   const int inset = scaled_value(window, 4);
   RECT rect = visuals[index].rect;
   InflateRect(&rect, -inset, -inset);   // 這個 inset 同時決定了 tab 間的視覺 gap 與上下留白
   ...
   text_rect.left += inset;              // 這個 inset 又決定了文字左右 padding
   ```
   `InflateRect(-inset, -inset)` 把每個 tab 的矩形四邊都內縮 4px,相鄰兩個 tab 之間因此產生 8px 的視覺 gap(使用者要減 2px),而文字的左右 padding 也是同一個 4px(使用者要加 2px)。**兩個需求方向相反,但目前共用同一個常數,必須拆開才可能同時滿足。**
3. **「+」只是一個字元。**
   ```cpp
   RECT add = state.tab_add_rects[pane_index];
   InflateRect(&add, -scaled_value(window, 5), -scaled_value(window, 5));
   DrawTextW(dc, L"+", 1, &add, DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
   ```
   用 LISTBOX 預設字型(`DEFAULT_GUI_FONT`,偏小的舊 stock 字型)畫一個 `+` 字元,線條細、尺寸受字型擺布,而且 `InflateRect` 內縮 5px 後可用空間更小。這正是使用者說的「太小太醜」。
4. **Tab 條高度目前由 `apply_layout` 的排版決定**,`apply_tab_item_size`(第 1071-1115 行)只把 `visuals[index].rect` 的 `bottom` 設為 `client.bottom`,完全跟隨控制項高度。要加高需要改 `apply_layout` 裡分配給 tab 條的高度。

## 已確認的產品決策

1. **Tab 改用 `RoundRect` 繪製,圓角半徑與 pane 卡片體系協調。** 建議 radius 6px@96dpi(比卡片的 10px 小一階,符合「小元件用小圓角」的層級慣例),實際值由實作 agent 微調。畫法沿用專案既有模式:`SelectObject` 一個 `HBRUSH` 填色 + 一個 `HPEN` 描邊,再 `RoundRect` 一次即可同時完成填色與外框(不需要像現在分成 `FillRect` + `FrameRect` 兩步)。
2. **外框顏色改用專案色票,不再用 `GetSysColorBrush(COLOR_ACTIVEBORDER)`。** 建議 inactive tab 用 `RGB(232,237,242)`(與 `draw_pane_card` 的 inactive 邊框同色),active tab 用稍深一階或直接不描邊(靠填色區分)。由實作 agent 依實機截圖決定,但**不得繼續使用系統色**。
3. **把目前的單一 `inset` 拆成三個獨立的具名常數:**
   - tab 之間的水平 gap(目前等效 8px → 目標 6px,即使用者要求的「減 2px」)
   - tab 內文字的左右 padding(目前 4px → 目標 6px,即使用者要求的「加 2px」)
   - tab 上下的垂直留白(獨立於上述兩者)
   三個常數都要以 `scaled_value(window, ...)` 做 DPI 縮放,並加註解說明各自控制什麼。**這是本票最實質的結構改動——不拆開就無法同時滿足兩個方向相反的需求。**
4. **`apply_tab_item_size`(第 1071-1115 行)的寬度計算必須跟著新的 padding 走。** 目前 `widths.push_back(std::clamp(size.cx + scaled_value(strip, 24), min_width, max_width))` 裡的 `24` 是一個魔術數字,實質上是「文字寬度 + 左右 padding + 關閉鈕空間」的總和。padding 改變後這個數字要一併重新推導,不能只改繪製端而不改量測端,否則文字會被截斷或 tab 過寬。
5. **Tab 條高度適度增加**(使用者明確允許)。增加後必須確認:pane 內部由上到下的 tab 條 / 導覽列 / Shell view / 狀態列四段排版總和仍正確,Shell view 沒有被壓縮到不合理的高度。高度常數要 DPI 縮放。
6. **「+」改為以 GDI 線條繪製,不再用 `DrawTextW` 畫字元。** 兩條線(一橫一豎)交叉,用 `CreatePen(PS_SOLID, 縮放後的粗細, 顏色)`,粗細建議 2px@96dpi。這樣尺寸與粗細完全可控,不受字型影響。**繪製後必須 `SelectObject` 還原並 `DeleteObject` 釋放 pen**——本函式目前是純 `FillRect`/`DrawTextW`,沒有 pen 管理的既有程式碼,實作 agent 要自己確保沒有 GDI 洩漏。
7. **「+」的 hover 背景不在本票範圍**(PD-058 負責)。兩票都會改到 `paint_tab_strip`,實作順序由 dispatcher 決定,後做的那票要先 rebase。
8. **不改 tab 的文字內容、截斷邏輯、拖曳插入指示線的樣式。**

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks.
>
> (同樣的資源紀律適用於 GDI 物件:每個 `CreatePen`/`CreateSolidBrush`/`CreateFont` 都要有對應的 `DeleteObject`,且必須先 `SelectObject` 還原舊物件才能刪除。)

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers.

`docs/tickets/PD-037-dynamic-tab-width.md`(動態寬度的既有決策,本票不得推翻):
> tab 寬度依標題文字動態計算,並在總寬超出可用空間時等比壓縮。

## Files to read and trace first

- `src/app_shell/main.cpp` 第 2350-2385 行(`paint_tab_strip`)——**本票主要修改處。**
- `src/app_shell/main.cpp` 第 1071-1115 行(`apply_tab_item_size`)——寬度量測,padding 改變後必須同步(特別是第 1091-1093 行的魔術數字 `24`)。
- `src/app_shell/main.cpp` 第 1367-1429 行(`draw_pane_card`)——圓角半徑與色票的參考來源,新的 tab 圓角要與它協調。
- `src/app_shell/main.cpp` `draw_navigation_bar_background` 與 `kAddressBarBackgroundRadius`——另一個既有的圓角元件,半徑層級的參考。
- `src/app_shell/main.cpp` 的 `apply_layout` 中分配 tab 條高度的那一段——加高 tab 條的落腳處。
- `src/app_shell/main.cpp` 第 2266-2288 行(`draw_tab_insertion_indicator`)——確認拖曳指示線在新的 tab 幾何下仍然對齊。
- `docs/panedock-ui-demo-01-refined-quiet-header.html`——目標畫面的 tab 圓角與間距參考。

## Scope

1. `paint_tab_strip` 的 tab 改用 `RoundRect` 圓角繪製,外框改用專案色票。
2. 拆分 padding / gap / 垂直留白為三個獨立的 DPI 縮放常數,並依使用者要求調整數值。
3. `apply_tab_item_size` 的寬度計算同步新的 padding。
4. Tab 條高度適度增加。
5. 「+」改用 GDI 線條繪製,加大加粗。

## Non-goals

- 不加 hover 特效(PD-058)。
- 不修 tab 點擊失效的問題(PD-055,本票依賴它先完成才能實機驗證)。
- 不改 tab 的動態寬度演算法本身(PD-037 的等比壓縮邏輯保留)。
- 不改 tab 的文字截斷方式(`DT_END_ELLIPSIS`)。
- 不加 tab 上的關閉「×」按鈕(目前是中鍵關閉,改動超出範圍)。
- 不改拖曳插入指示線的顏色或寬度。

## Acceptance

1. Tab 呈現圓角外框,圓角半徑與 pane 卡片、位址列 pill 視覺協調(不會有的圓有的方)。
2. Tab 外框顏色不隨 Windows 主題色變化(已改用固定色票)。
3. 相鄰 tab 之間的水平間距比修改前縮小約 2px@96dpi。
4. Tab 內文字的左右留白比修改前增加約 2px@96dpi,文字不貼邊。
5. Tab 條高度增加後,pane 內部四段(tab 條 / 導覽列 / Shell view / 狀態列)排版正確,Shell view 沒有被異常壓縮。
6. 「+」明顯比修改前大且線條更粗,清楚可辨,置中於其可點擊區域。
7. Tab 標題很長時仍正確截斷,不溢出圓角外框。
8. Tab 數量很多導致等比壓縮時,圓角與 padding 仍正確,不會出現負寬度或繪製錯亂。
9. 拖曳 tab 時的插入指示線仍正確對齊 tab 邊界。
10. 在 150%/200% 顯示縮放下,圓角、間距、「+」的粗細都正確縮放。
11. 沒有 GDI 物件洩漏(可用工作管理員的「GDI 物件」欄位長時間觀察,或反覆切換版型後確認數字不持續上升)。
12. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
13. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "paint_tab_strip|apply_tab_item_size|RoundRect|COLOR_ACTIVEBORDER|tab_add_rects|kTabMinWidth|kTabMaxWidth" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:截圖 tab 條放大檢視圓角、間距、「+」;開很多個 tab 確認壓縮下仍正確;
# 切換到 150%/200% 顯示縮放確認 DPI 縮放;拖曳 tab 確認指示線對齊。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 而不是 `Graphics.CopyFromScreen`。tab 條很細,**必須**截圖後用 `InterpolationMode = NearestNeighbor` 放大至少 3 倍再檢視,否則無法判斷圓角是否真的畫出來、間距差 2px 是否生效。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 三個新常數的名稱與數值(gap / 文字 padding / 垂直留白)。
- Tab 圓角半徑的最終值,以及與 pane 卡片半徑的層級關係說明。
- Tab 外框與填色的最終色值(active / inactive)。
- `apply_tab_item_size` 裡原本魔術數字 `24` 的新值與推導過程。
- Tab 條高度的新值。
- 「+」的線條粗細與繪製方式,以及 GDI 資源釋放的處理。
- 放大 3 倍的 tab 條截圖(修改前後對照)。
- 高 DPI 下的驗證結果。

## 交接區

<!-- 實作 agent 填寫,append-only -->
