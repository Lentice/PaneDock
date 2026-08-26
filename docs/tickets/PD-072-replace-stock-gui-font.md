# PD-072 — chrome 字型:改用固定的 Latin 字面加系統字型連結,取代語系相依的 `DEFAULT_GUI_FONT`/`lfMessageFont`

Phase 7 · app_shell · Depends on: PD-061

- Source: dispatcher 於複驗 PD-061 時發現(2026-08-26)。放大 3 倍的側邊欄截圖顯示 Group 名稱已正確改為粗體無襯線,但同一張圖中的品牌標題「PaneDock」仍是**襯線體**,與設計稿完全不符。
- Origin: PD-061 複驗的衍生發現(非使用者原文項)。
- Priority: HIGH——影響整個應用程式幾乎所有文字的外觀。襯線體那一半只在中文語系的 Windows 上發生(英文語系看不出來,所以不能靠「我的機器看起來還可以」結案);而「同一份 UI 在不同語系長得不一樣」則是兩種語系都存在的問題。
- 追加需求(2026-08-26,使用者原文):「字型最好是採用通用的英文字型,只有中文採用中文字型,避免英文環境下顯示會有差異。」——此項改變了原本「直接沿用 `lfMessageFont`」的作法,見決策 1。

## 已確認的根因(有實測數值,不是猜測)

### `DEFAULT_GUI_FONT` 在本機解析成 PMingLiU

以 `GetObjectW` 讀出兩個字型的 `LOGFONTW` 實測:

```
DEFAULT_GUI_FONT (GetStockObject(17))
    lfFaceName = "PMingLiU"                 lfHeight = -12  lfWeight = 400

NONCLIENTMETRICS.lfMessageFont (SystemParametersInfoW SPI_GETNONCLIENTMETRICS)
    lfFaceName = "Microsoft JhengHei UI"    lfHeight = -12  lfWeight = 400
```

**PMingLiU 是繁體中文的明體(襯線體)**,不是 UI 字型。`DEFAULT_GUI_FONT` 是 Win16 時代留下的 stock 物件,微軟自 Windows 2000 起就在文件中不建議用它做 UI 字型;在中文語系的 Windows 上它被對應到 PMingLiU,拉丁字母因此渲染成襯線,這正是品牌標題「PaneDock」看起來像 Times 的原因。正確的 UI 字型是 `lfMessageFont`,在本機是 `Microsoft JhengHei UI`。

**注意這是語系相依的:** 在英文語系的 Windows 上 `DEFAULT_GUI_FONT` 通常對應到 MS Shell Dlg / Tahoma,外觀差異小得多。因此這個缺陷在英文機器上測不出來,不能因為「在我的機器上看起來還可以」就結案。

### PD-061 只修了側邊欄的清單項目

PD-061(commit `3e8af43`)在 `src/sidebar/sidebar.cpp` 第 160-173 行改用 `SystemParametersInfoForDpi` 取 `lfMessageFont`,方向正確(脫離 stock 字型),但 face 仍是語系相依的,**因此本票的決策 1 會覆寫它的 face 來源**;PD-061 的字級、字重與列高計算保留。`src/app_shell/main.cpp` 則完全沒改,仍有 **9 處** `GetStockObject(DEFAULT_GUI_FONT)`:

| 行 | 用途 |
|---|---|
| 1090 | (以 `rg` 確認實際用途後填寫) |
| 1294 | `brand_font()` 的基底——**品牌標題「PaneDock」,肉眼最明顯的一處** |
| 2693 | 「GROUPS」標題 STATIC |
| 2707 | tab strip |
| 2726 | (待確認) |
| 2776 | (待確認) |
| 2802 | (待確認) |
| 2827 | (待確認) |
| 2846 | 網址列 EDIT |
| 2855 | pane 狀態列 STATIC |

**實作時請以 `rg -n "DEFAULT_GUI_FONT" src\` 取得當下的完整清單,不要信任上表的行號**——PD-062 等票可能已經改動行號。上表的用意是說明範圍有多大,不是精確座標。

## 已確認的產品決策

1. **字面(face)一律固定為 `Segoe UI`,字級與字重仍取自 `SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, ...)` 的 `lfMessageFont`。**

   > **本項覆寫 PD-061 的字型來源決策。** PD-061 直接沿用 `lfMessageFont` 的 `lfFaceName`,那在中文 Windows 上是 `Microsoft JhengHei UI`、在英文 Windows 上是 `Segoe UI`——**同一個應用程式在不同語系會長得不一樣**。使用者於 2026-08-26 明確要求:「字型最好是採用通用的英文字型,只有中文採用中文字型,避免英文環境下顯示會有差異。」因此改為釘住 Latin 字面,CJK 交給系統的字型連結(font linking)。PD-061 的其他成果(字級、`FW_BOLD` 的名稱、依字型度量重算的列高)**全部保留**。

   具體寫法:取 `lfMessageFont` 之後只覆寫三個欄位,其餘(尤其 `lfHeight`)不動——這樣使用者在系統設定裡調整的 UI 字級與目前 DPI 都仍然生效:

   ```cpp
   LOGFONTW logfont = metrics.lfMessageFont;   // keeps lfHeight / lfWeight
   // PD-072: pin the Latin face so the app looks identical on every
   // locale. lfMessageFont's face is locale-dependent (Microsoft
   // JhengHei UI on zh-TW, Segoe UI on en-US). DEFAULT_CHARSET lets GDI
   // font linking substitute a CJK face for glyphs Segoe UI lacks, so
   // Chinese folder names still render.
   wcscpy_s(logfont.lfFaceName, L"Segoe UI");
   logfont.lfCharSet = DEFAULT_CHARSET;
   logfont.lfQuality = CLEARTYPE_QUALITY;
   ```

   **`lfCharSet = DEFAULT_CHARSET` 是這個作法能成立的關鍵**,不要改成 `ANSI_CHARSET`——那會關掉字型連結,中文檔名會變成豆腐方塊。

1a. **`Segoe UI` 不存在時,退回 `lfMessageFont` 原本的 face。** Segoe UI 自 Windows Vista 起隨系統提供,實務上一定在;但精簡版 Windows 映像可能缺字型,一個 `EnumFontFamiliesExW` 或建立後以 `GetTextFaceW` 比對實際 face 的檢查就夠了,不要因此加設定項或安裝字型。

1b. **不要硬寫字級。** `lfHeight` 一律沿用 `lfMessageFont` 的值,不要寫成 `-12` 這類常數——那會讓使用者的「文字大小」系統設定失效。

2. **新增一個共用的取字型函式,不要在 9 個地方各寫一次。** 放在 `main.cpp` 既有的字型輔助函式附近(`brand_font()` 旁邊),簽名比照現有慣例:

   ```cpp
   // PD-072: DEFAULT_GUI_FONT is a Win16 stock object that resolves to
   // PMingLiU (a serif Ming face) on Chinese Windows, so every control
   // using it rendered Latin text with serifs. lfMessageFont is the
   // system's actual UI font.
   HFONT ui_font(HWND window) noexcept;
   ```

   `brand_font()` 改成以這個函式的 `LOGFONTW` 為基底再套 `FW_BOLD`,而不是以 stock 物件為基底。

3. **字型快取的生命週期比照 `brand_font()` 既有的作法,並且必須能因應 DPI 變化。** `brand_font()` 目前用 function-local `static` 只建一次,在 Per-Monitor-V2 下跨螢幕移動時字級不會跟著變。**本票必須解決這一點**:與 PD-061 在 `sidebar.cpp` 採取的策略一致(每次繪製時依當下 DPI 取字型,不做跨 DPI 的快取),或改為以 DPI 為鍵的快取。**採哪一種由實作者依 `sidebar.cpp` 的既有作法決定,並在交接區寫明理由與釋放路徑。** 既有的 `release_navigation_refresh_font()` 之類的釋放函式是既有慣例,新字型若需要釋放請比照。

4. **不改任何字級、字重或顏色。** 本票只換字型家族(face)。字級目前是 `lfHeight = -12`,兩個字型相同,所以換 face 不應改變版面高度;若實測發現行高改變導致某個控制項截字,**在交接區記錄,不要順手改高度常數**——高度屬於 PD-069 與各元件的票。

5. **狀態列與網址列也要換。** 它們顯示的是檔案系統路徑與 `n items` 這類拉丁文數字混排,襯線體在小字級下最難讀。

6. **不改 Explorer view 內部的字型。** 那是原生 Shell view 自己繪製的,不在我們控制範圍內(AGENTS.md:file list 不重刻)。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

——本票新增一個共用函式是為了取代 9 處重複,屬於「減少重複」而非「新增抽象」。

`AGENTS.md`:
> A guard in the shared function is a smaller diff than a guard in every caller, and patching only the path the ticket names leaves sibling callers broken.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`:
> **App UI text must be English.** No Chinese strings ship in the binary.

——本票不新增任何字串;字型名稱來自系統,不是硬寫的字串常數。

`AGENTS.md`:
> The file list is never reimplemented.

——見決策 6。

## Files to read and trace first

- `src/sidebar/sidebar.cpp` 第 155-180 行——PD-061 建立字型的位置。**本票必須一併修改這裡的 face(決策 1 的覆寫),但要保留 PD-061 的字級、`FW_BOLD` 與列高計算。**
- `src/app_shell/main.cpp` `brand_font()`(約第 1290-1302 行)——決策 2 的改造對象。
- `src/app_shell/main.cpp` 全部 `GetStockObject(DEFAULT_GUI_FONT)` 的位置,以 `rg -n "DEFAULT_GUI_FONT" src\` 取得。
- `src/app_shell/main.cpp` `release_navigation_refresh_font()` 與其他 `release_*` 函式,以及它們在 `WM_DESTROY` 的呼叫點——決策 3 的釋放慣例。
- `src/app_shell/main.cpp` `WM_DPICHANGED` 的處理——確認 DPI 變化時的重繪路徑。
- `docs/tickets/PD-061-sidebar-group-typography.md` 的交接區——PD-061 對「不做跨 DPI 快取」的理由,直接沿用。

## Scope

1. 新增共用的 `ui_font(HWND)`:以 `SystemParametersInfoForDpi` 的 `lfMessageFont` 為基礎,face 覆寫為 `Segoe UI`、`lfCharSet = DEFAULT_CHARSET`、`lfQuality = CLEARTYPE_QUALITY`,並含 Segoe UI 不存在時的退回。
2. `main.cpp` 全部 `GetStockObject(DEFAULT_GUI_FONT)` 的使用點改用它。
3. `brand_font()` 改以 `ui_font` 的 `LOGFONTW` 為基底套用 `FW_BOLD`。
4. `src/sidebar/sidebar.cpp` 的兩個字型(名稱、副標題)同樣改為固定 `Segoe UI` 的 face,**保留 PD-061 的字級、字重與列高計算**。
5. 依 `sidebar.cpp` 的既有策略處理 DPI 變化與字型釋放。

## Non-goals

- 不改 PD-061 決定的字級、字重與側邊欄列高;本票在側邊欄只改 face。
- 不改任何其他字級、字重(除 `brand_font()` 既有的 `FW_BOLD`)或文字顏色。
- 不硬寫 `lfHeight`,不繞過使用者的系統文字大小設定。
- 不加字型的使用者設定項,不隨程式安裝或內嵌字型。
- 不改任何高度、內距或間距常數(PD-069)。
- 不改 tab 的視覺樣式(PD-062)。
- 不碰 Explorer view 內部的字型。

## Acceptance

1. **品牌標題「PaneDock」以無襯線的系統 UI 字型渲染**,實機以 `PrintWindow` 截圖後放大至少 3 倍判讀,與修改前的襯線外觀對照。
2. 「GROUPS」標題、tab 文字、網址列路徑、pane 狀態列文字全部改為系統 UI 字型,同樣以放大截圖確認。
3. **沒有任何控制項因換字型而截字或溢出**;特別檢查 tab(寬度受 `kTabMinWidth`/`kTabMaxWidth` 限制)與 pane 狀態列。
4. **中文檔名/路徑仍正確渲染,沒有豆腐方塊。** 導覽到一個含中文名稱的資料夾(例如 `C:\Users\公用`,或自建 `測試資料夾`),確認 tab 標題與網址列的中文字顯示正常。**這是決策 1 最重要的驗收:`DEFAULT_CHARSET` 一旦寫錯成 `ANSI_CHARSET`,中文就會變方塊。**
4a. 側邊欄 Group 名稱與副標題的**字級、字重與列高與 PD-061 完成時一致**,只有 face 改變。
4b. **同一份 UI 在中文與英文語系的 Windows 上,拉丁文字的字面相同。** 若無法取得英文語系環境,以 `GetTextFaceW` 讀回實際 face 為 `Segoe UI` 作為替代證據,並在交接區說明。
5. 在 150% 或 200% DPI 下字級按比例變化;若本機只有單一 DPI,至少確認取字型的路徑經過 `SystemParametersInfoForDpi` 並傳入當下的 DPI,並在交接區說明未能實測的原因。
6. 關閉程式不洩漏字型物件:若採用快取,`WM_DESTROY` 的釋放路徑有對應的 `DeleteObject`;若不快取,每次使用後釋放。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
8. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 完成後這條應該只剩下註解中的提及,不再有實際呼叫
rg -n "DEFAULT_GUI_FONT" src\
rg -n "SystemParametersInfoForDpi|lfMessageFont|ui_font|brand_font" src\
git diff --check
```

**字型實測方法(本票已驗證可用,請直接沿用):** 用 `GetObjectW` 把 `HFONT` 讀回 `LOGFONTW` 比對 `lfFaceName`,**這比看截圖可靠**。P/Invoke 宣告 `LOGFONTW` 時 `lfFaceName` 必須是 `[MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)]` 且結構標 `CharSet=CharSet.Unicode`,否則取回亂碼。

**截圖驗證方法:** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,放大時用 `InterpolationMode.NearestNeighbor` 以免插值把襯線抹掉。控制項座標用 `GetDlgItem` + `GetWindowRect` 取真實幾何,不要目測。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 改動的每一處位置與數量(應與 `rg` 修改前的計數相符)。
- 修改前後,以 `GetObjectW` 讀出的 `lfFaceName` 對照(至少品牌標題與狀態列各一)。
- 放大後的品牌標題截圖判讀結果。
- DPI 變化的處理策略與理由,以及字型的釋放路徑。
- 若發現任何控制項因換字型而截字,寫明位置與量測值,**不要順手改高度常數**。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-26 撰票時的既有證據(dispatcher 記錄,供實作者直接引用)

- 本機 `GetStockObject(DEFAULT_GUI_FONT)` → `lfFaceName = "PMingLiU"`、`lfHeight = -12`、`lfWeight = 400`。
- 本機 `NONCLIENTMETRICS.lfMessageFont` → `lfFaceName = "Microsoft JhengHei UI"`、`lfHeight = -12`、`lfWeight = 400`。
- 兩者 `lfHeight` 相同,因此換 face 預期不改變版面高度;決策 4 的「若截字則記錄不要改高度」是保險條款,不是預期會發生。
- 撰票時的 3 倍放大側邊欄截圖顯示:Group 名稱與副標題已是無襯線(PD-061 的成果),但同一張圖的品牌標題「PaneDock」明顯是襯線體。這是同一張截圖內的直接對照,可作為修改前的基準。

### 2026-08-26 決策 1 的可行性實測(dispatcher,使用者要求改為固定 Latin 字面後補做)

用**真正的 GDI 路徑**(`CreateFontIndirectW` + `SelectObject` + `DrawTextW`,不是 GDI+)驗證「face 固定 Segoe UI、`lfCharSet = DEFAULT_CHARSET`」能否同時顯示拉丁與中文:

| 請求的 face | `GetTextFaceW` 回報 | 文字 | 墨跡取樣點數 | 目視結果 |
|---|---|---|---|---|
| `Segoe UI` | `Segoe UI` | `PaneDock` | 283 | 正常,無襯線 |
| `Segoe UI` | `Segoe UI` | `測試中文資料夾` | 550 | **中文正常顯示,不是豆腐方塊**(已存圖目視確認) |
| `Microsoft JhengHei UI` | `Microsoft JhengHei UI` | `PaneDock` | 336 | 正常 |
| `Microsoft JhengHei UI` | `Microsoft JhengHei UI` | `測試中文資料夾` | 526 | 正常 |

**結論:GDI 的字型連結(font linking)確實會為 Segoe UI 缺少的 CJK 字符自動代換中文字型**,拉丁部分仍由 Segoe UI 渲染。墨跡數與原生中文字型相當(550 vs 526),排除「畫成空框但仍有墨跡」的可能;兩張圖都已目視確認。`Segoe UI` 在本機存在(`InstalledFontCollection` 確認)。

另補一筆:`InstalledFontCollection` 列不出 `PMingLiU`(GDI+ 以不同的家族名列舉),但 GDI 的 `GetStockObject(DEFAULT_GUI_FONT)` 確實回報 `lfFaceName = "PMingLiU"`。**判斷字型時請用 GDI 的 `GetObjectW`/`GetTextFaceW`,不要用 GDI+ 的字型列舉**——本專案的繪製走的是 GDI。
