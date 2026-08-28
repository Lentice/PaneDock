# PD-108 — 拖曳分隔線時,矩形沒有變動的 pane 仍被重新 `SetWindowPos`/重繪

Phase 7 · app_shell · Depends on: PD-095, PD-077

- Source: 使用者需求(2026-08-28)。
- Origin: 使用者原文:「resize the panes (drag the splitter) should not re-render all items(e.g. all pane nav buttons). some items do not change the position and size, re-render may not be required.」
- Priority: MEDIUM——延續 PD-095/PD-097(拖曳分隔線效能)這條線的下一層優化,不是崩潰或資料問題,但拖曳期間的實際重繪/訊息量比理論最小值明顯更多。

## 已確認的現況(有程式碼證據,不是猜測)

`apply_layout`(`main.cpp:1949`)逐 pane 迴圈裡,已經有一個現成的「這個 pane 的矩形是否真的變了」旗標,而且已經**算出來但只用在最後一步**:

```cpp
// main.cpp:1989-1995
bool pane_geometry_changed = false;
if (visible) {
    pane_rect = to_win32_rect(rects[index]);
    pane_geometry_changed =
        !state.laid_out_pane_rects[index].has_value() ||
        !EqualRect(&state.laid_out_pane_rects[index].value(), &pane_rect);
```

這個旗標目前**唯一**的用途是在整個迴圈跑完對應 pane 之後,決定要不要對該 pane 矩形強制重繪(PD-077 的既有修法):

```cpp
// main.cpp:2126-2130
state.explorers[index].set_visible(visible);
if (visible && pane_geometry_changed) {
    RedrawWindow(window, &pane_rect, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
```

但在算出這個旗標之後、用到它之前(`main.cpp:2000-2107`),同一個迴圈對**每一個可見 pane**無條件執行下列全部工作,完全沒有檢查 `pane_geometry_changed`:

- `SetWindowPos` 移動/縮放 tab strip(`:2000-2004`)。
- `SetWindowPos` 移動/縮放使用者本次原文點名的**五個導覽按鈕**(back/forward/up/refresh/view mode,`:2011-2028`)。
- `SetWindowPos` 移動/縮放 address bar(`:2037-2042`)。
- `SetWindowPos` 移動/縮放 status bar(`:2051-2055`)。
- `SetWindowPos` 移動/縮放 explorer container,外加 `apply_pane_container_region` 重新 `CreateRoundRectRgn`/`CombineRgn`/`SetWindowRgn`(`:2065-2073`)。
- 已初始化的 Shell view 呼叫 `state.explorers[index].set_rect(local_rect)`(`:2106`)重新定位真正的 `IExplorerBrowser` 視窗。

每一次 `SetWindowPos`(未加 `SWP_NOMOVE`/`SWP_NOSIZE`)都會觸發該子視窗的 `WM_WINDOWPOSCHANGING`/`WM_WINDOWPOSCHANGED`,即使新座標與舊座標完全相同——這是 Windows 已知行為,`SetWindowPos` 不會自行比較新舊矩形是否相等就跳過發送這兩個訊息。以三分割(`three_pane`)或四宮格(`four_pane_grid`)版型為例,兩條分隔線中只拖曳其中一條時,理論上只有緊鄰該分隔線的 pane 矩形會變,其餘 pane 的矩形應該維持不變,但目前程式碼會讓**所有**可見 pane(包含矩形沒變的)都跑一次上述全部工作。

`docs/tickets/PD-095-...md` 的既有交接區已經確認:PD-095/PD-097 這條線處理的是「拖曳期間每個 `WM_MOUSEMOVE` 呼叫幾何重排的**頻率**」與「幾何重排本身該不該包含內容重算(status bar/item counts/tab 文字量測)」,兩者都沒有處理「幾何重排這一次呼叫裡面,矩形沒變的 pane 該不該被跳過」——這是同一個效能問題底下,尚未處理的第三層,不是重工。

## Binding constraints — quoted, do not weaken

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`pane_geometry_changed`已經是既有、已驗證正確(PD-077 用它判斷是否需要強制重繪)的訊號,本票只是把同一個旗標的作用範圍擴大到它理應涵蓋、但目前漏掉的那些 `SetWindowPos` 呼叫,不需要新增計算邏輯或第二套判斷方式。

`docs/tickets/PD-095-...md` 既有決策(本票沿用,不覆寫):
> 拖曳分隔線逐幀只做幾何重排,內容相關的重算……在拖曳期間不重複執行。

本票是在「幾何重排」這一類工作內部再做一層篩選(只對真的變動的 pane 做),不是重新定義「幾何 vs 內容」這個既有分類。

## Files to read and trace first

- `src/app_shell/main.cpp:1949-2134`(`apply_layout`)——本票唯一要修改的函式,尤其是 `:1989-1995`(既有 `pane_geometry_changed` 計算)與 `:2000-2107`(目前無條件執行的每 pane 子視窗重新定位)。
- `src/app_shell/main.cpp:2126-2130`——`pane_geometry_changed` 目前唯一的既有用法(PD-077 的強制重繪),本票新增的判斷式要與其邏輯一致、不衝突。
- `src/app_shell/main.cpp:459`(`laid_out_pane_rects` 欄位定義)、`:2109`(`pane_geometry_changed` 為真時如何寫回)、`:2111`(pane 變不可見時如何重置)——確認旗標的既有生命週期,新增的跳過邏輯不能破壞這個既有狀態機。
- `src/explorer_host/explorer_host.h`/`.cpp` 的 `set_rect`——確認在矩形未變時完全不呼叫它,是否會遺漏任何 `set_rect` 內部才做的副作用(目前程式碼看起來只是單純重新定位,若有例外需在交接區記錄)。
- `docs/tickets/PD-095-splitter-drag-full-relayout-and-item-count-rescan-per-mousemove.md`——「幾何 vs 內容」既有分類的完整決策與交接區,確認本票不重疊、不覆寫。
- `docs/tickets/PD-097-splitter-drag-geometry-throttling.md`——目前進行中的節流票,確認本票改動範圍(`apply_layout` 內部的每 pane 迴圈)與其改動範圍(呼叫 `update_splitter_drag` 的頻率/timer)不衝突,兩票可獨立驗證。

## 已確認的產品決策

1. **判斷基準沿用既有的 `pane_geometry_changed`,不新增第二套比對邏輯。** 一個 pane 從不可見變可見(`state.laid_out_pane_rects[index]` 原本是 `std::nullopt`)時,既有定義已經視為「有變動」,自然會做完整的一次定位,不需要額外處理「首次顯示」這個特例。
2. **被跳過的具體呼叫範圍是:tab strip、五個導覽按鈕、address bar、status bar、explorer container(含 `apply_pane_container_region` 的 region 重算)的 `SetWindowPos`,以及已初始化 Shell view 的 `set_rect`。** 這些都是純粹的「移動/縮放到同一個位置」操作,矩形沒變時重跑完全是浪費;`ShowWindow(SW_SHOW)` 呼叫則維持原樣不特別跳過——同一個視窗連續呼叫 `ShowWindow(SW_SHOW)` 是無害的 no-op(不會觸發重繪或訊息),移除它沒有效能意義,反而增加程式碼複雜度,不符合「smallest working change」。
3. **未初始化(尚未 `Initialize` 的)Shell view 分支(`main.cpp:2075-2104`)不受影響,維持原樣。** 該分支本來就是「這個 pane 第一次要顯示」的情境,`pane_geometry_changed` 在這種情況下必然為真(因為 `laid_out_pane_rects[index]` 原本沒有值),不會被本票的跳過邏輯誤判為「不需要處理」。
4. **`refresh_status_bar`(`:2108`)與 `apply_tab_item_size`(`:2005`)已經各自被 `recompute_content` 這個獨立旗標(PD-095)控制,本票不改動這兩者的既有 gate 條件**,只處理它們各自所在區塊裡「跟幾何相關、目前沒被任何旗標控制」的 `SetWindowPos`/`SetWindowRgn`/`set_rect` 呼叫。
5. **拖曳結束(`WM_LBUTTONUP`)的最終一次完整 `apply_layout` 呼叫不受影響**——那次呼叫時,受影響的 pane 矩形本來就會被判定為「有變動」(拖曳期間可能因節流而跳過幾次幾何重排,最終位置與上一次記錄的 `laid_out_pane_rects` 通常不同),自然會正確執行完整定位;真正矩形完全沒變的 pane(例如三分割中沒有跟這條分隔線相鄰的第三個 pane)本來就不該被重新定位,跳過它們不影響任何驗收行為。

## Scope

1. 在 `apply_layout` 的每 pane 迴圈中,把 `main.cpp:2000-2004`(tab strip)、`:2011-2028`(五個導覽按鈕)、`:2037-2042`(address bar)、`:2051-2055`(status bar)、`:2065-2073`(explorer container + region)的 `SetWindowPos` 呼叫,以及 `:2106` 的 `set_rect` 呼叫,改為只在 `pane_geometry_changed` 為真時執行。
2. 未初始化 Shell view 的 `initialize` 分支(`:2075-2104`)維持不受本票影響——它已經隱含在「`pane_geometry_changed` 必為真」的情境內,不需要額外判斷式包住。
3. `ShowWindow(SW_SHOW)` 呼叫維持現狀,不跳過。

## Non-goals

- 不改變 PD-095/PD-097 定義的「幾何 vs 內容」分類本身,也不改變 `recompute_content`/節流 timer 的既有邏輯——本票只在「幾何重排」這一類工作內部,對矩形沒變的 pane 額外跳過。
- 不改變拖曳結束、視窗縮放、版型切換、Group 切換等其他觸發 `apply_layout` 的既有呼叫語意——這些呼叫本身該做的完整或部分重算範圍不變,只是各自呼叫內、矩形沒變的個別 pane 現在會被跳過。
- 不新增快取層或事件系統,直接重用既有的 `state.laid_out_pane_rects`/`pane_geometry_changed`。
- 不處理側邊欄(`layout_sidebar`)或頂部工具列(`layout_header`)的重繪——那些已經由 `recompute_content` 這個不同的旗標控制,不在使用者本次回報範圍(pane nav buttons)內。

## Acceptance Criteria

1. 在 `three_pane` 或 `four_pane_grid` 版型下拖曳其中一條分隔線,矩形實際沒有變動的 pane,其 tab strip/導覽按鈕/address bar/status bar/explorer container 都不會再收到新的 `SetWindowPos` 呼叫(可用暫時的計數/診斷輸出驗證呼叫次數,驗證完畢後移除,比照 PD-097 的既有暫時 instrumentation 慣例)。
2. 矩形真的有變動的 pane,拖曳跟手效果與修改前一致,不因為新增的判斷式而漏掉任何必要的重新定位。
3. 拖曳結束、視窗縮放、版型切換、Group 切換後,所有 pane 的最終幾何、tab strip、導覽按鈕、address bar、status bar、Shell view 位置都與修改前完全一致,沒有視覺 regression(可用 `PrintWindow` 截圖比對 diff)。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "pane_geometry_changed|laid_out_pane_rects|SetWindowPos" src\app_shell\main.cpp
git diff --check
```

**驗證原則(本專案共同約定):單次點擊/單一操作 + 截圖由 Agent 或本人執行;實際拖曳分隔線的跟手流暢度與大量 pane 情境下的效果,留給使用者實機驗證**,不要用 computer-use 工具連續拖曳搶占使用者的滑鼠鍵盤。若某項驗收條件無法用單次動作完成,如實在交接區標記未驗證並說明需要使用者手動驗證的具體步驟。**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉。**

## Handoff requirements

- 實際跳過 `SetWindowPos` 的判斷式插入位置與具體改動範圍。
- 驗證矩形未變 pane 確實被跳過的方法與結果(暫時計數/診斷輸出,完成後是否已移除)。
- 拖曳結束/縮放/切換版型/切換 Group 後最終狀態與修改前一致的驗證結果。
- 未驗證項目與原因(若有,例如實際拖曳跟手感受留給使用者)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
