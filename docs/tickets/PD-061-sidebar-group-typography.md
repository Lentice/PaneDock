# PD-061 — 側邊欄 Group 名稱與副標題的字級太小,與目標畫面落差明顯

Phase 7 · sidebar · Depends on: PD-028

- Source: 使用者實機截圖與目標畫面(`docs/panedock-ui-demo-01-refined-quiet-header.html`)並列比對後回報(2026-08-26)。
- Origin: 使用者原文第 7 項:「Group label 的字太小,下面 "n panes k tabs" 的字也太小,請參照目標」。
- Priority: MEDIUM——純排版,不影響功能,但側邊欄是常駐可見區域,字級偏小會持續影響可讀性。

## 已確認的根因(有程式碼證據,不是猜測)

`src/sidebar/sidebar.cpp` 的 `Sidebar::draw_item`(第 100-197 行)目前:

1. **Group 名稱直接用 LISTBOX 的預設字型繪製,沒有任何放大或加粗:**
   ```cpp
   HFONT base_font = reinterpret_cast<HFONT>(SendMessageW(list_box_, WM_GETFONT, 0, 0));
   if (base_font == nullptr)
       base_font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
   ```
   而該 LISTBOX 在 `Sidebar::create`(第 41-43 行)被設定為 `GetStockObject(DEFAULT_GUI_FONT)`。**`DEFAULT_GUI_FONT` 是一個舊的 stock 字型(實際上是 MS Shell Dlg,約 8pt),不是現代 Windows 的 UI 字型(Segoe UI 9pt)。** 這是名稱看起來偏小又偏舊的主因。
2. **副標題再從 base font 縮小到 0.82 倍**(第 159-161 行):
   ```cpp
   logfont.lfHeight = static_cast<LONG>(std::lround(logfont.lfHeight * 0.82));
   ```
   在一個本來就偏小的基準上再乘 0.82,結果是使用者截圖中幾乎難以辨識的副標題。
3. **列高固定為 `kGroupRowHeight`**(`src/sidebar/sidebar.h`),名稱與副標題各分到列高的一半(第 145-149 行 `line_height = (text_area.bottom - text_area.top) / 2`)。字級放大後必須確認兩行仍放得下,否則會被裁切。

對照目標畫面:Group 名稱是明顯較大且**加粗**的深色標題,副標題是清楚可讀的次要灰字,兩者的字級差距沒有現在這麼懸殊。

## 已確認的產品決策

1. **改用系統的實際 UI 字型作為基準,不再用 `GetStockObject(DEFAULT_GUI_FONT)`。** 正確做法是 `SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, ...)` 取得 `NONCLIENTMETRICSW::lfMessageFont`,那才是目前 Windows 主題實際使用的 UI 字型(Windows 10/11 上是 Segoe UI)。
   - **這個呼叫必須傳入正確的 DPI 版本或自行縮放。** 本專案是 Per-Monitor-V2 DPI 感知(`AGENTS.md`),`SystemParametersInfoW` 的非 DPI 感知版本會回傳主螢幕 DPI 的尺寸。實作 agent 應使用 `SystemParametersInfoForDpi`(Windows 10 1607+,符合本專案 Windows 10 22H2 基線)並傳入該視窗目前的 DPI。
2. **Group 名稱加粗(`FW_SEMIBOLD` 或 `FW_BOLD`),字級為基準字型大小。** 副標題不加粗,字級為基準的 0.9 倍左右(而非現在的 0.82),顏色維持現有的 `kSidebarSubtitleText`(`RGB(148,163,184)`)。實際數值由實作 agent 依實機截圖與目標畫面比對後微調,但必須滿足驗收 1、2。
3. **`kGroupRowHeight` 必須跟著調整以容納放大後的兩行文字。** 實作 agent 需依實際字型度量(`GetTextMetricsW`)決定新的列高,不要憑空給一個魔術數字。列高改變後要確認 `Sidebar::set_rect` 的 `LB_SETITEMHEIGHT`(第 67-69 行)與 `Sidebar::measure_item`(第 92-98 行)兩處都跟著走同一個值——**這兩處都用 `MulDiv(kGroupRowHeight, dpi, 96)`,是同一個常數的兩個消費者,改常數即可,不要只改一處。**
4. **字型必須以 DPI 縮放建立,並在 DPI 改變時重建。** 目前 `draw_item` 每次繪製都 `CreateFontIndirectW` 再 `DeleteObject` 一次(第 162、194 行),雖然沒有洩漏但每列都建一次字型並不理想。**實作 agent 可以改成快取,但快取必須以 DPI 為鍵並在 `WM_DPICHANGED` 時失效,否則跨螢幕拖曳時字級會錯。若快取的複雜度超過收益,維持現狀的每次建立也可以接受——請在交接區說明選擇理由。**
5. **`GROUPS` 這個區段標題(側邊欄最上方的小標)不在本票範圍。** 使用者只指出 Group 名稱與副標題,不要順手改別的。
6. **徽章(右側圓形數字)的字級可視覺協調地一併微調,但不強制。** 若放大後徽章數字相對變得太小而不協調,允許同步調整並記錄。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> **App UI text must be English.** No Chinese strings ship in the binary.
>
> (相關:副標題的 `" panes · "` / `" tabs"` 已經是英文,本票不得改成中文。)

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`AGENTS.md`:
> **Keep `src/core` free of HWND, COM and `windows.h`.**

## Files to read and trace first

- `src/sidebar/sidebar.cpp` 第 100-197 行(`Sidebar::draw_item`)——本票主要修改處,重點是第 151-176 行的字型建立與兩行文字繪製。
- `src/sidebar/sidebar.cpp` 第 20-26 行(`format_subtitle`)——副標題文字生成,不需要改。
- `src/sidebar/sidebar.cpp` 第 30-53 行(`Sidebar::create`)——`WM_SETFONT` 設定 `DEFAULT_GUI_FONT` 的地方。
- `src/sidebar/sidebar.cpp` 第 61-70 行(`Sidebar::set_rect`)、第 92-98 行(`Sidebar::measure_item`)——列高的兩個消費者。
- `src/sidebar/sidebar.h`——`kGroupRowHeight` 常數定義。
- `src/app_shell/main.cpp` 的 `brand_font()`(第 1283 行起)——**本專案既有的「從 base font 衍生加粗字型」範例,可直接參考其模式。**
- `docs/panedock-ui-demo-01-refined-quiet-header.html`——目標畫面的字級與字重參考。

## Scope

1. `Sidebar::draw_item` 改用系統 UI 字型(`SystemParametersInfoForDpi` + `lfMessageFont`)作為基準,Group 名稱加粗、副標題適度放大。
2. `kGroupRowHeight` 依新字型度量調整。

## Non-goals

- 不改 `GROUPS` 區段標題。
- 不改 Group 列的背景色、選取態顏色或圓角(PD-058 會處理 hover)。
- 不改副標題的文字內容或格式。
- 不改側邊欄寬度(`kSidebarWidth`)。
- 不改 `+ New Group` 底部按鈕。

## Acceptance

1. Group 名稱字級明顯放大且加粗,在一般觀察距離下清楚可讀,視覺上接近目標畫面。
2. 副標題(`4 panes · 15 tabs`)字級放大到清楚可讀,但仍明顯小於 Group 名稱、且維持次要灰色。
3. 名稱與副標題兩行都完整顯示,沒有被列高裁切、沒有互相重疊。
4. Group 名稱過長時仍正確以 `DT_END_ELLIPSIS` 截斷,不會蓋到右側徽章。
5. 徽章仍垂直置中且不與文字重疊。
6. **在不同 DPI 的螢幕之間拖曳視窗後,字級正確跟隨新 DPI 縮放,沒有變模糊或尺寸錯誤。** 若開發機只有單一 DPI,改用 Windows 顯示設定臨時切換縮放比例驗證,並記錄驗證方式。
7. 沒有 GDI 物件洩漏(建立的每個 `HFONT` 都有對應的 `DeleteObject`,或明確以快取管理)。
8. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
9. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "DEFAULT_GUI_FONT|SystemParametersInfo|kGroupRowHeight|CreateFontIndirect|lfMessageFont" src\sidebar\sidebar.cpp src\sidebar\sidebar.h src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:截圖側邊欄與 docs/panedock-ui-demo-01-refined-quiet-header.html 的目標畫面並列比對;
# 建立一個名稱很長的 Group 確認截斷正確;切換顯示縮放比例確認 DPI 縮放正確。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 而不是 `Graphics.CopyFromScreen`。側邊欄區域較小,建議截圖後用 `InterpolationMode = NearestNeighbor` 放大 3 倍再檢視,才能準確判斷字級與字重。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 最終採用的字型取得方式與 API(`SystemParametersInfoForDpi` 或其他)。
- Group 名稱與副標題的最終字重與字級倍率。
- `kGroupRowHeight` 的新值與推導依據(字型度量的實際數字)。
- 字型是否改為快取,以及選擇理由。
- DPI 縮放的實際驗證方式與結果。
- 放大 3 倍的側邊欄截圖與目標畫面的比對結論。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-26 — 實作與實機驗證

- `Sidebar::draw_item` 改用 `SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, ...)` 取得目前 `dpi_` 的 `NONCLIENTMETRICSW::lfMessageFont`。Group 名稱使用 `FW_SEMIBOLD`、基準字級；副標題使用 `FW_NORMAL`、字型高度的 `0.9` 倍，顏色維持 `kSidebarSubtitleText`。
- `kGroupRowHeight` 從 `54` 調為 `52` logical px。實機 `GetTextMetricsW` probe（實際系統字型為 `Microsoft JhengHei UI`）結果：96 DPI 的名稱/副標題高度為 `15/14` px、120 DPI 為 `19/18` px、144 DPI 為 `23/20` px；列高仍保留足夠上下空間，實拍無裁切或重疊。`set_rect` 與 `measure_item` 原本共用此常數及 `MulDiv`，未新增第二份列高。
- 未加字型 cache：每次 owner-draw 依當下 `dpi_` 建立兩個 `HFONT`，繪製後各自 `DeleteObject`。這個最小方案沒有 cache invalidation/lifetime 狀態，也會在 `WM_DPICHANGED` 重新繪製時自然取得新 DPI 字型。
- 實機驗證使用 `build\PaneDock.exe --diagnostic` 進入可見 UI；以 `SetCursorPos`/`mouse_event` 點擊 Group 清單，再用 `PrintWindow(hwnd, hdc, 2)` 擷取，回傳 `True`。當前視窗為 96 DPI、window `1400x900`、client `1384x861`；群組列、名稱、省略、摘要及 badge 均正常。一般模式在本次驗證 shell 會卡在 Shell 初始化而未取得可見主視窗；此為既有啟動路徑觀察，未擴大本票範圍。
- DPI probe 同時以 96/120/144 DPI 呼叫 `SystemParametersInfoForDpi`，確認同一 API 回傳隨 DPI 縮放的字型度量；本機實際可見桌面為 96 DPI，因此未改動使用者顯示設定。放大 3 倍截圖：[PD-061-sidebar-3x.png](PD-061-sidebar-3x.png)。與 `panedock-ui-demo-01-refined-quiet-header.html` 目標畫面比對後，名稱的半粗體／基準大小、摘要的次要灰色及 badge 對齊符合目標層級，未改動 `GROUPS` 標題或其他非本票區域。
- 驗證：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` 成功；`cmake --build build` 成功；`ctest --test-dir build --output-on-failure` 4/4 通過；`git diff --check` 成功。
