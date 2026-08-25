# PD-039 — 移除「PANE LAYOUT」文字標籤,版面配置按鈕改用 hover tooltip

Phase 6 · app_shell · Depends on: PD-029

- Source: 使用者比對 `.\build\PaneDock.exe` 實際畫面與理想稿後回報(2026-08-25)。
- Origin: 「右上 PANE LAYOUT label 可以移除,但是 layout onhover 需要 tooltip」。
- Priority: LOW——純視覺/易用性微調,不影響功能正確性。

## 已確認的產品決策

1. **`state->layout_label`(目前顯示文字 `"PANE LAYOUT"` 的 `STATIC` 控制項)整個刪除,不保留隱藏版本。** 拿掉文字後,版面配置按鈕群直接靠右對齊(既有 `navigation_geometry`/header 右對齊邏輯已經是「從右邊界往左排」的計算方式,拿掉 label 只是減少一個排版元素,不需要重新設計對齊演算法)。比照 PD-033 刪除 `set_active` 的判斷標準:留一個永遠不顯示文字的 `STATIC` 控制項比直接刪除更容易誤導之後的讀者以為它還有作用。
2. **5 個版面配置按鈕與新的 more-actions 佔位按鈕,改用原生 Common Controls tooltip(`TOOLTIPS_CLASS32`)顯示對應說明,而不是任何自繪的懸浮提示框。** 沿用 Win32 標準做法:建立一個 `WS_POPUP | TTS_ALWAYSTIP` 的 tooltip 視窗,對每個按鈕 `TTM_ADDTOOLW` 註冊一筆 `TOOLINFOW`(`uFlags = TTF_IDISHWND | TTF_SUBCLASS`,`uId` 為該按鈕 HWND),文字對應各按鈕代表的版面配置(例如「Single pane」「Two panes side by side」「Two panes stacked」「Four panes」「More layouts」,依 `kLayoutButtonLabels`/實際版面配置語意逐一命名;more-actions 佔位按鈕的 tooltip 文字為「More actions」)。這是本程式碼庫第一個 tooltip,選 Common Controls 原生實作而非自繪,因為 `AGENTS.md`「Reach for the standard library and Win32 before adding a dependency」與「No unrequested abstractions」都指向不要為了一個提示框發明一套繪製系統。
3. **tooltip 只加在這一排按鈕(5 個版面配置按鈕＋more-actions 佔位按鈕),不因為本票而擴及應用程式其他按鈕(側邊欄按鈕、tab 的 `+`、導覽列按鈕等)。** 拿掉文字標籤導致「看不出按鈕功能」這個問題只存在於本來就沒有文字說明、純圖示的版面配置按鈕群;其他既有按鈕若原本就有清楚文字或既有慣例,不在本票範圍內,YAGNI,若之後有人回報其他按鈕也需要 tooltip,另開票或視為候選。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Don't add features, refactor, or introduce abstractions beyond what the task requires... Don't design for hypothetical future requirements.

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`docs/tickets/PD-029-quiet-header-alignment-and-layout-icons.md`(既有 header 右對齊邏輯,本票在其上刪減,不重寫排版計算):
> `x_start = max(sidebar_width + margin, client.right - margin - total_width)`

## Files to read and trace first

- `src/app_shell/main.cpp` 建立 `state->layout_label` 的位置(第 2276 行附近)與所有引用 `layout_label` 的地方(排版計算、`WM_DPICHANGED`、`WM_DESTROY` 是否有需要清理的資源)——用 `rg -n "layout_label"` 重新確認完整清單,不要憑本文件枚舉的位置。
- `src/app_shell/main.cpp` 的 header 排版計算函式(`navigation_geometry`/PD-029 決策提到的 `x_start` 計算式所在函式)——確認拿掉 `layout_label` 佔用的寬度後,總寬度(`total_width`)計算要跟著移除這一項,不會留下多餘的空白間距。
- `state->layout_buttons`、`state->more_actions_button` 的建立位置與既有 `kLayoutButtonIds`/`kLayoutButtonLabels`——確認每個按鈕目前代表哪一種版面配置,決定 tooltip 文字內容要對應正確。
- Win32 `TOOLTIPS_CLASS32`/`TTM_ADDTOOLW`/`TOOLINFOW` 文件——本程式碼庫第一次使用,需確認 `InitCommonControlsEx` 是否已包含 `ICC_WIN95_CLASSES`(tooltip 屬於這組),若未包含需要在既有 `InitCommonControlsEx` 呼叫處補上旗標,而不是另外呼叫一次。

## Scope

1. 刪除 `state->layout_label` 的建立、顯示、排版計算與(若有的話)`WM_DESTROY`/`WM_DPICHANGED` 清理程式碼。
2. 調整 header 排版計算,拿掉 `layout_label` 佔用的寬度項。
3. 確認/補上 `InitCommonControlsEx` 的 `ICC_WIN95_CLASSES` 旗標。
4. 建立一個 tooltip 控制項(`CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS32, ...)`),對 5 個 `state->layout_buttons` 與 `state->more_actions_button` 各自 `TTM_ADDTOOLW` 註冊對應文字。
5. Tooltip 視窗的生命週期(建立時機、`WM_DESTROY` 時是否需要顯式清理——一般子視窗會隨父視窗銷毀自動清理,需確認並記錄)。

## Non-goals

- 不替其他既有按鈕(側邊欄、tab strip、導覽列)新增 tooltip(已確認的產品決策 3)。
- 不改變任何按鈕的點擊行為、版面配置切換邏輯。
- 不自訂 tooltip 外觀(顏色、圓角、陰影),使用系統預設樣式。

## Acceptance

1. 應用程式右上角不再顯示「PANE LAYOUT」文字,版面配置按鈕群與 more-actions 按鈕維持正確靠右對齊,沒有殘留空白間距。
2. 滑鼠停留在任一版面配置按鈕上約一秒後,顯示對應該按鈕功能的 tooltip 文字。
3. 滑鼠停留在 more-actions 佔位按鈕上,顯示「More actions」tooltip。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "layout_label" src\app_shell\main.cpp
# 預期:無命中
rg -n "TOOLTIPS_CLASS|TTM_ADDTOOL" src\app_shell\main.cpp
# 預期:命中新增的 tooltip 註冊程式碼
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:確認「PANE LAYOUT」文字消失、按鈕群靠右對齊正常;
# 滑鼠停留在每個版面配置按鈕與 more-actions 按鈕上,確認 tooltip 正確顯示
```

## Handoff requirements

- 5 個版面配置按鈕與 more-actions 按鈕最終採用的 tooltip 文字清單。
- header 排版計算移除 `layout_label` 寬度項後的最終計算方式(是否有連帶調整 margin)。
- 若真實桌面測試發現 tooltip 在 DPI 縮放或多螢幕情境下位置異常,記錄下來。

## 交接區

<!-- 實作 agent 填寫,append-only -->
