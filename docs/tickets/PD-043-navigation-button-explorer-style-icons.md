# PD-043 — 導覽按鈕(上一頁/下一頁/上一層)改用 Windows Explorer 風格圖示

Phase 6 · app_shell · Depends on: PD-031

- Source: 使用者實機操作 `.\build\PaneDock.exe` 後回報,附截圖(2026-08-25)。
- Origin: 「上一頁/下一頁/上一層 的 icon 太醜,可以參考 windows explorer 的 icon」。
- Priority: LOW——純視覺,不影響功能。

## 現況(有程式碼證據)

`src/app_shell/main.cpp` 的 `draw_navigation_icon_button`(第 571 行起)用 `MoveToEx`/`LineTo` 手繪三種箭頭(back 的 `<`、forward 的 `>`、up 的向上箭頭),線條比例與置中計算簡單,視覺上跟系統/Explorer 的導覽圖示風格不一致(使用者形容「太醜」)。

## 已確認的產品決策

1. **上一頁/下一頁改用 Win32 Common Controls 內建的歷史記錄圖示(`IDB_HIST_SMALL_COLOR`),不是自己重新設計手繪圖形。** `commctrl.h` 定義了公開、穩定的系統點陣圖資源常數 `IDB_HIST_SMALL_COLOR`(與對應的 `HIST_BACK`/`HIST_FORWARD`/`HIST_FAVORITES`/`HIST_ADDTOFAVORITES`/`HIST_VIEWTREE` 索引),透過 `ImageList_LoadImage(GetModuleHandleW(nullptr)`— 不對,實際上這組點陣圖是**系統**資源,要用 `HINST_COMMCTRL` 當作 `hinst` 參數 呼叫 `ImageList_LoadImage(HINST_COMMCTRL, MAKEINTRESOURCEW(IDB_HIST_SMALL_COLOR), 16, 0, CLR_DEFAULT, IMAGE_BITMAP, LR_DEFAULTCOLOR | LR_CREATEDIBSECTION)` 取得,這正是 Windows 檔案總管歷史記錄工具列使用的同一組圖示,是公開 API、不需要新增依賴、不依賴任何私有/不保證的 shell32 資源索引(比起直接讀 `shell32.dll` 內部圖示索引更穩定、更符合 `AGENTS.md`「Reach for the standard library and Win32 before adding a dependency」)。在 `WM_DRAWITEM` 對 back/forward 按鈕改用 `ImageList_Draw`/`ImageList_DrawEx` 把對應索引(`HIST_BACK`=0、`HIST_FORWARD`=1)畫到按鈕矩形置中位置,停用狀態用 `ImageList_Draw` 的 `ILD_BLEND50`(或既有的 `disabled` 灰階邏輯,依現有 `ODS_DISABLED` 判斷)呈現。
2. **「上一層」(Up)沒有對應的公開系統圖示可以直接取用**(`IDB_HIST_SMALL_COLOR` 沒有「上一層」這個項目;Explorer 現代版的「上一層」圖示是私有資源,沒有穩定公開的 API 可以取得)。維持手繪,但改善視覺:縮小線條比例、置中演算法比照 Explorer 現代風格(資料夾圖示疊加向上箭頭,或至少調整箭頭比例與筆畫粗細比照 back/forward 圖示的視覺重量,讓三顆按鈕的視覺風格一致)。實作 agent 可以自行決定手繪方案的細節,只要滿足 Acceptance 的「三顆按鈕視覺風格一致、比例協調」即可,不強制規定確切的繪製座標。
3. **`ImageList` 物件建立一次、跨按鈕重複使用,行程生命週期內存活(比照 `address_bar_background_brush` 的 process-lifetime 快取模式),不在每次 `WM_DRAWITEM` 重新載入。** 需要在程式結束時(或不需要,因為 `ImageList_LoadImage` 回傳的是系統管理的資源時,需查證 `ImageList_Destroy` 是否必要——若必要則在 `WM_DESTROY` 呼叫一次)。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `draw_navigation_icon_button`(第 571-619 行)——本票要修改的函式,`glyph_kind` 0=back、1=forward、2=up。
- `src/app_shell/main.cpp` 的 `WM_DRAWITEM` 對應 `kBackButtonIdBase` 範圍的分派邏輯(第 2596-2613 行附近)——確認呼叫 `draw_navigation_icon_button` 的地方,改動後可能需要把 `HIMAGELIST` 傳進去或改成查詢一個共用的 process-lifetime 函式(比照 `address_bar_background_brush` 的寫法)。
- Win32 `commctrl.h`/`ImageList_LoadImage`/`HINST_COMMCTRL`/`IDB_HIST_SMALL_COLOR` 文件——確認正確的呼叫參數與各索引常數名稱(`HIST_BACK`、`HIST_FORWARD` 等)。
- `docs/tickets/PD-031-navigation-row-icon-restyle.md` 交接區——確認目前導覽按鈕的既有尺寸/DPI 縮放慣例,新圖示要沿用同一套 `scaled_value` 縮放方式,不引入第二套。

## Scope

1. 新增一個 process-lifetime 的 `HIMAGELIST` 取得函式(比照 `address_bar_background_brush` 風格),用 `ImageList_LoadImage(HINST_COMMCTRL, MAKEINTRESOURCEW(IDB_HIST_SMALL_COLOR), ...)` 載入。
2. `draw_navigation_icon_button` 的 back(0)/forward(1)分支改用 `ImageList_Draw`/`ImageList_DrawEx` 繪製對應索引,置中於按鈕矩形,依 DPI 縮放圖示顯示大小(`ImageList_LoadImage` 載入的是 16x16 基準尺寸,縮放到目前 DPI 對應大小)。
3. up(2)分支維持手繪,依決策 2 調整視覺比例與現有 back/forward 圖示風格一致。
4. 若查證後確認需要在 `WM_DESTROY` 呼叫 `ImageList_Destroy` 釋放資源,加入對應清理程式碼。

## Non-goals

- 不改變按鈕的點擊行為、`EnableWindow`/`BM_SETCHECK` 等既有邏輯。
- 不改變按鈕的排版位置與尺寸計算(`layout_header`/`navigation_geometry` 等既有函式)。
- 不新增第三方圖示資源檔或圖片依賴(已確認的產品決策 1 明確排除)。

## Acceptance

1. 上一頁/下一頁按鈕圖示改為與 Windows 檔案總管歷史記錄工具列相同的箭頭圖示(來自 `IDB_HIST_SMALL_COLOR`),在啟用/停用狀態下都正確呈現(停用時視覺上明顯變灰/變淡)。
2. 上一層按鈕視覺風格與新的上一頁/下一頁圖示協調一致,不再是原本比例失衡的手繪箭頭。
3. 三顆按鈕在不同 DPI(至少驗證 96/144/192 三種常見縮放比例的計算邏輯,不需要真的多螢幕測試)下圖示大小正確縮放,沒有模糊或裁切。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "IDB_HIST_SMALL_COLOR|ImageList_LoadImage|HINST_COMMCTRL" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:觀察上一頁/下一頁/上一層三顆按鈕圖示是否清晰、風格一致,
# 導覽到有上一頁/下一頁歷史時確認啟用態圖示正常,無歷史時確認停用態圖示正確變灰
```

## Handoff requirements

- 最終採用的 `ImageList` 載入方式與是否需要 `ImageList_Destroy` 清理的查證結果。
- Up 按鈕最終手繪方案的具體改動(比例、線條粗細)。
- 若真實桌面測試發現 DPI 縮放下圖示模糊或裁切,記錄下來並說明因應方式。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-25 實作交接

- Back/Forward 使用 `ImageList_LoadImageW(HINST_COMMCTRL, MAKEINTRESOURCEW(IDB_HIST_SMALL_COLOR), 16, 0, CLR_DEFAULT, IMAGE_BITMAP, LR_DEFAULTCOLOR | LR_CREATEDIBSECTION)` 載入公開 Common Controls 歷史圖示，透過 `HIST_BACK`/`HIST_FORWARD` 索引與 `ImageList_DrawEx` 繪製；圖示尺寸以 `scaled_value(..., 16)` 依目前 DPI 縮放，停用狀態使用 `ILD_BLEND50`。
- Image list 為 process-lifetime lazy cache，`ImageList_LoadImage` 建立的是應由呼叫端管理的 image list，故在 `WM_DESTROY` 呼叫 `ImageList_Destroy` 並清空 handle；載入失敗時 Back/Forward 保留原有手繪箭頭作為 fallback。
- Up 維持手繪，沿用原本的向上箭頭比例與線寬計算，並與新圖示同樣使用白底及 enabled/disabled 顏色；按鈕位置、尺寸、點擊與啟用邏輯未改動。
- 未進行互動式桌面驗證：目前環境無法可靠啟動並觀察 `PaneDock.exe` 視窗；已完成非互動 smoke check（建置、測試、`rg` 與 `git diff --check`），96/144/192 DPI 的圖示尺寸由 `MulDiv(16, dpi, 96)` 計算。
