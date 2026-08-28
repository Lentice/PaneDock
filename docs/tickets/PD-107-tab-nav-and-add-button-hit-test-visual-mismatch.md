# PD-107 — Tab 條捲動按鈕(nav buttons)視覺位置偏移到超出自己的可點擊熱區,「+」新增按鈕邊界因此被波及

Phase 7 · app_shell · Depends on: PD-080, PD-085

- Source: 使用者實機回報(2026-08-28)。
- Origin: 使用者原文:「pane tabs' nav buttons and add button 在UI上顯示的位置與onhover/click觸發的位置不同。」
- Priority: MEDIUM——不是崩潰或資料遺失,但是使用者天天會用到的互動元件,點擊命中率不穩定會持續造成困擾(點了沒反應、或點到相鄰按鈕的功能)。

## 已確認的現況(有程式碼與 commit 歷史證據,不是猜測)

`kTabScrollButtonWidth`(`main.cpp:85`)旁邊的既有註解白紙黑字寫著這個熱區矩形的設計初衷(PD-080 決策):

```cpp
// PD-080: keep the larger PD-073 rect as the hit-test target, but paint a
// compact button inside it so the visual control is smaller than the tab row.
constexpr int kTabScrollButtonWidth = 20;
```

也就是說:**外層矩形(`kTabScrollButtonWidth=20`)是唯一權威的點擊/hover 熱區,內層視覺矩形(`kTabScrollButtonVisualWidth=18`)只是畫在熱區裡面的裝飾,兩者理論上同心、熱區只比視覺矩形多一點緩衝**。這個不變量(invariant)在 PD-080 剛完成時是成立的。

但接下來一連串「使用者實機微調位置」的像素修正 commit(`68dd9ad` 2px、之後陸續調整、最終 `3c18e4e` 定案),把**畫出來的視覺矩形**往右、往下推,卻從未同步移動**權威熱區矩形**,兩者從此不再同心:

```cpp
// draw_tab_scroll_button, main.cpp:2813-2818
const auto visual = panedock::app_shell::tab_scroll_button_visual(
    static_cast<int>(rect.left), static_cast<int>(rect.top),
    static_cast<int>(rect.right), static_cast<int>(rect.bottom),
    scaled_value(window, kTabScrollButtonVisualWidth),
    scaled_value(window, kTabScrollButtonVisualHeight),
    scaled_value(window, 6), scaled_value(window, 1), forward);
```

```cpp
// tab_overflow.h:44-59
constexpr TabScrollButtonVisual tab_scroll_button_visual(
    int rect_left, int rect_top, int rect_right, int rect_bottom,
    int visual_width, int visual_height, int visual_offset_x,
    int visual_offset_y, bool forward) noexcept {
  const int width = rect_right - rect_left;
  const int height = rect_bottom - rect_top;
  const int clamped_width = std::min(visual_width, width);
  const int clamped_height = std::min(visual_height, height);
  const int top = rect_top + (height - clamped_height) / 2 + visual_offset_y;
  const int left =
      (forward ? rect_left : rect_right - clamped_width) + visual_offset_x;
  return {left, top, left + clamped_width, top + clamped_height};
}
```

`visual_offset_x`/`visual_offset_y` 只加在**畫出來的 `visual` 矩形**上,`rect`(傳入的參數,實際上就是 `apply_tab_item_size` 算出來、原封不動存進 `state.tab_scroll_button_rects[pane_index]` 的權威熱區——見下方)完全沒被同步偏移。以 96 DPI 基準的目前數值實際代入(`kTabScrollButtonWidth=20`、`kTabScrollButtonVisualWidth=18`、`visual_offset_x=6`):

- **back 按鈕**(`forward=false`):`left = rect.right - 18 + 6 = rect.right - 12`,`right = rect.right + 6`——視覺矩形的右邊**超出自己熱區右界 6px**,實際畫進的是 forward 按鈕的熱區範圍。
- **forward 按鈕**(`forward=true`):`left = rect.left + 6`,`right = rect.left + 24 = rect.right + 4`(因為該熱區寬度固定是 20)——視覺矩形的右邊**超出自己熱區右界 4px**,實際畫進的是緊接在後的「+」新增按鈕熱區(`state.tab_add_rects[pane_index]`,從 `available` 開始,`main.cpp:1266-1267`,而 forward 熱區右界正好等於 `available`,見 `main.cpp:1274-1276`)。

`tab_strip_proc` 的 `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`(`main.cpp:3048-3105`)命中測試用的是未被偏移的原始熱區(`state->tab_scroll_button_rects[pane_index]`、`state->tab_add_rects[pane_index]`),彼此互斥、無重疊、依序判斷(先 back、再 forward、才輪到 tab/add)。結果:

1. 使用者看到 back 按鈕的箭頭圖示畫面「跑到」和 forward 按鈕重疊的區域,**點在那塊視覺重疊區域,觸發的其實是 forward 的捲動**,不是眼睛看到的 back 圖示。
2. 使用者看到 forward 按鈕的箭頭圖示尾端畫進「+」新增按鈕的視覺範圍,**點在那塊區域,觸發的其實是新增分頁**,不是捲動。反過來說,使用者以為自己點在「+」按鈕左側邊緣(視覺上有一小塊被 forward 箭頭蓋住),但只要 x 座標落在 `available` 右側,實際判定仍是「+」——這部分本身沒錯,但**視覺上被 forward 箭頭覆蓋、使用者誤判自己點到 forward** 正是使用者回報「add button 位置與觸發位置不同」的來源。

這與 `docs/tickets.md` 既有的「三方架構稽核」決策記錄(god-module 筆記)點名的缺陷模式完全一致:「chrome 繪製函式把『純幾何計算』與『HDC 繪製』揉在一起,PD-073/080/081 這類像素微調 commit 正是這個缺陷的直接證據」——這次的偏移正是又一輪同類型的像素微調 commit,只改了畫面沒有回頭同步熱區,才第一次真正讓「畫面」與「熱區」出現實際重疊/越界,不是理論疑慮。

「+」新增按鈕本身(`main.cpp:2969-3010`)的背景/邊框(`hover`,對 `add` 做等比例 `InflateRect`)與熱區(`add`,即 `state.tab_add_rects[pane_index]`)兩者本身是同心、沒有偏移的——「+」按鈕自身的按鈕框沒有這個 bug;它只是被相鄰 forward 按鈕溢出的視覺圖形「入侵」了邊界。`main.cpp:3005` 的 `OffsetRect(&plus_rect, 0, -scaled_value(window, 2))` 只讓「+」文字符號本身在按鈕框內小幅垂直置中微調,不影響按鈕框或熱區位置,不是本票要處理的 mismatch(見下方 Non-goals)。

## Binding constraints — quoted, do not weaken

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`tab_scroll_button_visual`(`tab_overflow.h:44-59`)已經是 PD-085 抽出來的純幾何函式,本票應該重用它作為熱區計算的唯一權威來源,不要另外發明第二套偏移計算。

`docs/tickets.md` 的三方稽核決策記錄(見「已否決的方向」上方的架構筆記段落):
> chrome 繪製函式把「純幾何計算」與「HDC 繪製」揉在一起,PD-073/080/081 這類像素微調 commit 正是這個缺陷的直接證據……建議推廣到其餘 chrome 幾何計算。

本票是把同一個「幾何與繪製分離」原則,套用在「熱區與視覺必須用同一份幾何計算」這個更具體的不變量上。

`kTabScrollButtonWidth`(`main.cpp:86-87`)既有註解記載的原始設計不變量(本票要恢復,不是重新發明):
> keep the larger PD-073 rect as the hit-test target, but paint a compact button inside it.

## Files to read and trace first

- `src/app_shell/main.cpp:81-89`——`kTabScrollButtonWidth`/`kTabScrollButtonVisualWidth` 等常數與既有註解的原始不變量。
- `src/app_shell/main.cpp:1226-1277`(`apply_tab_item_size`)——`state.tab_add_rects`/`state.tab_scroll_button_rects` 目前如何獨立計算,未使用 `tab_scroll_button_visual`。
- `src/app_shell/main.cpp:2671-2676`(`tab_viewport_rect`)、`2698-2708`(`tab_scroll_button_at_point`)——目前熱區命中測試依賴的既有函式。
- `src/app_shell/main.cpp:2809-2855`(`draw_tab_scroll_button`)——視覺矩形計算與繪製,含目前的 `visual_offset_x=6`/`visual_offset_y=1`。
- `src/app_shell/main.cpp:2969-3010`——「+」按鈕背景/熱區/文字繪製,確認其本身不受影響、只是被相鄰按鈕波及。
- `src/app_shell/main.cpp:3048-3105`(`tab_strip_proc` 的 `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`)——熱區命中測試呼叫順序(back → forward → tab/add)。
- `src/app_shell/tab_overflow.h:44-59`(`tab_scroll_button_visual`)——PD-085 抽出的純幾何函式,本票要重用其輸出作為熱區來源。
- `docs/tickets/PD-080-tab-scroll-buttons-oversized.md` 交接區——像素微調的完整歷史脈絡(2px → 4px → 6px,1px)。
- `git log -p -- src/app_shell/main.cpp` 搜尋 `visual_offset_x`/`visual_offset_y` 可看到完整微調序列(commit `68dd9ad` 起)。

## 已確認的產品決策

1. **修正方向是讓熱區跟著視覺走,不是把視覺縮回舊熱區。** 使用者多輪「再往右移 2px」「再往下移」的實機回饋,是對視覺位置的明確確認,不能因為這票而悄悄撤銷——那需要新的使用者證據才能覆寫,不是本票的範圍。本票的問題是「熱區沒有跟著移」,不是「視覺移錯了」。
2. **熱區與視覺矩形改為同一個權威來源:直接重用 `tab_scroll_button_visual`(含相同的 offset 常數),不要在熱區計算與繪製各自維護一份偏移邏輯。** `apply_tab_item_size` 目前把 `state.tab_scroll_button_rects[pane_index]` 設成未偏移的原始 slot(`main.cpp:1268-1277`);改為呼叫 `tab_scroll_button_visual` 算出偏移後的視覺矩形,再視需要外擴一圈既有的緩衝(對照 PD-080 決策的「熱區只比視覺矩形多 2px 緩衝」精神,緩衝寬度由實作者決定,記錄在交接區),作為 `state.tab_scroll_button_rects[pane_index]` 真正存入的值。
3. **back/forward 兩個熱區、以及 forward 與「+」熱區之間,偏移後絕對不能互相重疊或跨越彼此的邊界。** 兩個 scroll button 共用同一段可用寬度(`tab_strip_viewport` 算出的 `scroll_button_width × 2`),偏移後若熱區膨脹導致相鄰兩者重疊,必須有明確的歸屬規則(例如以兩者原始 slot 的分界線為準,超過分界線的部分歸相鄰按鈕、或直接裁掉),裁決規則與實際採用的緩衝寬度都由實作者決定並記錄在交接區,但驗收標準(見下方)是可測的:任何一點只能被恰好一個熱區命中(或都不命中),不能同一點命中兩個熱區、也不能出現「視覺看得到但完全沒有熱區覆蓋」的邊界情況比目前更嚴重。
4. **「+」按鈕自身的按鈕框(`add`/`hover`)與其熱區(`tab_add_rects`)維持現狀不變**——問題只在於 forward 按鈕的視覺會不會畫進 `tab_add_rects` 的範圍,一旦 back/forward 的熱區依決策 2 收斂到自己真正的視覺範圍內,forward 就不會再畫出界、也就不會再入侵「+」的視覺範圍,不需要修改「+」按鈕本身的任何程式碼。
5. **DPI 縮放行為不變**——所有既有的 `scaled_value(window, ...)` 呼叫維持原樣,只重新配置「哪個矩形是熱區的權威來源」,不改變任何縮放邏輯本身。

## Scope

1. `apply_tab_item_size`(`main.cpp:1226-1277`)在算 `state.tab_scroll_button_rects[pane_index]` 時,改為呼叫 `panedock::app_shell::tab_scroll_button_visual`(帶入與 `draw_tab_scroll_button` 相同的 `visual_offset_x`/`visual_offset_y` 常數),而不是直接存入未偏移的 slot 矩形。
2. 视觉常數(`kTabScrollButtonVisualWidth`/`Height`、偏移量 6/1)若要在熱區計算與繪製兩處共用,抽成共同可見的具名常數(例如既有 `kTabScrollButtonWidth` 旁邊),不要各自硬編一份。
3. 決策 3 提到的「重疊裁決規則」——實作者選定一種簡單、可驗證的方式(例如以原始 slot 中線為界裁切,或限制偏移膨脹的緩衝上限)避免 back/forward 熱區之間、forward 與 add 熱區之間互相重疊。
4. `draw_tab_scroll_button` 本身的繪製邏輯與目前的視覺位置(6px 右、1px 下)不變。

## Non-goals

- 不改變任何按鈕目前的視覺位置(6px 右、1px 下的既有像素微調維持原樣)——這是使用者已經用實機回饋確認過的畫面結果,本票只補回失聯的熱區同步。
- 不處理「+」按鈕文字符號本身的 `OffsetRect(&plus_rect, 0, -scaled_value(window, 2))` 垂直置中微調——那是字型 baseline 的獨立小瑕疵,不是熱區/視覺矩形不同心的問題,不在本票範圍;如需調整另開票。
- 不重構 `tab_strip_proc` 的訊息分派順序或既有的 tab 拖曳排序邏輯。
- 不新增使用者可設定的按鈕位置/緩衝寬度設定值。
- 不擴大處理範圍到側邊欄或其他非 tab 條的按鈕(例如版面配置按鈕列),那些不在使用者本次回報範圍內。

## Acceptance Criteria

1. 在 tab 溢出(捲動按鈕出現)的狀態下,對 back 按鈕實際畫出來的視覺矩形範圍內任一點點擊,必定觸發向後捲動,不會觸發向前捲動或新增分頁。
2. 同上,對 forward 按鈕實際畫出來的視覺矩形範圍內任一點點擊,必定觸發向前捲動,不會觸發新增分頁或向後捲動。
3. 對「+」按鈕實際畫出來的視覺矩形(含背景/邊框在內,不含被相鄰按鈕覆蓋的區域,因為決策 2/3 落地後不應再有覆蓋)範圍內任一點點擊,必定觸發新增分頁。
4. 三個按鈕彼此的熱區互不重疊(同一點不會同時命中兩個熱區)。
5. hover 高亮(`state.tab_hover_indices`/`tab_scroll_hover_indices`)的觸發範圍與上述點擊熱區完全一致(用同一份矩形判斷,不是各自獨立的邏輯)。
6. 96 DPI 與至少一組非 100% DPI 縮放(如 150%)下,以上四點皆成立——用實際 `EnumChildWindows`/`GetWindowRect`/`PrintWindow(..., 2)` 截圖比對視覺矩形與熱區矩形是否同心,不得用猜測座標代替。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
8. `git diff --check` 通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "tab_scroll_button_visual|tab_scroll_button_rects|tab_add_rects|visual_offset_x|visual_offset_y" src\app_shell\main.cpp src\app_shell\tab_overflow.h
git diff --check
```

**驗證原則(本專案共同約定):只做單次點擊/滑鼠移入 + 截圖的驗證由 Agent 或本人執行**,截圖驗證方法沿用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,搭配 `EnumChildWindows`/`GetWindowRect` 取得真實矩形座標比對,不得用猜測座標。**驗證時務必立即釋放 computer use、盡量縮短測試內容**,完成必要的單次點擊+截圖確認後立刻用不帶 `/F` 的 `taskkill /PID <pid>` 關閉測試實例。

## Handoff requirements

- 實際採用的「熱區緩衝寬度」與「重疊裁決規則」數值/邏輯與選擇理由。
- 96 DPI 與至少一組非 100% DPI 下,back/forward/add 三個熱區與視覺矩形同心的截圖比對結果。
- 三個熱區互不重疊的驗證方式與結果。
- 未驗證項目與原因(若有)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
