# PD-081 — Tab 條「+」新增按鈕手繪線段有缺角,改回字型字符繪製(覆寫 PD-062 決策 5)

Phase 7 · app_shell · Depends on: PD-062

- Source: 使用者實機截圖回報(2026-08-27)。
- Origin: 使用者原文:「pane tab 的 ADD 按鈕怪怪的有缺角,請用正常的字型達成,除非效能更好才用畫的」,並附截圖。
- Priority: MEDIUM——純視覺缺陷,沒有功能問題。

## 使用者附圖

[PD-081-current-plus-button-notched.png](assets/PD-081-current-plus-button-notched.png)——放大後可見十字兩條線交會處不平整,像缺了一角,不是乾淨的「+」。

## 明確覆寫 PD-062 已確認的產品決策

`docs/tickets/PD-062-tab-strip-visual-polish.md` 決策 5(已 `done`)明確要求:「『+』改用 GDI 線條繪製,加大加粗」,理由是當時使用者要求「加大加粗」,交接區記錄用 `CreatePen(PS_SOLID, scaled kTabPlusLineWidth=2, RGB(31,41,55))` 畫兩條線並置中(第 160 行)。`docs/tickets/PD-075-unify-pane-chrome-icon-style.md` 決策 9 也因此把「+」列為非目標,理由是「PD-062 才剛定案的值」。

**本票的新證據是使用者實機截圖:手繪十字在真實螢幕上出現缺角瑕疵,不是理論上的風格選擇問題,而是實際渲染有視覺缺陷。** 使用者的新指示是「用正常的字型達成,除非效能更好才用畫的」——即**預設改回字型繪製**,只有在有實測證據顯示手繪效能明顯更好時才保留手繪路徑。本票覆寫 PD-062 決策 5 與 PD-075 決策 9 對「+」的保留决定,兩者都不得再引用來阻擋本票的修改。

## 已確認的根因(有程式碼證據,不是猜測)

`src/app_shell/main.cpp`,`paint_tab_strip` 內(第 2764-2785 行):

```cpp
HPEN plus_pen = CreatePen(PS_SOLID, scaled_value(window, kTabPlusLineWidth), RGB(31, 41, 55));
...
MoveToEx(dc, center_x - half, center_y, nullptr);
LineTo(dc, center_x + half, center_y);
MoveToEx(dc, center_x, center_y - half, nullptr);
LineTo(dc, center_x, center_y + half);
```

`kTabPlusLineWidth = 2`(第 74 行)。兩條線分別由兩次獨立的 `MoveToEx`/`LineTo` 呼叫畫出。GDI 的 `PS_SOLID` 幾何筆(`CreatePen` 建立的是 cosmetic pen,寬度 >1 時仍受 `PS_ENDCAP_ROUND` 預設端點行為影響,但兩條垂直/水平線在中心點交會時,若像素座標沒有對齊到同一個中心,端點的圓角/方角收尾會讓交會處出現不對稱的缺角)——這與 PD-062 交接區「修改後『+』hover」截圖(`assets/PD-062-tab-strip-after-plus-hover-3x.png`)未必看得出來的細節,在使用者的實機、實際 DPI/縮放下被放大成明顯瑕疵。實作 agent 不需要重新論證這個 GDI 交會像素問題的精確成因,只需要驗證新方案(字型字符)不重現這個缺陷即可,不必消耗時間去修好舊的手繪路徑。

## 已確認的產品決策

1. **「+」改用字型字符渲染**(`DrawTextW` 畫一個 `+` 字元或等效符號),而不是兩條手繪線段。這是 PD-062 之前的舊做法的回歸,也是本專案在 PD-052/PD-075 對其他圖示已經驗證過的路線(`Segoe MDL2 Assets` 字型字符路徑),整合成本低。
2. **字型選擇**:優先使用一般 UI 字型(例如 `state.chrome_font`,和 tab 文字同一支字型)直接畫半形 `+` 字元(`U+002B`)——這是最簡單、最可能沒有缺角問題的路徑,因為一般 TrueType 字型的字符渲染走的是完全不同的 hinting/anti-aliasing 管線,不會有 GDI 線段交會的像素對齊問題。若一般 UI 字型畫出來的「+」字面太小或太細,不符合 PD-062「加大加粗」的原始意圖,才考慮改用 `Segoe MDL2 Assets` 的粗體加號字符(例如 `U+E710`)或直接把字型設為 Bold。實作 agent 需要實際渲染比較後在交接區記錄最終選擇與理由,附截圖。
3. **維持 PD-062「加大加粗」的視覺意圖**——不是走回 PD-062 之前那個「太小太細」的舊字型「+」。用字型 point size 或 Bold 粗細達成放大加粗的效果,而不是回退成修改前的樣子。
4. **效能例外條款**:若實測(例如反覆開關/切換 tab 時的 `PtInRect`/重繪耗時)顯示手繪路徑效能明顯更好,允許保留手繪路徑,但必須先修好缺角問題,並在交接區寫明測到的具體效能差異數字,不能只憑印象保留手繪。**預設情況下沒有這種效能量測,直接採字型路徑即可,不需要為了走這個例外條款而額外做效能測試。**
5. **hover 狀態的底色高亮不變**(第 2756-2763 行的 `RGB(236, 240, 244)` 淡色填充),本票只改「+」符號本身的繪製方式。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> When a ticket overrides an earlier decision, state the override inside the new ticket. Never edit a completed ticket's document — that rule protects its scope, decisions and 交接區, which are the historical record.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`AGENTS.md`:
> No network, no telemetry, no third-party runtime, no services, no drivers, no admin elevation.

## Files to read and trace first

| 檔案 | 讀什麼 |
|---|---|
| `src/app_shell/main.cpp` 第 73-74 行 | `kTabPlusSize`、`kTabPlusLineWidth` 常數。 |
| `src/app_shell/main.cpp` `paint_tab_strip` 第 2752-2785 行 | 目前手繪「+」的完整邏輯,含 hover 底色。**本票要修改的主戰場。** |
| `docs/tickets/PD-062-tab-strip-visual-polish.md` 決策 5、第 89、160-161 行 | 舊決策的完整理由與交接證據,本票覆寫它。**只讀,理解覆寫的對象,不要編輯這份文件。** |
| `docs/tickets/PD-075-unify-pane-chrome-icon-style.md` 決策 9 | 「+」被列為非目標的原因,本票也覆寫這一條。**只讀,不要編輯這份文件。** |
| `src/app_shell/main.cpp` `draw_navigation_icon_button` 附近的字型 fallback 模式(PD-075 已導入) | 若字型不可用時的 fallback 慣例可參考,但「+」是最基本的 ASCII 字元,一般 UI 字型必然涵蓋,fallback 風險遠低於 `Segoe MDL2 Assets` 特殊字符,不需要照搬完整 fallback 機制,除非決策 2 最終選了 `Segoe MDL2 Assets`。 |

## 範圍

1. 移除 `MoveToEx`/`LineTo` 手繪十字的程式碼路徑(除非決策 4 的效能例外成立)。
2. 改用 `DrawTextW`(或等效字型繪製 API)畫「+」字符,置中於 `state.tab_add_rects[pane_index]`。
3. 選定字型/字符/粗細,確保視覺上「加大加粗」的效果不回退。
4. 確認 hover 底色與 disabled(若有)狀態仍正確疊加。

## 非目標

- tab 本身的圓角、間距、寬度計算(PD-062 範圍,不動)。
- 捲動按鈕(PD-080 的範圍,不在本票)。
- pane 導覽列圖示(PD-075 範圍,不動)。
- 側邊欄按鈕。

## 驗收條件

1. 3× 以上 NearestNeighbor 放大截圖,「+」符號乾淨無缺角,四個端點對稱。
2. 視覺上不比 PD-062 修改前(細字型「+」)更小更細——維持或優於 PD-062 的「加大加粗」效果,截圖需附上與 PD-062 修改後截圖(`assets/PD-062-tab-strip-after-3x.png`)的並排比較。
3. hover 狀態底色高亮仍正確顯示。
4. 96 DPI 與一個高 DPI 設定(例如 150%)下各截一組圖,符號沒有模糊或裁切。
5. 若採用決策 4 的效能例外,交接區必須附實測數字;否則預期看到手繪路徑被移除的 `git diff`。
6. `cmake --build build` 與既有 CTest 全數通過。
7. `git diff --check` 無尾隨空白。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "kTabPlusLineWidth|kTabPlusSize" src\app_shell\main.cpp
git diff --check
```

**建置輸出路徑注意:** 確認目前 build directory 的 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 設定,實際執行檔路徑以該設定為準。

**截圖驗證方法(本環境已驗證可用,請直接沿用):** `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,不要用 `Graphics.CopyFromScreen`;放大用 `InterpolationMode = NearestNeighbor`。

**驗證要快、要最小化。** 只需要一次單擊+截圖證明「+」不再缺角、且沒有變小變細,不要做多字型/多字符的全面比較展示,只在交接區記錄最終選擇與被否決的候選(若有嘗試但不理想的選項,一句話說明即可)。**computer-use 工具的單次操作完成、截圖存檔後,立刻結束該互動 session,不要停留。**

## Handoff requirements

- 最終選用的字型/字符/粗細與理由。
- 修改前(手繪缺角)/修改後(字型)3× 放大截圖並排。
- 與 PD-062 修改後截圖的視覺比較,證明沒有回退「加大加粗」的效果。
- 若採用效能例外(決策 4),附實測數字;否則附「手繪路徑已移除」的證據(`rg` 結果或 diff)。
- 96 DPI 與高 DPI 截圖。
- 未驗證項目與原因(若有)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-27 實作交接

- `src/app_shell/main.cpp` 的 `paint_tab_strip` 現在用半形 `+` (`U+002B`) 與 `DrawTextW` 繪製新增按鈕。字型沿用 `state.chrome_font` 的 face 與字型設定，臨時衍生為 18px、`FW_BOLD`，讓字面維持 PD-062 要求的較大、較粗視覺重量；衍生字型建立失敗時仍使用已選入 DC 的一般 UI 字型，不退回手繪線段。
- 已移除 `kTabPlusSize`、`kTabPlusLineWidth` 以及新增按鈕的 `CreatePen`／`MoveToEx`／`LineTo` 路徑；未採用效能例外。新增按鈕矩形、hover 底色 `RGB(236, 240, 244)`、tab 幾何、顏色與 PD-080 捲動按鈕均未修改。
- 視覺比較基準保留為：[PD-081 修改前手繪缺角](assets/PD-081-current-plus-button-notched.png)、[PD-062 修改前 3×](assets/PD-062-tab-strip-before-3x.png)、[PD-062 修改後 3×](assets/PD-062-tab-strip-after-3x.png) 與 [PD-062 修改後 hover 3×](assets/PD-062-tab-strip-after-plus-hover-3x.png)。本次唯一一次桌面視覺擷取嘗試啟動 `build\\PaneDock.exe` 後找到主視窗，但 `GetDlgItem(main, 200)` 找不到 tab strip，因此未呼叫 `PrintWindow(hwnd, hdc, 2)`、沒有產生本次修改後截圖；依驗證限制不重試。故本次 18px 粗體 glyph 相對 PD-062 修改後畫面的實機視覺、hover 與 96 DPI 結果尚未人工確認。
- 高 DPI（150%）截圖未執行；跨 DPI 的縮放程式碼沿用 `scaled_value(window, kTabPlusFontSize)`，但需使用者後續在實機確認沒有裁切或模糊。
- Agent checks：`cmake --build build` 通過；`ctest --test-dir build --output-on-failure` 為 5/5 PASS；`rg -n "kTabPlusLineWidth|kTabPlusSize" src\\app_shell\\main.cpp` 無命中；`git diff --check` 通過。未新增單元測試：繪製路徑屬 `app_shell` 的 HWND/HDC UI 邏輯，不在 `core` 的自動測試 seam，已以建置、既有 CTest、靜態路徑檢查及一次受限視覺嘗試驗證。
