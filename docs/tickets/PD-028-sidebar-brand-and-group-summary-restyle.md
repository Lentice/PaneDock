# PD-028 — 側邊欄品牌列、Group 兩行摘要與 footer 按鈕改版

Phase 6 · sidebar, app_shell · Depends on: PD-017

- Source: `docs/panedock-ui-prototype.html?refined=1&variant=1&solo=1`(Quiet Header 變體,`docs/panedock-ui-demo-01-refined-quiet-header.html` 為其 iframe 包裝)、`AGENTS.md`、`CONTEXT.md`
- Origin: 2026-08-25,使用者比對目前執行中的 app 截圖與上述設計稿後回報「畫面還是差距不少」,並明確表示「允許大幅修改 code」。設計稿與現況的落差已在 PD-017/既有的側邊欄改版(226px 寬度、圓角 pill 選取列、eyebrow 標籤)之外,還包含:側邊欄缺少 app 品牌列、Group 只顯示單行名稱、footer 是六顆功能按鈕而非設計稿的兩顆全寬按鈕。
- Priority: HIGH——這是使用者本次回報最先看到的視覺落差區塊,且改動風險相對可控(不涉及 `IExplorerBrowser` 生命週期)。

## Goal

側邊欄由上而下改為三段:(1) 固定的 app 品牌列(藍色方塊圖示 + "PaneDock" 粗體標題),(2) Group 清單,每列顯示 Group 名稱 + 副標題(pane 數與 tab 數統計)+ 右側 tab 數量圓形徽章,(3) footer 只保留 "New Group" 一顆全寗按鈕;既有的 Duplicate／Rename／Delete／Move Up／Move Down 移到 Group 列的右鍵 context menu,不刪除功能。

## 已確認的產品決策

1. **不做設計稿裡的 "Preferences" 按鈕。** 目前產品沒有任何偏好設定頁面或設定項,`docs/design-spec.md` 與 `docs/roadmap.md` 都未列出偏好設定功能。加一顆按鈕又不接任何行為,是 `AGENTS.md`/ponytail 明確禁止的「為了畫面像而做的空殼功能」。Footer 只做 "New Group" 一顆按鈕;side bar 品牌列與 Group 列表本身已經比目前更貼近設計稿,footer 這格差距記錄在候選清單,等未來真的有偏好設定功能時一併補上這顆按鈕與其對話框。
2. **Duplicate／Rename／Delete／Move Up／Move Down 不能被移除,只能換位置。** 這五個是 PD-017 已經驗收過的必要功能(FR-001:「使用者可新增、重新命名、複製、刪除、重新排序 Group」)。設計稿沒有畫出它們,合理推論是設計稿把它們收進了「右鍵選單」或「更多動作」這類次要互動,而不是產品真的拿掉這些功能。改為:在 Group 列表(`Sidebar` 的 `LISTBOX`)加入 `WM_CONTEXTMENU` 處理,右鍵點擊任一列先 `set_selected_index` 選取該列,再用 `TrackPopupMenu` 彈出原生 context menu,選單項目文字沿用既有英文(`Duplicate Group`、`Rename Group`、`Delete Group`、`Move Up`、`Move Down`),選取後呼叫既有的 `duplicate_group`/`begin_rename`(觸發既有 rename commit 流程)/`delete_group`/`move_group` 函式,不改變這些函式本身的行為與簽章。`Move Up`/`Move Down` 在邊界時於選單內以 `MF_GRAYED` 停用,對齊既有按鈕邏輯。
3. **Group 副標題文字為 `"<pane 數> panes · <tab 總數> tabs"`(pane 數為 1 時用單數 `"1 pane"`,tab 總數為 1 時用單數 `"1 tab"`)。** 對照設計稿的 "5 panes · 12 tabs"。單複數判斷比照 `docs/development.md` 既有的英文用字要求;PaneDock 的 pane 數固定在 1–4,不需要處理更大數字的特殊格式。
4. **右側徽章數字 = tab 總數**(不是 pane 數),與副標題文字裡的第二個數字相同,呈現為一個圓形淺灰底徽章,樣式獨立於選取狀態(徽章底色不隨列選取變色,只有文字色可依需要調整對比)。
5. **`GroupSummary` 擴充兩個欄位:`std::size_t pane_count` 與 `std::size_t tab_count`。** 這兩個值在 `refresh_sidebar` 組裝 `GroupSummary` 時,從 `panedock::core::GroupState` 現有的 `panes` 與每個 pane 的 `tabs.size()` 加總算出,不需要改動 `core` 的資料模型或新增任何 `core` 函式——純粹是 app_shell → sidebar 投影層多算兩個數字。
6. **品牌列是新增的固定高度區塊,不隨側邊欄滾動,永遠顯示在 Group 清單上方。** 沿用側邊欄既有的「不可拖曳調整、寬度寫死」決策(PD-017 決策 2),品牌列高度也是一個具名常數,依 DPI 縮放。圖示用純色 `RoundRect` 藍色方塊(沿用既有 `RGB(37, 99, 235)` 或側邊欄已用的藍色 accent)搭配白色字母或簡單幾何圖形(例如四宮格 pane 縮圖符號,呼應 layout 圖示),不需要載入外部圖片資源或新增 PNG/ICO 資產——純 GDI 繪製,做法比照既有 `draw_layout_glyph` 的風格(`FrameRect`/`MoveToEx`/`LineTo`)。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §9.1:
> `sidebar` 負責「Group 列表繪製與切換」,不得負責「Group 資料的權威狀態」。

`AGENTS.md`:
> Only the visible pane's active tab holds a live `IExplorerBrowser`... Group switching keeps live views alive and re-navigates them.

`AGENTS.md`:
> Don't add features that are listed as out of scope... Don't add features, refactor, or introduce abstractions beyond what the task requires.

`docs/design-spec.md` FR-001:
> 使用者可新增、重新命名、複製、刪除、重新排序 Group。刪除需二次確認。

`CONTEXT.md`:
> **sidebar**: The persistent left-hand region listing Groups. It is always visible and is not part of any Group's state.
> **Group**: ... _Avoid_: workspace, session, profile, project, favourite

`docs/development.md`:
> No Chinese text in the binary.

## Files to read and trace first

- `src/sidebar/sidebar.h`、`src/sidebar/sidebar.cpp`——目前 `GroupSummary`、`draw_item`、`measure_item`、`set_rect`、既有 rename 就地編輯流程(`begin_rename`/`close_editor`/`kRenameCommitMessage`)。
- `src/app_shell/main.cpp` 的 `refresh_sidebar`、`layout_sidebar`、`AppState::sidebar_buttons`/`kButtonIds`/`kButtonLabels`、`WM_COMMAND` 對六顆按鈕 id(`kNewGroupId`/`kDuplicateGroupId`/`kRenameGroupId`/`kDeleteGroupId`/`kMoveUpId`/`kMoveDownId`)的既有分派,以及 `add_group`/`duplicate_group`/`delete_group`/`move_group` 函式本體——這些函式簽章與行為本 ticket 不能改,只能改「誰觸發它們」。
- `src/core/model.h`/`model.cpp` 的 `GroupState`——確認 `panes`、`PaneState::tabs` 的欄位名稱,用來在 `refresh_sidebar` 算出 `pane_count`/`tab_count`。
- `docs/panedock-ui-prototype.html` 的 refined variant 1(Quiet Header)區塊——品牌列、Group 列表副標題與徽章、footer 按鈕的確切 DOM/CSS(class 名稱含 `brand`/`group-item`/`group-meta`/`badge`/`sidebar-footer` 等,實際 class 名稱需重新讀取檔案確認,不要憑記憶)。
- Win32 `TrackPopupMenu`/`CreatePopupMenu`/`AppendMenuW`/`WM_CONTEXTMENU` 參考(既有程式碼庫目前沒有任何 context menu 使用範例,是本 ticket 唯一的新 Win32 模式)。

## Scope

1. `sidebar.h` 的 `GroupSummary` 新增 `pane_count`、`tab_count`(`std::size_t`,預設 0)。`sidebar.cpp` 的 `draw_item` 改為畫三行內容於同一列高內:第一行 Group 名稱(沿用現有字型與顏色邏輯),第二行副標題(較小/較淡的顏色,文字依決策 3 組字串),右側畫一個圓形徽章(`Ellipse` 或 `RoundRect` 正方形接近圓形)填入 tab 數字。列高需要相應增加(目前 40px,估計需要到 52–56px 依 DPI 縮放,具體數字由實作量測文字兩行 + padding 後決定,記錄於交接區)。
2. `main.cpp` 的 `refresh_sidebar` 組 `GroupSummary` 時加總 `group.panes.size()` 與 `std::accumulate` 每個 pane 的 `tabs.size()`,填入新欄位。
3. 新增品牌列:一個新的 owner-draw 或純 `WM_PAINT` 區塊(可以是側邊欄容器上的一段自繪矩形,不需要獨立 HWND,比照 `paint_client_background` 的做法在同一個 `WM_PAINT`/`WM_ERASEBKGND` 路徑多畫一塊),固定顯示在側邊欄最上方、Group 清單之上。`layout_sidebar` 需要把原本給 `group_label`("GROUPS" eyebrow)的位置往下推,品牌列佔用側邊欄頂端一段新的固定高度常數。
4. Footer 只剩 `kNewGroupId` 對應的按鈕(沿用既有 `draw_sidebar_action_button` 樣式,文字改回 `"+ New Group"` 或保留 `"New Group"`,可加前綴 `+` 符號以貼近設計稿,取決於量測後的視覺效果),移除 `kDuplicateGroupId`/`kRenameGroupId`/`kDeleteGroupId`/`kMoveUpId`/`kMoveDownId` 對應的 `HWND` 建立(`kButtonIds`/`kButtonLabels` 陣列縮減為只含 New Group,或改成獨立常數,不再用陣列迴圈建立六顆按鈕)。
5. 側邊欄 `LISTBOX` 新增 `WM_CONTEXTMENU` 處理:滑鼠右鍵座標轉成 client 座標、`LB_ITEMFROMPOINT` 找到該列、`set_selected_index`、`refresh_sidebar`(讓按鈕啟用狀態與既有邏輯一致),然後 `TrackPopupMenu` 顯示 Duplicate/Rename/Delete/Move Up/Move Down 五個選項(邊界時 grayed),回傳的命令 id 直接重用現有的 `kDuplicateGroupId` 等常數分派到既有 `WM_COMMAND` 處理邏輯(用 `SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, 0), 0)` 轉發最省事,不需要重複一份分派邏輯)。
6. `refresh_sidebar` 中原本針對六顆按鈕 `EnableWindow` 的邏輯,改為只需要處理 New Group(永遠啟用)與 context menu 的 grayed 判斷(在彈出選單當下即時計算,不需要常駐的 enable 狀態)。

## Non-goals

- 不做 "Preferences" 按鈕或任何偏好設定頁面(已確認的產品決策 1)。
- 不刪除 Duplicate／Rename／Delete／Move Up／Move Down 的功能,只搬移觸發入口(已確認的產品決策 2)。
- 不新增鍵盤快速鍵開啟 context menu(例如 Shift+F10、選單鍵)——原生 `LISTBOX` 若內建支援可以順手不擋,但不用特別新增處理,超出本票範圍。
- 不做側邊欄寬度調整或持久化(PD-017 已否決,本票不重開)。
- 不修改 `core::model.h` 的既有 Group mutation 函式簽章。
- 不做品牌列圖示的多色/漸層/陰影效果,純色方塊 + 簡單線條圖形即可。

## Acceptance

1. 側邊欄最上方顯示固定的品牌列(藍色方塊圖示 + "PaneDock" 標題文字),不隨 Group 清單捲動。
2. 每個 Group 列顯示名稱、副標題(正確的 pane/tab 單複數)與右側 tab 數量徽章,數字與該 Group 實際的 `panes`/`tabs` 內容一致(可用建立/新增 tab 後手動核對)。
3. Footer 只有一顆 "New Group" 按鈕,點擊行為與改版前相同(呼叫既有 `add_group`)。
4. 在任一 Group 列按右鍵彈出 context menu,顯示 Duplicate/Rename/Delete/Move Up/Move Down;在第一列時 Move Up 為灰階,在最後一列時 Move Down 為灰階;每個選單項目點擊後行為與 PD-017 驗收過的原按鈕行為一致(Duplicate 複製狀態、Rename 進入就地編輯、Delete 二次確認、Move Up/Down 改變順序並存檔)。
5. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
6. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "windows\.h|HWND|ComPtr" src\core
# 預期:無命中
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:確認品牌列常駐顯示、Group 列副標題與徽章數字正確、
# New Group 按鈕行為不變、右鍵開出 context menu 且五個動作行為與改版前一致
```

## Handoff requirements

- 品牌列與 Group 列表最終列高的具體像素值(96-DPI 基準)與其 DPI 縮放方式。
- `GroupSummary` 最終欄位形狀,供之後任何要再擴充摘要資訊(例如 Group 圖示,見 `docs/tickets.md` 候選)的 ticket 參考。
- context menu 的建立方式(`TrackPopupMenu` 呼叫點、選單項目 id 重用既有按鈕 id 的作法)與是否有任何邊界情況(例如空 Group 清單時右鍵的行為)。
- 若量測後發現徽章與兩行文字在 226px 寬度、最小視窗尺寸下會截斷或重疊,記錄實際遇到的尺寸限制與採取的因應(例如徽章在極窄時隱藏)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-25 實作交接

#### 完成內容(對照 Scope 1–6)

- `src/sidebar/sidebar.h`:`GroupSummary` 新增 `std::size_t pane_count{0}`、`std::size_t tab_count{0}`;新增具名常數 `kGroupRowHeight = 54`(96-DPI logical pixels)取代原本寫死的 `40`,`measure_item`、`set_rect` 都改用這個常數搭配既有 `MulDiv(…, dpi, 96)` 換算。
- `src/sidebar/sidebar.cpp` 的 `draw_item` 改為畫三塊內容於同一列(`pill` 矩形內):
  - 名稱行(上半,沿用既有選取色 `kSidebarActiveText`/`kSidebarText`)。
  - 副標題行(下半,新色 `kSidebarSubtitleText = RGB(148,163,184)`,字型用從 listbox 目前字型衍生、`lfHeight * 0.82` 的較小字型,每次繪製時建立/刪除,做法比照既有 brush 每次繪製建立/刪除的風格,不引入常駐字型快取)。文字由新的匿名命名空間函式 `format_subtitle(pane_count, tab_count)` 組成,單複數規則依決策 3(`"1 pane · 1 tab"` vs `"4 panes · 4 tabs"`),中點使用 `·`(U+00B7)。
  - 右側圓形徽章(`Ellipse`,直徑 22px@96DPI,底色固定 `kBadgeBackground = RGB(228,231,236)`,不受選取影響;文字固定 `kBadgeText = RGB(71,85,105)`,數字 = `tab_count`,與副標題第二個數字一致)。
  - 名稱/副標題文字區寬度扣掉徽章寬度與間距後再做 `DT_END_ELLIPSIS`,避免長名稱蓋到徽章。
- `src/app_shell/main.cpp` 的 `refresh_sidebar`:改用 `std::accumulate`(新增 `#include <numeric>`)加總每個 `group.panes[i].tabs.size()` 得到 `tab_count`,連同 `group.panes.size()` 一起塞進 `GroupSummary`。按鈕啟用邏輯簡化為只保留 `EnableWindow(state.sidebar_buttons[0], TRUE)`(New Group 永遠啟用),原本針對 index 1–5 的啟用/停用邏輯整段刪除——這五個按鈕已經不存在,對應的 grayed 判斷改成在 `WM_CONTEXTMENU` 當下即時計算(Scope 6)。
- 品牌列:新增 `kBrandBarHeight = 52`(96-DPI)常數、`brand_font()`(process-lifetime 靜態 `HFONT`,以 `DEFAULT_GUI_FONT` 的 `LOGFONTW` 改 `lfWeight = FW_BOLD` 產生,只建立一次,行為比照既有 `GetStockObject(DEFAULT_GUI_FONT)` 全程不刪除的慣例)與 `draw_brand_bar(window, dc, rect)`。畫法完全是純 GDI:`RoundRect` 畫藍色方塊圖示(`RGB(37,99,235)`,與既有 `draw_layout_button` 選取態同色)、`MoveToEx`/`LineTo` 疊一個白色十字(四宮格線條的簡化版,呼應 `draw_layout_glyph` 的風格)、`DrawTextW` 搭配 `brand_font()` 畫粗體 "PaneDock"。在 `paint_client_background` 裡,填完側邊欄背景色之後、畫 header 之前呼叫 `draw_brand_bar`,所以品牌列在 `WM_ERASEBKGND` 路徑裡繪製,不是獨立 HWND,永遠鋪在側邊欄最上方且不隨 `LISTBOX` 捲動(`LISTBOX` 的 rect 由 `layout_sidebar` 算過品牌列高度後才決定 `list_top`)。
- `layout_sidebar`:新增 `brand_height = scaled_value(window, kBrandBarHeight)`,`group_label`("GROUPS" eyebrow)的 y 位置與 `list_top` 都加上這個偏移量,把整個 Group 清單與其上緣文字往下推;footer 按鈕迴圈本身沒有改邏輯,因為 `kButtonIds`/`kButtonLabels` 陣列已經縮成 1 個元素,迴圈自然只佈局一顆按鈕。
- footer:`kButtonIds` 從 6 個元素縮成 `std::array<int,1>{kNewGroupId}`,`kButtonLabels` 縮成 `{L"+ New Group"}`。`kDuplicateGroupId`/`kRenameGroupId`/`kDeleteGroupId`/`kMoveUpId`/`kMoveDownId` 這五個常數**沒有刪除**,因為它們現在是 context menu 的命令 id,直接複用進 `WM_COMMAND` 既有的 `switch` 分支(該分支完全沒動)。`WM_CREATE` 建立按鈕的迴圈維持原樣,依陣列大小只建一顆 HWND。
- Group 列表 `WM_CONTEXTMENU`:加在 `window_proc` 裡(不是 subclass `sidebar` 的 `LISTBOX`)——右鍵訊息預設會經由子控制項的 `DefWindowProc` 冒泡給父視窗處理,不需要額外 subclass 就能在主視窗的 `WM_CONTEXTMENU` case 收到,用 `reinterpret_cast<HWND>(wparam) == state->sidebar.window()` 過濾只處理側邊欄清單的事件。流程:`lParam == -1`(鍵盤觸發,如 Shift+F10)時退回清單左上角附近當錨點(非目標範圍但避免當機或座標亂跳),否則用 `GET_X_LPARAM`/`GET_Y_LPARAM`(`<windowsx.h>`)取螢幕座標;`ScreenToClient` 轉成 client 座標後用 `LB_ITEMFROMPOINT` 找列;`HIWORD(hit) != 0`(點在清單外)或索引超出目前 `groups.size()`(**空清單時的邊界情況**)一律直接 `return 0`,不彈出選單。命中有效列後 `set_selected_index` + `refresh_sidebar`,`CreatePopupMenu`/`AppendMenuW` 建出 Duplicate/Rename/Delete/分隔線/Move Up/Move Down,`Move Up` 在 `index == 0`、`Move Down` 在 `index + 1 >= groups.size()` 時各自加 `MF_GRAYED`。`SetForegroundWindow(window)` 之後呼叫 `TrackPopupMenu(…, TPM_RETURNCMD | TPM_RIGHTBUTTON, …)`,拿到的命令 id 直接用 `SendMessageW(window, WM_COMMAND, MAKEWPARAM(command,0), 0)` 轉發給既有 `WM_COMMAND` 分派(完全重用 PD-017 驗收過的 `add_group`/`duplicate_group`/`begin_rename`/`delete_group`/`move_group` 呼叫路徑,本票沒有新增一份平行的分派邏輯)。`TrackPopupMenu` 回傳 0(使用者按 Esc 或點外面取消)時不轉發。

#### 未做的事(對照 Non-goals,確認沒有超出範圍)

- 沒有做 Preferences 按鈕、沒有刪除 Duplicate/Rename/Delete/Move Up/Move Down 的功能(只搬進 context menu)、沒有動側邊欄寬度常數(`kSidebarWidth` 維持 226)、沒有動 `core::model.h` 任何函式簽章、沒有新增鍵盤快速鍵開啟 context menu(`lParam == -1` 分支只是防呆,不是刻意支援 Shift+F10 的新功能)、品牌列圖示純色 + 線條,沒有漸層或陰影。

#### 驗證結果

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` 與 `cmake --build build`:成功(過程中修正一個既有 `SendMessageW(...)` 回傳值 `LRESULT` 不能直接 `static_cast<HFONT>` 的編譯錯誤,改用 `reinterpret_cast`,只影響這次新增的 `draw_item` 字型取得那一行)。
- `ctest --test-dir build --output-on-failure`:4/4 通過(`panedock_diagnostic_flag`、`panedock_core_model`、`panedock_core_layout`、`panedock_core_session`)。
- `rg -n "windows\.h|HWND|ComPtr" src\core`:無命中。
- `git diff --check`:通過。
- 視覺驗證:在真實桌面啟動 `build\PaneDock.exe`,用 `System.Drawing`(`Graphics.CopyFromScreen`)對整個虛擬桌面截圖後裁切出 PaneDock 視窗區域確認畫面。**確認品牌列(藍色方塊圖示 + 粗體 "PaneDock")固定顯示在側邊欄最上方、"GROUPS" 標籤與 Group 清單都在其下方**;**"Group 1" 這一列正確顯示副標題 "4 panes · 4 tabs" 與右側圓形徽章數字 "4"**(與該 Group 實際 4 個 pane、每個 pane 1 個 tab 的內容一致);**footer 只有一顆 "+ New Group" 按鈕**,沒有其餘五顆按鈕殘留。以上三點對應 Acceptance 1–3,截圖比對通過。
- 右鍵 context menu(Acceptance 4)**未能完成互動式驗證**:這個執行環境裡,`Get-Process` 能拿到 `PaneDock.exe` 的 `MainWindowHandle` 且 `GetWindowRect`/`IsWindowVisible` 都回報正常,`CopyFromScreen` 也確實能在虛擬桌面畫面裡看到這個視窗的即時內容(用來完成上一段的靜態畫面驗證);但 `FindWindow(L"PaneDockMainWindow", nullptr)` 在同一個 PowerShell session 裡回傳 `0`(找不到),對著螢幕截圖量出的座標送出 `SetCursorPos` + `mouse_event(MOUSEEVENTF_RIGHTDOWN/UP)` 模擬右鍵點擊 "Group 1" 那一列後,再次截圖比對,畫面完全沒有變化、沒有跳出任何 context menu。這代表這個 session 執行滑鼠/視窗訊息模擬的座標系或視窗站(window station)跟畫面實際顯示的合成結果是脫勾的——比 PD-017 交接區記錄的「沒有互動式桌面,因此無法驗證按鈕點擊」限制更嚴重一階:PD-017 當時完全沒有畫面可看,這次雖然能看到畫面即時內容,卻依然送不進滑鼠事件。因此 Acceptance 4(右鍵選單彈出、Move Up/Down 邊界灰階、五個選單項行為與原按鈕一致)只完成了**靜態程式碼路徑核對**:重新讀過 `WM_CONTEXTMENU` case 全文,確認命中判斷、`set_selected_index`/`refresh_sidebar` 呼叫時機、`MF_GRAYED` 的邊界條件(`index == 0` / `index + 1 >= groups.size()`,與 PD-017 原按鈕邏輯的 `*selected > 0` / `*selected + 1 < groups.size()` 等價)、以及 `TrackPopupMenu` 回傳值轉發到 `WM_COMMAND` 的路徑與既有六個按鈕 case 完全共用,沒有分岔出第二套邏輯。這點需要有真正互動桌面的人工核對,依循 PD-017 的先例(該票同樣以程式碼路徑核對 + 有限的自動化檢查判定完成),在程式碼審查已足以確認邏輯正確、且截圖已證實非互動部分渲染正確的前提下,判定本票完成,但把「右鍵選單彈出與五個動作的真人手動驗證」明列為遺留給下一次真人操作時的檢查項。

#### Handoff requirements 回覆

- 品牌列高度:`kBrandBarHeight = 52`(96-DPI),Group 列高度:`kGroupRowHeight = 54`(96-DPI),兩者都經由既有的 `scaled_value`/`MulDiv(…, dpi, 96)` 路徑做 DPI 縮放,沒有新增縮放機制。
- `GroupSummary` 最終形狀:
  ```cpp
  struct GroupSummary final {
      std::string id;
      std::wstring name;
      std::size_t pane_count{0};
      std::size_t tab_count{0};
  };
  ```
  之後若要加 Group 圖示,建議直接在這個結構體上再加一個欄位(例如 `COLORREF accent` 或一個小 enum),`draw_item` 已經把三塊內容(名稱/副標題/徽章)拆成獨立矩形計算,插入第四塊圖示只需要再切一塊 `RECT` 出來,不需要重寫整個函式。
- context menu 建立方式:掛在主視窗 `window_proc` 的 `WM_CONTEXTMENU` case,靠子控制項 `WM_CONTEXTMENU` 訊息未處理時經 `DefWindowProc` 冒泡給父視窗的預設行為接收,**沒有** subclass `sidebar` 的 `LISTBOX` HWND。選單項 id 直接重用 `kDuplicateGroupId`/`kRenameGroupId`/`kDeleteGroupId`/`kMoveUpId`/`kMoveDownId`,靠 `SendMessageW(window, WM_COMMAND, MAKEWPARAM(id,0), 0)` 轉發進既有分派,沒有新增第二套 command 處理。邊界情況:空 Group 清單(`groups.size() == 0`)時,`LB_ITEMFROMPOINT` 命中的索引必然 `>= groups.size()`(因為 listbox 本身沒有任何項目),已經在 `index >= state->application.groups.size()` 判斷裡涵蓋,直接 `return 0` 不彈出選單,不需要另外特判「清單是否為空」。
- 尺寸限制:226px 寬度下,副標題文字區扣掉左邊 6px inset、右邊給徽章的 `22 + 4*2 = 30px` 之後大約還有 180px 可用,兩行英文(如 "4 panes · 4 tabs")在預設 96 DPI 下不會截斷;若之後 Group 名稱本身很長,名稱行本來就有 `DT_END_ELLIPSIS`,副標題目前是固定格式的短字串(pane/tab 數字最多到個位數,PaneDock pane 數固定 1–4),沒有觀察到截斷風險。因為前述環境限制沒有真人在高 DPI 或最小視窗尺寸下實機檢視,若之後真人測試發現極端 DPI/視窗尺寸下有截斷或徽章被名稱行蓋到,需要另開票處理,目前程式碼邏輯上有做寬度扣除,理論上不會重疊。
