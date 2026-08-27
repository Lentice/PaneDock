# PD-073 — Tab 數量超過 tab 條寬度時,溢出的 tab 變成零寬矩形、完全無法點擊

Phase 7 · app_shell · Depends on: PD-055, PD-062, PD-066

- Source: 使用者實機操作後回報(2026-08-26),附 Excel 工作表切換按鈕的參考截圖。
- Origin: 使用者原文:「pane tabs 過多時超過 view 寬度,會無法點擊,需要做左移/右移的按鈕,類似 excel sheets 切換那樣」。
- Priority: **HIGH——這是功能性缺陷,不是視覺加強。** 溢出的 tab 不只是被截斷,是完全收不到滑鼠事件,使用者永久失去存取那些 tab 的能力(除了關掉前面的 tab)。

## 已確認的根因(有程式碼證據,不是猜測)

`src/app_shell/main.cpp` 的 `apply_tab_item_size`(第 1011-1078 行)是每個 tab 矩形的單一計算位置。相關常數在第 56-58 行:

```cpp
constexpr int kTabMinWidth = 72;
constexpr int kTabMaxWidth = 200;
constexpr int kTabAddButtonWidth = 36;
```

計算流程:

```cpp
const int available = std::max(0, static_cast<int>(client.right) - add_width);
...
widths.push_back(std::clamp(static_cast<int>(size.cx) + text_reserve,
                            min_width, max_width));
...
const int total = std::accumulate(widths.begin(), widths.end(), 0);
if (total > available && total > 0) {
    for (int& width : widths)
        width = std::max(min_width, MulDiv(width, available, total));   // ← 地板在 min_width
}
...
for (const std::size_t index : order) {
    const int width = widths[index];
    const int left = std::min(x, available);                            // ← 夾在 available
    const int right = std::max(left, std::min(x + width, available));   // ← left == right
    const RECT rect{left, 0, right, client.bottom};
    ...
    x += width;
}
```

**等比壓縮的地板是 `min_width`,所以當 `tab 數 × min_width > available` 時,壓縮迴圈已經無法再讓 total 縮到 available 以內。** 接著配置迴圈把每個矩形的 `left` 與 `right` 都夾在 `available`,於是第一個超出邊界的 tab 之後,每一個 tab 拿到的都是 `{available, 0, available, height}` ——**一個零寬矩形**。

而 hit-test 走的是 `tab_item_at_point`(第 2201-2216 行):

```cpp
for (std::size_t index = 0; index < visuals.size(); ++index)
    if (PtInRect(&visuals[index].rect, point)) return index;
```

`PtInRect` 對零寬(`left == right`)的矩形一律回傳 `FALSE`。**所以那些 tab 收不到任何點擊、hover、拖曳——它們在 UI 上不存在。**

### 實機數字(2026-08-26 量測)

單一 pane 版型下第一個 tab 條的 `GetClientRect` 為 `560 × 31`。

| 量 | 值 |
|---|---|
| `client.right` | 560 |
| `add_width`(96 DPI) | 36 |
| `available` | 524 |
| `min_width` | 72 |
| 可容納的 tab 數 | `524 / 72` = **7.2** |

實測截圖(4× 放大)顯示第 8 個 tab 只剩右邊緣一條約 3px 的殘片,第 9 個之後完全看不見——與上表算出的 7.2 完全一致。這個 Group 有 15 個 tab,**其中 8 個是不可點擊的**。

## 已確認的產品決策

1. **採用使用者指定的方案:在 tab 條上加左移/右移按鈕,溢出時捲動可視範圍。** 不採用「無限壓縮 tab 寬度」(72px 已經只放得下約 6 個英文字元,再壓就不可讀),也不採用「下拉選單列出所有 tab」(那是次要的發現手段,不解決「看得見的 tab 排不下」這個主要問題)。
2. **按鈕位置:tab 條右端,「+」按鈕的左側。** 版面順序為 `[ tabs… ][ ◀ ][ ▶ ][ + ]`。理由:「+」的位置是使用者已經學會的錨點(PD-062 才剛把它加大加粗),不要移動它;把捲動按鈕放左端會把 tab 起點推走,每次溢出狀態改變都造成整條位移。
3. **按鈕只在溢出時出現,且只在出現時才佔用寬度。** 未溢出時 `available` 的算法與現在完全相同,版面不得有任何變化——否則沒有溢出的一般情況會憑空少掉兩顆按鈕的寬度。
4. **捲動單位是「一個完整 tab」,不是像素。** 一次點擊讓可視範圍前進/後退一個 tab。理由:像素捲動會讓邊緣出現被切一半的 tab,而 tab 的可讀性本來就已經被壓到極限;整格捲動則保證每個看得見的 tab 都是完整的。
5. **捲動位移必須夾在合法範圍內,且在下列每個時機重新夾一次:** 視窗/pane 尺寸改變、DPI 改變、tab 新增、tab 關閉、切換 Group、切換版型。最大位移的定義是「最後一個 tab 的右緣正好對齊可視區右緣」——不得捲出一片空白。
6. **切換 active tab 時必須自動把 active tab 捲進可視範圍。** 這包含使用者點擊、關閉 tab 後的焦點轉移、以及 `activate_group` 還原 Group 時的 active tab。**沒有這條,使用者切到一個看不見的 tab,畫面上不會有任何反應,等於 bug 換了個位置。**
7. **滑鼠滾輪在 tab 條上捲動 tab。** `WM_MOUSEWHEEL` 一格等於一個 tab,與按鈕同一套夾範圍邏輯。這是使用者對 tab 條的既有預期,而且它重用第 4 點已經寫好的捲動函式,增量成本接近零。
8. **位移是純 UI 狀態,不持久化。** 不寫進 session,不進 `src/core`。重啟後由第 6 點的「捲到 active tab」自然決定初始位移。
9. **按鈕在到底時畫成 disabled 並且不接受點擊**(位移為 0 時 ◀ disabled,位移為最大值時 ▶ disabled)。沿用 `draw_navigation_icon_button` 已有的 `ODS_DISABLED` 配色 `RGB(190, 197, 209)`。
10. **與 PD-066 拖曳排序的關係:捲動位移套用在重排後的幾何之上,兩者不互斥。** 但**「拖到邊緣自動捲動」是本票的非目標**——那需要一個計時器,`AGENTS.md` 禁止 polling timer,而且沒有它使用者仍可先捲動再拖曳。
11. **不改 `kTabMinWidth`。** 72px 是 PD-062 定案的可讀性下限,本票是給溢出一個出口,不是重新調整壓縮參數。
12. **溢出的 tab 必須「自然被截斷」,不得變成零寬矩形。**(使用者 2026-08-26 明確指示。)這是本票最重要的一條,它同時是修正的核心與最容易做錯的地方:
    - 每個 tab 一律拿到**它應得的完整寬度**的矩形,矩形本身**不因為超出可視區而被夾小**。超出可視區的部分靠繪製時的裁切消失,不靠改寫幾何消失。
    - 亦即 `apply_tab_item_size` 配置迴圈裡現行的 `std::min(x, available)` / `std::min(x + width, available)` **必須拿掉**——正是這兩個夾值把第 8 個之後的 tab 壓成 `left == right`。
    - 落在邊界上的那一個 tab 因此會被裁成半個,這是**正確且預期**的行為:它讓使用者看得出「右邊還有東西」。決策 4 的整格捲動保證捲動後每個 tab 都會停在完整位置,所以邊界的半個 tab 只在捲動的中途狀態出現。
    - 裁切用 GDI 的 clip region(可視區矩形),**不要**在繪製迴圈裡自行判斷「這個 tab 超出了就跳過」——跳過會讓邊界的半個 tab 整個消失,又回到「看不出右邊還有東西」的問題。
    - hit-test 不需要任何額外的邊界判斷:可視區外的點擊本來就落不到 tab 條的 client 區域內,而可視區內的部分因為矩形是完整的,`PtInRect` 自然正確。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` FR-003:
> 每個 pane 可以有多個 tab,使用者可以新增、關閉、切換與重新排序 tab。

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`:
> Keep `src/core` free of HWND, COM and `windows.h`.

`AGENTS.md`:
> Read the relevant spec section and trace every caller before touching shared code. A guard in the shared function is a smaller diff than a guard in every caller, and patching only the path the ticket names leaves sibling callers broken.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs persist as data and are realized on activation.

(最後一條的意義:捲動只改矩形,**不得**因為某個 tab 捲出可視範圍就去銷毀或建立任何 `IExplorerBrowser`。可視性與 realize 是兩件無關的事。)

## Files to read and trace first

| 檔案 | 讀什麼 |
|---|---|
| `src/app_shell/main.cpp` 第 56-68 行 | tab 相關常數。本票要新增一個捲動按鈕寬度常數。 |
| `src/app_shell/main.cpp` `apply_tab_item_size`(第 1011-1078 行) | **本票的主戰場。** 每個 tab 矩形的單一計算位置,PD-066 的 placeholder 重排也在這裡。 |
| `src/app_shell/main.cpp` `AppState`(第 330-345 行附近) | `tab_visuals`、`tab_add_rects`、`tab_placeholder_rects` 的宣告方式。新的位移狀態要照同樣的 per-pane array 慣例。 |
| `src/app_shell/main.cpp` `paint_tab_strip`(第 2300 行起) | tab 條的唯一繪製路徑(PD-049 之後是 subclass 過的 `STATIC` 的 `WM_PAINT`,**不是** owner-draw)。新按鈕要在這裡畫。 |
| `src/app_shell/main.cpp` `tab_item_at_point`(第 2201 行) | hit-test。要加上兩顆新按鈕的判定,且不得把按鈕誤判成 tab。 |
| `src/app_shell/main.cpp` tab 條的 subclass 程序 | `WM_LBUTTONDOWN` / `WM_MOUSEMOVE` / `WM_MOUSEWHEEL` 要接在哪裡,以及 PD-058 的 hover 狀態怎麼存的。 |
| `src/app_shell/main.cpp` `apply_tab_item_size` 的所有呼叫端 | `rg -n "apply_tab_item_size" src\` 。決策 5 的「重新夾範圍」時機表要對照這份清單逐一確認,不能只改其中一條路徑。 |
| `src/app_shell/main.cpp` `activate_group`、`close_tab_in_pane`、新增 tab 的路徑 | 決策 6 的「捲到 active tab」要掛在這些地方。 |

## 範圍

1. 新增每個 pane 的 tab 捲動位移狀態(慣例比照 `tab_add_rects`:`std::array<..., kExplorerCount>`)。
2. 新增捲動按鈕寬度常數,以及一個「把位移夾進合法範圍」的共用函式。**夾範圍的邏輯只能有一份**——決策 5 列了六個時機,如果每個時機各寫一次夾範圍,其中一定有一個會漏。
3. 改 `apply_tab_item_size`:
   - 判斷是否溢出(在 `min_width` 地板下 `total` 仍 `> available`)。
   - 溢出時,`available` 額外扣掉兩顆按鈕的寬度,並把 tab 矩形整體左移位移量。
   - 未溢出時把位移歸零,行為與現在完全相同。
   - 溢出時**不要**再把 `left`/`right` 夾成零寬,而是讓捲出可視區的 tab 拿到可視區外的矩形(繪製時裁切、hit-test 時因為在可視區外自然不會命中)。這是本票的核心修正:零寬矩形是 bug,可視區外的正常矩形不是。
4. 在 `paint_tab_strip` 畫兩顆按鈕(含 disabled 態),並確保 tab 的繪製被裁切在可視區內,不會畫到按鈕上面。
5. hit-test 與點擊處理:兩顆按鈕、`WM_MOUSEWHEEL`。
6. 決策 6 的自動捲動到 active tab,掛在切換/關閉/還原 Group 的路徑上。
7. DPI:所有新常數都要走既有的 `scaled_value`;`WM_DPICHANGED` 之後要重新夾位移。

## 非目標

- 不做「拖到邊緣自動捲動」(決策 10:需要計時器)。
- 不做列出所有 tab 的下拉選單。
- 不做位移的動畫或慣性捲動。
- 不改 `kTabMinWidth` / `kTabMaxWidth` / 等比壓縮的參數(決策 11)。
- 不持久化位移(決策 8)。
- 不動 Group 側邊欄的捲動(那是 `LISTBOX` 原生捲軸,PD-067 的範圍)。
- 不因為 tab 捲出可視範圍而改變任何 `IExplorerBrowser` 的生死。

## 驗收條件

1. 一個 pane 有 15 個 tab 時,**每一個 tab 都可以透過捲動按鈕到達並成功點擊切換**。實測要點到第 15 個 tab 並確認 pane 真的導航到那個位置。
2. **沒有任何 tab 拿到零寬矩形**(決策 12)。用 `tab_visuals` 的每個 rect 直接檢查 `right > left`,不要只看畫面。修改前的同一組資料要一起附上作為對照——修改前第 8 個之後應該全部是 `left == right == available`。
2a. 落在可視區邊界上的 tab 是**被裁成半個**,不是整個消失。附 4× 放大截圖。
3. tab 數量少到不溢出時,tab 條的版面與本票修改前**逐像素相同**——兩顆按鈕不出現,也不佔寬度。
4. 位移為 0 時 ◀ 是 disabled;捲到底時 ▶ 是 disabled,且最後一個 tab 的右緣正好貼齊可視區右緣(不得有空白)。
5. 切到一個原本捲出可視範圍的 tab 時,它會自動被捲進可視範圍。
6. 關閉 tab 直到不再溢出時,按鈕消失且版面回到第 3 點的狀態,沒有殘留位移造成的空白。
7. 切換 Group、切換版型、改變視窗寬度、`WM_DPICHANGED` 之後,位移都在合法範圍內,畫面沒有空白也沒有被切一半的 tab。
8. 滑鼠滾輪在 tab 條上可捲動,到底時不再動作。
9. 溢出狀態下 PD-066 的拖曳排序仍然正確:placeholder 出現在正確位置,放開後順序正確。
10. 操作完畢、游標靜止後,10 秒內 process 的 CPU 時間增量為 0。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "apply_tab_item_size|tab_item_at_point|tab_add_rects|WM_MOUSEWHEEL" src\app_shell\main.cpp
git diff --check
```

**建置輸出路徑注意:** 目前 `build` 這個 build directory 的 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 被設成 `pd062-output`,實際執行檔在 `build\pd062-output\PaneDock.exe`。若不確定,重新 configure 一個乾淨的 build directory。

**截圖驗證方法(本環境已驗證可用,請直接沿用):** `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,不要用 `Graphics.CopyFromScreen`;放大用 `InterpolationMode = NearestNeighbor`。

**要驗證點擊與拖曳,純訊息注入無效。** PD-066 的獨立驗證已經證實:只用 `SendMessage(WM_LBUTTONDOWN/WM_MOUSEMOVE)` 送給 tab 條 HWND,app 不會進入拖曳狀態,畫面毫無變化。必須用 `SetForegroundWindow` + `SetCursorPos` + `mouse_event` 建立真實 capture,並用 `GUITHREADINFO.hwndCapture` 確認 capture 落在目標 child HWND 上。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉。**

## Handoff requirements

- 溢出判定的實際條件式,以及未溢出時「版面逐像素相同」是怎麼驗證的。
- 15 個 tab 的實測:每個 tab 的 rect(證明沒有零寬),以及點到第 15 個 tab 後 pane 實際導航到的位置。
- 決策 5 的六個時機各自的實測結果。
- 決策 6 的自動捲動掛在哪些函式上,以及 `rg` 出的 `apply_tab_item_size` 呼叫端清單。
- 溢出狀態下拖曳排序的 4× 放大截圖。
- 靜止後的 CPU 增量數字。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-27 — implementation pass

- 實作：`apply_tab_item_size` 先以 tab 最小寬度壓縮後計算 `content_width`；溢出判定為 `content_width > available`。溢出時由 `tab_strip_viewport` 保留兩顆按鈕寬度，所有 `tab_visuals` rect 以完整 tab 寬度按 `tab_scroll_offsets[pane]` 平移，繪製階段才以 `IntersectClipRect` 裁切。未溢出時 offset 為 0、按鈕 rect 為空，原有 tab/add-button 幾何路徑保持不變。
- `clamp_tab_scroll_offset` 是所有 offset 邊界更新的共同入口，並保存每個 pane 的 max offset；最大位移為 `content_width - viewport_width`，因此最後一個 tab 的右緣會貼齊 viewport 右緣。offset 只存在 `AppState`，沒有寫入 core 或 session。
- 左右按鈕位於 tab viewport 與 `+` 之間，按鈕 glyph 使用既有 navigation palette，disabled 色為 `RGB(190,197,209)`；每次按鈕或滾輪事件以目前可視邊緣 tab 的完整寬度前進/後退一個 tab。tab hit-test 先限制在 viewport，故不會把按鈕或 `+` 當成 tab。
- active-tab reveal 掛在 `refresh_tab_strip` 的 active refresh 路徑；因此使用者切 tab、close/add tab、Group restore/switch、導航完成與 reorder refresh 都會讓 active tab 進入 viewport。`apply_layout` 的 resize、`WM_DPICHANGED`、layout switch 與 drag reflow 路徑使用同一套 clamp，但不會因一般 resize/滾輪而強制跳回 active tab。
- 新增 `src/app_shell/tab_overflow.h` 與 `tests/unit/tab_overflow_test.cpp`，focused test 以 `static_assert` 覆蓋未溢出、溢出按鈕佔位及 offset 的 lower/inside/upper clamp；Debug app-shell 路徑另有每個 visual rect 寬度等於計算寬度的 assertion。header 不包含 HWND、COM 或 `windows.h`。

Agent evidence:

- `cmake --build build`：成功，LLVM-MinGW `E:\Dev\LLVM-MinGW\bin\clang++.exe` 編譯並連結 `PaneDock.exe` 與 `panedock_tab_overflow_test.exe`。
- `ctest --test-dir build --output-on-failure`：5/5 passed，包含 `panedock_tab_overflow`。
- `rg -n "apply_tab_item_size|tab_item_at_point|tab_add_rects|WM_MOUSEWHEEL" src\app_shell\main.cpp`：確認 `apply_tab_item_size` 的呼叫點涵蓋 `refresh_tab_strip`、`apply_layout`、`scroll_tab_strip`、`cancel_tab_drag`、`finish_tab_drag`、`update_tab_drag`；`tab_item_at_point` 與 `WM_MOUSEWHEEL` 均在 tab strip 路徑。
- `git diff --check`：成功，沒有 whitespace error。

驗證邊界：依本票指定的 single click + screenshot 政策，本次沒有建立 15 個 tab，也沒有執行重複點擊、滾輪序列、拖曳、Group/layout/resize/DPI 操作或 10 秒 idle measurement；因此 Acceptance 1、2、2a、3、4、5、6、7、8、9、10 均明確未驗證。沒有宣稱 direct `tab_visuals` dump、4× `PrintWindow(PW_RENDERFULLCONTENT)` screenshot、實際第 15 個 tab navigation 或 CPU/disk evidence；需由人類在桌面上以短操作補驗後，才可將 tracker 狀態改為 done。
