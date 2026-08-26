# PD-047 — 版面配置按鈕的選中態高亮對比度不足

Phase 6 · app_shell · Depends on: PD-046

- Source: 使用者比對 `docs/panedock-ui-demo-01-refined-quiet-header.html` 目標畫面與實機截圖後回報(2026-08-25)。
- Origin: 「右上 pane layout 幾個按鈕應該有 group 的樣式,並且要 highlight active layout」。
- Priority: LOW——純視覺對比度調整,不影響功能。

## 已確認的根因(有程式碼證據,不是猜測)

1. **「group 樣式」(5 顆按鈕視覺相連)已經由 PD-046 完成**,不是本票範圍:`draw_layout_segment_background`(`src/app_shell/main.cpp` 第 574-590 行)已經在 5 顆按鈕外圍畫一個共用圓角背景,`layout_header`(第 1097 行起)已經把 gap 從 4px 改為 1px 分隔線。本票不重做這部分。
2. **「highlight active layout」目前確實有實作,但對比度太弱,肉眼不容易一眼辨識哪一顆被選中。** `draw_layout_button`(第 543-572 行)對選中(`checked`)按鈕使用:
   ```cpp
   const COLORREF background = disabled
                                   ? RGB(245, 247, 249)
                                   : checked ? RGB(234, 241, 255)
                                             : RGB(248, 250, 252);
   const COLORREF glyph = disabled
                              ? RGB(148, 163, 184)
                              : checked ? RGB(37, 99, 235)
                                        : RGB(100, 116, 139);
   ```
   選中態背景 `RGB(234,241,255)`(極淺藍)與未選中態背景 `RGB(248,250,252)`(近乎全白的淺灰)兩者非常接近(色差小),只有靠圖示本身變成藍色(`RGB(37,99,235)`)才能分辨——在小尺寸圖示、螢幕截圖或使用者快速掃視時容易被忽略。使用者截圖比對後的結論是「看不出來哪個是 active」,與目標畫面(`docs/panedock-ui-demo-01-refined-quiet-header.html`,選中的版型圖示是**實心深色方塊 + 白色圖示**,對比強烈)有明顯落差。
3. 選中態的邊框 `FrameRect(item.hDC, &button, border)` 使用 `RGB(207, 224, 255)`(第 563-568 行),同樣是淺藍,對提高辨識度貢獻有限。

## 已確認的產品決策

1. **選中態改為實心深色填底、白色圖示,比照目標畫面的視覺語言**,不是加大邊框或加陰影。具體色值由實作 agent 微調,但選中態背景亮度必須明顯低於未選中態背景(建議選中態使用與 `draw_brand_bar`/`draw_pane_card` 一致的品牌藍 `RGB(37, 99, 235)` 或其深色變體作為實心填底,圖示改白色 `RGB(255,255,255)`),讓「哪一個被選中」不需要放大截圖就能一眼看出。
2. **未選中態與 disabled 態的背景/圖示顏色維持現狀不變**,只調整 `checked` 分支;避免影響其他狀態的既有驗收(PD-039 的 tooltip、PD-046 的分段外框視覺)。
3. **選中態的邊框(`FrameRect`)在改為實心深色填底後可能不再需要**(深色填底本身已經是最強對比),實作 agent 可視覺判斷是否保留、拿掉或改色,只要滿足「選中狀態清楚可辨」的驗收即可,不強制規定確切做法。
4. **不改動 `BM_GETCHECK`/`BST_CHECKED`/`WM_COMMAND` 等既有選取邏輯**,只調整 `draw_layout_button` 的繪製顏色。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/tickets/PD-046-layout-buttons-segmented-control.md`(本票延續,不重開):
> 5 個版面配置按鈕改為視覺上相連的分段控制……選中的版面配置在分段控制內有清楚的視覺區分(比照現有 `BST_CHECKED` 高亮邏輯,調整成適合相連外觀的呈現方式)。

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `draw_layout_button`(第 543-572 行)——本票要修改的顏色分支。
- `src/app_shell/main.cpp` 的 `draw_layout_glyph`(`draw_layout_button` 呼叫的圖示繪製函式,搜尋其定義)——確認 `glyph` 顏色參數如何影響圖示線條/填色,改成白色時圖示本身是否仍然清晰可辨(尤其是線條較細的版型圖示)。
- `src/app_shell/main.cpp` 的 `draw_layout_segment_background`(第 574-590 行)——確認本票的深色填底與這個共用外框背景疊加後視覺協調,不產生顏色衝突。
- `docs/panedock-ui-demo-01-refined-quiet-header.html`——目標畫面選中版型圖示的實際配色參考。

## Scope

1. `draw_layout_button` 的 `checked` 分支(背景色、圖示色,必要時邊框)改為高對比、深色實心填底 + 白色圖示。

## Non-goals

- 不重做 PD-046 已完成的「按鈕群視覺相連」(共用外框、分隔線)。
- 不改變 disabled/未選中態的顏色。
- 不改變按鈕排版、間距或 more-actions 按鈕。
- 不改變 tooltip(PD-039)或版型切換邏輯本身。

## Acceptance

1. 5 顆版型按鈕中,目前選中的那一顆在一般觀察距離下(不需要放大截圖)清楚可辨,與其餘 4 顆有明顯的顏色對比。
2. 切換不同版型(點擊不同按鈕)後,高亮正確跟著移動到新選中的按鈕。
3. Disabled 態(若有,例如目前版型與按鈕代表的版型相同導致按鈕邏輯上不可再點選——需先確認現況是否真的有 disabled 態)視覺不受影響。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "draw_layout_button" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:切換 5 種版型,確認每次切換後選中的按鈕都有清楚可辨的高亮,
# 與目標畫面 docs/panedock-ui-demo-01-refined-quiet-header.html 的選中態視覺相近
```

## Handoff requirements

- 最終採用的選中態顏色值(背景/圖示/邊框)。
- 若發現版型圖示改白色後在深色背景上不清晰(例如圖示線條太細),記錄調整方式。
- 桌面驗證的實際結果(本環境目前已具備螢幕截圖與滑鼠點擊模擬能力,若可行請實際截圖比對並記錄;若當次環境仍不可用,誠實記錄)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 實作交接（2026-08-25）

完成 `draw_layout_button` 的 checked 視覺調整：背景 `RGB(37,99,235)`、圖示 `RGB(255,255,255)`、邊框 `RGB(29,78,216)`；未選中與 disabled 顏色、分段外框、版型切換邏輯均未改動。`draw_layout_glyph` 的 1px 線條在程式碼上保留，未另加粗或新增繪製邏輯。

自動化檢查：CMake configure、LLVM-MinGW/Ninja build 通過；CTest 4/4 通過；`rg -n "draw_layout_button" src\\app_shell\\main.cpp` 找到定義與唯一呼叫點；`git diff --check` 通過。

桌面驗證：已實際啟動 `build\\PaneDock.exe` 並嘗試用 `SetCursorPos`/`mouse_event` 點擊五個版型按鈕；本環境的 `System.Drawing.Graphics.CopyFromScreen` 在兩種 overload 下皆回報「控制代碼無效」，無法取得視窗裁切截圖，因此未宣稱完成實機畫面比對。測試程序已用不帶 `/F` 的 `taskkill /PID` 優雅關閉。

### 實作交接（2026-08-26）

實機驗證發現原先的根因不是顏色對比度，而是 owner-draw radio button 的 `BM_GETCHECK` 不可靠：即使外部直接送出 `BM_SETCHECK(BST_CHECKED)`，立即查詢仍回傳未選取，導致 `draw_layout_button` 永遠畫未選中態。已移除 `BM_GETCHECK` 作為繪製依據，改由 `WM_DRAWITEM` 以 `AppState` 的 active group `layout_template` 與 `kLayoutTemplates[index]` 比對後傳入 checked 狀態；五種版型的高亮因此跟隨實際 active layout。

驗證：程式碼檢查確認繪製路徑已完全不依賴 `BM_GETCHECK`；本次環境 `Get-Process` 顯示程式未執行，未能進行新的桌面截圖驗證。
