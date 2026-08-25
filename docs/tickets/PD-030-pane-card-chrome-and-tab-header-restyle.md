# PD-030 — Pane 卡片背景(圓角上緣＋陰影)與 tab header 圖示化重繪

Phase 6 · app_shell · Depends on: PD-028, PD-029

- Source: `docs/panedock-ui-prototype.html?refined=1&variant=1&solo=1`(Quiet Header 變體)
- Origin: 2026-08-25,同 PD-028/PD-029 的使用者回報。設計稿每個 pane 是一張白底、圓角、有輕微陰影的卡片,pane 最上方一列顯示彩色資料夾圖示 + 資料夾名稱(粗體)+ 右上角 "+"。目前 pane 是貼齊畫布、無圓角、無陰影的方形區塊,最上方是原生 tab 控制項顯示純文字分頁標籤。
- Priority: MEDIUM——視覺影響最大的一塊,但技術風險也最高(涉及既有 `SysTabControl32` 的 owner-draw 改造),需要獨立於 PD-028/029 的簡單改動之外驗證。

## 已確認的架構決策(先讀這節,避免重工)

**不新增每個 pane 的外層容器 HWND。** 目前分析過在每個 pane 外面包一層容器 HWND 是否必要,結論是不必要:tab strip 與導覽列(back/forward/up + address bar)已經是不透明的原生控制項,真正需要畫「卡片」視覺(白底圓角上緣、細邊框、陰影)的地方只有 tab strip 上緣與 pane 四周的窄邊——這些區域目前本來就由 `paint_client_background` 在主視窗背景上直接畫出。因此本票延續同一做法:**在 `paint_client_background` 裡,對每個目前可見 pane 的矩形(用既有的 `layout_rects`/`to_win32_rect` 算出),額外畫一層卡片背景(白底、細邊框、上緣圓角、偏移陰影),再讓既有的 tab strip/導覽列/Shell view 子視窗疊在其上。** 這比新增容器 HWND 少一層視窗階層、少一組訊息轉發,且不影響 `AGENTS.md` 對「每個 `Initialize` 過的 `IExplorerBrowser` 必須 `Destroy`」與「保活式 Group 切換不得 destroy/recreate pane HWND」的既有規則——這些規則管的是 `ExplorerHost`,本票完全不動它的生命週期。

**只圓上緣(左上、右上兩角),下緣維持方角。** 真實 Shell view(`IExplorerBrowser` 的內容區)貼著 pane 矩形下緣,若下緣也畫圓角背景,Shell view 內容的方形邊角會蓋在圓角外面,反而比不圓角更難看,且要讓 Shell view 內容本身呈現圓角需要 `SetWindowRgn` 或額外裁切邏輯,這是先前已排除的做法(見 `AGENTS.md` 對 `IExplorerBrowser` 生命週期的既有限制與 §已知邊界)。上緣是我們自己畫的 tab strip/header,可以自然地圓角;下緣接觸真實 Shell view 的地方維持方角,邊框仍畫滿一圈以維持卡片的「有邊界」視覺。若之後要做完整四角圓角,需要先有一個能安全裁切 `IExplorerBrowser` HWND 的方案,列入 `docs/tickets.md` 候選,不在本票。

## 已確認的產品決策

1. **卡片陰影用簡單的偏移純色矩形近似,不做真實模糊。** 在卡片背景矩形右下方偏移 2px(依 DPI 縮放)畫一層更深的灰階 `RoundRect`,再在其上疊卡片本體,製造出「有厚度」的視覺效果。不使用 `AlphaBlend`/`GradientFill` 或任何逐像素模糊運算——原生 GDI 沒有內建高斯模糊,自己刻一個超出本票的視覺投資報酬率。
2. **Tab strip 改為 `TCS_OWNERDRAWFIXED`,由 `WM_DRAWITEM` 自繪每個分頁項目:一個固定顏色的資料夾圖示符號 + 分頁顯示名稱(active tab 用粗體,非 active 用一般字重)。** 資料夾圖示是我們自己畫的裝飾符號(純色 `RoundRect` 加一個小缺角或簡單梯形,類似常見的資料夾剪影),**不是**呼叫 `SHGetFileInfo` 取得該資料夾的真實系統圖示——這一點刻意避免碰觸 Shell 圖示 API,因為 `CONTEXT.md` 明訂「Shell view 內部畫的東西屬於 Windows,不屬於 PaneDock」,而 pane header 的這個小圖示是我們自己的 chrome 裝飾,不是 Shell view 的一部分,用固定色的簡化符號比呼叫圖示 API、管理 `HICON` 生命週期更省。既有 `+`(新增分頁)項目維持在最後一格,不加圖示。
3. **卡片邊框色與底色:白底 `RGB(255,255,255)`、邊框 `RGB(223,229,236)`(沿用 PD-028/現有 quiet header divider 的邊框色,保持全應用一致)、陰影 `RGB(210,216,224)` 或更深一階,實際數值由實作時視覺比對設計稿微調並記錄於交接區。**
4. **`TCS_OWNERDRAWFIXED` 改變後,既有 `refresh_tab_strip`/`TCM_INSERTITEMW`/`TCM_SETCURSEL`/`TCM_DELETEALLITEMS` 呼叫與 tab 點擊/命中測試邏輯完全不變。** owner-draw 只影響繪製,不影響 `TCN_SELCHANGE` 等既有通知處理路徑;`WM_NOTIFY` 分派邏輯不需要修改。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive.

`AGENTS.md`:
> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups.

`CONTEXT.md`:
> **Shell view**: The Windows-supplied folder view hosted inside a realized tab... Everything the Shell view draws — icons, thumbnails, columns, selection rendering — belongs to Windows, not to PaneDock.
> **tab**: One navigable location within a pane. A tab owns its location, view mode, sort order and navigation history.
> _Avoid_ (pane): window, view, frame, split

`docs/design-spec.md` §9.1:
> `explorer_host` 負責「每個已 realize pane 的 `IExplorerBrowser`、site 物件、生命週期、事件」,不得負責「產品層決策、持久化」。

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `paint_client_background`、`layout_rects`、`to_win32_rect`、`pane_content_area`、`apply_layout`(tab strip/導覽列/Shell view 的 rect 計算與 `SetWindowPos` 順序)、`kTabStripHeight`、`AppState::tab_strips`。
- `refresh_tab_strip`/`tab_display_text`——目前 `TCM_INSERTITEMW` 只填 `TCIF_TEXT`,owner-draw 改造需要在 `WM_DRAWITEM` 內自己用 `TabCtrl_GetItem`/既有的 `active_group(state).panes[pane_index].tabs` 資料重新取得每個 tab 的顯示文字與是否為 active(不能單純依賴 `TCITEMW` 裡沒有的欄位)。
- `src/explorer_host/explorer_host.h`/`.cpp`——確認 Shell view 內容 HWND 的最終定位方式(`rect.top = navigation_top + navigation_height` 這行之後,`initialize`/`set_rect` 是否還有額外 padding),本票新增的卡片邊框/圓角背景不能讓 Shell view 內容被裁切或位移。
- `docs/panedock-ui-prototype.html` refined variant 1 的 pane 卡片 CSS(圓角半徑、陰影 `box-shadow` 數值、header 圖示與字重)——需要重新讀取檔案取得精確數值,不要憑截圖臆測。
- `src/app_shell/main.cpp` 中既有 `draw_layout_glyph`/`draw_sidebar_action_button` 的純 GDI 繪製手法,新的資料夾圖示繪製函式應該沿用同樣的 `CreateSolidBrush`/`CreatePen`/`SelectObject`/`DeleteObject` 生命週期風格,不引入新的繪製抽象層。

## Scope

1. 新增一個純函式(例如 `draw_pane_card(HDC dc, RECT pane_rect, UINT dpi)`),在 `paint_client_background` 對每個目前可見 pane(依 `has_active_group`/`active_group(state).panes.size()` 判斷,只畫真正顯示的 pane 數)呼叫:先畫偏移陰影矩形,再畫白底卡片本體(`RoundRect` 只套用在矩形頂部——做法可以是先畫一個完整 `RoundRect`,再用一個方角矩形蓋掉下半部剩餘的圓角,或直接用兩個 `LineTo`/`FillRect` 手繪只有上緣圓角的路徑,擇一,以視覺結果為準),邊框用 `FrameRect`/`Rectangle` 沿卡片矩形描一圈。
2. `paint_client_background` 的呼叫點需要能取得 `AppState`(目前簽章只有 `HWND window, HDC dc`)——確認呼叫處(`WM_PAINT`/`WM_ERASEBKGND`)是否已經能存取 `state`,若沒有,把 `paint_client_background` 簽章加一個 `const AppState&` 參數,呼叫端從既有的 `GetWindowLongPtrW`/全域指標取得(比照其他訊息處理函式既有的取得 `AppState*` 方式,不新增第二套機制)。
3. `AppState::tab_strips` 的建立(`WM_CREATE` 建立 `SysTabControl32` 處)加上 `TCS_OWNERDRAWFIXED` 樣式。
4. 新增 `WM_DRAWITEM` 分支:當 `CtlType == ODT_TAB` 且 `hwndItem` 命中 `state.tab_strips` 其中之一時,呼叫新的 `draw_tab_item(const DRAWITEMSTRUCT&, const wchar_t* text, bool is_add_button, bool active)` ——若 `dwItemSpec` 等於 pane 目前 tab 數(也就是最後一格的 `+`),`is_add_button = true`,只畫 `+` 文字置中,不畫資料夾圖示;否則畫資料夾圖示 + 文字,文字用 `active_tab_id` 比對決定是否用粗體字型(`CreateFontIndirectW` 加粗一份,比照 owner-draw 按鈕現有拿 `WM_GETFONT` 再加粗的做法,若目前程式碼庫沒有這個手法就新增,但只在這一個函式內建立/釋放,不做成共用資源快取——tab 數量最多 4×若干 tab,重複建立字型物件的成本可忽略)。
5. 新增資料夾圖示繪製的小工具函式,固定尺寸與顏色,繪製手法比照 `draw_layout_glyph`(`FrameRect`/`MoveToEx`/`LineTo` 或 `Polygon` 畫簡化資料夾剪影)。

## Non-goals

- 不新增每個 pane 的容器 HWND(見上方已確認的架構決策)。
- 不圓下緣角、不裁切 Shell view 內容本身的視覺邊角(見上方已確認的架構決策)。
- 不呼叫任何 Shell 圖示 API(`SHGetFileInfo`/`IExtractIcon`)取得真實資料夾圖示,pane header 圖示是固定色的裝飾符號。
- 不做卡片的真實模糊陰影或半透明效果。
- 不修改 `refresh_tab_strip` 的 `TCM_*` 呼叫序列或 tab 點擊/`TCN_SELCHANGE` 分派邏輯,只改繪製。
- 不動 `explorer_host` 模組本身。

## Acceptance

1. 每個可見 pane 顯示白底卡片背景,上緣圓角、四周有邊框、右下方可見偏移陰影,視覺上與其餘 pane 之間有明確的卡片分隔感(對照設計稿截圖)。
2. Tab strip 每個分頁項目顯示資料夾圖示 + 名稱,active tab 的名稱以粗體呈現;`+` 分頁維持在最後一格且不畫圖示。
3. 既有 tab 點擊切換、新增 tab(`+`)、關閉 tab(既有中鍵/右鍵或既有互動,若有)行為與改版前完全一致(owner-draw 不得改變任何 `TCM_*`/`WM_NOTIFY` 行為)。
4. 拖曳分隔線改變版型比例、新增/刪除 Group、切換 layout 版型時,卡片背景與陰影正確跟著新的 pane 矩形重繪,沒有殘影(可透過反覆觸發 `apply_layout` 後截圖比對,或至少人工於真實桌面操作驗證)。
5. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
6. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
.\build\PaneDock.exe
# 手動:確認卡片背景/陰影正確跟隨每個 pane 矩形、tab 圖示與粗體 active 樣式、
# 拖曳分隔線與切換版型時無殘影、既有 tab 新增/切換行為不變
```

## Handoff requirements

- 卡片圓角半徑、邊框色、陰影偏移量與顏色的最終數值(96-DPI 基準)。
- `paint_client_background` 簽章變更後的最終呼叫點寫法。
- 資料夾圖示繪製函式的最終視覺參數,供之後任何要再美化這個符號(例如依副檔類型變色)的 ticket 參考——若有這類需求,先記錄在 `docs/tickets.md` 候選,不在本票展開。
- 若真實桌面測試發現 owner-draw tab 在高 DPI 或深色系統主題下有已知渲染限制,記錄下來。
- 未來要做完整四角圓角(裁切 Shell view 本身)所需的技術路徑評估(是否可行、需要什麼 Win32 機制),即使結論是「目前不做」,也要把調查結果寫下來供候選清單參考。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-25 實作交接

#### 完成內容(對照 Scope 1–5)

- `src/app_shell/main.cpp` 新增 `draw_pane_card(HDC dc, RECT pane_rect, UINT dpi)`:純函式,不吃 `HWND`/`AppState`,只依賴傳入的 pane 矩形與 DPI。做法照 Scope 1 的兩種擇一方案採「先畫完整 `RoundRect`,再用方角矩形蓋掉下半部」:先畫一層偏移 `shadow_offset` 的純色 `RoundRect` 陰影,再畫白底 `RoundRect` 卡片本體、用 `FillRect` 蓋掉 `card.top + radius` 以下的區域把下緣方角化,最後用 `HPEN` 疊一次 `RoundRect` 外框(圓角上緣)加三條 `MoveToEx`/`LineTo`(左邊、下緣、右邊)把邊框的下緣兩角覆寫成直角。三個步驟都是獨立的 `CreateSolidBrush`/`CreatePen`/`SelectObject`/`DeleteObject` 生命週期,沒有引入共用資源快取,風格與 `draw_layout_glyph`/`draw_sidebar_action_button` 一致。
- **架構決策以外新增的一個工程判斷:card 矩形比 `pane_rect` 向外(左/上/右)outset 2px@96DPI,下緣維持與 `pane_rect.bottom` 完全齊平。** 原因:tab strip 的 `SetWindowPos` 呼叫(`apply_layout`)把它精確定位在 `pane_rect` 本身(寬度=pane 寬、起點=pane 左上角),如果卡片矩形跟 `pane_rect` 完全相同,圓角/邊框的像素會整個被不透明的 `SysTabControl32` 蓋住,人眼完全看不到「圓角」與「上緣邊框」——這正是 Acceptance 1 要求可見的東西。既有版面在每個 pane 之間本來就留有 `divider_thickness`(96DPI 下 8px,經 `scaled_value` 縮放)的間距、外圈則有 `kPaneCanvasPadding`(15px)的留白,兩者都遠大於 2px,所以把卡片矩形向外擴 2px 完全落在既有的留白/分隔線間距內,不會跟鄰居 pane 的卡片重疊,也不影響 `layout_rects`/`apply_layout`/tab strip/導覽列/Shell view 的任何既有矩形計算(它們全部維持用原本的 `pane_rect`,只有畫卡片背景這一步驟用擴大過的矩形)。這個 outset 值與陰影偏移量(2px@96DPI)刻意取相同量級,兩者都用 `MulDiv(2, dpi, 96)` 各自獨立算,沒有互相依賴。
- **卡片圓角、邊框、陰影最終數值(96-DPI 基準,已寫入程式碼注釋)**:圓角半徑 `radius = MulDiv(10, dpi, 96)`(對照設計稿 `.pane { border-radius: 10px }`);邊框色 `RGB(223,229,236)`(沿用既有 quiet header divider/PD-028 邊框色,已確認的產品決策 3 給的數值,原樣採用未調整);陰影色從決策 3 建議的 `RGB(210,216,224)` 微調成 `RGB(205,211,219)`——實測發現在畫布底色 `RGB(243,246,249)` 上原建議值對比稍弱、幾乎看不出陰影厚度,深一階後在 96 DPI 螢幕截圖上可辨識但仍是「輕微」陰影,沒有超出決策 1「純色矩形近似」的範圍;陰影偏移 `shadow_offset = MulDiv(2, dpi, 96)`,與決策 1 的 2px 相符。
- 新增 `draw_folder_glyph(HDC dc, RECT icon_rect)`:固定色資料夾剪影,畫法是兩個 `Rectangle`(一個窄的「頁籤」矩形疊在一個寬的「本體」矩形上緣),`fill = RGB(255,216,115)`、`border = RGB(197,139,6)`,數值取自設計稿 `docs/panedock-ui-prototype.html` 第 203 行 `.folder-icon { color:#e7a514; fill:#ffd873; stroke:#c58b06; }` 的 fill/stroke 色。沒有呼叫任何 Shell 圖示 API(`SHGetFileInfo`/`IExtractIcon`),符合 Non-goals。
- 新增 `draw_tab_item(const DRAWITEMSTRUCT& item, const wchar_t* text, bool is_add_button, bool active)`:active 分頁底色改白(`RGB(255,255,255)`)、非 active 用 `RGB(248,250,252)`(對照設計稿 `.tab-button.active`/`.pane-tabs` 底色);非 `+` 分頁在文字左側畫 `draw_folder_glyph`,文字用 `DT_LEFT`,`+` 分頁不畫圖示、文字用 `DT_CENTER`;active 分頁的粗體字型透過 `SendMessageW(item.hwndItem, WM_GETFONT, 0, 0)` 取回目前字型的 `LOGFONTW`(`GetObjectW`)、把 `lfWeight` 改成 `FW_BOLD` 後 `CreateFontIndirectW` 建立,只在這次繪製呼叫內建立/`SelectObject`/繪製完立刻 `DeleteObject`,沒有做成常駐快取(對照 Scope 4 的「重複建立字型物件的成本可忽略」判斷,tab 數量上限就是每個 pane 的 tab 數 + 1 個 `+`,乘以 4 個可見 pane)。
- `AppState::tab_strips` 的 `CreateWindowExW(0, WC_TABCONTROLW, ...)` 加上 `TCS_OWNERDRAWFIXED`,其餘既有樣式位元(`WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP`)不變。
- 新增 `WM_DRAWITEM` 分支,`item->CtlType == ODT_TAB` 時用既有的 `tab_strip_index(*state, item->hwndItem)`(PD-020/PD-021 就已存在的函式,原本只給 `WM_NOTIFY` 用)反查 pane index,再用 `active_group(*state).panes[*pane_index]` 直接讀 `tabs`/`active_tab_id` 決定 `is_add_button`(`item_index >= pane.tabs.size()`)、顯示文字(`tab_display_text`)與 `active`(`pane.tabs[item_index].id == pane.active_tab_id`)——完全沒有讀取 `DRAWITEMSTRUCT::itemState` 的 `ODS_SELECTED` 位元或任何 `TCITEMW` 欄位判斷 active,對照 Files-to-read 那節提到的「不能單純依賴 `TCITEMW` 裡沒有的欄位」。這個分支放在既有 `ODT_BUTTON` 各分支之後、`state->sidebar.draw_item(...)` 呼叫之前,`CtlType` 不同不會互相誤判。
- 沒有新增 `WM_MEASUREITEM` 對 `ODT_TAB` 的分支——`SysTabControl32` 的分頁尺寸不像 `ODT_BUTTON`/owner-draw listbox 需要應用程式回報 `itemWidth`/`itemHeight`,tab 尺寸沿用控制項既有的自動量測邏輯,owner-draw 只換繪製,符合已確認的架構決策 4「owner-draw 只影響繪製」。

#### `paint_client_background` 簽章變更後的最終呼叫點

簽章改為 `void paint_client_background(HWND window, HDC dc, const AppState& state) noexcept`。唯一呼叫點在 `WM_ERASEBKGND`(`state != nullptr` 已經在外層判斷過)：

```cpp
case WM_ERASEBKGND:
    if (state != nullptr) {
        paint_client_background(window, reinterpret_cast<HDC>(wparam), *state);
        return 1;
    }
```

函式本體最後新增的卡片繪製迴圈：

```cpp
if (has_active_group(state)) {
    const auto& group = active_group(state);
    const auto rects = layout_rects(window, group);
    const UINT dpi = GetDpiForWindow(window);
    const std::size_t visible = std::min(group.panes.size(), rects.size());
    for (std::size_t index = 0; index < visible; ++index) {
        draw_pane_card(dc, to_win32_rect(rects[index]), dpi);
    }
}
```

`layout_rects`/`to_win32_rect`/`has_active_group`/`active_group` 都是既有函式,呼叫方式與 `apply_layout`/`pane_at_point` 等既有呼叫點一致,沒有新增第二套取矩形的機制。

#### 未做的事(對照 Non-goals,確認沒有超出範圍)

- 沒有新增每個 pane 的容器 HWND——卡片背景完全在 `paint_client_background` 這一個既有的背景繪製路徑裡多畫一層,沒有新視窗、沒有新訊息轉發。
- 沒有圓下緣角、沒有對 Shell view 內容本身做任何裁切或 `SetWindowRgn` 操作——`draw_pane_card` 畫完卡片後,`apply_layout` 對 tab strip/導覽列/`ExplorerHost::initialize`/`set_rect` 的矩形計算完全沒有改動一行,Shell view 的 `rect.top = navigation_top + navigation_height`、`rect.bottom = pane_rect.bottom` 維持原樣,已重新確認 `src/explorer_host/explorer_host.cpp` 的 `set_rect`/`initialize` 把傳入的 `rect` 原封不動交給 `SetWindowPos`,沒有額外 padding,所以卡片的 2px outset 不會讓 Shell view 內容被裁切或位移(outset 只影響卡片背景怎麼畫,不影響任何子視窗的 `SetWindowPos` 呼叫)。
- 沒有呼叫 `SHGetFileInfo`/`IExtractIcon` 或任何其他 Shell 圖示 API——資料夾圖示是純 GDI 畫的固定色剪影。
- 沒有用 `AlphaBlend`/`GradientFill` 或任何逐像素模糊——陰影是一層純色 `RoundRect`。
- 沒有動 `refresh_tab_strip`/`TCM_INSERTITEMW`/`TCM_SETCURSEL`/`TCM_DELETEALLITEMS` 呼叫序列,也沒有動 `WM_NOTIFY`(`TCN_SELCHANGE`)分派邏輯或 `close_tab_at_point`/`switch_active_tab`——這幾個函式簽章與呼叫序列跟 PD-030 之前完全一致,只是它們背後那顆 `SysTabControl32` 現在多了 `TCS_OWNERDRAWFIXED` 樣式與對應的 `WM_DRAWITEM` 繪製分支。
- 沒有動 `explorer_host` 模組本身的任何一行。

#### 驗證結果

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` 與 `cmake --build build`:成功,無警告無錯誤。
- `ctest --test-dir build --output-on-failure`:4/4 通過(`panedock_diagnostic_flag`、`panedock_core_model`、`panedock_core_layout`、`panedock_core_session`)——這四個既有測試都在 `src/core`,PD-030 完全沒有動 `core`,測試內容本身跟本票無關,通過只代表沒有連帶弄壞既有 `core` 邏輯。
- `git diff --check`:通過,無空白字元問題。
- 視覺驗證:啟動 `build\PaneDock.exe`,沿用 PD-028/PD-029 交接區記錄的手法(`System.Drawing.Graphics.CopyFromScreen` 對整個虛擬桌面截圖、`GetWindowRect` 裁切、`NearestNeighbor` 放大局部區域核對細節)。**確認以下項目**:
  - 每個可見 pane 顯示白底卡片,tab strip 上緣可見明確的圓角(左上/右上兩角),四周有 `RGB(223,229,236)` 邊框,與相鄰 pane 之間有清楚的卡片分隔感(對應 Acceptance 1)。放大局部截圖確認左上角圓角弧線清晰可辨。
  - Tab strip 每個分頁項目都顯示琥珀色資料夾圖示 + 分頁名稱(取自 `tab_display_text`,即路徑最後一段,例如 "C:\"、"Windows"、"Users"、"Program Files"),active 分頁(第一個)文字明顯比其他分頁粗;`+` 分頁維持在最後一格、只有置中的 "+" 文字、沒有畫圖示(對應 Acceptance 2)。
  - 陰影在畫面上可見但偏輕微(半透明模擬的純色矩形本來就無法做出真實模糊的厚度感),兩個左右相鄰 pane 之間的窄縫裡可以看到一小段深一階的灰色矩形邊緣。
  - **透過 `WM_COMMAND`(`SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(400,0)/MAKEWPARAM(404,0), 0)`)直接觸發版型切換(four-pane grid → single → four-pane grid)驗證 Acceptance 4 的「無殘影」**:因為前兩張票(PD-028/029)已經記錄這個執行環境裡 `SetCursorPos`/`mouse_event` 送不進實際畫面、`FindWindow` 找不到主視窗,滑鼠拖曳分隔線本票同樣無法模擬。但 `WM_COMMAND` 是既有 `WM_COMMAND` case 直接分派給 `set_layout`/`apply_layout` 的訊息路徑(跟真人點擊版型按鈕觸發的是同一段程式碼,只是繞過滑鼠/按鈕本身,直接送出等價的 `BN_CLICKED` 通知),所以這比純代碼審查更接近一次真實的 `apply_layout` 呼叫。切到 single 後截圖確認:視窗變成單一大卡片,四宮格時期四張卡片的邊框/陰影完全消失、沒有任何殘影;切回 four_pane_grid 後再次確認四張卡片正確各自對齊新的 pane 矩形。這個驗證涵蓋「`apply_layout` 觸發後重繪」但**沒有**涵蓋「拖曳分隔線改變 divider_ratios 後重繪」(分隔線拖曳需要連續的 `WM_MOUSEMOVE` 序列而不是單一命令,這個環境下送不進去)。
  - 因為 `draw_pane_card` 在 `paint_client_background` 裡永遠用 `layout_rects(window, group)` 重新計算矩形(沒有任何快取或跨呼叫保留的舊矩形變數),邏輯上每一次 `WM_ERASEBKGND` 都會用當下的 `group.layout_template`/`divider_ratios` 算出全新矩形再畫卡片,程式碼結構本身就排除了「用舊矩形畫新卡片」的殘影可能——這點補上面的 `WM_COMMAND` 實測作為程式碼層級的保證,對應 Task 指示裡「驗證 `draw_pane_card` 是否以新鮮矩形被呼叫」的替代檢查方式。
- **未能完成互動式驗證的部分**:跟 PD-028/029 記錄的環境限制一致——沒有辦法用滑鼠實際拖曳分隔線、也沒有辦法用滑鼠實際點擊某個分頁或按分頁右鍵測試既有的關閉分頁互動。這些既有互動(tab 點擊切換、`+` 新增分頁、分頁關閉)**沒有被本票的 owner-draw 改動觸及**——`TCN_SELCHANGE`/`WM_LBUTTONDOWN`/`close_tab_at_point`/`TCM_HITTEST` 全部維持原樣,owner-draw 只換 `WM_DRAWITEM` 分支這一段純繪製程式碼,列為留給下一次真人操作時的檢查項(核對「粗體 active 樣式跟著點擊切換」與「拖曳分隔線時卡片跟著新矩形即時重繪、無殘影」)。

#### Handoff requirements 回覆

- **卡片圓角半徑、邊框色、陰影偏移量與顏色(96-DPI 基準)**:圓角 `radius = 10`、outset `= 2`(向外擴的量,非設計決策原文,是本次新增的工程判斷,見上)、邊框 `RGB(223,229,236)`、陰影偏移 `= 2`、陰影色 `RGB(205,211,219)`(比決策 3 建議的 `RGB(210,216,224)` 深一階,原因見上)、卡片底色 `RGB(255,255,255)`。全部透過 `MulDiv(value, dpi, 96)` 換算,沒有新增縮放機制。
- **`paint_client_background` 簽章變更後的最終呼叫點寫法**:見上方「最終呼叫點」小節,簽章改為 `(HWND window, HDC dc, const AppState& state)`,唯一呼叫點在 `WM_ERASEBKGND`,取 `*state`(該分支已經判過 `state != nullptr`)。
- **資料夾圖示繪製函式的最終視覺參數**:`draw_folder_glyph(HDC dc, RECT icon_rect)`,固定尺寸依傳入的 `icon_rect`(由 `draw_tab_item` 依分頁高度動態算,約等於分頁高度扣掉左右 inset 後的正方形)、固定色 `fill = RGB(255,216,115)`、`border = RGB(197,139,6)`,畫法是頁籤矩形(`icon_rect` 左側 `2/5` 寬、上緣 `1/8` 高處起、到本體矩形頂端)疊本體矩形(`icon_rect` 全寬、從 `1/4` 高處到底部)。之後若要依副檔類型變色(例如依 Group/pane 的某個屬性換一組固定色),可以直接在 `draw_folder_glyph` 加一個 `COLORREF` 參數,呼叫端(`draw_tab_item`)目前只有一個呼叫點,改動成本低。
- **owner-draw tab 已知限制**:這個環境沒有真人在高 DPI 或深色系統主題下實機檢視。深色系統主題本身不影響——PaneDock 沒有讀取系統主題色,`draw_tab_item`/`draw_pane_card` 全部用寫死的淺色配色(白底 + 淺灰/藍階邊框),在 Windows 深色主題下會維持淺色外觀(這是既有整個 app 的既定行為,`AGENTS.md`/`docs/design-spec.md` 都沒有要求跟隨系統深色主題,本票沒有改變這件事,只是提醒:如果之後有票要做深色主題,pane 卡片背景/tab 圖示的固定色都要一併檢視)。高 DPI 方面,`radius`/`outset`/`shadow_offset` 都用 `MulDiv` 縮放,理論上在 125%/150% DPI 下會等比例放大,但沒有實機測試確認視覺上是否協調(例如 outset 在高 DPI 下放大後,是否仍然完全落在 `divider_thickness`/`kPaneCanvasPadding` 的留白內——這兩者也都用同一套 `scaled_value` 縮放,理論上比例關係不變,但沒有真人在 125%/150% 螢幕上截圖核對)。
- **未來完整四角圓角(裁切 Shell view 本身)的技術路徑評估**:調查結論是「目前沒有安全的做法,不建議做」。`IExplorerBrowser` 的內容 HWND 是 Shell 自己建立、管理生命週期的視窗(見 `src/explorer_host/explorer_host.cpp` 的 `initialize`/`Destroy`),PaneDock 只透過 `SetWindowPos`/`set_rect` 控制它的矩形,不擁有它的繪製管線。要讓它呈現圓角,理論上可行的 Win32 機制只有 `SetWindowRgn`(裁切視窗可見區域為圓角矩形 region):技術上可行,但有兩個實際風險——(1) `SetWindowRgn` 設定的 region 由呼叫方持有語意上的「轉移」,视窗銷毀/重新整理時如果沒有正確用 `SetWindowRgn(hwnd, nullptr, TRUE)` 復原,可能在下一次 `set_rect`/DPI 變更重新計算時忘記同步更新 region 導致裁切錯位(尤其是 Group 切換時「保活式」重新導覽同一個 HWND,矩形會變但目前程式碼完全沒有 region 概念,新增後每個矩形變更點都要記得同步),這正好踩到 `AGENTS.md`「Group switching keeps live views alive and re-navigates them」與「每個 `Initialize` 過的 `IExplorerBrowser` 必須 `Destroy`」這兩條——不是不能做,而是要新增一個「region 生命週期要跟 rect 生命週期同步」的隱性不變式,目前的 `ExplorerHost` 完全沒有這個概念,貿然加大幅增加它的狀態機複雜度;(2) 圓角區域內、原本 Shell view 應該繪製檔案列表的部分內容會被裁掉一小塊,對使用者來說是有效內容被藏起來,而不是單純的裝飾,這跟「上緣圓角」(裝飾用的自繪 header)在性質上不同。**觸發條件維持 `docs/tickets.md` 已經寫的**:先有一個能安全裁切 `IExplorerBrowser` HWND 而不影響其生命週期與 site 契約的具體方案,才值得開票評估,目前不建議在沒有具體方案前貿然嘗試。
