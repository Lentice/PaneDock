# PD-083 — 導覽按鈕/側邊欄「New Group」按鈕/tab 捲動按鈕完全沒有 hover;版型按鈕的 hover 對比度不足

Phase 7 · app_shell · Depends on: PD-058, PD-047

- Source: 使用者實機操作後回報(2026-08-27)。
- Origin: 使用者原文:「所有的 buttons 都應該有 onhover style」、「現在的 layout onhover style 不明顯,需要重新調整」。
- Priority: MEDIUM——不影響功能,但介面上仍有大量可點擊按鈕沒有任何互動回饋,且已做的 hover 之一(版型按鈕)使用者實機看不出來。

## 已確認的根因(有程式碼證據,不是猜測)

### 缺口一:三組按鈕完全沒有 hover 分支

PD-058 已經把 hover 加到版型按鈕、tab、tab 的「+」、Group 側邊欄列——但那張票的 Scope 明確只列這 4 項,以下三組**當時不在範圍內,現在仍是原樣**:

1. **每個 pane 的五個導覽圖示按鈕(上一頁/下一頁/上層/重新整理/檢視模式)**——`src/app_shell/main.cpp` 的 `draw_navigation_icon_button`(第 798-822 行)只讀 `ODS_DISABLED`,背景永遠是 `CreateSolidBrush(RGB(255, 255, 255))`(第 801 行),沒有任何讀取 `ODS_HOTLIGHT` 或追蹤滑鼠位置的程式碼。四個 pane × 五個按鈕 = 20 個按鈕全部沒有 hover。
2. **側邊欄「New Group」按鈕**——`draw_sidebar_action_button`(第 824-843 行)只讀 `ODS_DISABLED`,背景永遠是白色 `RoundRect`,同樣沒有 `ODS_HOTLIGHT` 分支。
3. **Tab 條的左移/右移捲動按鈕(PD-080 新增)**——`draw_tab_scroll_button`(第 2638 行起)的參數只有 `forward`、`disabled`,沒有任何 hover 相關參數或狀態讀取。

三者建立時都帶 `BS_OWNERDRAW`(導覽按鈕與 New Group 按鈕是真正的 `BUTTON` 控制項,第 3152-3159、3238-3246 行),理論上和版型按鈕一樣能收到 `ODS_HOTLIGHT`;捲動按鈕則和 tab 本身一樣是 `paint_tab_strip` 內的自繪矩形,必須比照 PD-058 決策 3/4 自行追蹤滑鼠。

### 缺口二:版型按鈕已有的 hover 對比度太低,使用者實機看不出來

`draw_layout_button`(第 647-678 行)第 653-657 行:

```cpp
const COLORREF background = disabled
                                ? RGB(245, 247, 249)
                                : checked   ? RGB(37, 99, 235)
                                : hovered   ? RGB(242, 245, 248)
                                            : RGB(248, 250, 252);
```

一般態 `RGB(248, 250, 252)` 與 hover 態 `RGB(242, 245, 248)` 每個色版只差 `6/5/4`——這與 `docs/tickets/PD-076-unify-group-tab-active-hover-style.md` 已確認的根因**完全同一種缺陷**(該票原本的 tab hover 只差 `2/1/0`,判定「肉眼幾乎無法分辨」,改為 `RGB(226, 232, 240)` 後差距拉開到 `18/14/8` 才有感)。版型按鈕這裡的邏輯已經接上 `ODS_HOTLIGHT`(PD-058 做的),**問題純粹是顏色值本身,不是追蹤機制壞掉**。

## 已確認的產品決策

1. **三組缺口比照 PD-058 已建立的兩種做法,不要發明新機制:**
   - 導覽按鈕、New Group 按鈕(真正的 `BS_OWNERDRAW` `BUTTON`):直接讀 `DRAWITEMSTRUCT::itemState & ODS_HOTLIGHT`,比照 `draw_layout_button` 的既有寫法。**先實機確認 `ODS_HOTLIGHT` 在這兩種按鈕上真的會送達**(PD-058 已經對版型按鈕驗證過,原理相通,但仍需針對這兩種按鈕各自確認一次,不要假設一定成立;若不可用,改用下方的 `TrackMouseEvent` 自行追蹤,並在交接區記錄實測結果)。
   - Tab 捲動按鈕(`paint_tab_strip` 內的自繪矩形,不是獨立 HWND):比照 PD-058 對 tab 本身與「+」按鈕的做法,用既有的 `tab_strip_proc` 內 `WM_MOUSEMOVE`/`WM_MOUSELEAVE`/`TrackMouseEvent` 追蹤機制擴充,把「滑鼠是否在左移/右移按鈕的熱區矩形內」也算進同一套 hover 狀態,**只有在 hover 目標真的改變時才 `InvalidateRect`**(`AGENTS.md` 閒置 CPU 規則)。
2. **hover 視覺語言與 PD-058/PD-076 一致:比常態稍深一階的中性灰背景,不是外框、不是陰影、不是動畫。** disabled 態(導覽按鈕的 `RGB(190,197,209)`/捲動按鈕既有的 disabled 配色)不受影響,hover 與 disabled 是互斥狀態,disabled 時不套用 hover 背景。
3. **版型按鈕的 hover 背景色值必須有肉眼可辨的對比度,比照 PD-076 修正後的判斷標準(每色版差距至少兩位數,不是個位數)。** 具體數值由實作 agent 決定,但不得只是把現有兩個值各自微調 1-2,必須先做「修改前/修改後同一張放大截圖」的並排比對確認真的看得出來,而不是只看 RGB 數字差距。
4. **New Group 按鈕的 hover 背景需與其既有的白底圓角外框視覺語言相容**——`draw_sidebar_action_button` 目前是白底 + 淺灰圓角外框(`RoundRect`),hover 態建議背景填一個比白色略深的中性灰(同一個 `RoundRect` 形狀,不要變成方角),具體色值由實作 agent 決定並在交接區記錄。
5. **本票不處理 tab 本身、tab 的「+」按鈕、Group 側邊欄列的 hover——那三者 PD-058 已經做過,若實機發現這三者也有對比度問題,那是另一張獨立票,不在本票範圍內。** 使用者本次只回報「版型」按鈕對比度不足,不要順手擴大範圍去改 PD-058 已經做好的其他三處。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/tickets/PD-058-hover-feedback-for-interactive-chrome.md` 已確認的產品決策 4(本票沿用,不覆寫):
> 在該控制項的訊息處理中攔 `WM_MOUSEMOVE`,……只有在 hover 項目真的改變時才 `InvalidateRect`,並呼叫 `TrackMouseEvent(TME_LEAVE)` 以便在 `WM_MOUSELEAVE` 時清除 hover 狀態。

`docs/tickets/PD-076-unify-group-tab-active-hover-style.md` 的根因分析(本票對版型按鈕的對比度問題採用同一判準):
> hover 填色必須有可感知的對比度,不能只是抄一個「看起來合理」的絕對值。

## Files to read and trace first

- `src/app_shell/main.cpp` `draw_navigation_icon_button`(第 798-822 行)——導覽按鈕的繪製,決策 1 第一項的修改處。
- `src/app_shell/main.cpp` 第 3352-3385 行(`WM_DRAWITEM` 內導覽按鈕的五個 `if` 分支)——確認呼叫端是否需要多傳一個 hover 旗標(比照 `draw_layout_button` 呼叫端第 3347-3349 行的 `state->layout_hover_index == index` 寫法)。
- `src/app_shell/main.cpp` `draw_sidebar_action_button`(第 824-843 行)、其 `WM_DRAWITEM` 呼叫點(`kButtonIds` 相關,約第 3389-3395 行)——New Group 按鈕的修改處。
- `src/app_shell/main.cpp` `draw_layout_button`(第 647-678 行)第 653-657 行——版型按鈕對比度的修改處。
- `src/app_shell/main.cpp` `draw_tab_scroll_button`(第 2638 行起)——捲動按鈕的修改處,需要新增 hover 判斷的參數。
- `src/app_shell/main.cpp` `tab_strip_proc` 內既有的 `WM_MOUSEMOVE`/`WM_MOUSELEAVE` 分支(PD-058/PD-050 建立,約第 2900-2930 行)——擴充追蹤範圍到捲動按鈕熱區的落腳處。**注意這裡已經同時處理 tab hover 與拖曳邏輯,新增捲動按鈕 hover 不能互相覆蓋既有行為。**
- `src/app_shell/main.cpp` `tab_scroll_button_rects` 或等效的熱區矩形取得函式(PD-073/PD-080 建立)——捲動按鈕 hit-test 可直接重用,不要重新計算矩形。
- `docs/tickets/PD-058-hover-feedback-for-interactive-chrome.md`——既有 hover 機制的完整交接區,直接沿用其驗證結論。
- `docs/tickets/PD-076-unify-group-tab-active-hover-style.md`——對比度不足的診斷方法與修正後判準,直接沿用。

## Scope

1. 導覽按鈕(五個 × 四個 pane)加上 hover 背景。
2. 側邊欄 New Group 按鈕加上 hover 背景。
3. Tab 捲動按鈕(左移/右移)加上 hover 背景,沿用 tab 條既有的滑鼠追蹤機制擴充。
4. 版型按鈕的 hover 背景色值重新調整,確保有肉眼可辨的對比度。

## Non-goals

- 不改 tab 本身、tab 的「+」按鈕、Group 側邊欄列的既有 hover(PD-058 範圍,除非本票查證發現這三者也有對比度問題才需要另開票)。
- 不加動畫/漸變效果。
- 不改任何按鈕的尺寸、圖示、文字或版面位置。
- 不改 disabled 態的既有配色。
- 不改 Preferences 按鈕(目前 `kButtonIds` 只有一個 `kNewGroupId`,尚未實作 Preferences 按鈕本身,不在本票範圍)。

## Acceptance

1. 每個 pane 的五個導覽按鈕,滑鼠移入時背景出現可辨識的變化,移出後恢復。
2. 側邊欄 New Group 按鈕,滑鼠移入時背景出現可辨識的變化(圓角形狀不變)。
3. Tab 條的左移/右移捲動按鈕,滑鼠移入各自獨立出現 hover 背景,兩顆互不干擾;disabled(捲到底/捲到頂)時不套用 hover。
4. 版型按鈕的新 hover 背景色與常態背景色,3× 以上放大截圖並排比對,肉眼可清楚分辨兩種狀態(不是像修改前那樣幾乎看不出來)。
5. 上述四項均不影響既有的 checked/active/disabled 視覺狀態。
6. 滑鼠靜止不移動時,程式回到 0% CPU、無重繪(`InvalidateRect` 只在 hover 目標真的改變時觸發)。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
8. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "draw_navigation_icon_button|draw_sidebar_action_button|draw_tab_scroll_button|draw_layout_button|ODS_HOTLIGHT|layout_hover_index" src\app_shell\main.cpp
git diff --check
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,放大用 `InterpolationMode.NearestNeighbor`。

**驗證原則(本專案共同約定):只做單次點擊/滑鼠移入 + 截圖的驗證由 Agent 或本人執行;需要連續、多步驟操控滑鼠鍵盤的測試交給使用者本人執行**,若某項驗收條件無法用單次動作完成,如實在交接區標記未驗證並說明需要使用者手動驗證的具體步驟。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 三組原本缺少 hover 的按鈕,各自最終採用 `ODS_HOTLIGHT` 或 `TrackMouseEvent` 自行追蹤,以及理由。
- `ODS_HOTLIGHT` 在導覽按鈕與 New Group 按鈕上的實測結果(送達/不送達)。
- 版型按鈕修改前後的實際 RGB 值與放大截圖對比結果。
- Tab 捲動按鈕 hover 熱區與既有拖曳/tab hover 追蹤邏輯共存的驗證結果。
- 未驗證項目與原因(若有)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-27 — implementation pass

- `src/app_shell/main.cpp` 已完成本票四項範圍：導覽按鈕、側邊欄 `New Group`、tab 左右捲動按鈕，以及版型按鈕 hover 對比度。
- 導覽按鈕與 `New Group` 最終採用 `ODS_HOTLIGHT` 加上 `TrackMouseEvent(TME_LEAVE)` fallback。兩者共用 `AppState::owner_draw_hovered_button` 儲存目前被追蹤的 HWND；移入、移出或由另一顆按鈕接手時，只 invalidate 受影響的按鈕。採 fallback 的理由是既有 PD-058 已證實 owner-draw radio button 的 `ODS_HOTLIGHT` 不可靠，且本回合無法在鎖定桌面上另做實機確認。
- `ODS_HOTLIGHT` 導覽按鈕／`New Group` 實測結果：**未判定送達或不送達**。本回合唯一一次 Computer Use 視窗狀態擷取顯示 Windows 鎖定畫面／黑色視窗內容，依安全規範在任何點擊前停止；未把該畫面當成控制項證據。使用者需在真實桌面解鎖後確認兩類按鈕的 `ODS_HOTLIGHT` 行為；fallback 已使 hover 不依賴此結果。
- 版型按鈕色值由 `RGB(248, 250, 252)` 改為 `RGB(226, 232, 240)`；修改前後每色版差距為 `22/18/12`。本回合未取得可用的放大並排截圖，因此肉眼對比仍交由使用者在真實桌面確認。
- 導覽按鈕一般／hover 為 `RGB(255, 255, 255)`／`RGB(242, 245, 248)`；`New Group` 同值，維持既有白底圓角 `RoundRect` 與邊框；tab scroll 一般／hover 為 `RGB(255, 255, 255)`／`RGB(236, 240, 244)`。disabled 狀態保留既有配色且不套用 hover。
- Tab scroll 使用 `AppState::tab_scroll_hover_indices` 分別儲存左／右按鈕，`tab_scroll_button_at_point` 直接重用 `tab_scroll_button_rects` 及現有 offset/max 狀態。既有 `tab_strip_proc` 的 `WM_MOUSEMOVE` 先更新 tab／`+` 與 scroll hover，再以一次條件式 `InvalidateRect` 重繪，最後照原路徑呼叫 `update_tab_drag`；`WM_MOUSELEAVE` 一次清除兩種 hover，程式碼檢查確認沒有覆蓋拖曳邏輯。
- Agent checks：指定 CMake configure 成功；`cmake --build build` 成功；`ctest --test-dir build --output-on-failure` 為 5/5 PASS；票據指定的 `rg` 檢查通過；`git diff --check` 通過。沒有 linker lock，也沒有啟動或關閉既有 PaneDock.exe。
- 視覺驗證限制：未取得 layout、導覽、`New Group` 或 tab scroll 的有效 PaneDock 截圖；沒有做額外滑鼠／截圖序列。上述控制項需使用者在解鎖後確認，尤其是 layout 修改前後的 3× 對比、兩顆 scroll button 的獨立 hover，以及 disabled 不變。

### 2026-08-28 — 修正:layout 按鈕在相鄰按鈕間移動時 hover 殘留未清除

- **使用者實機回報**:layout 按鈕的 hover 樣式在滑鼠移出時可能沒有清除。
- **根因**:`layout_button_proc`(`main.cpp` 約 3122-3146 行)的 `WM_MOUSEMOVE` 分支只在 `layout_hover_index` 改變時 `InvalidateRect(window, ...)`——只重繪「新的」被 hover 按鈕,沒有重繪「舊的」被 hover 按鈕。5 個 layout 按鈕是彼此獨立、緊鄰排列的 `HWND`(PD-046 的分段控制),當滑鼠直接從按鈕 A 移到相鄰按鈕 B 時:B 的 `WM_MOUSEMOVE` 先把 `layout_hover_index` 改成 B 並只 invalidate B;A 隨後收到的 `WM_MOUSELEAVE` 檢查 `layout_hover_index == button_index(A)` 已經是 false(現在是 B),因此不會重繪 A——A 殘留舊的 hover 高亮,直到下一次任何原因觸發 A 重繪為止。
- 對照組 `owner_draw_button_proc`(導覽按鈕/`New Group` 用)的 `WM_MOUSEMOVE` 分支本來就有先取出 `previous`、同時 invalidate 「新舊兩個」按鈕的寫法(3101-3107 行)——`layout_button_proc` 少做了同一件事,這是純粹的實作遺漏,不是設計差異。
- **修正**:`layout_button_proc` 的 `WM_MOUSEMOVE` 分支在切換 hover index 前,先取出舊的 `layout_hover_index`,若存在就額外 `InvalidateRect(state->layout_buttons[previous], ...)`,寫法與 `owner_draw_button_proc` 一致。
- **驗證**:`taskkill /PID <pid>`(不帶 `/F`)關閉既有執行中的 `PaneDock.exe` 後,重新 `cmake --build build`(成功)與 `ctest --test-dir build --output-on-failure`(5/5 PASS)。未做連續滑鼠移動的實機截圖驗證(單一多步驟滑鼠操作留給使用者),邏輯修正已對照 `owner_draw_button_proc` 的既有正確寫法。
