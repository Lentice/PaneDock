# PD-029 — Quiet header 右對齊、版型圖示重繪與 more-actions 佔位按鈕

Phase 6 · app_shell · Depends on: PD-028

- Source: `docs/panedock-ui-prototype.html?refined=1&variant=1&solo=1`(Quiet Header 變體)
- Origin: 2026-08-25,同 PD-028 的使用者回報。目前頂部工具列(`kLayoutBarHeight` 區塊)的版型按鈕圖示與設計稿的圖示風格不同,且按鈕群整體靠左貼齊側邊欄,設計稿是整組內容靠右對齊、與視窗右邊界留白。
- Priority: MEDIUM——視覺可見但互動邏輯完全不變,風險低於 PD-030 的 pane 卡片改版。

## Goal

Quiet header 內容(eyebrow 標籤 + 5 個版型按鈕)整組改為靠右對齊(呼應設計稿 `justify-content:flex-end`),版型圖示改為貼近設計稿的簡化線條符號,並在版型按鈕群組右側加一顆視覺上的「更多動作」圖示按鈕佔位。

## 已確認的產品決策

1. **靠右對齊只調整既有元件的 x 座標計算,不改變任何元件的建立方式、id 或事件處理。** `layout_header` 目前用 `x = sidebar_width + margin` 從左邊界開始擺放；改為先算出「eyebrow 標籤寬度 + 5 個按鈕寬度 + 間距 + more-actions 按鈕寬度」的總寬度,再用 `client.right - margin - total_width` 當起始 x,若總寬度超過可用空間則退回目前的靠左邏輯(避免在極窄視窗下裁切或負座標)。
2. **"more actions" 按鈕是視覺佔位,不接任何選單或行為。** 設計稿沒有標示這顆按鈕要做什麼,`docs/design-spec.md`/`docs/roadmap.md` 沒有任何「更多動作」相關功能被定義。比照 PD-028 對 "Preferences" 按鈕的判斷(YAGNI,不做空殼功能的對話框或選單),這顆按鈕建立後直接 `EnableWindow(..., FALSE)` 停用,不註冊 `WM_COMMAND` 分支,只用來補上視覺元素。等未來真的定義出「更多動作」要做什麼(例如批次操作、匯出設定)時,由那張新 ticket 啟用它,不在本票猜測內容。
3. **版型圖示改為貼近設計稿的簡化樣式,但沿用既有 `draw_layout_glyph` 的純線條繪製手法,不新增圖片資源。** 設計稿的五個圖示是:單一矩形(single)、左右兩欄矩形、上下兩欄矩形、左大右上下兩小(three-pane)、四宮格。目前 `draw_layout_glyph` 已經用 `FrameRect` + `MoveToEx`/`LineTo` 畫出對應的分隔線,索引語意(1=left/right、2=top/bottom、3=three-pane、4=four-pane-grid)已經正確;本票只調整線條粗細、圖示尺寸與外框樣式讓視覺更貼近設計稿的細線風格(例如把 `FrameRect` 外框改成 1px 圓角矩形,`RoundRect` 取代方角 `FrameRect`),不改變 `index` 對應的版型語意。
4. **eyebrow 標籤 "PANE LAYOUT" 與按鈕群之間、按鈕群與 more-actions 按鈕之間的間距沿用 PD-028 之前既定的 `gap` 常數,不新增額外的視覺分隔線。** 設計稿在按鈕群左側有一條細分隔線把 "PANE LAYOUT" 標籤跟版型按鈕隔開,本票視為可省略的裝飾細節(non-goal),優先做完靠右對齊與圖示重繪。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Don't add features, refactor, or introduce abstractions beyond what the task requires... Don't design for hypothetical future requirements.

`docs/design-spec.md` §9.1:
> `app_shell` 負責「WinMain、STA 初始化、訊息迴圈、主視窗、命令路由」,不得負責「資料模型計算、Shell 呼叫」。

`CONTEXT.md`:
> **layout template**: One of exactly five fixed pane arrangements... _Avoid_: layout mode, split configuration, arrangement

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `layout_header`、`draw_layout_glyph`、`draw_layout_button`、`kLayoutButtonIds`/`kLayoutButtonLabels`/`kLayoutTemplates`、`kLayoutButtonIdBase`、`WM_DRAWITEM` 對這些按鈕 id 的分派。
- `docs/panedock-ui-prototype.html` refined variant 1 的 quietbar 區塊——五個圖示按鈕與 more-actions 圖示的實際 SVG/CSS 描述(需要重新讀取檔案取得精確線條數,不要憑截圖臆測)。
- `src/sidebar/sidebar.h` 的 `kSidebarWidth`(header 起始 x 需要扣掉側邊欄寬度,PD-028 若已完成不影響這個常數)。

## Scope

1. `layout_header` 改為先量測 eyebrow 標籤寬度(`label_width`,可沿用現有固定常數或用 `GetTextExtentPoint32W` 動態量測,擇一,優先選現有固定常數以維持最小改動)、5 個版型按鈕總寬、more-actions 按鈕寬度與彼此間的 `gap`,加總後從右往左排列各元件的 `SetWindowPos` 座標;若計算出的最左側 x 小於 `sidebar_width + margin`,整組改回目前靠左的邏輯(即現有程式碼路徑保留作為窄視窗 fallback)。
2. `draw_layout_glyph` 的外框繪製由 `FrameRect`(方角)改為 `RoundRect`(小圓角,半徑用 DPI 縮放的小常數例如 2–3px),線條粗細與置中邏輯視覺調整以貼近設計稿的細線風格;`index` 參數與對應的 `switch` 分支語意不變。
3. 新增一顆 more-actions 圖示按鈕(`BS_OWNERDRAW`,沿用 `draw_sidebar_action_button` 或新增一個更貼近「圖示按鈕」(單純畫三個小圓點或省略符號)的 owner-draw 繪製函式),建立時即 `EnableWindow(..., FALSE)`,不分派任何 `WM_COMMAND`。
4. `AppState` 新增這顆按鈕的 `HWND` 欄位與對應 id 常數,`layout_header` 中一併排版。

## Non-goals

- 不實作 more-actions 選單或任何行為(已確認的產品決策 2)。
- 不畫 "PANE LAYOUT" 標籤左側的分隔線裝飾(已確認的產品決策 4)。
- 不改變版型按鈕的點擊行為、`BM_SETCHECK` 邏輯或 `kLayoutTemplates` 對應關係。
- 不處理側邊欄本身的視覺(屬於 PD-028)。

## Acceptance

1. Quiet header 內容(eyebrow 標籤、5 個版型按鈕、more-actions 佔位按鈕)整組靠視窗右側對齊,與右邊界保留與設計稿相近的留白;在把視窗縮到接近最小尺寸時不裁切、不與側邊欄重疊(退回靠左 fallback)。
2. 5 個版型圖示視覺上改為圓角細線風格,且點擊任一按鈕仍正確切換對應版型(行為與改版前一致,可用既有的 `BM_SETCHECK` 高亮驗證選中狀態正確)。
3. More-actions 按鈕可見但呈現停用樣式,點擊無反應、無當機。
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
# 手動:確認 header 內容靠右對齊、縮小視窗到接近最小尺寸時的 fallback 行為、
# 5 個版型按鈕點擊行為與 checked 高亮正確、more-actions 按鈕為停用樣式
```

## Handoff requirements

- 靠右對齊計算的最終公式與 fallback 觸發的實際寬度門檻。
- more-actions 按鈕的最終 id 常數與繪製函式,供未來真的要接上選單的 ticket 直接啟用。
- 若圖示重繪後在高 DPI 下有鋸齒或線條錯位的已知限制,記錄下來。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-25 實作交接

#### 完成內容(對照 Scope 1–4)

- **Scope 1(靠右對齊)**:`layout_header`(`src/app_shell/main.cpp`)改為先把 5 個版型按鈕與新的 more-actions 按鈕視為同一組「共 6 個等寬 slot」計算 `button_width`(沿用原本「用剩餘寬度除以按鈕數,取與固定尺寸的較小值」的邏輯,只是分母從 5 改成 `kButtonSlotCount = kLayoutButtonIds.size() + 1 = 6`,讓 more-actions 按鈕跟 5 個版型按鈕一起在窄視窗時等比例縮小),再算出 `total_width = label_width + 6*gap + 5*button_width + more_actions_width`(`more_actions_width` 目前直接等於 `button_width`,兩者同尺寸,沒有分開量測,理由見下方 Handoff 回覆)。最終公式:`x_start = max(sidebar_width + margin, client.right - margin - total_width)`。這一行同時涵蓋「靠右對齊」與「窄視窗 fallback」兩種情況——因為 `std::max` 在 `total_width` 太大導致算出的靠右起點小於 `sidebar_width + margin` 時,自動退回原本靠左貼齊側邊欄的位置,不需要額外的 if/else 分支或旗標。實測縮小視窗到 520×700 時(見下方視覺驗證),header 內容確實整組貼齊側邊欄右側、不裁切、不與側邊欄重疊。
- **Scope 2(圖示重繪)**:`draw_layout_glyph` 的外框繪製從 `FrameRect`(方角)改為 `RoundRect`,做法是建立一支 `HPEN`(沿用原本的線條色 `color`)、選入 DC 並把 brush 換成 `GetStockObject(NULL_BRUSH)`(避免填滿內部,只要外框線),圓角半徑 `radius = std::max(1, size / 6)`(`size` 是既有的圖示邊長變數,已經隨按鈕的 DPI 縮放尺寸自然縮放,不需要額外呼叫 `scaled_value`,因為 `draw_layout_glyph` 本身沒有 `HWND` 參數)。`index` 對應的分隔線 `switch` 分支(1=left/right、2=top/bottom、3=three-pane、4=four-pane-grid)完全沒動,點擊行為與 `BM_SETCHECK` 高亮邏輯也沒有觸碰。
- **Scope 3(more-actions 佔位按鈕)**:新增 `kMoreActionsButtonId = kLayoutButtonIdBase + 5`(=405,不與既有任何 id 範圍衝突,`WM_COMMAND` 裡 `id >= kLayoutButtonIdBase && id < kLayoutButtonIdBase + kLayoutButtonIds.size()` 這段只涵蓋 400–404,405 不會被誤判成版型切換)。`AppState` 新增 `HWND more_actions_button{nullptr}` 欄位。`WM_CREATE` 建立時用 `WS_CHILD | WS_VISIBLE | BS_OWNERDRAW`(不含 `WS_TABSTOP`,因為停用按鈕不該進入 Tab 順序),建立後立刻 `EnableWindow(state->more_actions_button, FALSE)`。新增 `draw_more_actions_button` 繪製函式(獨立於 `draw_sidebar_action_button`,因為設計稿的 more-actions 是純圖示按鈕不是文字按鈕):畫法是先用既有的淺灰底色 `RGB(248,250,252)`(跟版型按鈕未選取時同色,視覺上融入同一組按鈕列)`FillRect`,再用 `FillRect` 畫三個小方點(原本嘗試用 `Ellipse` + `NULL_PEN` 畫圓點,實測發現點完全沒有渲染出來,原因未查明,懷疑跟 owner-draw 停用按鈕的 HDC 狀態或 `Ellipse` 與 `SelectObject(NULL_PEN)` 的互動有關;換成 `FillRect` 畫小方塊後截圖確認可見,判斷成本效益後采用 `FillRect` 版本,不再深究 `Ellipse` 失敗的根因——這是本票對「簡化樣式」決策 3 允許的線條风格彈性範圍內的取捨)。`WM_MEASUREITEM`/`WM_DRAWITEM` 都新增對 `kMoreActionsButtonId` 的獨立分支(直接比對單一 id,不是範圍比對),`WM_COMMAND` 完全沒有新增任何分派——沒有註冊、也沒有讓它落入既有的 `BN_CLICKED` 版型切換範圍判斷。
- **Scope 4(排版)**:`layout_header` 在既有版型按鈕迴圈後面接著呼叫一次 `SetWindowPos`/`ShowWindow` 把 `more_actions_button`放到最後一個版型按鈕右側(間距沿用既有 `gap` 常數),沒有新增分隔線裝飾(對應 Non-goals)。

#### 未做的事(對照 Non-goals,確認沒有超出範圍)

- 沒有實作 more-actions 選單或任何行為;按鈕永遠停用,`WM_COMMAND` 沒有它的分支。
- 沒有畫 "PANE LAYOUT" 標籤左側的分隔線裝飾。
- 沒有改變版型按鈕的點擊行為、`BM_SETCHECK` 邏輯或 `kLayoutTemplates` 對應關係——`index` 語意、`WM_COMMAND` 裡 `set_layout` 呼叫路徑都原封不動。
- 沒有動側邊欄本身(PD-028 範圍),只在 `layout_header`/`draw_layout_glyph`/`WM_CREATE`/`WM_MEASUREITEM`/`WM_DRAWITEM` 這幾個既有函式裡加東西。

#### 驗證結果

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` 與 `cmake --build build`:成功,無警告無錯誤。
- `ctest --test-dir build --output-on-failure`:4/4 通過(`panedock_diagnostic_flag`、`panedock_core_model`、`panedock_core_layout`、`panedock_core_session`)。
- `git diff --check`:通過。
- 視覺驗證:啟動 `build\PaneDock.exe`,沿用 PD-028 交接區記錄的手法(`System.Drawing.Graphics.CopyFromScreen` 對整個虛擬桌面截圖,再用 `GetWindowRect` 取視窗座標裁切),並額外用 `NearestNeighbor` 插值放大裁切區域方便肉眼核對細節。**確認以下項目(對應 Acceptance 1–2)**:
  - Header 內容(eyebrow 標籤、5 個版型按鈕、more-actions 佔位按鈕)整組貼齊視窗右側,與右邊界有留白,視覺上與設計稿的 `justify-content:flex-end` 效果一致。
  - 5 個版型圖示外框已從方角改為圓角細線風格,`index` 語意(單一/左右/上下/three-pane/四宮格)視覺上仍然正確對應各按鈕標籤與圖示形狀。
  - More-actions 按鈕以三個小方點的淺灰圖示顯示,位於版型按鈕群組右側。
  - 用 `SetWindowPos` 把視窗寬度縮到 520px(不透過滑鼠拖曳,而是直接呼叫 API 模擬「縮到接近最小尺寸」)後重新截圖,確認 header 內容整組改回貼齊側邊欄左側(fallback 生效),沒有裁切、沒有與側邊欄重疊,4 個 pane 版面配置本身也還維持正常(這部分不是本票範圍,只是順便確認縮小視窗沒有連帶弄壞既有版面)。
- **未能完成互動式驗證的部分(對應 Acceptance 2 的「點擊行為」與 Acceptance 3 的「點擊無反應」)**:跟 PD-028 交接區記錄的環境限制一樣,這個執行環境裡 `SetCursorPos`/`mouse_event` 模擬滑鼠點擊送不進實際畫面(視窗站/座標系脫勾),因此**沒有**用滑鼠實際點擊 5 個版型按鈕確認 `BM_SETCHECK` 高亮切換與 more-actions 按鈕點擊無反應。這部分改用程式碼路徑核對代替:重新讀過 `WM_COMMAND` 對 `kLayoutButtonIdBase` 範圍(400–404)的既有分派邏輯完全沒有改動一行;`kMoreActionsButtonId`(405)不在該範圍內,也沒有出現在任何其他 `WM_COMMAND` 分支或 `switch` case 裡,加上按鈕本身在建立時就 `EnableWindow(..., FALSE)`,Windows 對停用的子視窗預設不會送出 `WM_COMMAND`/`BN_CLICKED`,所以「點擊無反應、無當機」在邏輯上有雙重保障(既沒有分派邏輯,控制項本身也停用)。這點與 5 個版型按鈕的高亮邏輯一樣,列為留給下一次真人操作時的檢查項。

#### Handoff requirements 回覆

- **靠右對齊最終公式**:`x_start = std::max(sidebar_width + margin, client.right - margin - total_width)`,其中 `total_width = label_width + kButtonSlotCount * gap + kLayoutButtonIds.size() * button_width + more_actions_width`(`kButtonSlotCount = 6`、`more_actions_width == button_width`)。**Fallback 觸發的實際寬度門檻**:當 `client.right - margin - total_width < sidebar_width + margin` 時觸發,也就是當「側邊欄寬度 + 兩倍 margin + header 內容總寬」超過視窗寬度時。以 96 DPI、`sidebar_width = 226`、`margin = 12`、`gap = 4`、`label_width = 76`、`button_width` 上限 30 為例(6 個按鈕都沒被壓縮的情況下),`total_width = 76 + 6*4 + 5*30 + 30 = 76+24+150+30 = 280`,門檻視窗寬度約為 `226 + 24 + 280 = 530`px 左右;實測 520px 寬度確實已經觸發 fallback(見上方截圖驗證),但因為 `button_width` 會隨可用空間縮小而不是維持 30 不變,實際切換點不是單一硬數字,而是由上述公式動態決定——這是刻意的設計(縮小視窗時按鈕先等比例變窄,寬度真的不夠時才整組退回靠左),不是需要另外記錄的常數。
- **more-actions 按鈕的最終 id 常數與繪製函式**:`constexpr int kMoreActionsButtonId = kLayoutButtonIdBase + 5;`(即 405),`HWND` 存在 `AppState::more_actions_button`。繪製函式是 `void draw_more_actions_button(const DRAWITEMSTRUCT& item) noexcept`(定義在 `draw_layout_button`之後、`draw_sidebar_action_button` 之前),目前只畫底色 + 三個小方點,完全沒有讀取 `item.itemState` 的 `ODS_DISABLED`/`ODS_SELECTED` 位元(因為按鈕本來就一直停用,不需要區分狀態)。之後要啟用選單時,直接把 `EnableWindow(state->more_actions_button, FALSE)` 那行改成依需求啟用,並在 `WM_COMMAND` 裡新增 `case kMoreActionsButtonId:` 分支(參考同檔案裡側邊欄 context menu 的 `TrackPopupMenu` 用法,`src/app_shell/main.cpp` 的 `WM_CONTEXTMENU` case 有現成範例)即可,不需要動 `layout_header` 的排版計算或 `draw_more_actions_button` 的視覺(除非要畫 hover/pressed 態,屆時可以參考 `draw_layout_button` 讀取 `ODS_SELECTED`/`ODS_DISABLED` 的寫法)。
- **已知限制**:
  1. `draw_more_actions_button` 原本用 `Ellipse` + `SelectObject(GetStockObject(NULL_PEN))` 畫圓點失敗(截圖顯示按鈕區塊完全空白,只有底色),換成 `FillRect` 畫小方點後才確認可見。沒有查出 `Ellipse` 失敗的根本原因(懷疑跟這顆按鈕一直是 disabled 狀態的 HDC 或 GDI 物件選取時機有關),如果之後要改回畫真正的圓點(更貼近設計稿的 `...` 圖示),需要先在真人可互動的環境重新排查 `Ellipse` 為何不繪製,而不是照抄目前的程式碼直接替換繪製函式內容。
  2. 沒有真人在高 DPI 螢幕上實機檢視;`draw_layout_glyph` 的圓角半徑 `size / 6` 在極小的 `size`(例如視窗被壓縮到按鈕本身只剩個位數像素寬)下可能退化成 1px 幾乎看不出圓角效果,但這屬於已經很極端的視窗尺寸,和 fallback 觸發的門檻(視窗寬度約 530px 以下按鈕即開始等比例縮小)相比是更極端的情況,沒有特別處理,若之後真人測試在常見的 125%/150% DPI 下發現線條錯位或鋸齒,需要另開票處理。
