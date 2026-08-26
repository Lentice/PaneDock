# PD-066 — 拖曳排序改用「撐開空位」的 placeholder;順帶修復 tab 插入指示線自 PD-049 起就是死碼

Phase 7 · app_shell · sidebar · Depends on: PD-055, PD-050, PD-036

- Source: 使用者實機操作後回報(2026-08-26)。
- Origin: 使用者原文追加項:「reorder 拖曳 group / pane tab 時,插入的位置需要有 place holder,這樣有更好的 UX」。
- Priority: MEDIUM——其中「tab 插入指示線完全不顯示」是 PD-049 遺留的功能性回歸,不只是 UX 加強。

## 已確認的根因(有程式碼證據,不是猜測)

### 缺口一(功能性回歸):tab 的插入指示線自 PD-049 起就完全沒有繪製

`src/app_shell/main.cpp` 第 2917 行,`WM_DRAWITEM` 中處理 tab 的分支條件是:

```cpp
if (item != nullptr && item->CtlType == ODT_TAB) {
    ...
    draw_tab_item(*item, text.c_str(), is_add_button, active);
    draw_tab_insertion_indicator(*item, *state, item_index);   // 第 2937 行
    return TRUE;
}
```

**`ODT_TAB` 是原生 `SysTabControl32`(`WC_TABCONTROLW`)才會送出的 owner-draw 類型。** PD-049 已經把 tab 條換成一個 subclass 過的 `STATIC` 子視窗,由 `paint_tab_strip`(第 2350-2385 行)在 `WM_PAINT` 中自行繪製——**它永遠不會送出 `CtlType == ODT_TAB` 的 `WM_DRAWITEM`。**

因此第 2917-2941 行整段是**死碼**:`draw_tab_item` 與 `draw_tab_insertion_indicator` 兩個函式都不再有任何實際呼叫者。而 `paint_tab_strip` 自己**完全沒有**繪製插入指示線的程式碼(第 2350-2385 行從頭到尾只畫 tab 背景、外框、文字與「+」)。

**結果:拖曳 tab 排序時沒有任何視覺提示,使用者完全看不到會插到哪裡。** PD-050 的驗收當時沒有互動驗證能力,所以沒有發現。

(Group 側邊欄的 `draw_group_insertion_indicator`(第 2452 行、呼叫點第 2946 行)走的是 `Sidebar::draw_item` 的 owner-draw `LISTBOX` 路徑,**那條路徑仍然有效**,Group 的插入指示線應該是有畫出來的。實作 agent 需實機確認。)

### 缺口二(UX):即使指示線會畫,「一條 2px 細線」也不是使用者要的 placeholder

`draw_tab_insertion_indicator`(第 2266-2288 行)與 `draw_group_insertion_indicator`(第 2452 行起)的做法都是在目標項目的左緣或右緣填一條 2px 的藍色細線:

```cpp
const int width = std::max(2, MulDiv(2, dpi, 96));
const int x = *state.tab_drag->target_index > state.tab_drag->source_index
                  ? item.rcItem.right - width
                  : item.rcItem.left;
RECT indicator{x, item.rcItem.top + MulDiv(3, dpi, 96), x + width,
               item.rcItem.bottom - MulDiv(3, dpi, 96)};
FillRect(item.hDC, &indicator, brush);
```

使用者要的是**「撐開一個空位」**——被拖曳的項目在原位消失(或變半透明),其餘項目讓開,在目標位置留出一個與被拖曳項目等寬/等高的空白槽。這是現代瀏覽器分頁與工作列的標準拖曳語言,比一條細線清楚得多。

## 已確認的產品決策

1. **placeholder 的定義:在目標插入位置留出一個與被拖曳項目等尺寸的空槽,其餘項目即時讓位。** 空槽用比背景稍深的淺灰填色 + 虛線或淺色外框標示,不是空白不畫。被拖曳的項目本身在原位**不繪製**(它已經「被拿起來」了)。
2. **不做「跟隨游標的浮動縮圖」。** 那需要 layered window 或即時 blit,複雜度遠高於收益,而且與本專案的純 GDI 繪製路線不符。本票只做「原位消失 + 目標位置留空槽」,這已經足以達成使用者要的 UX。
3. **tab 與 Group 兩邊都要做,視覺語言一致。** tab 是水平排列(空槽是垂直的一格),Group 是垂直排列(空槽是水平的一列),但填色/外框樣式要相同。
4. **實作方式:在「幾何計算」階段就把 placeholder 納入,而不是在繪製階段疊加。**
   - tab:`apply_tab_item_size`(第 1071-1115 行)已經是計算每個 tab 矩形的單一位置。拖曳中時,依 `state.tab_drag->source_index` 與 `target_index` 重新排列矩形順序(把 source 抽掉、在 target 位置插入一個等寬空槽),`paint_tab_strip` 只要照著新的矩形畫即可。**這樣「讓位」的動畫感是免費的,而且 hit-test(`tab_item_at_point`)自動跟著正確。**
   - Group:`LISTBOX` 的項目位置由控制項自己決定,無法重排。**改用在 `Sidebar::draw_item` 中判斷「本列是否為 placeholder 位置」並整列改畫成空槽樣式**(而不是畫成 Group 內容)。這是 `LISTBOX` 架構下的合理折衷,實作 agent 需在交接區說明兩邊做法不同的原因。
5. **`draw_tab_item` 與 `draw_tab_insertion_indicator` 這兩個死碼函式,連同第 2917-2941 行的整段 `ODT_TAB` 分支,一併刪除。** `AGENTS.md`:「If you are certain that something is unused, you can delete it completely.」實作 agent 必須先 grep 確認真的沒有其他呼叫者再刪。
6. **`draw_group_insertion_indicator` 若被 placeholder 取代,同樣刪除;若實作 agent 判斷細線與 placeholder 併用視覺更好,可保留並說明理由。**
7. **效能要求:拖曳中每次 `WM_MOUSEMOVE` 都可能改變 placeholder 位置。既有的 `update_tab_drag`/`update_group_drag` 已經有「target 沒變就 return、不 invalidate」的保護(第 2345、2530 行),必須維持。** 不得因為新增 placeholder 而改成每次移動都重繪。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

`AGENTS.md`:
> Avoid backwards-compatibility hacks like renaming unused _vars, re-exporting types, adding // removed comments for removed code, etc. If you are certain that something is unused, you can delete it completely.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Read the relevant spec section and trace every caller before touching shared code.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes.

`docs/design-spec.md` FR-003:
> 每個 pane 可以有多個 tab,使用者可以新增、關閉、切換與重新排序 tab。

## Files to read and trace first

- `src/app_shell/main.cpp` 第 2917-2941 行(`WM_DRAWITEM` 的 `ODT_TAB` 分支)——**要刪除的死碼,先 grep 確認無其他呼叫者。**
- `src/app_shell/main.cpp` 第 2266-2288 行(`draw_tab_insertion_indicator`)、`draw_tab_item`——同上。
- `src/app_shell/main.cpp` 第 1071-1115 行(`apply_tab_item_size`)——**tab placeholder 的核心落腳處:在這裡重排矩形。**
- `src/app_shell/main.cpp` 第 2350-2385 行(`paint_tab_strip`)——依重排後的矩形繪製,並畫出空槽。
- `src/app_shell/main.cpp` 第 2290-2348 行(`cancel_tab_drag`/`finish_tab_drag`/`update_tab_drag`)——拖曳狀態機,`source_index`/`target_index`/`dragging` 的語意來源。
- `src/app_shell/main.cpp` 第 2253-2264 行(`tab_item_at_point`)——確認矩形重排後 hit-test 仍正確。
- `src/app_shell/main.cpp` 第 2452 行起(`draw_group_insertion_indicator`)、第 2477-2533 行(Group 拖曳狀態機)。
- `src/sidebar/sidebar.cpp` 第 100-197 行(`Sidebar::draw_item`)——Group placeholder 的落腳處;需要新的方式把「哪一列是 placeholder」傳進去。
- `docs/tickets/PD-050-tab-drag-behaviors-on-custom-strip.md`、`docs/tickets/PD-035-tab-drag-reorder.md`、`docs/tickets/PD-036-group-drag-reorder.md`——既有拖曳設計。

## Scope

1. 刪除自 PD-049 起失效的 `ODT_TAB` 死碼分支與 `draw_tab_item` / `draw_tab_insertion_indicator`。
2. tab 拖曳排序改為 placeholder 空槽:`apply_tab_item_size` 在拖曳中重排矩形,`paint_tab_strip` 繪製空槽、不繪製被拖曳的項目。
3. Group 拖曳排序改為 placeholder 空槽:`Sidebar::draw_item` 對 placeholder 那一列改畫空槽樣式。

## Non-goals

- 不做跟隨游標的浮動縮圖。
- 不做動畫補間(GDI 無內建動畫,會需要 timer,違反閒置規則)。
- 不改拖曳的觸發門檻(`SM_CXDRAG`/`SM_CYDRAG`)或取消條件。
- 不改 `panedock::core::reorder_tab` / `reorder_group` 的資料模型邏輯。
- 不改 tab 或 Group 的一般(非拖曳中)外觀——那是 PD-061/PD-062。
- 不處理跨 pane 拖曳 tab(目前不支援,超出範圍)。

## Acceptance

1. **拖曳 tab 時,被拖曳的 tab 在原位不再繪製,目標位置出現一個等寬的空槽,其餘 tab 讓位。**
2. 拖曳中移動游標經過不同 tab,空槽位置即時跟著改變。
3. 放開後 tab 順序正確變更為空槽所在位置(`reorder_tab` 行為未回歸)。
4. 拖曳中取消(拖出範圍、放開左鍵於原位、`WM_CAPTURECHANGED`)時,空槽消失、所有 tab 恢復原位、順序不變。
5. **拖曳 Group 時有對應的空槽視覺,行為同上。**
6. tab 數量多到觸發等比壓縮時,空槽寬度與壓縮後的 tab 一致,不會撐破 tab 條。
7. 只有一個 tab / 一個 Group 時拖曳不會出現異常。
8. 拖曳中的 hit-test 正確:放開的位置與空槽顯示的位置一致,不會差一格。
9. **拖曳中游標連續移動時,只有在 placeholder 位置真的改變時才重繪;游標靜止時 CPU 回到 0%。**
10. `rg -n "ODT_TAB|draw_tab_item|draw_tab_insertion_indicator" src\` 為零筆結果。
11. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
12. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 刪除死碼後這一條必須是零筆結果
rg -n "ODT_TAB|draw_tab_item|draw_tab_insertion_indicator" src\
rg -n "apply_tab_item_size|paint_tab_strip|tab_drag|group_drag|draw_group_insertion_indicator" src\app_shell\main.cpp src\sidebar\sidebar.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:開多個 tab,按住其中一個慢慢拖過其他 tab,每移動一格截圖確認空槽位置;
# 放開確認順序正確;拖到一半拖出範圍確認取消復原;
# Group 側邊欄重複同樣測試;拖曳中觀察 CPU。
```

**本票的驗證需要「拖曳中」的截圖,做法:** 用 `mouse_event(MOUSEEVENTF_LEFTDOWN)` 按下後,以 `SetCursorPos` 分段移動游標,**在尚未送出 `MOUSEEVENTF_LEFTUP` 之前**用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 截圖,即可捕捉拖曳中的畫面。記得最後一定要送出 LEFTUP,否則左鍵會卡在按下狀態。

細小元件必須截圖後以 `InterpolationMode = NearestNeighbor` 放大 3 倍以上再檢視。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 確認刪除的死碼範圍,以及 grep 證明無其他呼叫者。
- **Group 的插入指示線在修改前是否真的有畫出來**(tab 的已確認是死碼,Group 的需實機確認)。
- tab placeholder 的實作方式:`apply_tab_item_size` 如何重排矩形。
- Group placeholder 的實作方式,以及為何無法沿用 tab 的重排做法。
- 空槽的最終視覺(填色、外框樣式)。
- 「只在 placeholder 位置改變時才 invalidate」的具體實作與 CPU 觀察結果。
- 拖曳中的截圖(tab 與 Group 各一組,放大 3 倍)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
