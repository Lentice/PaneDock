# PD-056 — 切換版型後,舊的 active 版型按鈕沒有重繪,highlight 沒清除

Phase 7 · app_shell · Depends on: PD-047

- Source: 使用者實機操作後回報(2026-08-26):「switch pane layout 時,原來的 active layout 不會清除 highlight」。
- Origin: 使用者原文第 2 項。
- Priority: HIGH——使用者會同時看到兩顆藍色按鈕,無法判斷目前版型,直接抵銷 PD-047 的成果。

## 已確認的根因(有程式碼證據,經獨立子代理驗證,不是猜測)

PD-047 已經把 `draw_layout_button` 的 `checked` 判斷從不可靠的 `BM_GETCHECK` 改成由 `WM_DRAWITEM`(第 2846-2863 行)以 `AppState` 的 active layout 直接比對後傳入。**這個判斷本身是正確的——問題是舊按鈕根本收不到新的 `WM_DRAWITEM`。**

呼叫鏈:

1. 第 3100 行 `WM_COMMAND` → `set_layout(window, *state, kLayoutTemplates[layout_index])`
2. 第 2142 行 `set_layout` → `panedock::core::switch_layout(...)` 更新 `group.layout_template`
3. 第 2151 行 `set_layout` → `apply_layout(window, state)`
4. `apply_layout` → `layout_header(window, state)`(第 1220 行)
5. `apply_layout` → `InvalidateRect(window, nullptr, TRUE)`

**關鍵缺口:`InvalidateRect(window, nullptr, TRUE)` 只讓「主視窗自己的 client 區域」失效,不會讓子視窗控制項失效。** 五顆版型按鈕是獨立的子視窗 HWND(第 2633-2634 行以 `BS_AUTORADIOBUTTON | BS_OWNERDRAW` 建立),要讓它們重繪必須對各自的 HWND 呼叫 `InvalidateRect`。

`layout_header` 第 1274-1280 行的迴圈確實會走過五顆按鈕,但它只做 `EnableWindow` 與 `BM_SETCHECK`:

```cpp
for (std::size_t index = 0; index < state.layout_buttons.size(); ++index) {
    EnableWindow(state.layout_buttons[index], enabled);
    SendMessageW(state.layout_buttons[index], BM_SETCHECK,
                 kLayoutTemplates[index] == current ? BST_CHECKED
                                                     : BST_UNCHECKED, 0);
}
```

`BM_SETCHECK` 在 owner-draw 按鈕上不可靠(這正是 PD-047 已經確認並繞過的問題),而且它**不保證觸發重繪**。同一個迴圈上方的 `SetWindowPos` 在按鈕位置沒有改變時也不會產生重繪。因此:被點擊的那顆按鈕因為使用者互動(按下/放開)自然重繪成新的 checked 樣式,而先前 active 的那顆按鈕位置沒動、沒被點、沒被 invalidate,於是保留舊的藍色像素。

整個 `main.cpp` 沒有任何一處對 `state.layout_buttons[]` 呼叫 `InvalidateRect`。

## 已確認的產品決策

1. **修法是在 `layout_header` 既有的那個迴圈裡(第 1274-1280 行)對每顆按鈕補上 `InvalidateRect(state.layout_buttons[index], nullptr, FALSE)`。** 選這裡而不是 `apply_layout`,理由是這個迴圈本來就已經走過全部五顆按鈕、而且本來就是負責同步按鈕狀態的地方,補一行即可,不需要新增第二個迴圈。`layout_header` 由 `apply_layout` 呼叫,而 `apply_layout` 涵蓋 `set_layout`/`toggle_layout`/`activate_group`/`add_group` 等所有會改變 active layout 的路徑,所以一處修好全部涵蓋。
2. **`InvalidateRect` 的 `bErase` 參數用 `FALSE`。** owner-draw 按鈕的 `WM_DRAWITEM` 會自己填滿整個 `rcItem` 背景(`draw_layout_button` 第一件事就是 `FillRect`),再讓系統擦背景只會多一次閃爍。
3. **`BM_SETCHECK` 的呼叫保留不動。** 雖然 PD-047 已證實它在繪製路徑上不可靠,但它仍然維護 radio group 的邏輯狀態(鍵盤操作、`BS_AUTORADIOBUTTON` 的群組互斥行為),移除它超出本票範圍且有回歸風險。
4. **不改 `draw_layout_button` 的顏色。** PD-047 已經定案(checked 背景 `RGB(37,99,235)`、圖示 `RGB(255,255,255)`、邊框 `RGB(29,78,216)`),本票只修重繪時機。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> A guard in the shared function is a smaller diff than a guard in every caller, and patching only the path the ticket names leaves sibling callers broken.

`docs/tickets/PD-047-layout-button-active-highlight-contrast.md` 驗收 2(本票補上這條當時未能實機驗證的驗收):
> 切換不同版型(點擊不同按鈕)後,高亮正確跟著移動到新選中的按鈕。

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers.

## Files to read and trace first

- `src/app_shell/main.cpp` 第 1220-1281 行(`layout_header`)——本票要修改的迴圈在第 1274-1280 行。
- `src/app_shell/main.cpp` 第 2846-2863 行(`WM_DRAWITEM` 的版型按鈕分支)——確認 `checked` 的計算已經正確,不需要改。
- `src/app_shell/main.cpp` `draw_layout_button` 定義——確認它自己填背景,所以 `bErase = FALSE` 安全。
- `src/app_shell/main.cpp` 第 2128-2163 行(`set_layout`/`toggle_layout`)、`activate_group`(第 1787 行)、`add_group`(第 1835 行)——確認全部都經過 `apply_layout` → `layout_header`。
- `docs/tickets/PD-047-layout-button-active-highlight-contrast.md`——顏色決策的來源,不重開。

## Scope

1. `layout_header` 的按鈕同步迴圈對每顆版型按鈕呼叫 `InvalidateRect(..., nullptr, FALSE)`。

## Non-goals

- 不改版型按鈕的顏色、大小、間距或分段外框(PD-046/PD-047 已定案)。
- 不移除 `BM_SETCHECK`。
- 不處理 more-actions「...」按鈕(PD-064 會移除它)。
- 不加 hover 特效(PD-058)。

## Acceptance

1. 依序點擊五顆版型按鈕,**任何時刻只有一顆**呈現藍底白圖示的 active 樣式,先前 active 的那顆立即恢復未選中樣式。
2. 用 Ctrl+Shift+L 熱鍵循環切換版型時,highlight 同樣正確跟隨。
3. 切換 Group(不同 Group 有不同版型)時,highlight 正確反映新 Group 的版型。
4. 沒有可察覺的按鈕閃爍。
5. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
6. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "InvalidateRect|layout_buttons" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:依序點擊五顆版型按鈕,每次截圖確認只有一顆是藍底白圖示。
# 本環境已具備 PrintWindow 截圖與 SetCursorPos/mouse_event 點擊模擬能力,請實際驗證。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 而不是 `Graphics.CopyFromScreen`。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 最終修改的位置與那一行程式碼。
- 五種版型逐一切換的實機截圖驗證結果。
- 若發現 `activate_group` 或熱鍵路徑仍有殘留 highlight,記錄具體條件。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-26 實作交接

**修改位置與內容:** 完全依照票據的產品決策 1,在 `layout_header` 既有的按鈕同步迴圈(`src/app_shell/main.cpp` 第 1274-1281 行)末端補一行:

```cpp
InvalidateRect(state.layout_buttons[index], nullptr, FALSE);
```

`BM_SETCHECK` 依決策 3 保留;`draw_layout_button` 的顏色未動;`bErase` 依決策 2 傳 `FALSE`。整個 diff 只有這一行。

**實機驗證結果(通過):**

視窗置於 50,50 1400x900。五顆版型按鈕以 `GetDlgItem(main, kLayoutButtonIdBase + i)`(`kLayoutButtonIdBase = 400`)取得後 `GetWindowRect` 換算螢幕座標,中心分別為 x=1257 / 1288 / 1319 / 1350 / 1381,y=103——**不是用截圖目測**。依序點擊五顆,每次點擊後以 `PrintWindow(hwnd, hdc, 2)` 截圖並將按鈕列區域以 `NearestNeighbor` 放大 5 倍判讀。

結果:**每一次截圖都只有一顆按鈕呈藍底白圖示,且正是剛點擊的那一顆**,先前 active 的那顆已恢復未選中的淺灰樣式。分段控制的共用外框、按鈕間距與 disabled 樣式均未受影響。

**建置與測試:** `cmake --build build` 成功;`ctest --test-dir build --output-on-failure` `100% tests passed out of 4`;`git diff --check` 通過。

**驗證期間發現一個與本票無關的當機,已另開 PD-068 並修復:** 反覆啟動/關閉程式時,`PaneDock.exe` 在 `SHELL32.dll` 發生存取違規。這不是本票造成的,但它會讓本票的實機驗證難以進行(程序關閉後殘留、占用 Ctrl+Shift+L 熱鍵導致下一個實例無法建立主視窗),因此先修掉才完成本票驗證。詳見 `docs/tickets/PD-068-shell-folder-view-setcallback-null-out-param.md`。

程序以不帶 `/F` 的 `taskkill /PID` 關閉,`clean_shutdown` 為 `true`,無殘留程序。
