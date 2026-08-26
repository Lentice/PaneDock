# PD-058 — 版型按鈕、tab、tab 的「+」、Group 列全部缺少滑鼠 hover 視覺回饋

Phase 7 · app_shell · sidebar · Depends on: PD-055, PD-046

- Source: 使用者實機操作後回報(2026-08-26)。
- Origin: 使用者原文第 3 項「pane layout and pane tabs 在 mouse onhover 時沒有特效」、第 12 項「pane tab 的 add 按鈕……要有 onhover 特效」、第 13 項「groups 也要有 onhover 特效」。
- Priority: MEDIUM——不影響功能,但缺少 hover 回饋讓整個介面感覺不可互動,是使用者一次列出三個相關項目的原因。

## 已確認的根因(有程式碼證據,不是猜測)

**目前整個應用程式沒有任何一處實作滑鼠 hover 狀態。** 四個受影響的控制項各自的現況:

1. **版型按鈕(`draw_layout_button`)**——owner-draw 按鈕的 `DRAWITEMSTRUCT::itemState` 有 `ODS_HOTLIGHT` 位元可用,但 `draw_layout_button` 只判斷 `disabled`(`ODS_DISABLED`)與外部傳入的 `checked`,完全沒有讀 `ODS_HOTLIGHT`。
2. **Tab 條的 tab(`paint_tab_strip`,第 2350-2385 行)**——自繪路徑,顏色只有 active(`RGB(226,232,240)`)與 inactive(`RGB(244,246,248)`)兩種,沒有 hover 分支,也沒有追蹤滑鼠位置。
3. **Tab 條的「+」按鈕(`paint_tab_strip` 第 2379-2384 行)**——只用 `DrawTextW` 畫一個「+」字元,沒有背景、沒有 hover。
4. **Group 側邊欄列(`Sidebar::draw_item`,`src/sidebar/sidebar.cpp` 第 100-197 行)**——owner-draw `LISTBOX`,只判斷 `ODS_SELECTED`,沒有讀 `ODS_HOTLIGHT`(而且 `LISTBOX` 預設不送 hot-tracking,見下方決策 3)。

## 已確認的產品決策

1. **hover 的視覺語言統一為「比常態稍深一階的背景色」,不是外框、不是陰影、不是動畫。** 與目標畫面(`docs/panedock-ui-demo-01-refined-quiet-header.html`)的 `:hover` 規則一致,也符合 Windows 原生慣例。具體色值由實作 agent 微調,但必須滿足:hover 態明顯可辨、且不會與 active/selected 態混淆(active 是藍色系,hover 是灰色系)。
2. **版型按鈕直接讀 `DRAWITEMSTRUCT::itemState & ODS_HOTLIGHT`,不需要自己追蹤滑鼠。** owner-draw 按鈕由系統提供這個狀態位元,這是最小改動。實作 agent 需先實機確認 `ODS_HOTLIGHT` 在 `BS_OWNERDRAW | BS_AUTORADIOBUTTON` 上真的會送達——**注意 PD-047 已經證實 `BM_GETCHECK` 在同一種按鈕上不可靠,所以不要假設 `ODS_HOTLIGHT` 一定可用。若實測不可用,改用下方決策 4 的 `TrackMouseEvent` 做法,並在交接區記錄實測結果。**
3. **Tab 條與 Group 清單必須自己追蹤滑鼠位置,不能依賴 `ODS_HOTLIGHT`。** tab 條是完全自繪的 `STATIC`(PD-049);`LISTBOX` 預設不做 hot-tracking。兩者都需要決策 4 的做法。
4. **自行追蹤 hover 的標準做法(兩者共用同一套模式):** 在該控制項的訊息處理中攔 `WM_MOUSEMOVE`,算出目前滑鼠底下是哪一個項目(tab 條用既有的 `tab_item_at_point` 與 `tab_add_rects`;Group 清單用既有的 `LB_ITEMFROMPOINT`,已封裝在 `group_item_at_point`),把結果存進狀態(tab 條存在 `AppState`,Group 清單存在 `Sidebar` 的成員),**只有在 hover 項目真的改變時才 `InvalidateRect`**,並呼叫 `TrackMouseEvent(TME_LEAVE)` 以便在 `WM_MOUSELEAVE` 時清除 hover 狀態。「只在改變時才 invalidate」是硬性要求,否則每次滑鼠移動都重繪整條 tab 條會違反 `AGENTS.md` 的閒置 CPU 規則。
5. **「+」按鈕在本票只加 hover 背景。** 它的大小與線條粗細屬於 PD-062,兩票會改到同一個 `paint_tab_strip` 函式,實作順序由 dispatcher 決定,後做的那票要先 rebase。
6. **Group 清單的 hover 背景必須畫成與 selected 態同樣的圓角矩形**(`Sidebar::draw_item` 第 113-131 行已有 `pill` 矩形與 `RoundRect` 的既有程式碼可直接重用),不能畫成方角,否則兩種狀態的形狀不一致。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> **Keep `src/core` free of HWND, COM and `windows.h`.**

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes.

## Files to read and trace first

- `src/app_shell/main.cpp` `draw_layout_button` 定義——加入 `ODS_HOTLIGHT` 分支處。
- `src/app_shell/main.cpp` 第 2350-2385 行(`paint_tab_strip`)——tab 與「+」的繪製。
- `src/app_shell/main.cpp` 第 2387-2438 行(`tab_strip_proc`)——加入 `WM_MOUSEMOVE`/`WM_MOUSELEAVE` 的落腳處。**注意既有的 `WM_MOUSEMOVE` 分支已經被 `update_tab_drag` 佔用(第 2422-2425 行),hover 追蹤要與拖曳邏輯共存,不能互相覆蓋。**
- `src/app_shell/main.cpp` 第 2253-2264 行(`tab_item_at_point`)、`state.tab_add_rects`——hover hit-test 可直接重用。
- `src/sidebar/sidebar.cpp` 第 100-197 行(`Sidebar::draw_item`)、`src/sidebar/sidebar.h`——Group 列的繪製與狀態成員落腳處。
- `src/app_shell/main.cpp` 第 2440-2450 行(`group_item_at_point`)——Group 的 hit-test。
- `docs/panedock-ui-demo-01-refined-quiet-header.html`——目標畫面的 `:hover` 配色參考。

## Scope

1. 版型按鈕的 hover 背景(`ODS_HOTLIGHT` 或自行追蹤)。
2. Tab 條的 tab hover 背景(自行追蹤 + `TrackMouseEvent`)。
3. Tab 條的「+」按鈕 hover 背景(與 2 共用同一套追蹤)。
4. Group 側邊欄列的 hover 圓角背景(自行追蹤 + `TrackMouseEvent`)。

## Non-goals

- 不加動畫/漸變(GDI 沒有內建動畫,加上去要靠 timer,直接違反閒置規則)。
- 不改任何控制項的 active/selected 態顏色。
- 不改「+」按鈕的大小或線條粗細(PD-062)。
- 不改 Group 列的字體大小(PD-061)。
- 不對導覽列的 back/forward/up/refresh/view 按鈕加 hover——那五顆是原生 owner-draw 按鈕,若 `ODS_HOTLIGHT` 可用可順手加,但不列入驗收。

## Acceptance

1. 滑鼠移到任一顆版型按鈕上,該按鈕背景變深;移開後恢復。已經是 active 的那顆 hover 時不可變回灰色(active 狀態優先)。
2. 滑鼠移到任一個 tab 上,該 tab 背景變深;移開後恢復;移到另一個 tab 時,前一個正確恢復。
3. 滑鼠移到 tab 條的「+」上,出現 hover 背景。
4. 滑鼠移到任一個 Group 列上,出現與 selected 態同形狀的圓角 hover 背景;移開後恢復。
5. 滑鼠移出控制項外(不是移到另一個項目,而是完全離開視窗或移到別的控制項)時,hover 狀態正確清除,不會殘留。
6. **滑鼠在 tab 條或 Group 清單上連續移動時,程式的 CPU 使用率不得持續偏高;滑鼠靜止不動時必須回到 0% CPU、無磁碟 I/O。** 實作 agent 需用工作管理員或 `Get-Counter` 實際觀察並在交接區記錄。
7. 拖曳 tab 排序的行為(PD-050/PD-055)未因新增的 `WM_MOUSEMOVE` 處理而回歸。
8. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
9. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "ODS_HOTLIGHT|TrackMouseEvent|WM_MOUSELEAVE|hover" src\app_shell\main.cpp src\sidebar\sidebar.cpp src\sidebar\sidebar.h
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:滑鼠依序停在版型按鈕、tab、「+」、Group 列上截圖確認 hover 背景;
# 移開確認恢復;連續移動滑鼠時觀察 CPU;靜止時確認回到 0%。
# 本環境已具備 PrintWindow 截圖與 SetCursorPos 游標移動模擬能力,請實際驗證。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 而不是 `Graphics.CopyFromScreen`。用 `SetCursorPos` 把游標移到目標位置後再截圖即可觀察 hover 態。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- `ODS_HOTLIGHT` 在 `BS_OWNERDRAW | BS_AUTORADIOBUTTON` 上實測是否可用(這是本票最重要的未知數)。
- 四個控制項最終採用的 hover 顏色值。
- hover 狀態的儲存位置(`AppState` 的哪個欄位、`Sidebar` 的哪個成員)。
- 「只在 hover 項目改變時才 invalidate」的具體實作方式。
- CPU 觀察的實際數字(滑鼠移動中 / 靜止時)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-26 實作交接

- 已在 `src/app_shell/main.cpp`、`src/sidebar/sidebar.h`、`src/sidebar/sidebar.cpp` 完成四類 hover feedback：版型按鈕、tab、tab 的 `+`、Group 列；未加入動畫、timer、active/selected 顏色或尺寸變更。
- `ODS_HOTLIGHT` 實測結果：在 `BS_OWNERDRAW | BS_AUTORADIOBUTTON` 上不會送達。第一版以像素比對確認非 active 按鈕仍為 `RGB(248,250,252)`，因此依決策 2 改用 layout button subclass 的 `TrackMouseEvent(TME_LEAVE)` fallback；`draw_layout_button` 仍保留 `ODS_HOTLIGHT` 讀取。fallback 後非 active hover 為 `RGB(242,245,248)`，active layout hover 維持 `RGB(37,99,235)`。
- 最終 hover 色值：layout `RGB(242,245,248)`；tab 與 `+` `RGB(236,240,244)`；Group `RGB(242,245,248)`。常態 tab 為 `RGB(244,246,248)`、Group 為 `RGB(251,252,254)`；active/selected 優先，不會被 hover 覆蓋。Group hover 沿用 `Sidebar::draw_item` 既有 `pill`/`RoundRect`。
- 狀態儲存：`AppState::layout_hover_index`；`AppState::tab_hover_indices`（每個 pane 一個 `optional<size_t>`，`pane.tabs.size()` 代表 `+`）；`Sidebar::hover_index_`。tab 與 Group 在各自 subclass 的 `WM_MOUSEMOVE` 呼叫 `TrackMouseEvent(TME_LEAVE)`，`WM_MOUSELEAVE` 清除。
- 只在狀態真的改變時 invalidate：layout button 只在 `layout_hover_index` 改變時 invalidate 自身；tab 比較新的 optional index 後才 invalidate strip；`Sidebar::set_hover_index` 先比較再 invalidate list。`refresh_tab_strip` 與 `Sidebar::set_groups` 會清除可能失效的舊 index。
- 自動驗證：指定 CMake configure、Release build、`ctest --test-dir build --output-on-failure` 全部通過（4/4）；`rg -n "ODS_HOTLIGHT|TrackMouseEvent|WM_MOUSELEAVE|hover" ...` 通過；`git diff --check` 通過。
- 實機驗證（Release，PID 39364，主視窗矩形 `50,50-1450,950`）：使用票據指定的 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`，搭配 `SetCursorPos`/`mouse_event`；所有擷取成功。非 active layout `248,250,252 → 242,245,248`；active layout 維持 `37,99,235`；tab `244,246,248 → 236,240,244`；`+` `255,255,255 → 236,240,244`；Group `251,252,254 → 242,245,248`；移出後 tab/`+`/Group 分別回到 `244,246,248`/`255,255,255`/`251,252,254`。四個 tab strip 均實際移入，拖曳既有 `WM_MOUSEMOVE` 路徑仍共存。
- CPU 觀察：在 tab strip 連續 120 次游標移動（每次間隔 10 ms）取樣，程序 CPU 平均 `0.1173%`；停止互動取樣約 3 秒為 `0%`。未新增 polling loop 或磁碟 I/O 路徑。測試後已用不帶 `/F` 的 `taskkill /PID 39364` 關閉。
