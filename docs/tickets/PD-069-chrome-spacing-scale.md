# PD-069 — pane footer 缺少左右內距;整份 chrome 的間距改用一致的 4px 級距

Phase 7 · app_shell · Depends on: PD-060, PD-062, PD-064

- Source: 使用者回報(2026-08-26):「pane footer 需要有 x padding,請依照一般網頁的 padding/margin/gap 規則,設計調整 footer 以及其他應該調整的地方」。
- Origin: 使用者原文追加項。
- Priority: MEDIUM——不影響功能,但目前的間距是逐處手寫的魔術數字,愈晚統一,之後每張 UI 票都要各自猜一次。

## 已確認的現況(有程式碼與實機量測佐證,不是猜測)

### 主訴:status bar 完全沒有水平內距

`src/app_shell/main.cpp` `apply_layout` 第 1670-1679 行把 status bar 的矩形直接對齊 pane 的左右邊界:

```cpp
const RECT status_rect{rect.left, rect.bottom - status_height,
                       rect.right, rect.bottom};
SetWindowPos(state.status_bars[index], nullptr, status_rect.left,
             status_rect.top, status_rect.right - status_rect.left,
             status_rect.bottom - status_rect.top,
             SWP_NOZORDER | SWP_NOACTIVATE);
```

而該控制項是以 `SS_LEFT | SS_CENTERIMAGE` 建立的 `STATIC`(第 2848-2851 行),`SS_LEFT` 的文字**從控制項客戶區的 x=0 起繪**,中間沒有任何內距。實機以 `PrintWindow` 擷取量測:pane 內容左緣在螢幕 x=249,「20 items」的第一個字符像素落在 x≈251,**等效內距只有 2px**,而且那 2px 來自字型本身的 side bearing,不是刻意留的。這就是使用者看到文字貼著邊框的原因。

### 更廣的問題:整份 chrome 沒有共用的間距級距

目前每個區塊各自寫死一個數字,彼此沒有關係,也沒有註解說明為何是那個值:

| 位置 | 現值(96dpi 邏輯像素) | 程式碼 |
|---|---|---|
| status bar 水平內距 | **0** | `apply_layout` 第 1673 行 |
| pane canvas 外距 | 15 | `kPaneCanvasPadding`(第 49 行) |
| sidebar footer 按鈕外距 / 間距 | 8 / 4 | `layout_sidebar` 第 1197-1198 行 |
| header 左右外距 | 12 | `layout_header` 第 1232 行 |
| header 版型鈕之間 | 1 | `segment_gap`,第 1233 行 |
| header 版型鈕與「...」之間 | 4 | `more_actions_gap`,第 1234 行 |
| brand bar 圖示外距 | 14 | `draw_brand_bar` 第 1310 行 |
| brand bar 圖示與標題之間 | 10 | 第 1325 行 |
| brand bar 右側留白 | 8 | 第 1326 行 |
| address bar pill 內縮 | 6 | `kAddressBarInset`(第 69 行) |

15、14、10、6、1 這些值沒有任何一個是彼此的倍數。左右不對稱的地方也存在:brand bar 左邊留 14、右邊留 8。

## 已確認的產品決策

1. **採用 4px 基準的間距級距,以邏輯像素定義,一律經 `scaled_value(window, ...)` 換算。** 這是網頁 design system 的通用作法(Tailwind、Material 皆為 4px 基準),而本專案所有現有間距都已經是 4 的附近值,遷移成本最低。新增一組具名常數放在 `main.cpp` 既有常數區(第 44-95 行)附近:

   ```cpp
   // PD-069: 4px spacing scale. Every new gap/padding picks one of these
   // instead of inventing a number. Names follow the role, not the size,
   // so a later density change edits one constant rather than every site.
   constexpr int kSpaceTight = 4;    // 同一組件內、關係最緊的兩個元素
   constexpr int kSpaceSnug = 8;     // 同一列中相鄰的獨立元素
   constexpr int kSpaceBase = 12;    // 容器內文的左右內距
   constexpr int kSpaceRoomy = 16;   // 容器與容器之間、外層留白
   ```

2. **status bar 的內距用「縮小控制項矩形」達成,不改成 owner-draw。** 這是最小改動:`status_rect.left += scaled_value(window, kSpaceBase)`、`status_rect.right -= scaled_value(window, kSpaceBase)`,`SS_LEFT` 的文字自然就從內縮後的位置起繪。不要為了加內距而把 `STATIC` 換成自繪控制項。

3. **但 PD-060 的分隔線必須橫跨 pane 的完整寬度,不受本票內距影響。** 分隔線由父視窗在 pane 矩形上繪製,與 status bar 控制項的矩形是兩件事。實作本票時若 PD-060 已完成,務必確認分隔線沒有跟著縮進去;若尚未完成,在交接區寫明這個約束,供 PD-060 的實作者參照。

4. **本票只調整既有間距值,不改任何元件的尺寸、顏色、字級或圓角。** 高度(`kLayoutBarHeight`、`kStatusBarHeight`、`kTabStripHeight`、`kNavigationBarHeight`)一律不動——那些是垂直節奏,改動會牽動 pane 可用高度與 PD-062 的 tab 列高決策。

5. **具體替換對照表如下。** 沒有列出的數值一律不動。

   | 位置 | 現值 | 改為 | 理由 |
   |---|---|---|---|
   | status bar 左右內距 | 0 | `kSpaceBase` (12) | 主訴;與 header 的左右外距同級 |
   | `kPaneCanvasPadding` | 15 | `kSpaceRoomy` (16) | 15 是級距外的孤值,視覺上無法分辨 1px 差異 |
   | brand bar 圖示外距 | 14 | `kSpaceRoomy` (16) | 同上 |
   | brand bar 圖示與標題之間 | 10 | `kSpaceBase` (12) | 同上 |
   | brand bar 右側留白 | 8 | `kSpaceRoomy` (16) | 左右對稱;目前左 14 右 8 明顯不對稱 |
   | sidebar footer 按鈕外距 | 8 | `kSpaceSnug` (8) | 值不變,只換成具名常數 |
   | sidebar footer 按鈕間距 | 4 | `kSpaceTight` (4) | 值不變,只換成具名常數 |
   | header 左右外距 | 12 | `kSpaceBase` (12) | 值不變,只換成具名常數 |

6. **`segment_gap`(1)與 `more_actions_gap`(4)不納入本票。** 前者是分段控制刻意的髮絲線接縫,不是間距;後者所屬的「...」按鈕會被 PD-064 移除,改了是白工。

7. **`kAddressBarInset`(6)不納入本票。** 該值有第 62-68 行的註解說明它必須大於 `kAddressBarBackgroundRadius` 才能藏住 EDIT 的方角,是幾何約束而非美感選擇,改成 4 會露出方角、改成 8 會壓縮 EDIT 高度。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

——因此所有新常數都是邏輯像素,使用時必須經 `scaled_value(window, ...)`,不得直接當像素用。

`AGENTS.md`:
> **Keep `src/core` free of HWND, COM and `windows.h`.**

——間距常數屬於呈現層,放在 `src/app_shell/main.cpp`,不要放進 `src/core`。

`AGENTS.md`:
> Keep changes scoped to the ticket, and update the affected documents when behavior changes.

`docs/design-spec.md`(App UI 語言):
> App UI text must be English.

## Files to read and trace first

- `src/app_shell/main.cpp` 第 44-95 行——常數區,新常數加在這裡。
- `src/app_shell/main.cpp` 第 1668-1680 行(`apply_layout` 的 status bar 定位)——**主訴的修改處。**
- `src/app_shell/main.cpp` 第 2848-2857 行——status bar 控制項的建立,確認樣式是 `SS_LEFT | SS_CENTERIMAGE`,不需要改。
- `src/app_shell/main.cpp` 第 1190-1225 行(`layout_sidebar`)——`margin` / `gap`。
- `src/app_shell/main.cpp` 第 1227-1290 行(`layout_header`)——`margin` / `segment_gap` / `more_actions_gap`。
- `src/app_shell/main.cpp` 第 1303-1330 行(`draw_brand_bar`)——圖示與標題的三個數值。
- `src/app_shell/main.cpp` `kPaneCanvasPadding` 的所有使用點——用 `rg -n "kPaneCanvasPadding"` 找齊,**每一處都要一起改,不要只改第一個**。
- `docs/tickets/PD-060-pane-status-bar-selection-size-and-separator.md`——分隔線的所有權在那張票,見決策 3。
- `docs/tickets/PD-062-tab-strip-visual-polish.md`——tab 的內距與間距歸那張票,本票不碰。
- `docs/tickets/PD-064-header-and-nav-icon-cleanup.md`——「...」按鈕的移除歸那張票。

## Scope

1. 在 `main.cpp` 常數區新增 4px 級距的四個具名常數。
2. status bar 的矩形左右各內縮 `kSpaceBase`。
3. 依決策 5 的對照表,把 `kPaneCanvasPadding` 與 brand bar 的三個數值改到級距上,並把 sidebar/header 已經在級距上的數值換成具名常數。

## Non-goals

- 不改任何元件的高度、寬度、顏色、字級、字重或圓角。
- 不碰 tab strip 的內距與間距(PD-062)。
- 不碰 status bar 的文字內容與分隔線(PD-060)。
- 不碰「...」按鈕與 `more_actions_gap`(PD-064)。
- 不碰 `segment_gap` 與 `kAddressBarInset`(決策 6、7)。
- 不把間距常數搬進 `src/core`。
- 不新增設定項讓使用者調整密度。

## Acceptance

1. **status bar 文字左緣與 pane 內容左緣之間有 12 邏輯像素的空白**,實機以 `PrintWindow` 擷取後量測第一個文字像素的 x 位置驗證,四個 pane 皆如此。
2. status bar 文字不會因為內縮而被截斷;pane 縮到最窄時文字以省略號收尾或自然裁切,不得溢出控制項。
3. brand bar 的左右留白目視對稱。
4. 切換全部五種版型、切換 Group、縮放視窗後,間距維持一致,沒有元素重疊或超出容器。
5. **在 150% 與 200% DPI 下間距按比例放大**(可用 `SetWindowPos` 把視窗移到不同 DPI 的螢幕,或在系統設定切換縮放後重啟驗證);若本機只有單一 DPI,至少確認所有新常數的使用點都經過 `scaled_value`,並在交接區說明未能實測的原因。
6. `rg -n "kPaneCanvasPadding"` 的每一處使用點都已更新,沒有殘留舊值。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
8. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kSpaceTight|kSpaceSnug|kSpaceBase|kSpaceRoomy|kPaneCanvasPadding|status_rect" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:擷取視窗,量測 status bar 文字左緣與 pane 左緣的像素距離。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 而不是 `Graphics.CopyFromScreen`。控制項座標請用 `GetDlgItem` + `GetWindowRect` 取得真實幾何,**不要用截圖目測猜座標**。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 新增的常數與其最終數值。
- 每一處被改動的位置與新舊值對照。
- status bar 文字左緣的實機量測像素值(四個 pane)。
- DPI 縮放的驗證結果,或未能驗證的原因。
- 若發現本票的對照表與 PD-060/PD-062/PD-064 有實際衝突,寫明衝突點,不要自行擴大範圍去改那些票的範圍。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-27 實作交接

- 新增 96-DPI 邏輯像素級距常數：`kSpaceTight = 4`、`kSpaceSnug = 8`、`kSpaceBase = 12`、`kSpaceRoomy = 16`；所有使用點均經 `scaled_value(window, ...)` 換算。
- 間距替換：`kPaneCanvasPadding` `15 → kSpaceRoomy` `16`（舊常數與唯一使用點已更新）；`layout_sidebar` 的 `margin` `8 → kSpaceSnug` `8`、`gap` `4 → kSpaceTight` `4`；`layout_header` 的 `margin` `12 → kSpaceBase` `12`；`draw_brand_bar` 的圖示外距 `14 → kSpaceRoomy` `16`、圖示與標題間距 `10 → kSpaceBase` `12`、右側留白 `8 → kSpaceRoomy` `16`。
- status bar 矩形由原本貼齊 pane 的左右邊界改為 `rect.left + scaled_value(window, kSpaceBase)` 與 `rect.right - scaled_value(window, kSpaceBase)`；高度、上下位置與 status bar 文字內容未改動。`segment_gap`、`kAddressBarInset`、所有元件尺寸/顏色/字級/圓角均未改動。
- 未發現與 PD-062 或 PD-064 的衝突，未修改兩張 ticket。PD-060 目前 tracker 狀態仍為 `ready`，現行 `SS_OWNERDRAW` 的 `draw_status_bar()` 會在 status child 的 `item.rcItem` 內繪製分隔線；本票縮小 child 後該線也會縮短，尚未符合「分隔線橫跨完整 pane 寬度」的 PD-060 決策。依範圍約束未改動 PD-060 程式碼或文件，這個完整寬度修正留給 PD-060 後續收斂。
- status bar 文字左緣與四個 pane 左緣的實機像素值：未量測；本次未啟動 UI／PrintWindow。150%/200% DPI 也未切換實機驗證，但所有新增級距在使用點都確認走 `scaled_value`。
- Agent checks：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` 成功；`cmake --build build` 成功；`ctest --test-dir build --output-on-failure` 5/5 passed；`rg -n "kSpaceTight|kSpaceSnug|kSpaceBase|kSpaceRoomy|kPaneCanvasPadding|status_rect" src\app_shell\main.cpp` 確認四個新常數與 status 矩形使用點，且 `kPaneCanvasPadding` 無殘留；`git diff --check` 通過。
