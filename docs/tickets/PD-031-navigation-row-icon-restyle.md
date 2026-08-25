# PD-031 — 導覽列圖示化按鈕與圓角網址欄背景

Phase 6 · app_shell · Depends on: PD-030

- Source: `docs/panedock-ui-prototype.html?refined=1&variant=1&solo=1`(Quiet Header 變體)
- Origin: 2026-08-25,同 PD-028/PD-029/PD-030 的使用者回報。設計稿每個 pane 的導覽列用箭頭圖示(‹ › ↑)按鈕 + 圓角淺灰底的路徑文字框;目前是文字按鈕(`<`、`>`、`Up`)+ 一般方角 `EDIT` 控制項。
- Priority: LOW——視覺差距明確但影響範圍最小,且依賴 PD-030 先把 pane 卡片背景畫出來,圓角網址欄背景才有可疊加的卡片底色可用。

## 已確認的產品決策

1. **Back/Forward/Up 三顆按鈕改為 owner-draw 圖示按鈕,不改變 id、`EnableWindow` 邏輯或 `WM_COMMAND` 分派。** 沿用 `PD-005`/`PD-020` 既有的 `kBackButtonIdBase`/`kForwardButtonIdBase`/`kUpButtonIdBase` 常數與現有 `refresh_navigation_buttons` 的啟用/停用判斷,只把三顆按鈕的建立樣式加上 `BS_OWNERDRAW`,並新增繪製函式畫出 `‹`/`›`/`↑` 對應的簡單線條符號(比照 `draw_layout_glyph` 的 `MoveToEx`/`LineTo` 手法畫箭頭,不使用 Wingdings 字型或圖片資源)。
2. **網址欄(`EDIT` 控制項)本身仍是原生方角矩形,但視覺上呈現圓角的做法是:在 `EDIT` 控制項底下的導覽列背景先畫一個圓角淺灰底 `RoundRect`,`EDIT` 控制項改為無邊框樣式(移除 `WS_EX_CLIENTEDGE`,若原本就沒有則不需改動)、背景透過 `WM_CTLCOLOREDIT` 設成與圓角底色相同,並讓 `EDIT` 的矩形比背景 `RoundRect` 內縮幾個像素,使方形的 `EDIT` 邊界被圓角背景的圓角部分自然蓋住(內縮量需大於圓角半徑,否則 `EDIT` 方角會突出圓角底色範圍外)。** 這是原生 Win32 唯一不需要自訂控制項或第三方繪圖庫就能做出「看起來圓角」的輸入框的方式;真正做一個逐字元繪製的自訂編輯控制項超出這張票的成本效益。
3. **這個圓角淺灰底背景畫在哪裡,取決於 PD-030 完成後的卡片背景繪製路徑——優先在同一個 `draw_pane_card`(或其呼叫序列)裡,對導覽列的矩形範圍多畫一塊,不要另外新增一個獨立的背景繪製呼叫點,避免兩處各自計算 pane 矩形導致之後其中一處忘記更新。**

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency... Prefer the smallest working change.

`docs/design-spec.md` §NFR-005:
> **必要狀態**(版型、location、tab、view mode、排序)必須精確還原,否則視為缺陷。

`CONTEXT.md`:
> **Shell location**: The persisted identity of where a tab points... _Avoid_: path (when the location may be virtual)

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `apply_layout`(back/forward/up 按鈕與 address bar 的 `SetWindowPos` 順序與寬度計算)、`refresh_navigation_buttons`、`refresh_navigation_chrome`、`kBackButtonIdBase`/`kForwardButtonIdBase`/`kUpButtonIdBase`/`kAddressBarIdBase`、`AppState::back_buttons`/`forward_buttons`/`up_buttons`/`address_bars`、`address_edit_proc`(既有 `WM_KEYDOWN`/Enter 提交邏輯,不能受影響)。
- `WM_CTLCOLOREDIT`/`WM_CTLCOLORSTATIC` 現有處理(若目前程式碼庫還沒有 `WM_CTLCOLOREDIT` 分支,這是本票要新增的訊息處理)。
- PD-030 完成後的 `draw_pane_card`(或其等價函式)簽章與呼叫序列,確認導覽列矩形背景要畫在哪個既有呼叫點內插入。
- `docs/panedock-ui-prototype.html` refined variant 1 的 breadcrumb/路徑列 CSS(圓角半徑、底色、圖示樣式)——需要重新讀取檔案取得精確數值。

## Scope

1. `WM_CREATE` 建立 `state.back_buttons[i]`/`forward_buttons[i]`/`up_buttons[i]` 時加上 `BS_OWNERDRAW`。
2. 新增 `draw_navigation_icon_button(const DRAWITEMSTRUCT&, std::size_t glyph_kind)` 繪製函式,`glyph_kind` 對應 back(‹)/forward(›)/up(↑)三種箭頭線條,disabled 狀態沿用既有 `draw_layout_button`/`draw_sidebar_action_button` 已經在用的「較淡顏色」判斷模式(`ODS_DISABLED`)。
3. `WM_DRAWITEM` 新增分支,`hwndItem` 命中 `back_buttons`/`forward_buttons`/`up_buttons` 任一陣列時呼叫上述函式,分別傳入對應的 `glyph_kind`。
4. `EDIT`(address bar)建立樣式移除 `WS_EX_CLIENTEDGE`(若有的話),新增 `WM_CTLCOLOREDIT` 處理,對 `state.address_bars` 命中的控制代碼回傳與導覽列圓角底色相同的 `HBRUSH`(可快取一個靜態 brush,程式生命週期內釋放,或沿用現有其他 `WM_CTLCOLORSTATIC` 已有的 brush 管理模式)。
5. 在 PD-030 的卡片背景繪製呼叫序列中,對每個可見 pane 的導覽列矩形(`navigation_top`/`navigation_height`,與 `apply_layout` 目前算法一致)追加畫一個圓角淺灰底矩形;`apply_layout` 對 `address_bars[index]` 的 `SetWindowPos` 矩形改為在這個圓角底矩形內縮後的範圍。

## Non-goals

- 不做網址列自動完成(`docs/tickets.md` 已有候選,觸發條件未到,本票不重開)。
- 不做真正逐字元自訂繪製的輸入框控制項。
- 不改變 back/forward/up 的啟用判斷邏輯(`refresh_navigation_buttons`)或 Enter 提交流程(`address_edit_proc`/`submit_address`)。
- 不修改 `core` 或 `explorer_host`。

## Acceptance

1. Back/Forward/Up 三顆按鈕顯示箭頭圖示而非文字,disabled/enabled 狀態視覺區分正確(對照既有 `refresh_navigation_buttons` 的邏輯手動測試上一頁/下一頁/上層的啟用時機)。
2. 網址欄視覺上呈現圓角淺灰底輸入框效果,且輸入文字、Enter 提交導覽、既有 focus/selection 行為與改版前一致。
3. 拖曳分隔線、切換版型、切換 Group 時,導覽列圓角背景正確跟隨新的 pane 矩形,無殘影或位置偏移。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
.\build\PaneDock.exe
# 手動:確認箭頭圖示按鈕的啟用/停用視覺與既有導覽行為一致、
# 網址欄圓角視覺與 Enter 提交導覽正常、拖曳/切版型後背景不殘影
```

## Handoff requirements

- 箭頭圖示的最終線條繪製參數與圓角底色數值。
- `EDIT` 內縮量與圓角半徑的最終關係(內縮量必須大於半徑的具體像素數字)。
- 若移除 `WS_EX_CLIENTEDGE` 後在某些 Windows 版本/主題下視覺異常,記錄下來。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-25 實作交接

#### 完成內容(對照 Scope 1–5)

- `src/app_shell/main.cpp` 的 `WM_CREATE` 建立 `state->back_buttons[i]`/`forward_buttons[i]`/`up_buttons[i]` 的 `CreateWindowExW` 樣式加上 `BS_OWNERDRAW`(原本 `WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON`),文字 label(`"<"`/`">"`/`"Up"`)仍照原樣傳入 `CreateWindowExW`(owner-draw 不讀取但保留無害)。
- 新增 `draw_navigation_icon_button(const DRAWITEMSTRUCT& item, std::size_t glyph_kind)`(緊接 `draw_layout_button`/`draw_layout_glyph` 之後、`draw_more_actions_button` 之前),`glyph_kind` 0=back(`‹`)/1=forward(`›`)/2=up(`↑`),手法比照 `draw_layout_glyph`:先 `FillRect` 白底(`RGB(255,255,255)`,讓按鈕融入卡片白底,而不是保留原生按鈕的立體邊框),再用單一 `HPEN`(`disabled` 用 `RGB(190,197,209)`、否則 `RGB(90,102,122)`,對照既有 `draw_layout_button` 的 disabled/enabled 兩階配色手法)以 `MoveToEx`/`LineTo` 畫出對應箭頭線條;back/forward 各是一個尖角(兩段線),up 是一條直線加一個尖頭(三段線)。沒有用 Wingdings 字型或圖片資源。
- `WM_DRAWITEM` 新增三個分支(緊接 `kMoreActionsButtonId` 分支之後、既有 `kButtonIds` 分支之前),用 `item->CtlID` 落在 `kBackButtonIdBase`/`kForwardButtonIdBase`/`kUpButtonIdBase` 各自的 `[base, base+kExplorerCount)` 範圍判斷,分別呼叫 `draw_navigation_icon_button(*item, 0/1/2)`,回傳 `TRUE`。沒有動 `refresh_navigation_buttons` 的 `EnableWindow` 判斷邏輯或 `WM_COMMAND` 分派(`kBackButtonIdBase`/`kForwardButtonIdBase`/`kUpButtonIdBase` 的既有 id 範圍判斷完全沒有改動)。
- `EDIT`(address bar)的 `CreateWindowExW` 擴充樣式從 `WS_EX_CLIENTEDGE` 改成 `0`;新增 `WM_CTLCOLOREDIT` 分支,用 `std::find(state->address_bars.begin(), state->address_bars.end(), control)` 命中時回傳一個快取的 `HBRUSH`(見下)、`SetBkMode(dc, OPAQUE)` + `SetBkColor` 設成與導覽列圓角底色相同的 `RGB(251,252,253)`、`SetTextColor` 設成 `RGB(76,89,107)`(對照設計稿 `.location { color:#4c596b }`)。
- 新增一個「新增一個工程判斷」的共用函式 `navigation_geometry(HWND window, RECT pane_rect)`,回傳 `navigation_top`/`navigation_height`/`button_width`/`address_background`(這是圓角底矩形要畫的完整範圍,也是 EDIT 內縮前的基準矩形)。這個函式把原本只存在於 `apply_layout` 內、逐行計算的邏輯抽出來,`apply_layout` 與 `paint_client_background` 兩處都改呼叫它——這正是 Scope 3 要求「不要另外新增一個獨立的背景繪製呼叫點,避免兩處各自計算 pane 矩形導致之後其中一處忘記更新」的具體實作方式:與其在 `paint_client_background` 重新推導一次 `navigation_top`/`navigation_height`/`button_width`(這樣兩處算法遲早會分岔),不如把算法本身變成單一函式,兩個呼叫點都吃同一份邏輯。另外新增 `inset_rect(RECT rect, int inset)` 小工具(四邊往內縮、並 clamp 避免反轉),供 `apply_layout` 计算 EDIT 的最終矩形使用。
- 新增 `draw_navigation_bar_background(HDC dc, RECT rect, UINT dpi)`,畫法與 `draw_pane_card`/`draw_sidebar_action_button` 相同的 `CreateSolidBrush`/`CreatePen`/`SelectObject`/`DeleteObject` 生命週期風格,一個 `RoundRect`(底色 `RGB(251,252,253)`、邊框 `RGB(217,225,234)`,半徑見下方 Handoff requirements)。呼叫點在 `paint_client_background` 既有的 pane 卡片繪製迴圈裡,`draw_pane_card(dc, pane_rect, dpi)` 呼叫之後緊接著呼叫 `navigation_geometry(window, pane_rect)` 取得 `address_background`,再呼叫 `draw_navigation_bar_background(dc, geometry.address_background, dpi)`——同一個迴圈、同一次 `WM_ERASEBKGND`,沒有新增獨立的背景繪製呼叫點,符合 Scope 3。
- `apply_layout` 的 back/forward/up 三顆按鈕與 address bar 的 `SetWindowPos` 改成呼叫 `navigation_geometry(window, pane_rect)` 取得 `navigation_top`/`navigation_height`/`button_width`,address bar 最終矩形改成 `inset_rect(geometry.address_background, scaled_value(window, kAddressBarInset))`,`SetWindowPos` 用這個內縮後的矩形。三顆按鈕與 tab strip 的 `SetWindowPos` 呼叫序列/尺寸計算邏輯完全不變(仍然用 `button_width` 依序往右排)。
- 新增一個快取的 `HBRUSH`(`address_bar_background_brush()`,function-local `static HBRUSH`,首次呼叫時 `CreateSolidBrush(RGB(251,252,253))`),在 `WM_DESTROY`(既有 case,只加一行 `release_address_bar_background_brush()`,沒有動 `UnregisterHotKey`/`PostQuitMessage` 這兩行既有邏輯)呼叫時 `DeleteObject` 釋放一次——`WM_DESTROY` 每個視窗生命週期只觸發一次,單次 delete 沒有 double-free 風險。**沒有動 `WM_QUERYENDSESSION`/`WM_ENDSESSION`**(PD-032 的既有 case,任務指示明確要求不能碰,已重新 `grep` 確認這兩個 case 內容與改動前完全一致,只是行號因為前面新增程式碼而往後移動)。

#### 未做的事(對照 Non-goals,確認沒有超出範圍)

- 沒有做網址列自動完成。
- 沒有做逐字元自訂繪製的輸入框控制項——`EDIT` 仍是原生方角控制項,只是視覺上被圓角底色包住、拿掉了 `WS_EX_CLIENTEDGE` 的立體邊框。
- 沒有改變 `refresh_navigation_buttons` 的啟用判斷邏輯,也沒有動 `address_edit_proc`/`submit_address` 的 Enter 提交流程——重新讀取這兩個函式確認一行都沒改。
- 沒有動 `src/core` 或 `src/explorer_host` 任何一行。

#### 與 PD-030(Codex 併行測試/微調)的互動

- 開工前重新讀取 `main.cpp`,`draw_pane_card`/`paint_client_background`/`draw_tab_item` 等 PD-030 函式當時已經是交接區記錄的最終形狀,過程中每次 Edit 前也都用 Grep 重新定位最新行號(行號在過程中因為前面新增程式碼而多次往後移動,`Edit` 工具的精確字串比對每次都一次命中,沒有遇到因為併行編輯導致 `old_string` 找不到的情況)。
- 沒有修改 `draw_pane_card`/`draw_tab_item`/`draw_folder_glyph`/tab strip 相關的任何程式碼,只在 `paint_client_background` 既有迴圈內、`draw_pane_card` 呼叫之後追加一行呼叫,沒有更動 PD-030 的簽章或呼叫序列。
- 建置過程中沒有遇到 PD-030 相關的合併衝突;最終 `git status` 顯示只有 `src/app_shell/main.cpp` 被修改(加上新增的 `docs/tickets/PD-031-*.md`),沒有動到 `sidebar.cpp`。

#### 驗證結果

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` 與 `cmake --build build`:成功,無警告無錯誤。
- `ctest --test-dir build --output-on-failure`:4/4 通過(`panedock_diagnostic_flag`、`panedock_core_model`、`panedock_core_layout`、`panedock_core_session`)——這四個既有測試都在 `src/core`,本票完全沒有動 `core`,通過只代表沒有連帶弄壞既有 `core` 邏輯(本票本身是純 UI 繪製/樣式改動,沒有新增 `core` 邏輯,依 Non-goals 也不需要新增獨立單元測試)。
- `git diff --check`:通過,無空白字元問題。
- 視覺驗證:啟動 `build\PaneDock.exe`,沿用 PD-028/029/030 交接區記錄的手法(`System.Drawing.Graphics.CopyFromScreen` 對主視窗矩形截圖、`NearestNeighbor` 放大局部區域核對細節)。**確認以下項目**:
  - 放大截圖(見任務執行過程,未存檔於 repo)清楚看到每個 pane 導覽列的 `‹`/`›`/`↑` 箭頭圖示線條,C:\ 這個 pane 因為位於歷史起點,`‹`(back)呈現深色(啟用)、`›`(forward)呈現明顯較淡的灰色(停用)、`↑`(up)深色(啟用)——這與 `refresh_navigation_buttons` 既有邏輯(`can_navigate_tab_back`/`can_navigate_tab_forward` 判斷、`up` 永遠啟用)的預期行為一致(對應 Acceptance 1)。
  - 地址欄 `EDIT` 周圍可見淺灰圓角底(`RGB(251,252,253)` 底、`RGB(217,225,234)` 邊框)、EDIT 本身與圓角底融合,沒有原生 `WS_EX_CLIENTEDGE` 的立體凹陷邊框,文字(如 `C:\`、`C:\Windows`)正常顯示於 EDIT 內(對應 Acceptance 2 的視覺部分)。
  - **透過 `WM_COMMAND`(`SendMessage(hwnd, 0x0111, 400, 0)` 切到 single、`SendMessage(hwnd, 0x0111, 404, 0)` 切回 four_pane_grid)驗證 Acceptance 3 的「無殘影」**:沿用 PD-030 交接區記錄的理由(這個環境送不進 `WM_MOUSEMOVE`/`SetCursorPos`,無法模擬滑鼠拖曳分隔線,但 `WM_COMMAND` 這條路徑跟真人點擊版型按鈕觸發的是同一段 `set_layout`/`apply_layout` 程式碼)。切到 single 後截圖確認:單一大卡片的導覽列圓角底正確跟著新的、寬得多的 pane 矩形重新計算(圓角底寬度明顯變寬,對齊新的 `pane_rect.right`),沒有任何舊四宮格時期殘留的圓角底或按鈕殘影;切回 four_pane_grid 後再次確認四個 pane 的導覽列圓角底、箭頭按鈕都正確對齊新的四宮格矩形。**沒有涵蓋**「拖曳分隔線改變 `divider_ratios` 後的連續重繪」,原因同 PD-030——分隔線拖曳需要連續 `WM_MOUSEMOVE` 序列,這個環境送不進去。
  - 因為 `navigation_geometry` 在 `paint_client_background` 的迴圈裡永遠用當次 `layout_rects(window, group)` 算出的 `pane_rect` 重新推導(沒有任何跨呼叫保留的舊矩形變數),邏輯上每一次 `WM_ERASEBKGND` 都會用當下版型重新算出全新的 `address_background` 矩形再畫圓角底,程式碼結構本身排除「用舊矩形畫新圓角底」的殘影可能——這點補上面的 `WM_COMMAND` 實測作為程式碼層級的保證。
- **未能完成互動式驗證的部分**:跟 PD-028/029/030 記錄的環境限制一致——沒有辦法用滑鼠實際點擊 back/forward/up 按鈕、也沒有辦法用滑鼠實際點擊 address bar 輸入文字並按 Enter 觸發 `submit_address`、也沒有辦法拖曳分隔線。這些既有互動(`WM_COMMAND` 分派給 `navigate_back`/`navigate_forward`/`navigate_up`、`address_edit_proc` 的 `WM_KEYDOWN`/Enter 提交)**沒有被本票的 owner-draw/樣式改動觸及**——這兩段程式碼一行都沒有變動,只是它們背後那三顆按鈕現在多了 `BS_OWNERDRAW` 樣式與對應的 `WM_DRAWITEM` 繪製分支、address bar 多了 `WM_CTLCOLOREDIT` 分支。留給下一次真人操作時的檢查項:點擊 `‹`/`›`/`↑` 確認導覽正常且圖示視覺回饋(disabled 變淡)正確跟著點擊後的新狀態更新、在 address bar 輸入路徑按 Enter 確認導覽與既有 focus/selection 行為一致、拖曳分隔線確認圓角底即時跟隨無殘影。

#### Handoff requirements 回覆

- **箭頭圖示的最終線條繪製參數**:每個按鈕先用 `RGB(255,255,255)` 白底 `FillRect`(融入卡片白底);線條顏色 enabled `RGB(90,102,122)`、disabled `RGB(190,197,209)`；線寬 `std::max(1, size/6)`(`size = std::max(4, std::min(width,height)/2)`,依按鈕實際尺寸動態算,沒有寫死像素數字,因為按鈕尺寸本身已經透過 `scaled_value(window, kNavigationButtonWidth)`/`navigation_height` 依 DPI 縮放)。back/forward 各兩段 `LineTo` 畫出尖角、up 三段 `LineTo` 畫出直線加箭頭。
- **圓角底色**:底色 `RGB(251,252,253)`、邊框 `RGB(217,225,234)`(取自設計稿 `.location { background:#fbfcfd; border:1px solid #d9e1ea }` 原始數值,未調整)。
- **`EDIT` 內縮量與圓角半徑的最終關係**:圓角半徑 `kAddressBarBackgroundRadius = 4`(@96dpi,透過 `MulDiv` 縮放),內縮量 `kAddressBarInset = 6`(@96dpi,同樣 `MulDiv` 縮放),`6 > 4`,滿足「內縮量必須大於半徑」。**這裡有一個刻意的數值調整,記錄如下**:設計稿 `.location` 的 `border-radius` 是 `6px`,原本想直接沿用 `6`,但既有 `kNavigationBarHeight = 28`(@96dpi)是一個已經存在、本票 Non-goals 明確排除改動的常數,若圓角半徑取 `6`、內縮量至少要大於 `6`(例如取 `7` 或 `8`)才滿足「大於半徑」的要求,而內縮量同時要在上下左右四邊都扣掉,會讓 `EDIT` 實際可視高度只剩 `28 - 2*7 = 14`px 甚至更少,實測感覺文字會被垂直方向擠壓。因此把半徑調小到 `4`、內縮量調到 `6`(`28 - 2*6 = 16`px 高的 `EDIT`),兩者關係仍然滿足「內縮量 > 半徑」,但視覺上圓角沒有設計稿那麼明顯——這是本票在既有版面高度預算限制下的工程折衷,不是產品決策 2 原文要求的數值。若之後要更貼近設計稿的 `6px` 圓角,需要先加大 `kNavigationBarHeight`,但那會改動整個導覽列高度(影響 tab strip 下方所有 pane 的可視面積),超出本票範圍,列為候選:「若要更貼近設計稿的圓角視覺,需要先評估加大 `kNavigationBarHeight` 的影響」。
- **`WS_EX_CLIENTEDGE` 移除後的視覺**:這個開發環境是 Windows 11 x64,拿掉 `WS_EX_CLIENTEDGE` 後 `EDIT` 呈現扁平無邊框視覺,搭配 `WM_CTLCOLOREDIT` 回傳的淺灰底色與圓角底色一致,截圖確認視覺融合正常、沒有出現原生凹陷邊框殘留或視覺斷層。**沒有在其他 Windows 版本(例如 Windows 10 22H2)或非預設系統主題下實機測試**,若之後有回報視覺異常,留意 `WM_CTLCOLOREDIT` 在深色系統主題下是否仍然正確接管背景色(本票的固定淺色配色策略與 PD-030 交接區記錄的既有決定一致:PaneDock 目前不跟隨系統深色主題)。
