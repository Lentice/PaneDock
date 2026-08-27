# PD-082 — Tab「+」新增按鈕字符往上微調 1px

Phase 7 · app_shell · Depends on: PD-081

- Source: 使用者實機比對後直接提出(2026-08-27)。
- Origin: 使用者原文:「pane add 往上移 1px」。
- Priority: LOW——純像素級微調,PD-081 剛把「+」改成字型繪製後,使用者實機比對覺得字符垂直位置需要再往上 1px。

## 範圍界定(這不是新的根因調查,是延續 PD-081 的微調)

PD-081 已把「+」改成 `DrawTextW` 畫 `U+002B`,置中於 `state.tab_add_rects[pane_index]`(整個新增按鈕的可點擊矩形,`{available, 0, client.right, client.bottom}`,見 `apply_tab_item_size` 第 1171 行)。**這個矩形的 top 已經是 0(tab strip 自身的頂端),不能再往上,也不應該去動這個矩形本身**——它同時是點擊熱區,縮動它會改變可點擊範圍,不是使用者要的。

**本票只調整字符的「繪製位置」,不動 `state.tab_add_rects` 這個矩形。** 做法是在 `paint_tab_strip` 內、呼叫 `DrawTextW` 之前,把傳給 `DrawTextW` 的那份 rect 複本(目前程式碼是 `plus_rect = add`,`add` 即 `state.tab_add_rects[pane_index]` 的複本)往上平移 1px(top 與 bottom 同時減去 `scaled_value(window, 1)`),`DrawTextW` 的 `DT_VCENTER` 會依這個平移後的 rect 重新置中,達到「符號往上移 1px、點擊熱區不變」的效果。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

——偏移量必須用 `scaled_value` 依 DPI 縮放,不可寫死 1 個裝置像素。

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

| 檔案 | 讀什麼 |
|---|---|
| `src/app_shell/main.cpp` `paint_tab_strip` 內,`RECT add = state.tab_add_rects[pane_index];` 之後到 `DrawTextW(dc, L"+", ...)` 之前的區塊(PD-081 新增) | **本票唯一要改的地方。** |
| `docs/tickets/PD-081-tab-add-button-glyph-notch.md` 交接區 | PD-081 最終採用的字型/字級(`kTabPlusFontSize = 18`、`FW_BOLD`、衍生自 `state.chrome_font`),確認本票不動字型本身,只動繪製 rect 的位置。 |

## 非目標

- 不改 `state.tab_add_rects` 本身(點擊熱區)。
- 不改字型、字級、字重、顏色(PD-081 範圍)。
- 不改 hover 底色。
- 不改捲動按鈕(PD-080 範圍)。

## 驗收條件

1. `DrawTextW` 傳入的 rect 相對 `state.tab_add_rects[pane_index]` 整體上移 `scaled_value(window, 1)`,`state.tab_add_rects[pane_index]` 本身數值不變。
2. 點擊「+」新增分頁功能沒有回歸——熱區未縮小,仍可正常點擊新增 tab。
3. 96 DPI 與一個高 DPI 設定(例如 150%)下位移量正確依比例縮放(150% 下約 1.5px,四捨五入或 `MulDiv` 既有慣例)。
4. `cmake --build build` 與既有 CTest 全數通過。
5. `git diff --check` 無尾隨空白。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

**驗證要快、要最小化。** 這是 1px 級的微調,不需要截圖比對(肉眼在 100% DPI 下很難精確驗證 1px 差異),用程式碼邏輯正確性(rect 平移量、`state.tab_add_rects` 未被誤改)加上 build/CTest 通過即可視為完成,不需要啟動程式或用 computer-use 截圖。

## Handoff requirements

- 實際採用的偏移實作方式(哪個 rect 變數、平移了 top/bottom 還是額外用 `OffsetRect`)。
- 確認 `state.tab_add_rects[pane_index]` 未被修改的證據(`rg` 或 diff 片段皆可)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-27 實作交接

- `paint_tab_strip` 保留 `RECT plus_rect = add`，並在 `DrawTextW` 前呼叫 `OffsetRect(&plus_rect, 0, -scaled_value(window, 1))`；這會讓 rect 的 `top`/`bottom` 同時依 DPI 向上移動，只影響「+」字符繪製位置。
- `state.tab_add_rects[pane_index]` 未被修改：布局寫入仍是 `{available, 0, client.right, client.bottom}`，點擊與 hover 仍直接使用原本的 `PtInRect` hit-test rect；`rg` 與 `git diff` 均確認沒有改動該 state rect 路徑。
- Agent checks：`cmake --build build` 通過；`ctest --test-dir build --output-on-failure` 通過，5/5 PASS；`git diff --check` 通過。未啟動 app、未截圖，符合本票 1px 微調的驗證政策。
