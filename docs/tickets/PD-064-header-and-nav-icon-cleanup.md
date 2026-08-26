# PD-064 — 移除無作用的 more-actions「...」按鈕,修正 Up 圖示的直線未對齊中心

Phase 7 · app_shell · Depends on: PD-029, PD-043

- Source: 使用者實機截圖比對後回報(2026-08-26)。
- Origin: 使用者原文第 6 項「pane layout 右邊的 "..." 按鈕暫時不用可以移除」、第 8 項「pane 上一層的 icon 沒畫好,直線應該右移一點才會對齊中心點」。
- Priority: LOW——兩個獨立的小型 chrome 修正,合併成一票是因為各自都遠小於半天,分開開票的追蹤成本大於實作成本。

## 已確認的根因(有程式碼證據,不是猜測)

### 缺口一:more-actions「...」按鈕是永久停用的視覺佔位符

`src/app_shell/main.cpp` 第 740-741 行,`draw_more_actions_button` 的註解本身就說明了現況:

```cpp
void draw_more_actions_button(const DRAWITEMSTRUCT& item) noexcept {
    // Visual placeholder only (PD-029 decision 2): always disabled, no menu.
```

且第 2652 行建立後立刻 `EnableWindow(state->more_actions_button, FALSE)`。**它從來沒有任何行為,只是為了讓畫面與設計稿一致而存在的死元件。** 使用者判斷「暫時不用」,要求移除。

涉及的位置(實作 agent 必須全部清乾淨,不要留下孤兒):
- 第 78 行:`constexpr int kMoreActionsButtonId = kLayoutButtonIdBase + 5;`
- 第 317 行:`AppState::more_actions_button` 成員
- 第 740 行起:`draw_more_actions_button` 函式
- 第 1227 行:`more_actions_gap`;第 1236-1249 行:`kButtonSlotCount`、`available_width`、`more_actions_width`、`total_width` 的計算中含 more-actions 的份額
- 第 1265-1270 行:`SetWindowPos` + `ShowWindow`
- 第 2647-2652 行:`CreateWindowExW` + `EnableWindow(FALSE)`
- 第 2674-2682 行:tooltip 註冊(`L"More actions"`)
- 第 2835 行:`WM_MEASUREITEM` 分支
- 第 2866-2867 行:`WM_DRAWITEM` 分支

**注意第 1236-1249 行的排版計算:移除後 `kButtonSlotCount` 應由 `kLayoutButtonIds.size() + 1` 改回 `kLayoutButtonIds.size()`,`total_width` 也要拿掉 more-actions 的寬度與 gap。若只刪除控制項而不改這段計算,五顆版型按鈕會被算成六個 slot,寬度變窄且右側留下一塊空白。**

### 缺口二:Up 圖示的直線未對齊箭頭中心

`src/app_shell/main.cpp` 第 690-696 行,`draw_navigation_icon_button` 的 `case 2`:

```cpp
case 2:  // up: arrow pointing up
    MoveToEx(item.hDC, cx, cy + half, nullptr);
    LineTo(item.hDC, cx, cy - half);                       // 直立的桿
    MoveToEx(item.hDC, cx - half / 2, cy - half / 2, nullptr);
    LineTo(item.hDC, cx, cy - half);                       // 箭頭左翼
    LineTo(item.hDC, cx + half / 2, cy - half / 2);         // 箭頭右翼
    break;
```

畫筆寬度來自第 676 行 `CreatePen(PS_SOLID, std::max(1, size / 8), color)`——在 `size = 16` 時是 **2px**。

**GDI 的粗線條是以座標為中心向兩側擴展,寬度為偶數時無法真正置中,實作上會向左/向上偏一個像素。** 因此:
- 直立的桿畫在 `cx`,實際佔用 `cx-1` 與 `cx` 兩欄像素 → 視覺重心落在 `cx - 0.5`;
- 箭頭的兩翼是對稱的(`cx - half/2` 到 `cx` 到 `cx + half/2`),視覺重心落在 `cx` 附近。

兩者的重心差半個像素,在 16px 的小圖示上就是使用者看到的「直線沒對齊箭頭中心、偏左」。這與使用者的描述「直線應該右移一點」完全吻合。

## 已確認的產品決策

1. **more-actions 按鈕整個移除,不是隱藏、不是停用。** 保留一個永遠不會啟用的隱藏控制項只是死碼。若未來要加回來,屆時另開票重建即可(`AGENTS.md`:「Avoid backwards-compatibility hacks…If you are certain that something is unused, you can delete it completely.」)。
2. **移除後五顆版型按鈕的排版必須重新正確計算並維持右對齊。** 右側邊距(`margin`)維持不變,使五顆按鈕的右緣落在原本 more-actions 按鈕右緣的位置——也就是視覺上整組按鈕往右移,而不是留下空白。實作 agent 需截圖確認右對齊正確。
3. **Up 圖示的修正方式:把直立桿的 x 座標補償半個畫筆寬度。** 具體做法由實作 agent 決定,可接受的方案包括:
   - 把桿畫在 `cx + pen_width / 2`(對 2px 筆即 `cx + 1`);
   - 或改用奇數畫筆寬度讓 GDI 能真正置中;
   - 或改用 `Segoe MDL2 Assets` 的 Up 字型圖示(`U+E74A` 或 `U+E70E`),與 refresh 圖示(PD-052 第二次交接已改用 `U+E72C`)採取一致的做法。
   **第三種方案在一致性上最佳,而且完全迴避手算幾何的問題——PD-052 的交接區記載 refresh 圖示用 `Arc()` 手算失敗兩輪後才改用字型圖示成功,這是本專案已經付過學費的教訓。實作 agent 應優先評估此方案,並在交接區說明最終選擇與理由。**
4. **本票不改 back/forward 圖示。** 它們用的是 Common Controls 的官方 history bitmap(`ImageList_DrawEx`,第 651-665 行),不是手繪,沒有這個問題。
5. **本票不改 refresh 與 view 圖示。** refresh 已在 PD-052 修好;view 的四方塊使用者沒有提出問題。**但若實作 agent 選擇了決策 3 的字型圖示方案,view 圖示是否也一併改用字型圖示,由實作 agent 判斷——若改動很小且視覺更一致,允許順手處理並記錄;若會擴大風險,留給後續票。**
6. **`kMoreActionsButtonId` 移除後,`kLayoutButtonIdBase + 5` 這個 ID 值變成未使用。不要把其他 ID 挪過去填補**——ID 區段的穩定性比緊湊性重要,挪動會讓未來讀 diff 的人誤以為有語意變化。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Avoid backwards-compatibility hacks like renaming unused _vars, re-exporting types, adding // removed comments for removed code, etc. If you are certain that something is unused, you can delete it completely.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Read the relevant spec section and trace every caller before touching shared code.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes.

`docs/tickets/PD-029`(本票覆寫其決策 2):
> more-actions 按鈕作為視覺佔位符加入,永遠停用,不掛選單。
>
> **本票明確覆寫這個決策:使用者判斷此佔位符「暫時不用」,予以移除。** 若未來要恢復,需另開新票並在該票說明覆寫理由。

## Files to read and trace first

- `src/app_shell/main.cpp` 第 78、317、740-770、1227、1236-1270、2647-2652、2674-2682、2835、2866-2867 行——**more-actions 的全部涉及位置,必須逐一清乾淨。**
- `src/app_shell/main.cpp` 第 1220-1281 行(`layout_header`)——排版重算的核心區塊。
- `src/app_shell/main.cpp` 第 638-738 行(`draw_navigation_icon_button`)——Up 圖示在第 690-696 行;**第 697-722 行的 refresh 分支是「改用字型圖示」的既有範例,含 `navigation_refresh_font` 的 lazy cache 與 fallback 模式,可直接參考。**
- `src/app_shell/main.cpp` 的 `navigation_refresh_font` / `release_navigation_refresh_font`——字型快取與釋放的既有模式。
- `docs/tickets/PD-052-pane-refresh-and-view-mode-switcher.md` 的交接區——refresh 圖示從 `Arc()` 手算失敗到改用字型圖示的完整歷程,本票的重要參考。
- `docs/tickets/PD-043-navigation-button-explorer-style-icons.md`——導覽圖示的既有決策。

## Scope

1. 完整移除 more-actions「...」按鈕及其所有相關程式碼,並重算 `layout_header` 的排版。
2. 修正 Up 圖示直立桿與箭頭中心的對齊。

## Non-goals

- 不改 back/forward 圖示。
- 不改 refresh 圖示。
- 不改版型按鈕的顏色、大小或分段外框(PD-046/PD-047/PD-056)。
- 不加 hover 特效(PD-058)。
- 不改導覽列的整體排版比例(`navigation_geometry` 的五按鈕分配維持不變)。
- 不把其他 ID 挪到空出來的 `kLayoutButtonIdBase + 5`。

## Acceptance

1. 頂部工具列不再有「...」按鈕,原本它佔用的空間沒有留下空白。
2. 五顆版型按鈕仍然正確右對齊,右側邊距與修改前一致。
3. `rg -n "more_actions|MoreActions|kMoreActionsButtonId"` 在 `src/` 下**零筆結果**(除了本票可能在 `docs/` 留下的記錄)。
4. 五顆版型按鈕的 tooltip 仍正常;不再有「More actions」tooltip。
5. 版型按鈕的 highlight(PD-047/PD-056)未回歸。
6. Up 圖示的直立桿與箭頭尖端在放大 3 倍檢視下**視覺上對齊同一條中心線**。
7. Up 圖示在 disabled 狀態下的顏色仍正確。
8. Up 圖示在 150%/200% 顯示縮放下正確縮放且仍對齊。
9. 若採用字型圖示方案:字型建立失敗時的 fallback 路徑存在且可見,`HFONT` 在 `WM_DESTROY` 正確釋放。
10. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
11. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 這一條必須是零筆結果
rg -n "more_actions|MoreActions|kMoreActionsButtonId" src\
rg -n "draw_navigation_icon_button|navigation_refresh_font|kLayoutButtonIds" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:截圖頂部工具列確認「...」已移除且五顆按鈕右對齊;
# 截圖 Up 按鈕放大 3 倍確認對齊;切換顯示縮放比例再確認。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 而不是 `Graphics.CopyFromScreen`。**Up 圖示只有 16px,必須用 `InterpolationMode = NearestNeighbor` 放大至少 3 倍(建議 6 倍)才能判斷 1px 的對齊差異。** 修改前先截一組基準圖做前後對照。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- more-actions 移除後 `layout_header` 排版計算的最終形式(`kButtonSlotCount`、`total_width` 等)。
- Up 圖示最終採用哪一種修正方案(座標補償 / 奇數筆寬 / 字型圖示)與理由。
- 若採用字型圖示:使用的 codepoint、fallback 行為、`HFONT` 的生命週期管理。
- View 圖示是否一併改用字型圖示,以及判斷依據。
- Up 圖示放大 6 倍的修改前後對照截圖。
- 頂部工具列右對齊的修改前後對照截圖。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-26 實作記錄

**缺口一:more-actions 按鈕已完整移除。** 票上列的九個位置全部清乾淨;`rg -n "more_actions|MoreActions|kMoreActionsButtonId" src\` 零筆。`kLayoutButtonIdBase + 5` 空出未挪用(決策 6)。

**`layout_header` 最終形式**(`kButtonSlotCount` 整個移除,不再有 +1 slot):
- `button_width = max(1, min(scaled(kLayoutButtonWidth), max(0, client.right − sidebar_width − 2·margin − 4·segment_gap) / 5))`
- `total_width = 5·button_width + 4·segment_gap`
- 右對齊不變:`x_start = max(sidebar_width + margin, client.right − margin − total_width)`

**實測右對齊**(1400px 寬、100% DPI):修改前 more-actions 右緣 = 1380;修改後 layout_last(ID 404)右緣 = 1380,右側邊距完全一致,無殘留空白。截圖:`PD-064-before-toolbar-3x.png` / `PD-064-after-toolbar-3x.png`(本目錄)。

**缺口二:Up 圖示採用決策 3 的字型圖示方案**——`Segoe MDL2 Assets` `U+E74A`,與 PD-052 的 refresh(`U+E72C`)一致,完全迴避 GDI 偶數筆寬無法置中的問題。實作上把 PD-052 的 `navigation_refresh_font`/`release_navigation_refresh_font` 更名為共用的 `navigation_icon_font`/`release_navigation_icon_font`(process-lifetime lazy cache,`WM_DESTROY` 釋放),並抽出 `draw_navigation_font_glyph(item, glyph, color)` 供 up/refresh 兩分支使用。**字型建立失敗的 fallback**:原本的筆畫箭頭,直立桿 x 補償 `+pen_width/2`(決策 3 第一案),可見且對齊。**View 圖示(四方塊)未一併改字型**:使用者未反映問題,改動會擴大風險,留給 PD-075(該票本來就計畫統一五個圖示)。

**像素探測實證**(100% DPI,Up 按鈕內相對座標,非背景色像素欄位):
- 修改前:箭頭尖端 row6–10 中心 ≈ col 16,直立桿 row11–15 佔 col 15、16(中心 15.5)→ 偏左 0.5px,與票上根因一致。
- 修改後:尖端 row6–7 與直立桿 row14–15 同為 col 15、16 → 同一中心線。6 倍對照截圖:`PD-064-before-up-6x.png` / `PD-064-after-up-6x.png`。

**重大發現:tooltip 自 PD-039 起從未顯示過(既有缺陷,非本票回歸),已於本票一併修復。** 驗收 4 要求 tooltip 正常,實機 hover 卻完全沒有 tooltip;用 stash 對照舊版建置同樣沒有,確認是既有問題。根因:**PaneDock 沒有任何 application manifest,行程載入的是 comctl32 v5**;v5 的 `TTTOOLINFOW` 沒有 `lpReserved` 欄位,`TTM_ADDTOOLW` 收到 `cbSize = sizeof(TOOLINFOW)`(x64 為 72)直接拒絕並靜默回傳 FALSE,五顆按鈕的工具從未註冊成功。修法:新增 `resources/panedock.manifest`(宣告 `Microsoft.Windows.Common-Controls 6.0.0.0` side-by-side 依賴)並在 `resources/panedock.rc` 加 `1 24 "panedock.manifest"`。修復後實機 hover(滑鼠移動 + UIA 讀取)量得五個 tooltip 文字全部正確:Single pane / Two panes side by side / Two panes stacked / Three panes / Four panes;原 more-actions 位置無「More actions」tooltip。**注意:v6 同時啟用 visual styles,已比對全窗截圖,位址列圓角、tab、版型按鈕高亮(驗收 5)均無回歸。**

**Disabled 顏色(驗收 7)**:以 `EnableWindow(FALSE)` + 重繪實測,Up 桿部 6×6 區域平均色 enabled = R228 G231 B233、disabled = R244 G245 B247,兩者明確不同(核心色 90,102,122 vs 190,197,209 經背景稀釋後的預期值吻合),disabled 路徑正確。

**DPI 150%/200%(驗收 8):未實測**——本機只有一面 100% 縮放的螢幕,無法在不更動使用者系統設定的前提下量測。字型圖示的縮放路徑(`scaled_value(button, 16)` 的 em size + `DrawTextW` 於 DPI 縮放後的 `rcItem` 置中)與 PD-052 的 refresh 圖示完全相同,而 PD-052 交接區同樣未做 150/200% 實測;此項仍待有雙螢幕/可調縮放環境時補驗。

**ClearType 備註**:字型圖示以 `CLEARTYPE_QUALITY` 建立(沿用 PD-052),6 倍放大截圖可見次像素色邊;1x 實際大小不明顯,與 refresh 一致,未另行處理。

**驗收檢查全數通過**:`cmake --build build`、`ctest`(4/4)、`rg` 零筆、`git diff --check`。建置產物在 `build\pd062-output\PaneDock.exe`(非票上寫的 `build\PaneDock.exe`);截圖驗證腳本留在 `build\pd064_*.ps1`(untracked),以 `PrintWindow(PW_RENDERFULLCONTENT)` + `InterpolationMode.NearestNeighbor` + `Bitmap.GetPixel` 取色。
