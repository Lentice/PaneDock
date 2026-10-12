# PD-221 — Active pane 改用標題區淡藍底、實心 tab 與 inactive 淡化

Phase 7 · UI polish · Depends on: PD-063, PD-076, PD-188

## 來源與約束

2026-10-12 使用者附截圖回報：「active的pane UI效果並沒有很好」。現況只有 tab strip 頂端一條 3px 藍線（PD-063 的決策），與分隔線、選取色太接近，一眼分不出哪個 pane 是 active。使用者看過兩輪 canvas demo 後指出「之前外框會有線條不均勻以及轉角鋸齒狀的問題，似乎難以解決，也許我們該避開整個外框顏色」，並在無外框的八個候選中選定 H：標題區淡藍底 + active pane 的目前 tab 實心 + inactive pane 文字淡化。

- `docs/design-spec.md` §4.5：「恰有一個 pane 為 active,以視覺方式明確指示。」
- `AGENTS.md`：「App UI text must be English.」本票不新增文字。
- `AGENTS.md`：「Event-driven idle path only. No busy loops, no polling timers.」顏色只在 active pane 改變時重繪。
- `AGENTS.md`：「Prefer the smallest working change.」「New non-trivial logic needs one focused runnable test or self-check.」
- `AGENTS.md`：「A `pane_index` parameter is a signal that the function belongs to that pane.」active 與否由 coordinator 決定，畫法屬於 `Pane` 自己。

## Overrides

- **覆寫 PD-063 的指示方式**：PD-063 以 tab strip 頂端 3px 實心條取代圓角外框。本票移除該條（`kActivePaneIndicatorHeight`），改用下述填色組合。PD-063 否決「圓角描邊外框」的理由不變，且本票延續它：所有線索都是填色或文字色，**不畫任何描邊**。新證據是使用者對頂線辨識度的直接回報。
- **覆寫 PD-076 的 tab active 色**：PD-076 讓 active tab 套用側邊欄的淡藍底與藍字。本票改為只有 active pane 的目前 tab 是實心 accent 白字；PD-076 的淡藍改當 active pane 整條標題區的底色，與側邊欄 active Group 列仍是同一個藍。

## 範圍

讀並追：`src/app_shell/pane_tab_strip.cpp` 的 `paint_contents`，`src/app_shell/pane.cpp` 的 `paint_background`、`draw_navigation_icon_button`、`draw_status_bar`、`color_address_bar`、`set_paint_geometry`，`src/app_shell/main.cpp` 的 `apply_layout`、`set_active_pane`、`tab_strip_paint_state`，以及 `tests/release/pane_paint_ownership_check.ps1`（它以字面比對網址列的顏色接線）。

1. 新增 `src/app_shell/pane_chrome_palette.h`：純函式 `pane_chrome_colors(bool pane_active)` 與 `pane_tab_colors(bool pane_active, bool tab_active, bool hovered)`，所有 active 指示色集中於此。
2. `Pane::set_chrome_active(bool)`：coordinator 在 `apply_layout`（任何 Group 切換或版型改變都會經過）與 `set_active_pane` 呼叫；旗標改變時只 invalidate pane 與其 chrome 子視窗，不同步重繪。
3. Active pane：tab strip 與導覽列底色 `RGB(234, 241, 255)`；卡片在這兩段的 outset 也填同色並以圓角填色收邊，避免出現白色細框；目前 tab 填 `RGB(37, 99, 235)` 白字，其他 tab 為白底淡藍細邊。狀態列底色不隨 active 改變（使用者要求），只有文字顏色隨 inactive 淡化。
4. Inactive pane：底色維持原樣；tab、網址列、導覽圖示、狀態列文字轉灰；目前 tab 為中性灰底。
5. Shell view（`IExplorerBrowser`）本身不染色、不覆蓋。

非目標：深色模式、高對比主題、Shell view 的淡化或覆蓋層、任何外框或描邊、側邊欄樣式。

## 驗收與 Agent checks

1. 啟動後 active pane 的標題區為淡藍、狀態列底色與其他 pane 相同，目前 tab 為實心藍；其他 pane 文字為灰色；放大檢查卡片轉角沒有描邊、白邊或鋸齒線。
2. 點擊其他 pane 或用 Tab 鍵切換後，舊 pane 恢復白底灰字、新 pane 變為淡藍，不需 hover 才刷新。
3. `pane_chrome_palette` 單元測試涵蓋：只有 active pane 的目前 tab 是 accent、tab 與其 strip 底色可區分、hover 在兩種 pane 都可見。

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

## 交接區

- 未驗證：本票只用 `PrintWindow` 截過一次啟動畫面（96 DPI 外接螢幕）；點擊切換 active pane 後的重繪與 150%／200% DPI 下卡片圓角收邊尚待使用者實機確認。
- 已知不屬本票：同一張截圖中右上 pane 狀態列顯示 `0 items`，但清單實際有 8 個項目；那是啟動時項目計數的既有行為，與配色無關。
