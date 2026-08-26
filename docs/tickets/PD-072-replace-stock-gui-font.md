# PD-072 — 除側邊欄以外的 chrome 全都用 `DEFAULT_GUI_FONT`,在中文 Windows 上被解析成襯線字型 PMingLiU

Phase 7 · app_shell · Depends on: PD-061

- Source: dispatcher 於複驗 PD-061 時發現(2026-08-26)。放大 3 倍的側邊欄截圖顯示 Group 名稱已正確改為粗體無襯線,但同一張圖中的品牌標題「PaneDock」仍是**襯線體**,與設計稿完全不符。
- Origin: PD-061 複驗的衍生發現(非使用者原文項)。
- Priority: HIGH——影響整個應用程式幾乎所有文字的外觀,而且只在中文語系的 Windows 上發生,英文語系看不出來。

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

PD-061(commit `3e8af43`)在 `src/sidebar/sidebar.cpp` 第 160-173 行改用 `SystemParametersInfoForDpi` 取 `lfMessageFont`,那部分是正確的、**本票不要改動它**。但 `src/app_shell/main.cpp` 仍有 **9 處** `GetStockObject(DEFAULT_GUI_FONT)`:

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

1. **一律改用 `SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, ...)` 的 `lfMessageFont`,與 PD-061 在側邊欄採用的作法一致。** 用 `ForDpi` 版本而不是 `SystemParametersInfoW`,因為前者會回傳對應該 DPI 的字型高度,符合 AGENTS.md 的 Per-Monitor-V2 要求。

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

- `src/sidebar/sidebar.cpp` 第 155-180 行——**PD-061 的正確作法,本票照抄這個模式。不要修改這個檔案。**
- `src/app_shell/main.cpp` `brand_font()`(約第 1290-1302 行)——決策 2 的改造對象。
- `src/app_shell/main.cpp` 全部 `GetStockObject(DEFAULT_GUI_FONT)` 的位置,以 `rg -n "DEFAULT_GUI_FONT" src\` 取得。
- `src/app_shell/main.cpp` `release_navigation_refresh_font()` 與其他 `release_*` 函式,以及它們在 `WM_DESTROY` 的呼叫點——決策 3 的釋放慣例。
- `src/app_shell/main.cpp` `WM_DPICHANGED` 的處理——確認 DPI 變化時的重繪路徑。
- `docs/tickets/PD-061-sidebar-group-typography.md` 的交接區——PD-061 對「不做跨 DPI 快取」的理由,直接沿用。

## Scope

1. 新增共用的 `ui_font(HWND)`,以 `SystemParametersInfoForDpi` 的 `lfMessageFont` 為來源。
2. `main.cpp` 全部 `GetStockObject(DEFAULT_GUI_FONT)` 的使用點改用它。
3. `brand_font()` 改以 `ui_font` 的 `LOGFONTW` 為基底套用 `FW_BOLD`。
4. 依 `sidebar.cpp` 的既有策略處理 DPI 變化與字型釋放。

## Non-goals

- 不改 `src/sidebar/sidebar.cpp`(PD-061 已正確)。
- 不改任何字級、字重(除 `brand_font()` 既有的 `FW_BOLD`)或文字顏色。
- 不改任何高度、內距或間距常數(PD-069)。
- 不改 tab 的視覺樣式(PD-062)。
- 不碰 Explorer view 內部的字型。
- 不新增字型的使用者設定項。

## Acceptance

1. **品牌標題「PaneDock」以無襯線的系統 UI 字型渲染**,實機以 `PrintWindow` 截圖後放大至少 3 倍判讀,與修改前的襯線外觀對照。
2. 「GROUPS」標題、tab 文字、網址列路徑、pane 狀態列文字全部改為系統 UI 字型,同樣以放大截圖確認。
3. **沒有任何控制項因換字型而截字或溢出**;特別檢查 tab(寬度受 `kTabMinWidth`/`kTabMaxWidth` 限制)與 pane 狀態列。
4. 側邊欄 Group 名稱與副標題的外觀**與 PD-061 完成時完全一致**(本票不應改變它們)。
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
