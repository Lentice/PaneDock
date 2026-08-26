# PD-076 — Group 與 pane tab 的 active/hover 樣式不一致,以 Group 現有樣式為準套用到 tab

Phase 7 · app_shell / sidebar · Depends on: PD-061, PD-062, PD-072

- Source: 使用者直接提出(2026-08-26)。「group and pane tab should apply uniform styles on active/hover item. base on current group active/hover style, apply to pane tab. pane tab can use its own proper font.」
- Priority: MEDIUM——純視覺一致性,不影響功能,但 active/hover 是使用者持續盯著看的兩個核心互動狀態,側邊欄用藍色強調、tab 用灰階強調,同一個應用程式裡兩套語言。

## 已確認的現況落差(有程式碼證據,不是猜測)

### Group 側邊欄(`src/sidebar/sidebar.cpp` `Sidebar::draw_item`,第 107-230 行)

```cpp
constexpr COLORREF kSidebarActiveBackground = RGB(234, 241, 255);  // 帶藍色調的 pill 底色
constexpr COLORREF kSidebarHoverBackground = RGB(242, 245, 248);   // 中性淺灰 pill 底色
constexpr COLORREF kSidebarText = RGB(75, 85, 101);
constexpr COLORREF kSidebarActiveText = RGB(23, 75, 180);          // active 時文字轉成強調藍
```

- active/hover 都用 `RoundRect` 畫一顆 pill(radius `MulDiv(10, dpi, 96)`),active 用 `kSidebarActiveBackground`(藍底)、hover 用 `kSidebarHoverBackground`(灰底)。
- 文字顏色隨 `selected` 切換:`selected ? kSidebarActiveText : kSidebarText`(第 195 行)——**active 狀態連文字顏色都變成強調藍**,不只是底色。
- 名稱字型用 `SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, ...)` 取 `lfMessageFont` 再套 `FW_SEMIBOLD`(第 176-192 行),副標題用同一份字型的 90% 字級、`FW_NORMAL`。**這個粗細是 active/hover/一般三態共用,不隨狀態切換**——真正做出「這是被選中的項目」區別的是底色 pill + 文字變藍,不是變粗。

### Pane tab(`src/app_shell/main.cpp` `paint_tab_strip`,第 2253-2310 行)

```cpp
const COLORREF fill_color =
    active ? RGB(226, 232, 240)
           : hovered ? RGB(236, 240, 244) : RGB(244, 246, 248);
const COLORREF border_color =
    active ? RGB(203, 213, 225) : RGB(232, 237, 242);
...
SetTextColor(dc, RGB(31, 41, 55));   // 迴圈外設一次,active/hover/一般三態文字顏色完全相同
```

- active/hover 只靠**灰階填色深淺**區分(`RGB(226,232,240)` vs `RGB(236,240,244)` vs `RGB(244,246,248)`),沒有任何強調色。
- **文字顏色寫死 `RGB(31,41,55)`,active/hover/一般三態完全相同**——這是與側邊欄最大的落差:側邊欄用「底色 + 文字色」雙重強調 active,tab 只有底色深淺、肉眼要仔細看才分得出 active 和一般 tab。
- **`paint_tab_strip` 全程沒有任何 `SelectObject` 選字型進 DC。** `DrawTextW`(第 2308-2309 行)用的是 `BeginPaint` 給的 DC 預設字型(GDI 的 stock `SYSTEM_FONT`),**不是**該控制項建立時 `WM_SETFONT` 設定的字型。證據:tab strip 建立於 `main.cpp` 第 2724-2739 行,`WM_SETFONT` 設成 `GetStockObject(DEFAULT_GUI_FONT)`(第 2736-2739 行),但 `tab_strip_proc`(第 2366 行起)把 `WM_PAINT` 整個交給 `paint_tab_strip` 自繪,從未讀回 `WM_GETFONT` 或 `GetWindowFont` 再 `SelectObject`——**這個 `WM_SETFONT` 呼叫是死碼,對實際渲染沒有任何效果**,tab 文字目前用的是系統 stock 字型,連 `DEFAULT_GUI_FONT` 都不是。

### 為什麼不是既有票的範圍

- **PD-062**(已完成)只處理 tab 的**幾何**(圓角、padding/gap、tab 條高度、「+」線條化),色票決策(第 2 條)明確只交代「不再用系統色,改用固定色票」,沒有要求與側邊欄的色票對齊,也沒有動文字顏色或字型。
- **PD-072**(`ready`,尚未實作)只處理**字型家面(face)語系一致性**(`DEFAULT_GUI_FONT` 解析成中文明體的問題),範圍限定在把 9 處 `GetStockObject(DEFAULT_GUI_FONT)` 換成新的共用 `ui_font(HWND)`。它的 root-cause 表格第 2707/2736 行雖然列出「tab strip」,但那是**建立控制項時的 `WM_SETFONT` 呼叫**,PD-072 只會把這個死碼呼叫的字型來源換掉,**不會**讓 `paint_tab_strip` 真的去選字型繪製——因為 PD-072 的 Non-goals 明講「不改 tab 的視覺樣式(PD-062)」,而「在 `paint_tab_strip` 裡真的套用字型」屬於視覺樣式改動,超出它的範圍。
- `docs/tickets.md` 的候選表(第 224 行)已有一項「統一 header 版型按鈕圖示與導覽列圖示的筆畫粗細」,那是**圖示**線寬,與本票的**色票/字型**主題不同,不衝突也不重疊。
- 「已否決的方向」表未涵蓋此項。

## 已確認的產品決策

1. **Tab 的 active/hover 底色與文字色,改為套用側邊欄現有的色票語言(藍色強調),而不是自己一套灰階。** 具體對應:
   - Tab active 底色 → 對應側邊欄 `kSidebarActiveBackground`(`RGB(234,241,255)`)的同一色值。
   - Tab hover(非 active)底色 → 對應側邊欄 `kSidebarHoverBackground`(`RGB(242,245,248)`)的同一色值。
   - Tab active 文字色 → 對應側邊欄 `kSidebarActiveText`(`RGB(23,75,180)`)的同一色值;hover/一般 tab 文字色維持目前的 `RGB(31,41,55)`(對應側邊欄的 `kSidebarText` 深灰,兩者已經很接近,不強求逐位元相同)。
   - Tab 的外框色(`border_color`)是側邊欄 pill 沒有的元素(側邊欄 pill 無描邊),**維持 tab 現有的描邊機制**,只需把 active 描邊色調整到與新的 active 底色協調(由實作者依實機截圖微調,不強制沿用側邊欄任何一個既有常數)。
   - **不建立跨 `sidebar.cpp`/`main.cpp` 的共用色票標頭。** 兩個模組目前本來就各自定義相近但不完全相同的顏色常數(例如 tab 的 placeholder `RGB(238,242,246)` 與側邊欄的 `kPlaceholderBackground` 是分開定義、數值也不同),這是本專案既有模式;本票延續此模式,在 `main.cpp` 用註解寫明「與 `sidebar.cpp` 的 `kSidebarActiveBackground` 同色」,不做模組間共用標頭的重構(超出本票範圍,YAGNI)。
2. **不統一圓角半徑。** 側邊欄 pill 是 10px(`MulDiv(10, dpi, 96)`),tab 是 PD-062 定案的 6px(`kTabCornerRadius`)。PD-062 的決策 1 已明確把 tab 圓角定在「比 pane 卡片小一階」的層級位置,重新拉到 10px 會破壞這個層級關係。**維持現狀,不在本票範圍內。**
3. **Tab 文字改為真正套用字型,而不是依賴 DC 的預設字型(修正上面的死碼問題)。**
   - 移除 `main.cpp` 第 2736-2739 行對 tab strip 送出的 `WM_SETFONT`(它從未生效,留著會誤導後續讀者以為 tab 字型是這樣控制的)。
   - `paint_tab_strip` 改為在繪製迴圈前 `SelectObject` 一個明確的字型,結束後照專案既有的 GDI 資源紀律還原並釋放。
   - 若 PD-072 已先落地,直接呼叫它新增的共用 `ui_font(HWND)`,取得語系無關的 Latin face(釘住 Segoe UI、`DEFAULT_CHARSET` 字型連結)。若 PD-072 尚未落地(依 dispatcher 排程順序而定),**本票仍必須自行修正「從未選字型」的問題**,可先用 `SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, ...)` 的 `lfMessageFont`(沿用其 face,不必等 PD-072),待 PD-072 完成後 tab 字型會自動一併轉成 Latin face——兩者不衝突,只是釘 face 的時機不同。**這個先後順序判斷與最終採用哪一種留給實作者依實際排程決定,並在交接區寫明理由。**
4. **"pane tab can use its own proper font"——不強制 tab 文字的字級/字重逐一比照側邊欄的名稱/副標題字型。**
   - 側邊欄的 Group 名稱是 `FW_SEMIBOLD` + 100% `lfMessageFont` 字級、副標題是 `FW_NORMAL` + 90% 字級——這是「兩行資訊」的排版,tab 只有一行文字,沒有理由照搬兩層字級。
   - Tab 文字統一用 `FW_NORMAL`(常規字重)+ `lfMessageFont` 原生字級即可,**不做 active 時加粗**——這與側邊欄的既有邏輯一致(側邊欄的 active/hover 區別本來就是「底色 + 文字色」,不是「字重」,見上面根因分析),維持同一套「用色彩做強調、不用字重做強調」的語言。
   - 若實機截圖顯示 `lfMessageFont` 原生字級在 `kTabStripHeight = 31`(對照側邊欄 `kGroupRowHeight = 52`)下明顯偏大導致擁擠或截字,允許實作者小幅下修字級(例如比照側邊欄副標題的 90% 比例做法),**在交接區記錄實際採用的字級與理由**,不必嚴格等於 `lfMessageFont` 原始值。
5. **Hover 狀態的判定邏輯不變。** `active`/`hovered` 布林值與其計算方式(`pane.tabs[index].id == pane.active_tab_id`、`state.tab_hover_indices[pane_index] == index`)完全沿用,本票只改「這兩個布林值決定的視覺輸出」,不改「這兩個布林值怎麼算出來」。
6. **不改「+」按鈕、拖曳 placeholder(虛線框)、拖曳插入指示線的顏色。** 這些元素各自有既有色票決策(PD-062 決策 6、PD-066 的 placeholder 樣式),本票範圍限定在 active/hover 的**一般 tab** 視覺與文字。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

——決策 1 明確不建共用色票標頭;決策 3 優先重用 PD-072 的 `ui_font`(若已存在)而非另建一套字型輔助函式。

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks.
>
> (同樣的資源紀律適用於 GDI 物件:每個 `CreatePen`/`CreateSolidBrush`/`CreateFont` 都要有對應的 `DeleteObject`,且必須先 `SelectObject` 還原舊物件才能刪除。)

——`paint_tab_strip` 新增的 `SelectObject` 字型呼叫必須比照這個紀律還原並釋放。

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

——新增的字型選取必須用 `scaled_value`/`SystemParametersInfoForDpi` 依 DPI 計算,不得寫死像素值。

`AGENTS.md`:
> **App UI text must be English.** No Chinese strings ship in the binary.

——本票不新增任何字串。

## Files to read and trace first

- `src/sidebar/sidebar.cpp` 第 12-21 行(色票常數)與第 107-230 行(`Sidebar::draw_item`)——active/hover 的來源樣式,本票要套用到 tab 的目標語言。
- `src/app_shell/main.cpp` 第 2253-2310 行(`paint_tab_strip`)——**本票主要修改處**。
- `src/app_shell/main.cpp` 第 2724-2739 行(tab strip 控制項建立與 `WM_SETFONT`)——決策 3 要移除的死碼呼叫。
- `src/app_shell/main.cpp` `kTabCornerRadius`/`kTabStripHeight` 等 PD-062 定案常數(第 50-70 行附近)——確認本票不動這些。
- `docs/tickets/PD-062-tab-strip-visual-polish.md` 交接區——tab 目前色票的最終定案值與理由。
- `docs/tickets/PD-072-replace-stock-gui-font.md`——`ui_font(HWND)` 若已實作,其簽名與呼叫慣例;若尚未實作,理解它未來會如何影響 tab 的 face。
- `docs/tickets/PD-061-sidebar-group-typography.md` 交接區——側邊欄字型決策的既有理由,確認本票的字重決策(FW_NORMAL、不隨 active 加粗)與其一致。

## Scope

1. `paint_tab_strip` 的 `fill_color`/`border_color`/`SetTextColor` 改用對應側邊欄 active/hover 語言的新色值(決策 1)。
2. 移除 tab strip 建立時的死碼 `WM_SETFONT` 呼叫(決策 3)。
3. `paint_tab_strip` 新增明確的字型選取(`SelectObject`)與正確的 GDI 資源釋放(決策 3、4)。
4. 依 PD-072 是否已落地,決定字型 face 的實際來源(決策 3)。

## Non-goals

- 不改 tab 圓角半徑(維持 PD-062 的 6px)。
- 不改 tab 的 padding/gap/tab 條高度(PD-062 範圍)。
- 不改「+」按鈕、拖曳 placeholder、拖曳插入指示線的顏色。
- 不建跨模組共用色票標頭。
- 不做 active 時的字重加粗(側邊欄本身也不這樣做)。
- 不改 hover/active 布林值的判定邏輯與觸發時機。
- 不改側邊欄的任何程式碼(側邊欄是「來源樣式」,本票只改 tab 去對齊它,不回頭改側邊欄)。

## Acceptance

1. Tab active 狀態的底色改為藍色調(對應側邊欄 `kSidebarActiveBackground` 色值),不再是純灰階。
2. Tab active 狀態的文字顏色改為強調藍(對應側邊欄 `kSidebarActiveText` 色值),與 hover/一般 tab 文字色可清楚區分。
3. Tab hover(非 active)狀態的底色對應側邊欄 `kSidebarHoverBackground` 色值。
4. `paint_tab_strip` 明確 `SelectObject` 了一個字型再呼叫 `DrawTextW`(不再依賴 DC 預設字型),且該字型隨目前 DPI 正確縮放。
5. 死碼 `WM_SETFONT` 呼叫已移除或已改為真正生效的字型設定路徑(依決策 3 選擇的方案,在交接區說明採用哪一種)。
6. Tab 標題文字不因字型改變而截斷或溢出圓角外框(長標題仍走 `DT_END_ELLIPSIS`)。
7. 側邊欄 Group 項目的視覺(色票、字型、圓角)完全未變動(用修改前後截圖對照確認)。
8. 沒有 GDI 物件洩漏(反覆切換 tab/pane 或長時間運行後,工作管理員「GDI 物件」欄位數字不持續上升)。
9. 在 150%/200% DPI 下,tab 文字字型與底色狀態切換正常。
10. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
11. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "paint_tab_strip|kSidebarActiveBackground|kSidebarActiveText|kSidebarHoverBackground|WM_SETFONT|ui_font" src\app_shell\main.cpp src\sidebar\sidebar.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:對照側邊欄 active/hover Group 項目與 pane active/hover tab,確認同一套色票語言;
# 截圖放大至少 3 倍檢視 tab 文字是否真的套用了字型(不再是系統 stock 字型的視覺特徵);
# 切到 150%/200% DPI 確認正確縮放。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 而不是 `Graphics.CopyFromScreen`,`InterpolationMode = NearestNeighbor` 放大至少 3 倍再檢視。側邊欄與 tab 兩處都要截圖對照。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 決策 1 的最終色值(active 底色/文字色、hover 底色、active 描邊色),與側邊欄對應常數的比對表。
- 決策 3 採用的字型來源方案(PD-072 的 `ui_font` 或自行取 `lfMessageFont`)與選擇理由,若 PD-072 尚未落地要說明後續如何銜接(是否需要 PD-072 完成後再微調 face)。
- 死碼 `WM_SETFONT` 的處理方式(移除或改為生效路徑)。
- 決策 4 的最終字級(是否維持 `lfMessageFont` 原生字級或下修比例)與理由。
- 放大 3 倍的側邊欄 + tab 截圖對照(修改前後,active/hover/一般三態各一張或合併對照)。
- GDI 資源檢查結果。
- 高 DPI 驗證結果。

## 交接區

<!-- 實作 agent 填寫,append-only -->
