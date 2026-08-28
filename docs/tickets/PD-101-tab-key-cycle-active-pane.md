# PD-101 — 新增 Tab 鍵在可見 pane 間循環切換 active pane

Phase 7 · app_shell · Depends on: PD-016

- Source: 使用者需求(2026-08-28)。
- Origin: 使用者原文:「press TAB to switch active pane (between visiable panes). 4 panes: 1-> 2-> 3-> 4-> 1, 2 panes: 1-> 2 -> 1」
- Priority: LOW——純鍵盤快捷鍵新增,不影響既有功能;等同行為已存在(F6),本票只是新增一個額外綁定。

## 已確認:這個循環邏輯已經存在,本票不是從零實作

`main.cpp:4239-4245`(PD-016 建立)已經是使用者描述的確切行為:

```cpp
if (message.message == WM_KEYDOWN && message.wParam == VK_F6) {
    const std::size_t count = active_group(state).panes.size();
    const std::size_t next = shift ? (active + count - 1) % count
                                   : (active + 1) % count;
    set_active_pane(window, state, next);
    continue;
}
```

4 pane 時 1→2→3→4→1、2 pane 時 1→2→1,正是這段程式碼已經產生的結果(`shift` 為 true 時反向)。本票的範圍純粹是**新增一個額外的鍵盤綁定(純 Tab,不含 Ctrl/Alt)觸發同一件事**,不是重新設計切換邏輯。

## 為什麼不能直接把 Tab 疊加在同一個 `if` 判斷式上

訊息迴圈(`main.cpp:4199-4249`)裡 Tab 鍵已經被兩種情境佔用,新綁定必須避開:

1. `state.explorers[active].translate_accelerator(&message)`(`:4204-4206`)——`IExplorerBrowser` 的內建加速鍵處理,在 address bar 沒有焦點時**優先於**所有自訂鍵盤邏輯執行,回傳 `S_OK` 就 `continue`,不會落到後面任何 `if`。若 Shell view 本身把 Tab 用在焦點在其內部元件間移動(例如清單/搜尋框等),那個行為會先發生,本票的 Tab 綁定永遠不會被呼叫到。
2. `Ctrl+Tab`/`Ctrl+Shift+Tab`(`:4222-4225`)已經是「同一個 pane 內切換分頁」的既有綁定(`cycle_active_tab`)。新綁定必須是**不含 Ctrl** 的純 Tab,兩者才不衝突。

## Fix 方向

在訊息迴圈同一組 `if` 鏈中(`:4222-4245` 之間任一位置,建議緊鄰既有 F6 判斷式之前或之後),新增一個判斷式:

- 觸發條件:`key_down && !control && !alt && message.wParam == VK_TAB && !address_bar_has_focus(state)`。
  - `!control`:避免與既有 Ctrl+Tab(分頁切換)衝突。
  - `!alt`:Alt+Tab 是作業系統層級的視窗切換,一般不會送達應用程式訊息佇列,但仍加上此判斷式以維持與其餘按鍵判斷式一致的防呆風格(比照 `:4226`/`:4230` 對 `alt` 的既有寫法)。
  - `!address_bar_has_focus(state)`:比照既有 Backspace 判斷式(`:4234-4235`)的既有守門邏輯——address bar 是單行 Edit 控制項,使用者在裡面編輯路徑時按 Tab 預期是文字編輯/焦點切換情境,不應該被劫持去切換 pane。**若沒有此守門,使用者在位址列打字後按 Tab 想跳出輸入焦點,卻意外整個 active pane 被切換,是明顯的使用者體驗回歸。**
- 觸發時執行的邏輯與既有 F6 分支完全相同(`count`/`next`/`set_active_pane`,`shift` 決定方向)——直接複製既有 3 行寫法即可,比照 `:4239-4245` 與其相鄰的 Ctrl+Tab、F6 兩個判斷式本身就是互相獨立、輕微重複的既有程式碼風格,不需要為了避免重複而抽出共用函式(`AGENTS.md`:「Prefer the smallest working change」)。若實作 agent認為抽成一個小的本地函式更清楚,也可以,但不是必要條件。
- **若實機測試發現某個 Shell view 情境下 Tab 被 `translate_accelerator` 攔截,導致本票的 Tab 綁定在該情境下不會觸發:記錄下來,不要為了解決攔截去 hack `translate_accelerator` 的行為或改變其呼叫順序。** 這是沿用 PD-016 自己票面上已經確立的政策(見下方引用),F6 本身當初也是用同一個態度處理。

## Binding constraints — quoted, do not go looking for them

`docs/tickets/PD-016` 已確認的既有政策(本票沿用,不覆寫):
> 若測試發現有 Shell view 攔截了 F6,記錄下來,不強行覆蓋。

本票對 Tab 鍵的攔截情境採用完全相同的態度。

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

複用既有的 `count`/`next`/`set_active_pane` 三行邏輯,不新增切換演算法或抽象層。

## Files to read and trace first

- `src/app_shell/main.cpp:4199-4249`——整個訊息迴圈鍵盤處理鏈,新判斷式的插入處。
- `src/app_shell/main.cpp:4204-4206`——`translate_accelerator` 優先攔截點,確認其對 Tab 的既有行為(需要實機驗證,見 Acceptance)。
- `src/app_shell/main.cpp:4222-4225`——既有 Ctrl+Tab 分頁切換分支,確認新綁定條件不重疊。
- `src/app_shell/main.cpp:4234-4235`、`:4239-4245`——既有 Backspace 的 address-bar 守門寫法、既有 F6 pane 切換分支,新判斷式直接比照這兩處的寫法組合。
- `src/app_shell/main.cpp:2622`(`address_bar_has_focus`)——既有守門函式,直接複用,不新增。

## Scope

1. 新增一個純 `VK_TAB`(不含 Ctrl,含或不含 Shift 決定方向,不含 Alt)的鍵盤判斷式,觸發與既有 F6/Shift+F6 完全相同的 pane 循環切換邏輯。
2. 新判斷式在 address bar 有輸入焦點時不觸發(比照既有 Backspace 守門邏輯)。
3. 既有 F6/Shift+F6、Ctrl+Tab/Ctrl+Shift+Tab 綁定與行為完全不變。

## Non-goals

- 不移除或取代既有 F6/Shift+F6 綁定——Tab 是額外新增的綁定,兩者並存。
- 不改變 `cycle_active_tab`(分頁切換)或 `set_active_pane`(pane 切換)本身的實作。
- 不處理 `translate_accelerator` 攔截 Tab 的情境(若發生,記錄即可,見上方 Fix 方向)。
- 不新增設定選項讓使用者停用此按鍵(目前所有既有快捷鍵都是寫死的,本票比照既有慣例)。

## Acceptance Criteria

1. 4 pane 版型下,連續按 Tab 依序切換 active pane:1→2→3→4→1。
2. 2 pane 版型下,連續按 Tab 依序切換:1→2→1。
3. 按 Shift+Tab 方向相反於 Tab(比照既有 F6/Shift+F6、Ctrl+Tab/Ctrl+Shift+Tab 的既有雙向慣例)。
4. Address bar 有輸入焦點時按 Tab,不觸發 pane 切換(具體行為——例如是否跳出焦點——由 Windows Edit 控制項既有行為決定,本票不額外處理,只要求「不切換 pane」這一點)。
5. Ctrl+Tab/Ctrl+Shift+Tab(分頁切換)行為與修改前完全相同,不受本票影響。
6. 若實機測試發現 Shell view 消費掉 Tab 導致本票綁定未觸發,在交接區如實記錄情境與範圍,不視為本票失敗。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
8. `git diff --check` 通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "VK_TAB|VK_F6|address_bar_has_focus|cycle_active_tab|set_active_pane" src\app_shell\main.cpp
git diff --check
```

**驗證原則(本專案共同約定):只做單次點擊/按鍵 + 截圖的驗證由 Agent 或本人執行;需要連續、多步驟按鍵操作(例如連續按 4 次 Tab 觀察循環)交給使用者本人執行**,若某項驗收條件無法用單次動作完成,如實在交接區標記未驗證並說明需要使用者手動驗證的具體步驟。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 新判斷式的實際插入位置(相對於既有 Ctrl+Tab/F6 判斷式的前後順序)。
- `translate_accelerator` 是否在任何情境下攔截了 Tab 的實測結果(若測過)。
- Address bar 有焦點時按 Tab 的實際觀察行為(是否跳出焦點、是否被 Edit 控制項吃掉)。
- 未驗證項目與原因(若有,例如連續多次按鍵循環的實機驗證留給使用者)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
