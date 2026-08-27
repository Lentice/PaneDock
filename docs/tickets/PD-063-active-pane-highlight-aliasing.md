# PD-063 — Active pane 的藍色圓角外框有鋸齒,需改用不依賴圓角描邊的強調方式

Phase 7 · app_shell · Depends on: PD-040, PD-048

- Source: 使用者實機截圖比對後回報(2026-08-26)。
- Origin: 使用者原文第 11 項:「highlight pane 的圓弧沒處理好,會有鋸齒,看看有沒有更好的替代方案。例如不要用外框改用光暈或陰影或其他方法。」
- Priority: MEDIUM——純視覺,但 active pane 指示是常駐可見的,鋸齒在高解析度螢幕上特別明顯。

## 已確認的根因(有程式碼證據,不是猜測)

`src/app_shell/main.cpp` 的 `draw_pane_card`(第 1367-1429 行)用 GDI 的 `RoundRect` 加一支 `PS_SOLID` 畫筆描出 active pane 的藍色外框:

```cpp
const int border_width = std::max(1, MulDiv(is_active ? 2 : 1, dpi, 96));
const COLORREF border_color = is_active ? RGB(37, 99, 235) : RGB(232, 237, 242);
HPEN border_pen = CreatePen(PS_SOLID, border_width, border_color);
...
RoundRect(dc, card.left, card.top, card.right, card.bottom, radius, radius);
```

**根因:GDI 的 `RoundRect` 沒有反鋸齒(anti-aliasing)。** GDI 是 1980 年代的 API,所有幾何繪製都是硬邊界的像素填色,圓弧一律以階梯狀像素近似。這個限制無法透過調整半徑、畫筆寬度或畫兩次來繞過——它是 API 本身的性質。

鋸齒之所以在 active pane 特別明顯而 inactive pane 不明顯,是因為:
- active 的邊框是 2px 寬的**飽和藍** `RGB(37,99,235)`,與白色卡片背景對比極高,每一個階梯像素都清晰可見;
- inactive 的邊框是 1px 的極淺灰 `RGB(232,237,242)`,與白色背景幾乎同色,階梯被低對比掩蓋。

也就是說 PD-048 把 inactive 邊框調淡之後,問題只剩在 active 這一種狀態上。

相關的既有機制:`apply_pane_container_region`(第 1338-1365 行)用 `CreateRoundRectRgn` 把 Shell view 容器裁切成同半徑的圓角。**`HRGN` 同樣是硬邊界、無反鋸齒**,所以即使外框問題解決,容器裁切邊緣仍會是階梯狀——實作 agent 必須把這一點納入考量。

## 已確認的產品決策

1. **本票要求實作 agent 先評估、再選擇,不預先指定唯一解法。** 使用者明確說「看看有沒有更好的替代方案」,這是一個需要實機比對的視覺判斷。以下三條路徑都可接受,實作 agent 必須**至少實作並截圖比對其中兩條**,再選定一條,並在交接區附上比對截圖與選擇理由:

   - **路徑 A:改用 GDI+ 繪製圓角外框(有反鋸齒)。** `Gdiplus::Graphics` 設 `SmoothingMode = SmoothingModeAntiAlias`,用 `GraphicsPath` 組出圓角矩形再 `DrawPath`。GDI+ 是 Windows 內建(`gdiplus.dll`,不是第三方依賴),但**需要 `GdiplusStartup`/`GdiplusShutdown` 的行程層級初始化,以及在 CMake 加上 `gdiplus` 連結**——這是本專案第一次引入 GDI+,是本路徑最主要的成本。
   - **路徑 B:改用「光暈/陰影」取代外框。** 不描邊,改成在卡片外圍畫數層逐漸變淡的藍色圓角矩形(2-3 層即可),模擬柔和的外發光。因為每一層都是低對比,階梯不明顯,視覺上反而更柔和。**純 GDI 即可,不需要新依賴,是成本最低的路徑。** 缺點是「多層 RoundRect」在極小的 pane 上可能顯得厚重。
   - **路徑 C:改用「不依賴圓弧」的強調方式。** 例如在 active pane 的 tab 條上方或左緣畫一條直的粗色條(直線沒有鋸齒問題),或把 active pane 的 tab 條底色改成品牌藍。**成本最低、完全沒有鋸齒風險,但改變了設計語言**,與目標畫面(`docs/panedock-ui-demo-01-refined-quiet-header.html`)的「整張卡片被強調」不同,是最後選項。

2. **不論選哪條路徑,inactive pane 的視覺維持現狀不變**(PD-048 已定案的 1px `RGB(232,237,242)` 外框與 `RGB(235,239,244)` 陰影)。本票只改 active 態。
3. **不得為了消除鋸齒而把圓角半徑改成 0(退回方角)。** PD-040 已明確決定四角全圓角,方角是倒退。
4. **若選擇路徑 A(GDI+),必須確認 `GdiplusStartup` 的呼叫時機與 `IExplorerBrowser` 的 COM 初始化不衝突,且 `GdiplusShutdown` 在正確的時機呼叫**(不能在還有視窗存活時關閉)。這是路徑 A 的主要風險點,必須實測而非推論。
5. **`apply_pane_container_region` 的 `HRGN` 硬邊界裁切在本票中不強制解決**,但實作 agent 必須在交接區記錄選定路徑之後容器邊緣的實際外觀(是否仍可見階梯、是否被新的視覺方案掩蓋)。若選定路徑讓容器邊緣的階梯變得更明顯,要如實記錄並建議後續票。
6. **效能要求:pane 卡片的繪製發生在每次 `WM_PAINT`。** 選定的方案不得讓視窗縮放/拖曳分隔線時出現可察覺的延遲。多層陰影(路徑 B)與 GDI+(路徑 A)都比單次 `RoundRect` 昂貴,實作 agent 需實測拖曳分隔線時的流暢度。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.
>
> (GDI+ 是 Windows 內建元件,不算第三方依賴;但仍應優先考慮純 GDI 能否達成。)

`AGENTS.md`:
> No network, no telemetry, no third-party runtime, no services, no drivers, no admin elevation.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes.

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers.

`docs/tickets/PD-040-pane-card-full-corner-rounding.md`(不得推翻的既有決策):
> pane 卡片四個角全部圓角,Shell view 容器以同半徑的 region 裁切,不留方角。

`docs/tickets/PD-048-pane-card-border-shadow-lightening.md`(inactive 態已定案,本票不改):
> inactive 邊框 `RGB(232,237,242)`、陰影 `RGB(235,239,244)`。

## Files to read and trace first

- `src/app_shell/main.cpp` 第 1367-1429 行(`draw_pane_card`)——**本票主要修改處,active 分支在第 1415-1428 行。**
- `src/app_shell/main.cpp` 第 1330-1332 行(`pane_card_radius`)——圓角半徑的單一來源,`draw_pane_card` 與 `apply_pane_container_region` 共用。
- `src/app_shell/main.cpp` 第 1338-1365 行(`apply_pane_container_region`)——`HRGN` 裁切,同樣無反鋸齒。
- `src/app_shell/main.cpp` 呼叫 `draw_pane_card` 的地方(`WM_PAINT` 路徑)——確認繪製頻率與效能影響。
- `CMakeLists.txt`——若選路徑 A 需要在此加 `gdiplus` 連結。
- `docs/tickets/PD-040-pane-card-full-corner-rounding.md`、`docs/tickets/PD-048-pane-card-border-shadow-lightening.md`——既有決策,不得推翻。
- `docs/panedock-ui-demo-01-refined-quiet-header.html`——目標畫面的 active pane 強調方式參考。

## Scope

1. 評估至少兩條路徑(A/B/C)並截圖比對。
2. 實作選定的路徑,消除 active pane 強調的鋸齒感。

## Non-goals

- 不改 inactive pane 的外框或陰影。
- 不改圓角半徑,不退回方角。
- 不改 `apply_pane_container_region` 的裁切機制(僅記錄觀察)。
- 不改 pane 卡片的白色底色或整體排版。
- 不為了本票引入任何非 Windows 內建的繪圖函式庫(Direct2D 亦不在本票範圍——它比 GDI+ 更重,且需要更大的架構改動)。

## Acceptance

1. Active pane 的強調視覺在放大 3 倍檢視下**沒有明顯的階梯狀鋸齒**,或鋸齒被柔和的過渡掩蓋到不易察覺。
2. Active pane 與 inactive pane 之間仍然一眼可辨,強調程度不低於修改前。
3. Inactive pane 的視覺完全未改變(可用修改前後截圖逐像素比對佐證)。
4. 五種版型下(1/2/3/4 pane 與各種分割),每個 pane 的強調視覺都正確,沒有被相鄰 pane 或分隔線裁掉。
5. 拖曳分隔線調整 pane 大小時**流暢,沒有可察覺的延遲或閃爍**。
6. 在 150%/200% 顯示縮放下正確縮放。
7. 沒有 GDI/GDI+ 資源洩漏(反覆切換 active pane 與版型後,工作管理員的 GDI 物件數不持續上升)。
8. 若採用路徑 A:程式正常啟動與關閉,`GdiplusShutdown` 沒有造成關閉時當機。
9. 靜止時程式回到 0% CPU。
10. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
11. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "draw_pane_card|pane_card_radius|RoundRect|Gdiplus|GdiplusStartup" src\app_shell\main.cpp CMakeLists.txt
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:切換 active pane,截圖放大 3 倍檢視邊緣;切換五種版型;
# 拖曳分隔線觀察流暢度;切換顯示縮放比例;反覆操作後看 GDI 物件數。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 而不是 `Graphics.CopyFromScreen`。**本票的驗收完全建立在放大檢視上——必須用 `InterpolationMode = NearestNeighbor` 放大至少 3 倍截取 pane 的圓角區域,不放大無法判斷鋸齒是否改善。** 修改前先截一組基準圖,才能做前後對照。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- **實際評估了哪幾條路徑,各自的放大截圖,以及最終選定哪一條與理由。** 這是本票最重要的交接內容。
- 選定路徑的具體實作方式與色值/層數/半徑等參數。
- 若採用 GDI+:`GdiplusStartup`/`GdiplusShutdown` 的呼叫位置、CMake 的連結設定、與 COM 初始化的先後關係。
- `apply_pane_container_region` 容器邊緣在新方案下的實際外觀觀察。
- 拖曳分隔線的流暢度實測結果。
- GDI 物件數的長時間觀察結果。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-27 實作交接

**方案比較與截圖證據。** 先以原始版本建立 baseline，再實作並截圖比較路徑 B、C；三組都使用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`，裁切 `(175,40,300,180)` 後以 nearest-neighbor 放大 3 倍。

- Baseline：active pane 是 `RGB(37,99,235)`、2px 的 GDI `RoundRect`，放大後可見左上角與上緣的階梯像素：[PD-063-before-3x.png](PD-063-before-3x.png)、[PD-063-before-full.png](PD-063-before-full.png)。
- 路徑 B（比較用）：純 GDI 三層填色 halo，96-DPI 外擴 4/3/2px，色值依序為 `RGB(219,234,254)`、`RGB(191,219,254)`、`RGB(147,197,253)`：[PD-063-route-b-3x.png](PD-063-route-b-3x.png)、[PD-063-route-b-full.png](PD-063-route-b-full.png)。它把高對比鋸齒柔化了，但 3× 仍看得到分層圓弧，且小 pane 會增加視覺厚度。
- 路徑 C（採用）：移除 active pane 的圓角藍框；所有卡片統一保留既有 1px `RGB(232,237,242)` 外框，active 狀態改在自繪 tab strip 最上方畫 3px、DPI-scaled 的 `RGB(37,99,235)` 直線。直線不經過圓弧描邊，因此 3× 沒有階梯狀圓角；同時比 B 更輕、更容易在五種版型辨識：[PD-063-route-c-3x.png](PD-063-route-c-3x.png)、[PD-063-route-c-full.png](PD-063-route-c-full.png)。
- 最終 Release 的再次截圖確認 C 方案已編入正式 `build\PaneDock.exe`：[PD-063-after-3x.png](PD-063-after-3x.png)、[PD-063-after-full.png](PD-063-after-full.png)。

**實作細節。** `draw_pane_card` 不再接收 active flag，也不再畫 active 圓角藍框；`paint_tab_strip` 在所有 tab、scroll button、`+` 圖示完成後畫 active indicator，避免 overflow 控制項覆蓋直線。`set_active_pane` 額外 invalidate 前一個與新的 tab strip，確保單次 active pane 變更立即重繪。inactive 卡片的幾何、陰影、外框色值未改動。`pane_card_radius` 與 `apply_pane_container_region` 完全未改動；full screenshot 未看到新的 active 外框與既有 HRGN 裁切產生接縫，HRGN 的硬邊界仍是既有容器機制，未在本票內重做。

**驗證紀錄。** 新增邏輯位於 `app_shell` 的真實 Win32 paint path，不屬於本專案唯一的 `src/core` 自動化 seam；本票以真實 Release `PrintWindow` 截圖作 focused self-check。LLVM-MinGW/Clang + Ninja 的 `cmake --build build` 通過，`ctest --test-dir build --output-on-failure` 為 5/5 通過，指定 `rg` 檢查與 `git diff --check` 通過。

依使用者指定的 single-click + screenshot 限制，只做了一次右下 pane tab 區單擊嘗試；Computer Use 回報無法 activate captured window，依規則沒有重試。因此 active pane 點擊路由、五種版型、分隔線拖曳流暢度、150%/200% DPI、長時間 GDI 物件趨勢與 idle CPU 均明確未驗證，沒有以單張 4-pane 截圖宣稱通過這些 acceptance。關閉最終 Release 使用不帶 `/F` 的 `taskkill /PID`，無殘留可見視窗。
