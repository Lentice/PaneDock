# PD-083 — Pane 導覽列五個按鈕(back/forward/up/refresh/view)整排右移 15px、下移 2px

Phase 7 · app_shell · Depends on: PD-075

- Source: 使用者實機比對後直接提出(2026-08-27)。
- Origin: 使用者原文:「pane navigate buttons 往右移 15px 往下移 2px」。
- Priority: LOW——純位置微調,沒有功能缺陷。

## 範圍界定與已確認的實作方式(不是新的根因調查)

五個導覽按鈕是**真正獨立的 `BUTTON` 子視窗**(`state.back_buttons`/`forward_buttons`/`up_buttons`/`refresh_buttons`/`view_mode_buttons`),不是像 tab 的「+」那樣畫在共用 DC 上的圖案。它們的位置由 `apply_layout`(`src/app_shell/main.cpp`,約第 1846-1861 行)的迴圈決定:

```cpp
const NavigationGeometry geometry = navigation_geometry(window, pane_rect);
const int navigation_top = geometry.navigation_top;
const int navigation_height = geometry.navigation_height;
const std::array<HWND, 5> buttons{...};
int x = pane_rect.left;
for (HWND button : buttons) {
    SetWindowPos(button, nullptr, x, navigation_top,
                 geometry.button_width, navigation_height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ShowWindow(button, SW_SHOW);
    x += geometry.button_width;
}
```

**移動這一整排按鈕的真正 HWND 位置,不是移動按鈕內部畫的圖示(owner-draw 的 `DRAWITEMSTRUCT` 內容會自動跟著整個按鈕視窗移動,不需要另外調整 `draw_navigation_icon_button` 內部的繪圖座標)。**

單純把迴圈的起始 `x`/`navigation_top` 各加一個偏移量,會產生兩個必須一併處理的副作用,兩者都已在 `navigation_geometry()`(第 557-574 行)裡有明確的程式碼依據,實作前務必先讀懂這個函式:

1. **地址列會被蓋到。** `navigation_geometry()` 算出的 `address_background`(進而是 `address_left`)是 `pane_rect.left + button_width * 5`——這個算式完全沒有考慮按鈕列本身有沒有位移。若只把按鈕右移 15px 而不動 `address_left`,五個按鈕的實際涵蓋範圍會變成 `[pane_rect.left+15, pane_rect.left+15+button_width*5]`,比 `address_left` 多出 15px,導致最後一顆(`view_mode_buttons`)按鈕視覺上會插進地址列的圓角背景 pill 裡。**`address_left` 必須跟著同一個水平偏移量一起加,維持「按鈕列右緣 = 地址列左緣」的既有關係不變。**
2. **按鈕會蓋到下方的檔案清單。** 按鈕高度目前直接等於 `navigation_height`(整個導覽列的高度),而下方 Shell view 容器的 `rect.top` 是用 `navigation_top + navigation_height` 算出來的(第 1877-1878 行,`navigation_top` 是**未位移**的原始值)。若只把按鈕的 `y` 往下加 2px、高度仍是原本的 `navigation_height`,按鈕的下緣會變成 `navigation_top + 2 + navigation_height`,比容器的 `rect.top`(`navigation_top + navigation_height`)多 2px,等於按鈕蓋進檔案清單容器的頂端 2px。**下移必須用「按鈕的 y 往下加 2px、同時按鈕高度減少 2px(`navigation_height - 2`)」的方式做,讓按鈕下緣維持在原本的 `navigation_top + navigation_height`,不侵入容器區域;地址列與容器本身的其餘幾何(頂端位置、高度)一律不動。**

## 已確認的產品決策

1. 只調整 `apply_layout` 內按鈕 `SetWindowPos` 迴圈的 `x`/`y`/`高度` 三個值,不動 `navigation_geometry()` 的回傳值定義,也不動 `address_background`/地址列/容器/狀態列自己的計算式——改成在**呼叫端**(迴圈本身)疊加位移量,`address_left` 的疊加則在 `navigation_geometry()` 內部一併加(它本來就是唯一計算 `address_left` 的地方,兩處位移量必須是同一個常數來源,不要各自硬編兩次)。
2. 位移量(15px 右、2px 下)與按鈕高度縮減量(2px)都是 96-DPI 基準,必須用 `scaled_value` 依目前視窗 DPI 換算,不可寫死裝置像素。
3. 常數化:比照本檔案既有的 `kNavigationButtonWidth`/`kNavigationBarHeight` 命名慣例,新增例如 `kNavigationButtonOffsetX = 15`、`kNavigationButtonOffsetY = 2` 兩個常數,不要把數字直接寫在呼叫點。
4. `draw_navigation_icon_button`/`draw_navigation_font_glyph`/`draw_navigation_fallback_glyph` 內部完全不用改——它們畫的座標系是相對於自己收到的 `item.rcItem`(即按鈕自身的 client rect),按鈕整體移動後這些函式自動跟著正確,不需要額外偏移。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Read the relevant spec section and trace every caller before touching shared code.

——`navigation_geometry()` 有兩個呼叫點(`apply_layout` 與繪製 pane 背景的另一處,約第 1743 行),兩處都要讀,確認位移只影響按鈕/地址列的相對關係,不影響 `draw_pane_card`/`draw_navigation_bar_background` 的其他繪製。

## Files to read and trace first

| 檔案 | 讀什麼 |
|---|---|
| `src/app_shell/main.cpp` `NavigationGeometry`/`navigation_geometry()`(約第 548-574 行) | **本票主要修改處之一。**`address_left`、`button_width`、`navigation_top`/`navigation_height` 的既有算式。 |
| `src/app_shell/main.cpp` `apply_layout` 內按鈕 `SetWindowPos` 迴圈(約第 1846-1861 行) | **本票主要修改處之二。** |
| `src/app_shell/main.cpp` 約第 1877-1889 行 | Shell view 容器 `rect.top`/狀態列 `status_rect` 的計算,確認本票不改動它們,只確保按鈕的新下緣不超過這裡用到的 `navigation_top + navigation_height`。 |
| `src/app_shell/main.cpp` 約第 1743-1747 行(`navigation_geometry` 第二個呼叫點) | 確認這裡只用到 `geometry.address_background` 畫背景 pill,`address_left` 位移後這裡會自動吃到新值,不需要額外修改。 |
| `docs/tickets/PD-075-unify-pane-chrome-icon-style.md` | 五個圖示目前的繪製技術(字型字符),確認本票不動圖示本身,只動整排按鈕的容器位置。 |

## 非目標

- 不改任何圖示的繪製方式、字型、顏色(PD-075 範圍)。
- 不改地址列 EDIT 的 inset 邏輯(PD-031 範圍)。
- 不改 Shell view 容器、狀態列的高度或位置。
- 不改 tab strip、「+」按鈕(PD-081/PD-082 範圍)。
- 不改 `kNavigationButtonWidth`/`kNavigationBarHeight` 本身的數值。

## 驗收條件

1. 五個導覽按鈕的視覺位置相對原本整排右移 `scaled_value(window, 15)`、下移 `scaled_value(window, 2)`(96 DPI 基準)。
2. 地址列的圓角背景 pill 與最後一顆按鈕(view mode)之間沒有視覺重疊。
3. 按鈕下緣沒有侵入下方 Shell view 容器的可視範圍(容器頂端沒有被按鈕遮住)。
4. 五個按鈕的點擊(back/forward/up/refresh/開啟檢視模式選單)全部正常運作,沒有因為位置改變而點不到或點到旁邊的控制項。
5. 96 DPI 與一個高 DPI 設定(例如 150%)下位移量與縮減的按鈕高度都正確依比例縮放。
6. `cmake --build build` 與既有 CTest 全數通過。
7. `git diff --check` 無尾隨空白。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "navigation_geometry|kNavigationButtonWidth|kNavigationBarHeight" src\app_shell\main.cpp
git diff --check
```

**驗證要快、要最小化。** 只需要一次單擊 + 單張截圖證明按鈕整排位移且沒有蓋到地址列/檔案清單即可,不要做多 DPI、多情境的完整比對展示。若用 computer-use 截圖,擷取後立刻結束該互動 session。其餘(例如逐一點擊五個按鈕驗證功能)可留在交接區標記未驗證,交回使用者確認。

## Handoff requirements

- 新增的偏移常數名稱與數值。
- `address_left`/`navigation_top`/按鈕高度的最終算式(展示 diff 片段即可)。
- 單張截圖(若有取得),證明按鈕沒有蓋到地址列或檔案清單。
- 未驗證項目與原因(若有)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-27 實作交接

- 新增 96-DPI 基準常數：`kNavigationButtonOffsetX = 15`、`kNavigationButtonOffsetY = 2`；兩者在使用點都透過 `scaled_value` 換算。
- 最終幾何：`address_left = pane_rect.left + button_width * 5 + button_offset_x`；按鈕列起點為 `pane_rect.left + button_offset_x`，y 為 `navigation_top + button_offset_y`，高度為 `navigation_height - button_offset_y`，所以按鈕下緣仍為 `navigation_top + navigation_height`。`navigation_top`、`navigation_height`、`address_background.top`、Shell view container 與 status bar 幾何均未改動。
- `navigation_geometry()` 的第二個呼叫點仍只消費 `geometry.address_background`，會自動取得新的 `address_left`。
- 驗證：`cmake --build build` 通過；`ctest --test-dir build --output-on-failure` 通過，5/5；ticket Agent checks 的 `rg` 與 `git diff --check` 後續檢查均通過。
- 未擷取單擊／PrintWindow 截圖，也未進行五個按鈕逐一點擊或 150% DPI 實機驗證；依本票最小化驗證政策留待使用者確認。未新增獨立測試，因本幾何配置直接依賴 app_shell 的 HWND/Win32 DPI，既有 CTest 無對應 seam。

### 2026-08-27 — 使用者實機比對後微調水平位移量

使用者實機比對後回報:「nav button row shifted right 12px」——把 `kNavigationButtonOffsetX` 從 15 改為 12。`kNavigationButtonOffsetY`(2)與其餘幾何(`address_left` 隨 `button_offset_x` 連動、按鈕高度隨 `button_offset_y` 縮減以維持下緣不變)完全沿用上面實作交接的邏輯,不需要改動,只改了常數值本身。`cmake --build build` 成功、`ctest --test-dir build --output-on-failure` 5/5 PASS。
