# PD-075 — Pane 導覽列的五個圖示由三種不同技術繪製,大小、線寬與配色互不一致

Phase 7 · app_shell · Depends on: PD-052, PD-064

- Source: 使用者實機觀察後回報(2026-08-26)。
- Origin: 使用者原文:「pane 中的 icon 風格應該統一,大小、線條粗細、....」。
- Priority: MEDIUM——純視覺一致性,沒有功能缺陷。

## 已確認的根因(有程式碼證據,不是猜測)

`src/app_shell/main.cpp` 的 `draw_navigation_icon_button`(第 656-757 行)用**一個 switch** 畫五個圖示,但每個 case 走的是**不同的繪製技術**:

| `glyph_kind` | 圖示 | 繪製技術 | 線寬來源 | 顏色是否受 `color` 控制 |
|---|---|---|---|---|
| 0 | back | `ImageList_DrawEx` 畫 Common Controls 的 **history 點陣圖**(`HIST_BACK`) | 點陣圖內建,無法調 | **否**——點陣圖自帶配色,`disabled` 只能靠 `ILD_BLEND50` |
| 1 | forward | 同上(`HIST_FORWARD`) | 同上 | **否** |
| 2 | up | 手繪 `MoveToEx` / `LineTo` | `CreatePen(PS_SOLID, size / 8, ...)` = 96 DPI 下 **2px** | 是 |
| 3 | refresh | **`Segoe MDL2 Assets`** 字型字符 ``,走 `DrawTextW` | 字型本身的筆畫粗細,**不是 2px** | 是 |
| 4 | view | 四個 3×3 px 的 `Rectangle()` | 用的是同一支 2px 畫筆,但畫的是 3px 見方的框,實際看起來是實心點 | 是 |

三種技術(點陣圖 / 手繪線段 / 字型字符)並排在同一列按鈕上,必然對不齊:

- **back/forward 的顏色完全不受控。** 它們是系統點陣圖,配色由 Common Controls 決定,和旁邊 `RGB(90, 102, 122)` 的手繪圖示不是同一個灰。
- **refresh 的筆畫粗細由字型決定**,和 up 的 2px 硬線寬沒有任何關係。
- **case 0/1 的手繪 `<` / `>` 是死路徑**——只有 `navigation_history_image_list()` 回傳 `nullptr` 時才會走到,實務上等於永不執行。所以「back/forward 有一份手繪版本」這件事不能拿來當一致性的依據。
- **`view` 的四個小方塊不是 grid 圖示,是四個點。** `Rectangle(cx ± half/2 - 1, ..., +2)` 在 96 DPI 下是 3px 見方,用 2px 畫筆描邊之後中間沒有空隙。

另外,header 的版型按鈕走的是另一個函式 `draw_layout_glyph`(第 513-563 行),用的是 **`CreatePen(PS_SOLID, 1, color)` ——1px**,而導覽列是 2px。tab 條的「+」又是第三個值,`kTabPlusLineWidth`(第 2377 行)。

## 已確認的產品決策

1. **統一到 `Segoe MDL2 Assets` 字型字符,一套字型畫完 pane 導覽列的全部五個圖示。** 理由:
   - refresh 已經是這條路了,而且它是 PD-052 在**手算 `Arc()` 失敗兩輪之後**才改過去的——那個學費已經付過,不要再付第二次。
   - 一套字型自動帶來一致的筆畫粗細、光學重心與字面大小,這正是使用者要的「大小、線條粗細統一」。手繪線段永遠要一個一個對齊。
   - `Segoe MDL2 Assets` 在 Windows 10 1607 以上與 Windows 11 全部內建,涵蓋本專案宣告的平台底線(Windows 10 22H2 / Windows 11 x64)。不是新增相依。
2. **候選字符如下,但實作 agent 必須實際渲染後目視確認,不得直接相信本表。** 字符表在不同字型版本上會有差異,而且視覺重量要並排比較才看得出來:

   | 圖示 | 首選 | 備選 |
   |---|---|---|
   | back | `` | ``、`` |
   | forward | `` | ``、`` |
   | up | `` | ``、`` |
   | refresh | ``(現行,不要動) | — |
   | view / details | ``(list)、``(grid) | ``、`` |

   選定後把最終字符寫進交接區,並附一張五個圖示並排的放大截圖。
3. **`draw_navigation_icon_button` 內的 switch 應該收斂成「查表拿字符 → 一次 `DrawTextW`」。** 五個 case 各自手繪的結構本身就是不一致的來源。移除 `ImageList` 路徑之後,`navigation_history_image_list()` / `release_navigation_history_image_list()`(第 618-634 行、呼叫點第 3270 行)會失去最後一個呼叫者,**一併刪除**。`AGENTS.md`:「If you are certain that something is unused, you can delete it completely.」刪之前先 `rg` 確認。
4. **`navigation_refresh_font` 要改名並升級為所有導覽圖示共用的字型 handle**(例如 `navigation_glyph_font`),不要為每個圖示各開一份 `HFONT`。它現在的 lazy-init + 單一 release 的生命週期模式是對的,維持它。
5. **字型不可用時的 fallback 必須保留,且要一次涵蓋全部五個圖示。** 目前只有 refresh 有 fallback(退回 `Ellipse` + 線段)。改成字型路徑之後,若 `CreateFontW` 失敗,五個按鈕都會變空白——**那是比不一致嚴重得多的退化**。fallback 可以是現行的手繪程式碼(back/forward 那兩個目前是死碼的 `<` / `>` 正好在這裡派上用場),但必須明確標註它是 fallback 路徑。
6. **圖示的字面大小統一由一個常數決定,且走 `scaled_value`。** 現行的 `std::max(4, scaled_value(item.hwndItem, 16))` 已經是對的基準(16 px @ 96 DPI),沿用這個值,但要讓字型高度由它導出,而不是另外寫一個字型大小。
7. **disabled 態統一用顏色表達,不用 blend。** 現行手繪路徑已經有 `RGB(190, 197, 209)`,`ImageList` 路徑用的是 `ILD_BLEND50`。統一到前者,`ImageList` 路徑消失後 `ILD_BLEND50` 也就不存在了。
8. **header 的版型按鈕圖示(`draw_layout_glyph`)是本票的非目標。** 那五個圖示是**版面示意圖**(一格 / 雙欄 / 上下 / 2×2 / 更多),沒有任何字型字符能表達「這個版型長什麼樣」,它們必須手繪。但**它們的 1px 線寬與導覽列不一致這件事是真的**,所以本票要在交接區明確記錄這個已知差異,並把它列進 `docs/tickets.md` 的候選,不要靜靜放過。
9. **tab 條的「+」是本票的非目標。** `kTabPlusLineWidth` 是 PD-062 才剛定案的值(使用者明確要求「加大加粗」),本票不得回頭改細。
10. **不改任何按鈕的尺寸、位置、間距或 hit-test。** 那是 PD-069 的範圍。本票只換圖示的繪製方式。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Platform baseline is Windows 10 22H2 / Windows 11 x64, C++20, native Win32.

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`AGENTS.md`:
> Avoid backwards-compatibility hacks like renaming unused _vars, re-exporting types, adding // removed comments for removed code, etc. If you are certain that something is unused, you can delete it completely.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Read the relevant spec section and trace every caller before touching shared code.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`:
> No network, no telemetry, no third-party runtime, no services, no drivers, no admin elevation.

## Files to read and trace first

| 檔案 | 讀什麼 |
|---|---|
| `src/app_shell/main.cpp` `draw_navigation_icon_button`(第 656-757 行) | **本票的主戰場。** 五個 case 的現行差異。 |
| `src/app_shell/main.cpp` 第 618-654 行 | `navigation_history_image_list` / `release_navigation_history_image_list` / `navigation_refresh_font` / `release_navigation_refresh_font` 的 lazy-init 與釋放模式。 |
| `src/app_shell/main.cpp` 第 3260-3280 行附近 | 兩個 release 函式的呼叫時機(關閉序列)。刪掉 imagelist 之後要一起清。 |
| `src/app_shell/main.cpp` `draw_layout_glyph`(第 513-563 行) | 決策 8 的非目標邊界。**只讀,不改。** |
| `src/app_shell/main.cpp` `paint_tab_strip` 第 2370-2388 行 | 決策 9 的非目標邊界(`kTabPlusLineWidth`)。**只讀,不改。** |
| `docs/tickets/PD-052-*.md` | refresh 圖示改用 `Segoe MDL2 Assets` 的完整理由與兩輪手算 `Arc()` 的失敗紀錄。**這是決策 1 的證據來源。** |
| `docs/tickets/PD-064-header-and-nav-icon-cleanup.md` | PD-064 已經在處理 Up 圖示的置中問題,並建議「優先評估改用 `Segoe MDL2 Assets` 字型圖示」。**若 PD-064 已經把 Up 換成字型字符,本票就是把剩下四個一起收進去;若 PD-064 選了別的做法,本票覆寫它的 Up 部分並在此說明。** 實作前先確認 PD-064 的實際結果。 |

## 範圍

1. `draw_navigation_icon_button` 改成字符查表 + 單一 `DrawTextW` 路徑,五個圖示共用一支字型與一個字面大小常數。
2. `navigation_refresh_font` 改名為共用的導覽字型 handle,維持既有的 lazy-init / 單一 release 生命週期。
3. 刪除 `navigation_history_image_list`、`release_navigation_history_image_list` 及其呼叫點。
4. 為五個圖示補上統一的字型不可用 fallback(決策 5)。
5. disabled 態統一用 `RGB(190, 197, 209)`。

## 非目標

- header 的版型按鈕圖示(決策 8)——但要在交接區與 `docs/tickets.md` 候選記錄它的 1px 線寬差異。
- tab 條的「+」(決策 9)。
- 側邊欄的按鈕與徽章。
- 檔案清單內部的任何圖示——那是原生 `IExplorerBrowser` 渲染的,`AGENTS.md` 明令不得重刻。
- 按鈕的尺寸、位置、間距、hit-test(PD-069 的範圍)。
- 不新增任何字型檔案或第三方資源。

## 驗收條件

1. 五個導覽圖示並排的 4× 以上放大截圖,**筆畫粗細目視一致**、字面大小一致、垂直光學重心對齊。
2. 五個圖示的前景色都是同一個 `RGB(90, 102, 122)`(啟用)/`RGB(190, 197, 209)`(停用)。用取色證明,不要只說「看起來一樣」。**back/forward 在修改前是系統點陣圖配色,修改後必須取到與其他三個相同的值——這是本票最直接的證據。**
3. back / forward 在無上一頁/下一頁時仍正確顯示 disabled 配色。
4. 導覽功能沒有回歸:上一頁、下一頁、上一層、重新整理、切換檢視模式全部仍然可用。
5. `rg -n "navigation_history_image_list|ILD_BLEND50|HIST_BACK|HIST_FORWARD" src\` 為零筆。
6. 96 DPI 與 一個高 DPI 設定(例如 150%)下各截一組圖,圖示都沒有模糊、裁切或位移。
7. 字型不可用的 fallback 路徑有被實際走過一次(可暫時把字型名稱改成不存在的名字驗證),五個圖示都畫得出來,不是空白。
8. 關閉程式時沒有 GDI 物件洩漏:反覆開關 pane / 切換版型後 `GetGuiResources(GR_GDIOBJECTS)` 沒有累積成長。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "navigation_history_image_list|navigation_refresh_font|ILD_BLEND50|HIST_BACK|HIST_FORWARD|Segoe MDL2" src\
rg -n "draw_layout_glyph|kTabPlusLineWidth" src\app_shell\main.cpp
git diff --check
```

**建置輸出路徑注意:** 目前 `build` 這個 build directory 的 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 被設成 `pd062-output`,實際執行檔在 `build\pd062-output\PaneDock.exe`。

**截圖驗證方法(本環境已驗證可用,請直接沿用):** `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,不要用 `Graphics.CopyFromScreen`;放大用 `InterpolationMode = NearestNeighbor`。取色請直接讀 `Bitmap.GetPixel`,不要目視判斷顏色是否相同。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉。**

## Handoff requirements

- 五個圖示最終選用的字符碼位,以及**被否決的候選字符與否決理由**(視覺重量不符、字型內不存在等)。
- 五個圖示並排的放大截圖,以及五個前景色的實際取色值。
- 刪除 `ImageList` 路徑的 `rg` 前後結果。
- fallback 路徑的實際驗證方式與截圖。
- 96 DPI 與高 DPI 兩組截圖。
- GDI 物件數的前後對照。
- 決策 8 的已知差異記錄:版型按鈕 1px vs 導覽列圖示的實際筆畫粗細,以及為什麼本票不處理。
- 與 PD-064 的實際關係(它把 Up 做成什麼樣,本票是接續還是覆寫)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
