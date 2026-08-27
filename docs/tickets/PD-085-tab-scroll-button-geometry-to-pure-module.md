# PD-085 — 把 tab 捲動按鈕的幾何計算抽成純函式(可單元測試)

## 來源

架構審查(2026-08-27,由 Claude/Codex/OpenCode 三個 agent 各自對 PaneDock 執行 `improve-codebase-architecture` 分析後產出的報告)一致指出 `src/app_shell/main.cpp` 是專案的「god module」,而其中重複被點名的最高槓桿切片是:**chrome/繪製幾何計算目前與 HDC 繪製動作混在一起,導致每次像素微調(PD-073、PD-080、PD-081 都是這類 commit)都要在 4000+ 行的檔案裡重新理解幾何邏輯**。三份報告都引用專案既有的 `src/app_shell/tab_overflow.h`(`tab_strip_viewport`、`clamp_tab_scroll_offset`,已有 `tests/unit/tab_overflow_test.cpp` 的 `static_assert` 測試)作為「已經證明可行的正確模式」,建議把同樣的模式擴大到其餘 chrome 幾何計算。

本票只取其中**最小、風險最低、最直接延續既有模式**的一塊:`draw_tab_scroll_button`(`main.cpp:2672-2729`)內部的純幾何計算。其餘候選(layout-button glyph 幾何、status-bar compartments、pane-card 圓角、tab strip 拖曳排序 slot 計算)刻意不在本票範圍內,留給後續票分別處理,避免單票過大。

## 為什麼是這一塊

`draw_tab_scroll_button` 目前把三類邏輯揉在同一個函式裡:

1. **純幾何計算**(給定 hit-test rect、DPI 已縮放的視覺寬高/位移量、forward/disabled/hovered 旗標,算出「視覺矩形」的位置、圓角半徑上限、箭頭三個端點座標)——這部分不含任何 HWND/HDC/COM,可以是純函式。
2. **顏色決策**(hover/disabled 對應哪個 `COLORREF`)——同樣是純邏輯,無外部相依。
3. **實際繪製**(`CreateSolidBrush`/`CreatePen`/`RoundRect`/`MoveToEx`/`LineTo`/`SelectObject`/`DeleteObject`)——這部分必須留在 `main.cpp`,因為需要 `HDC`。

PD-073/PD-080/PD-081 三張已完成的票,改動內容幾乎都落在第 1、2 類(視覺矩形的位移量、圓角半徑、按鈕尺寸)——這正是「每次都要重新理解整個函式才能改一個數字」的典型案例。抽出後,未來的像素微調會變成改一個純函式裡的一行,並且有 `static_assert` 立刻驗證,不需要重新建置整個 GUI 應用程式並肉眼比對畫面才能確認幾何正確。

## 綁定限制(引用)

- `AGENTS.md`:「Keep `src/core` free of HWND, COM and `windows.h`. It is the only automated test seam in this project.」——`tab_overflow.h` 屬於 `src/app_shell`,不是 `src/core`,但它延續的正是同一種「把不含 HWND/COM 的邏輯抽成純函式」精神,讓 `app_shell` 內也能有局部的自動測試 seam。不需要、也不應該把這段邏輯搬進 `src/core`(它是 UI 像素細節,不是核心領域模型)。
- `AGENTS.md`:「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」——直接擴充既有的 `tab_overflow.h`,不新增新檔案、不新增新的抽象層或介面。
- `AGENTS.md`:「New non-trivial logic needs one focused runnable test or self-check.」——延續 `tests/unit/tab_overflow_test.cpp` 既有的 `static_assert`、無框架風格,新增對應斷言。
- `docs/testing.md` 的既有慣例(見 `tab_overflow_test.cpp`):純函式、`constexpr`、`static_assert`,不需要執行期測試框架。

## 檔案與範圍

- `src/app_shell/tab_overflow.h`:新增純函式(常數名稱由實作者決定,建議延續現有的 `snake_case` 函式命名風格):
  - 一個計算「視覺矩形」(`visual_left`/`visual_top`/`visual_width`/`visual_height`)的函式,輸入為 hit-test rect 的寬高、DPI 已縮放的 `visual_width`/`visual_height`/`visual_offset_x`/`visual_offset_y`、`forward` 旗標——對應 `main.cpp:2676-2692` 目前的計算。
  - 一個計算圓角半徑上限的函式(`std::min(requested_radius, std::min(visual_width, visual_height) / 2)`)——對應 `main.cpp:2701-2703`,這段邏輯很小,可以視情況與視覺矩形函式合併或保持獨立,由實作者判斷哪種比較符合現有 `tab_overflow.h` 的函式粒度慣例。
  - 一個計算箭頭三個端點座標(起點、頂點、終點)的函式,輸入為視覺矩形中心點、`half` 長度、`forward` 方向——對應 `main.cpp:2713-2726`。
  - 顏色決策(`main.cpp:2693-2694`、`2719-2720`)是否一併抽出由實作者判斷;它不含幾何計算,抽出與否都不影響本票的核心目標,但若抽出必須是純函式(輸入旗標、輸出 `COLORREF` 或等價的顏色值,不得引入 HDC/HWND 相依)。
- `src/app_shell/main.cpp`:`draw_tab_scroll_button`(`main.cpp:2672-2729`)改為呼叫上述純函式取得幾何結果,函式本體只保留 HDC 繪製呼叫(`CreateSolidBrush`/`CreatePen`/`RoundRect`/`MoveToEx`/`LineTo`/`SelectObject`/`DeleteObject` 及其 null 檢查/釋放)。
- `tests/unit/tab_overflow_test.cpp`:新增對應新函式的 `static_assert` 斷言,涵蓋至少:forward 與非 forward 各一組正常案例、以及一組視覺尺寸大於 hit-test rect(需要被夾住/縮小)的邊界案例。
- `tests/unit/CMakeLists.txt`(若需要):確認 `tab_overflow_test` 已被既有建置設定涵蓋,通常不需要新增內容,僅供實作者確認。

## Scope

1. 把 `draw_tab_scroll_button` 目前內嵌的純幾何計算(視覺矩形、圓角半徑上限、箭頭端點座標)搬到 `tab_overflow.h`,成為不含 HWND/HDC 的純函式。
2. `draw_tab_scroll_button` 改為呼叫這些純函式,函式本體只做 HDC 繪製。
3. 為新函式新增 `static_assert` 單元測試,延續 `tab_overflow_test.cpp` 既有風格。
4. 建置與既有 5 項 ctest 全數通過,且視覺結果與修改前逐像素一致(這是純重構,不是行為變更——見下方 Non-goals)。

## Non-goals

- **這是純重構,不改變任何視覺輸出、行為或數值。** 不調整任何按鈕尺寸、位移量、顏色、圓角半徑的實際數值——只是把計算它們的邏輯搬到別的地方。修改前後,同樣輸入必須產生完全相同的視覺矩形/顏色/端點座標。
- 不處理架構審查報告中的其他候選(layout-button glyph 幾何、status-bar compartments、pane-card 圓角、tab/group/sidebar 拖曳排序 slot 計算、`AppState` 拆分、`main.cpp` 整體模組化)——這些留給後續個別開票,不在本票範圍內,避免單票過大。
- 不新增新的抽象層、介面或設計模式(例如不要為了「幾何 vs 繪製」發明一個 `IGeometryProvider` 之類的介面)——`tab_overflow.h` 目前就是一組自由函式,延續這個風格即可。
- 不修改 `kTabScrollButtonWidth`、`kTabScrollButtonVisualWidth`、`kTabScrollButtonVisualHeight`、`kTabScrollButtonCornerRadius`、`kTabScrollButtonGlyphHalf` 等常數的數值(`main.cpp:72-78`)——這些常數可以搬到 `tab_overflow.h` 或保留在 `main.cpp` 由呼叫端傳入,由實作者判斷,但數值本身不變。
- 不處理 hover 狀態追蹤本身(`hovered` 參數的來源,PD-083 剛完成的 `TrackMouseEvent` 邏輯)——本票只處理 `draw_tab_scroll_button` 內部已經拿到 `hovered` 之後的繪製幾何,不碰它是怎麼被算出來的。

## Acceptance Criteria

1. `tab_overflow.h` 新增的函式是 `constexpr`(或至少不依賴 HWND/HDC/COM 的一般函式),可以在 `static_assert` 中求值,或至少能在不建立視窗的情況下呼叫並比對結果。
2. `draw_tab_scroll_button` 的函式本體不再自行計算視覺矩形位置、圓角半徑上限或箭頭端點座標——改為呼叫 `tab_overflow.h` 的函式取得這些值。
3. 修改前後,`main.cpp` 建置出的執行檔在相同 DPI、相同輸入(forward/disabled/hovered 各種組合)下,tab 捲動按鈕的視覺輸出逐像素相同——用程式碼比對前後函式的計算式即可確認等價,不強制要求截圖比對(這是機械式重構,審視 diff 即可確認語意不變)。
4. `tests/unit/tab_overflow_test.cpp` 新增至少 3 組新斷言(forward、非 forward、邊界夾住案例各一),全部通過。
5. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過(5 個既有 test + 本票新增斷言所在的 `tab_overflow_test` 视为同一個 test target,不需要新增 test target)。
6. `git diff --check` 通過。
7. `rg -n "draw_tab_scroll_button"` 確認呼叫點(`main.cpp:2833`、`2837` 附近)未被誤改。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
rg -n "draw_tab_scroll_button" src/app_shell/main.cpp
```

本票是機械式的純重構(幾何計算搬家,數值不變),**不需要額外的視覺截圖驗證**——用建置通過 + 新增的 `static_assert` 斷言通過 + 手動比對 diff 前後計算式邏輯等價,即足以驗收。若實作者選擇額外做一次單次點擊+截圖比對前後畫面作為保險,依既有驗證政策(單一操作即可,不要連續多步驟)。

## Handoff 要求

依照 `docs/tickets.md` 既有票的 交接區 格式(繁體中文),記錄:

- 新函式的最終簽名與命名(若與本票建議的分組方式不同,說明理由)。
- 顏色決策是否一併抽出,以及理由。
- build/ctest/`git diff --check` 結果。
- 是否有任何額外的視覺驗證,以及結果。

## 交接區

（實作完成後由實作者填寫）
