# PD-074 — 拖曳排序時完全看不到「拖的是哪一個」:placeholder 是空槽,來源項目又已隱藏

Phase 7 · app_shell · sidebar · Depends on: PD-066, PD-061, PD-062

- Source: 使用者實機操作 PD-066 之後回報(2026-08-26)。
- Origin: 使用者原文:「draging group or pane tab 時,沒有顯示被 drag 的 item,是否有辦法辦到?或是直接在 placeholder 那邊顯示要被 put 的 item(顏色變淡或用灰色),或是有其他更好的方案。」
- Priority: MEDIUM——拖曳排序功能正確,但拖曳過程中的資訊是不完整的。

## 已確認的現況(有程式碼證據,不是猜測)

PD-066 剛完成的行為是:**來源項目在原位完全不繪製,目標位置畫一個空的虛線槽。** 兩處都經實機 4× 放大截圖確認。

`src/app_shell/main.cpp` `paint_tab_strip`,tab 迴圈開頭直接跳過來源(第 2298-2301 行):

```cpp
state.tab_drag->pane_index == pane_index &&
index == state.tab_drag->source_index) continue;
```

placeholder 分支只畫填色與虛線框,**沒有任何文字**(第 2337-2355 行):

```cpp
if (state.tab_placeholder_rects[pane_index].has_value()) {
    RECT rect = *state.tab_placeholder_rects[pane_index];
    ...
    HBRUSH fill = CreateSolidBrush(RGB(238, 242, 246));
    HPEN border = CreatePen(PS_DOT, border_width, RGB(203, 213, 225));
    ...
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    ...
}
```

`src/sidebar/sidebar.cpp` `Sidebar::draw_item` 同樣:來源列只清背景就 return(第 121 行),placeholder 列畫完空槽就 return(第 128-141 行),兩者都不畫任何內容。

**結果:整個拖曳過程中,被拖曳的項目在畫面上不存在。** 使用者知道「有東西要插在這裡」,但不知道「是哪一個」。tab 條上有 15 個 tab、側邊欄有多個 Group 時,這個資訊缺口是真的會造成誤操作的。

## 已確認的產品決策

1. **採用使用者提出的第二個方案:把被拖曳項目的內容畫在 placeholder 裡面,顏色調淡。** 不做「跟隨游標的浮動縮圖」——**PD-066 決策 2 已經明確否決那個方向**(需要 layered window 或即時 blit,與本專案純 GDI 繪製路線不符),**本票不重開它**。本票的做法完全在既有的 `WM_PAINT` / owner-draw 路徑內,不新增任何視窗。
2. **placeholder 的外框與填色維持 PD-066 定案的樣式不變**(填 `RGB(238, 242, 246)`、`PS_DOT` 外框 `RGB(203, 213, 225)`)。本票只是在那個槽裡**加上**淡化的內容。理由:虛線空槽已經正確傳達「這是即將落下的位置」,把它改成實心會讓它看起來像一個已經存在的項目。
3. **淡化的做法是「調淡文字/圖形的顏色」,不是 alpha 混色。** GDI 對文字沒有便宜的 alpha 路徑,而本專案是純 GDI。統一使用 `RGB(148, 163, 184)` 作為 placeholder 內容的前景色——這個值必須同時用在 tab 與 Group 兩邊,兩者的淡化程度要看起來一致。
4. **來源項目在原位仍然完全不繪製。** 不改 PD-066 這個決定:內容已經出現在 placeholder 裡了,原位再畫一次(即使是淡的)會變成同一個項目同時出現在兩個地方。
5. **`Sidebar::draw_item` 的簽章必須改。** 目前的簽章是 `draw_item(const DRAWITEMSTRUCT*, bool placeholder, bool dragged)`。問題在於 owner-draw 的 `item->itemID` 在 placeholder 那一列是**目標**索引,`groups_[item->itemID]` 因此是錯的那一個 Group——**要畫出被拖曳的 Group,`Sidebar` 必須知道來源索引。** 正確做法是把 `bool placeholder` 換成一個帶來源索引的參數(例如 `std::optional<std::size_t> placeholder_source`),由 `main.cpp` 的 `WM_DRAWITEM` 呼叫端(第 2972-2986 行)從 `state->group_drag->source_index` 傳進來。**不要在 `Sidebar` 內部另存一份拖曳狀態**——那會變成同一份狀態存在兩個地方。
6. **Group 的 placeholder 內容要畫完整的兩行 + 徽章,不是只有名稱。** PD-061 定案的列版面是「粗體 Group 名稱 + 副標題 `N panes · M tabs`」加上右側的 tab 數徽章;淡化版要保留同一個版面,只換顏色。理由:只畫名稱會讓 placeholder 的視覺重量明顯輕於其他列,拖曳過程中版面看起來會抖動。
7. **順手修掉一個 PD-066 遺留的視覺不一致:** `Sidebar::draw_item` 的 placeholder 用的是 `Rectangle`(方角),而同一個檔案裡 Group 列的正常 pill 用 `RoundRect(radius = MulDiv(10, dpi, 96))`,tab 的 placeholder 也是 `RoundRect`。**Group 的 placeholder 應改用與正常 pill 相同的 `RoundRect` 半徑。** PD-066 決策 3 要求「tab 與 Group 兩邊視覺語言一致」,這一點沒做到。
8. **不加任何計時器、動畫或淡入淡出。** `AGENTS.md` 的 event-driven 規則不變。
9. **PD-066 決策 7 的重繪保護必須維持:** `update_tab_drag` / `update_group_drag` 的「target 沒變就 return、不 invalidate」不得因為新增繪製內容而改成每次 `WM_MOUSEMOVE` 都重繪。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Read the relevant spec section and trace every caller before touching shared code.

`AGENTS.md`:
> Keep `src/core` free of HWND, COM and `windows.h`.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes.

`AGENTS.md`:
> **App UI text must be English.** No Chinese strings ship in the binary.

`docs/design-spec.md` FR-003:
> 每個 pane 可以有多個 tab,使用者可以新增、關閉、切換與重新排序 tab。

PD-066 決策 2(本票遵守,不重開):
> **不做「跟隨游標的浮動縮圖」。** 那需要 layered window 或即時 blit,複雜度遠高於收益,而且與本專案的純 GDI 繪製路線不符。

## Files to read and trace first

| 檔案 | 讀什麼 |
|---|---|
| `src/app_shell/main.cpp` `paint_tab_strip`(第 2280-2390 行) | tab 的唯一繪製路徑。第 2298-2301 行跳過來源、第 2337-2355 行畫空槽、第 2326-2335 行是正常 tab 的文字繪製(**淡化版要重用同一套 `text_padding` 與 `DrawTextW` 旗標**)。 |
| `src/app_shell/main.cpp` `WM_DRAWITEM` 的 sidebar 分支(第 2971-2988 行) | `placeholder` / `dragged` 兩個 bool 目前怎麼算出來的。決策 5 要改的呼叫端就是這裡。 |
| `src/sidebar/sidebar.h`(第 25-60 行) | `GroupSummary`(`id` / `name` / `pane_count` / `tab_count`)與 `draw_item` 的簽章。 |
| `src/sidebar/sidebar.cpp` `Sidebar::draw_item`(第 108 行起) | 第 121 行的來源列 early return、第 128-141 行的空槽、以及**其後正常列的完整繪製程式碼**——淡化版要重用它,不要另寫一份。 |
| `src/sidebar/sidebar.cpp` 的顏色常數(`kPlaceholderBackground` / `kPlaceholderBorder` / `kSidebarActiveBackground` 等) | 新的前景色常數要放在同一處,不要散在函式裡。 |
| `docs/tickets/PD-066-drag-reorder-placeholder-gap.md` 的決策與交接區 | 本票是它的延伸,決策 2 的界線與決策 7 的重繪保護都必須遵守。 |
| `docs/tickets/PD-061-sidebar-group-typography.md` | Group 列的字級、字重與徽章樣式的定案值,淡化版要沿用。 |

## 範圍

1. tab:`paint_tab_strip` 的 placeholder 分支加上被拖曳 tab 的文字,前景色 `RGB(148, 163, 184)`,padding 與 `DrawTextW` 旗標與正常 tab 完全相同。
2. Group:改 `Sidebar::draw_item` 的簽章,讓它拿到 placeholder 的**來源**索引;placeholder 列以淡化前景色畫出完整的兩行 + 徽章。
3. Group:placeholder 的 `Rectangle` 改成與正常 pill 同半徑的 `RoundRect`(決策 7)。
4. 新增前景色常數一處,tab 與 Group 共用同一個值。

## 非目標

- 不做跟隨游標的浮動縮圖(PD-066 決策 2,本票不重開)。
- 不做動畫、淡入淡出、透明度混色。
- 不改來源項目在原位的處理(維持完全不繪製)。
- 不改 placeholder 的填色與虛線外框顏色(維持 PD-066 定案值)。
- 不改拖曳的判定邏輯、hit-test、重排幾何——本票只動繪製。
- 不動 pane 之間的拖放(檔案拖放)相關的任何程式碼。

## 驗收條件

1. 拖曳 tab 時,placeholder 裡看得到被拖曳 tab 的名稱,顏色明顯比正常 tab 淡,但可讀。附 4× 放大截圖。
2. 拖曳 Group 時,placeholder 裡看得到被拖曳 Group 的**名稱 + 副標題 + 徽章**,版面與正常列一致,顏色明顯較淡。附 4× 放大截圖。
3. tab 與 Group 兩邊的淡化程度看起來一致(同一個前景色常數)。
4. 來源項目在原位仍然完全看不到——**同一個項目不得同時出現在兩個位置**。
5. Group 的 placeholder 是圓角,半徑與正常 pill 相同。
6. placeholder 的填色與虛線外框顏色與 PD-066 修改前後相同(逐像素比對填色值)。
7. 放開後排序正確,placeholder 消失,所有列/tab 回到正常繪製。
8. 拖曳中途按 Esc 取消、以及在原位放開,兩種情況都正確回復。
9. 拖曳過程中游標在同一個目標格內移動時**不重繪**(決策 9)——用 `WM_PAINT` 的呼叫次數或 `InvalidateRect` 的觸發次數證明。
10. 拖曳結束、游標靜止後,10 秒內 process 的 CPU 時間增量為 0。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "draw_item|placeholder|kPlaceholder" src\sidebar\sidebar.cpp src\sidebar\sidebar.h src\app_shell\main.cpp
git diff --check
```

**建置輸出路徑注意:** 目前 `build` 這個 build directory 的 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 被設成 `pd062-output`,實際執行檔在 `build\pd062-output\PaneDock.exe`。

**截圖驗證方法(本環境已驗證可用,請直接沿用):** `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,不要用 `Graphics.CopyFromScreen`;放大用 `InterpolationMode = NearestNeighbor`。

**要驗證拖曳,純訊息注入無效。** PD-066 的獨立驗證已經證實:只用 `SendMessage(WM_LBUTTONDOWN/WM_MOUSEMOVE)` 送給目標 child HWND,app 不會進入拖曳狀態,畫面毫無變化。必須用 `SetForegroundWindow` + `SetCursorPos` + `mouse_event(LEFTDOWN/MOVE)` 建立真實 capture,再用 `GUITHREADINFO.hwndCapture` 確認 capture 落在目標 child HWND 上,並在**還沒 `LEFTUP`** 的狀態下 `PrintWindow` 截圖。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉。**

## Handoff requirements

- `Sidebar::draw_item` 的最終簽章,以及為什麼來源索引必須由呼叫端傳入而不是由 `Sidebar` 自己存。
- 淡化前景色的最終值,以及 tab / Group 兩邊看起來一致的截圖證據。
- tab 與 Group 各一張按住未放開的 4× 放大截圖。
- placeholder 填色/外框顏色未變的逐像素比對結果。
- 決策 9 的不重繪證明(實際的量測方法與數字)。
- 靜止後的 CPU 增量數字。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-27 — 實作交接

- `Sidebar::draw_item` 最終簽章為 `draw_item(const DRAWITEMSTRUCT*, std::optional<std::size_t> placeholder_source, bool dragged) const noexcept`。`WM_DRAWITEM` 呼叫端從 `state->group_drag->source_index` 傳入來源索引；owner-draw placeholder 列的 `itemID` 是目標索引，`Sidebar` 自己保存拖曳狀態會造成第二份狀態且仍無法可靠區分來源，因此不在 `Sidebar` 內存狀態。
- 淡化前景色集中定義為 `panedock::sidebar::kPlaceholderContent = RGB(148, 163, 184)`（`src/sidebar/sidebar.h`），tab placeholder 文字與 Group placeholder 的名稱／副標題／badge 文字共用此值。Group 正常副標題原本即使用此色值，未另建重複色常數。
- tab placeholder 在既有 `RoundRect` 填色／點線框後，使用正常 tab 相同的 `text_padding`、chrome font 與 `DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX` 繪製來源 tab 文字；來源 tab 仍在原位完全跳過。
- Group placeholder 讀取 `groups_[placeholder_source]` 後沿用既有兩行版面與 badge 幾何繪製名稱、副標題及數字；placeholder 外框由原本的方角 `Rectangle` 改為 `RoundRect`，半徑與正常 pill 同為 `MulDiv(10, dpi, 96)`。填色 `RGB(238, 242, 246)` 與外框 `RGB(203, 213, 225)` 未改。
- 未新增 timer、動畫或拖曳狀態；`update_tab_drag`／`update_group_drag` 的 target 未變即 return、不 invalidate 保護維持原樣。
- 靜態／自動檢查：LLVM-MinGW Clang/LLD + Ninja configure 成功；`cmake --build build` 成功；`ctest --test-dir build --output-on-failure` 為 5/5；`rg -n "draw_item|placeholder|kPlaceholder" ...` 命中預期實作；`rg -n "ODT_TAB|draw_tab_item|draw_tab_insertion_indicator" src` 為零筆；`git diff --check` 通過。
- 依本票驗證邊界，本輪沒有使用 computer-use、滑鼠按下／移動／放開或拖曳訊息注入，也沒有擷取拖曳中的截圖。因此驗收 1–10（tab／Group 拖曳畫面、來源不重複、圓角與逐像素色值、放開／Esc／原位取消復原、排序、重繪次數及靜止 CPU）均未由 agent 實機證明；tab 與 Group 各一張按住未放開的 4× 截圖、逐像素比對、`WM_PAINT`／`InvalidateRect` 數字與 CPU delta 留給使用者手動驗證。
