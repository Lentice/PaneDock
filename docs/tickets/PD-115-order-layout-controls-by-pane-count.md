# PD-115 — 版型按鈕依 pane 數量由 1 到 4 排列

Phase 7 · app_shell · Depends on: PD-046, PD-114

- Source: 使用者需求（2026-08-29）。
- Origin: 使用者原文：「and reorder the pane layout. 1 pane to 4 panes」。
- Priority: MEDIUM——改善固定版型的可預測性與掃描順序。

## Outcome

將 header 的八個版型按鈕按 pane 數量遞增排列；相同 pane 數量內以軸向成對排列：

1. `single`
2. `left_right`
3. `top_bottom`
4. `three_pane`（1 left / 2 right）
5. PD-114 的 2 left / 1 right
6. PD-114 的 1 up / 2 bottom
7. `two_over_one`（2 up / 1 down）
8. `four_pane_grid`

這是唯一權威 UI 次序。左右方向成對後再放上下方向成對，讓鏡像版型相鄰；最外層仍嚴格維持 1 pane → 2 panes → 3 panes → 4 panes。

## 必讀與 caller trace

- `docs/design-spec.md` §4.3 與 FR-003
- `docs/development.md` 的 UI language、Change workflow
- `docs/tickets/PD-046-layout-buttons-segmented-control.md`
- `docs/tickets/PD-099-add-two-over-one-layout-template.md`
- `docs/tickets/PD-114-add-missing-three-pane-layout-orientations.md`
- `src/app_shell/main.cpp` 的 `kLayoutButtonIds`、`kLayoutButtonLabels`、`kLayoutTemplates`、`draw_layout_glyph`，以及所有以 `kLayoutButtonIdBase` 或 array index 查找、繪製、hover、click、active state 的 caller

## 實作範圍

- 讓 `kLayoutButtonLabels` 與 `kLayoutTemplates` 精確遵守 Outcome 次序。
- `kLayoutButtonIds` 可維持連續 ID；按鈕 identity 不持久化，因此不得新增 migration。
- `draw_layout_glyph` 目前以 array index 選 glyph，必須同步調整每個 index 的圖形，使 tooltip、click target、active highlight 與 glyph 指向同一個 template。
- 若 PD-114 已直接按最終次序加入 array，本票只需驗證並刪除任何剩餘的錯序；不要為「有一張 ticket」製造無效 churn。

## Non-goals

- 改動 `LayoutTemplate` enum 宣告順序或 session persisted identity。
- 改變 layout geometry、splitter 行為或 pane index 順序。
- 新增分類標題、分隔符、第二列按鈕、scroll/overflow UI。
- 重構三個平行 array、改成動態容器或建立 layout registry。
- 改變按鈕尺寸、色彩、hover、active highlight 或 glyph 視覺風格。

## 綁定限制（原文引用）

`docs/development.md`：
> All shipped strings are English.

> Make the smallest change that satisfies the acceptance criteria. Reuse before adding.

`AGENTS.md`：
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> App UI text must be English. No Chinese strings ship in the binary.

## Acceptance criteria

- 畫面由左至右嚴格呈現 Outcome 所列八個按鈕。
- 每個 glyph、English tooltip、click 行為與 active highlight 都對應同一版型。
- 重排不改任何 persisted layout identity；既有 session reload 後仍選中同一幾何版型。
- 不新增 abstraction、設定值或 runtime dependency。
- 窄視窗與目前支援的 DPI 下，八個按鈕不重疊；若 header 寬度不足，沿用既有按鈕寬度壓縮機制，不另建 overflow UI。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "kLayoutButtonIds|kLayoutButtonLabels|kLayoutTemplates|draw_layout_glyph|kLayoutButtonIdBase" src/app_shell/main.cpp
git diff --check
```

實機單步檢查：從左到右逐一 hover 並點選八個按鈕，截圖確認 tooltip、glyph、active highlight 與 pane 數量/形狀。這張票沒有新增非 UI 邏輯，因此不另建測試 framework；既有 core tests 負責確認重排沒有改動 layout identity 或 geometry。

## 交接區

完成時記錄：最終按鈕次序、修改檔案、Agent checks 結果與實機截圖驗證狀態。

### 實作交接（2026-08-29）

- 最終按鈕次序為：`Single`、`Left / Right`、`Top / Bottom`、`Three`、`Two beside One`、`One over Two`、`Two over One`、`Four`。這對應 1 pane → 2 panes → 3 panes（左右鏡像對、上下鏡像對）→ 4 panes。
- `kLayoutButtonIds` 維持連續的 400–407；`kLayoutButtonLabels`、`kLayoutTemplates`、八項 English tooltip 與 `draw_layout_glyph` 的 index 已同步。修正了原本八個按鈕索引六項 tooltip array 的越界風險。
- 修改檔案：`src/app_shell/main.cpp`；tracker 的 PD-115 列狀態更新於 `docs/tickets.md`。未修改 `LayoutTemplate` enum、幾何計算、session identity 或 schema。
- Agent checks：`cmake --build build` PASS；`ctest --test-dir build --output-on-failure` PASS（5/5）；ticket 指定的 `rg` caller trace PASS；`git diff --check` PASS。未新增測試，因本票只重排既有 UI 對應，沒有新的 core 邏輯。
- 依使用者指示，本回合未啟動實機視窗，未使用滑鼠、Computer Use、`SetCursorPos`、`mouse_event` 或互動後截圖；因此沒有宣稱實機 UI 驗收通過。

#### 需要使用者手動驗證

1. 啟動 Release build，從左至右逐一 hover 八個 layout buttons，確認 tooltip 依序為 Single pane、Two panes side by side、Two panes stacked、Three panes、Two panes on the left, one on the right、One pane over two panes、Two panes over one pane、Four panes。
2. 逐一點選八個 buttons，確認 glyph、active highlight、pane 數量與幾何方向一致；特別確認第 5–7 顆分別為 2 left / 1 right、1 up / 2 bottom、2 up / 1 down。
3. 在窄視窗與目前支援的 DPI（含混合 DPI 螢幕）檢查八個 buttons 不重疊、不裁切，且 click target 沒有錯位。
4. 以既有 session 含不同 layout 的狀態關閉並重新啟動，確認 persisted layout identity 與實際幾何不因按鈕顯示順序改變。
