# PD-040 — Pane 卡片四角圓角(覆寫 PD-030 決策:下緣維持方角)

Phase 6 · app_shell, explorer_host · Depends on: PD-030, PD-033

- Source: 使用者比對 `.\build\PaneDock.exe` 實際畫面與理想稿後回報(2026-08-25)。
- Origin: 「pane 上方有 radius,下方卻變成尖角」。
- Priority: LOW——純視覺一致性問題,不影響功能。

## 覆寫聲明(Override)

本票覆寫 `docs/tickets.md` §候選(尚未開 ticket) 記錄的既有結論:

> Pane 卡片只圓上緣,下緣(貼著真實 `IExplorerBrowser` 內容)維持方角,不做完整四角圓角(PD-030):四角圓角需要裁切 Shell view 本身的 HWND,目前沒有安全的做法……若之後要做完整四角圓角,觸發條件是「先有一個能安全裁切 `IExplorerBrowser` HWND 而不影響其生命週期與 site 契約的具體方案」。

**新證據:** 該結論假設的前提是「必須裁切 `IExplorerBrowser` 本身的 HWND」。實際上不需要裁切 `IExplorerBrowser` 的 HWND——`AGENTS.md` 已預先允許的做法是「幫每個 pane 加一層外層容器 HWND」;把 `ExplorerHost::initialize` 的 `parent` 參數從目前的主視窗(`window`)換成一個新增的、每個 pane 專屬的容器子視窗,對**這個容器 HWND**呼叫 `SetWindowRgn`(`CreateRoundRectRgn` 產生的區域)。`IExplorerBrowser` 物件本身、它建立的內部 Shell view 子視窗、`Advise`/`Unadvise`/`Destroy` 生命週期與 site 契約完全不受影響——它只是被裝在一個外框被裁成圓角的容器裡,容器裁切只影響繪製範圍,不影響任何 COM 介面或子視窗的存在與行為。這符合觸發條件實質要求(安全裁切、不影響生命週期與 site 契約),只是手段是「加一層容器」而非「直接裁 Shell HWND」,因此依 `AGENTS.md` 的 ticket 覆寫規則在此明確覆寫並提供新方案。

## 已確認的產品決策

1. **`ExplorerHost::initialize` 的 `parent` 改為每個 pane 專屬的一個新容器子視窗(`WS_CHILD | WS_CLIPCHILDREN`),而不是直接掛在主視窗下。** 容器視窗的 rect 與目前傳給 `initialize`/`set_rect` 的 pane rect 相同,容器本身不繪製任何內容(純粹作為裁切邊界＋子視窗宿主),`IExplorerBrowser` 內部建立的 Shell view 子視窗填滿這個容器。
2. **容器視窗的區域(`HRGN`)在建立與每次 `WM_SIZE`/`WM_DPICHANGED` 造成 rect 改變時,用 `CreateRoundRectRgn` 依 `draw_pane_card` 現有的 `radius`(96-DPI 基準 10px,經 `MulDiv` 縮放)重新計算並 `SetWindowRgn`。** 圓角半徑必須與 `draw_pane_card` 畫的白色卡片背景圓角完全一致,否則會出現「裁切邊界」與「卡片背景圓角」對不齊的鋸齒狀縫隙——兩者共用同一個 `radius` 計算式(必要時把 `draw_pane_card` 裡 `radius` 的計算抽成一個小函式共用,依 `AGENTS.md`「Reuse existing code before adding helpers」判斷是否值得抽,若只有兩處用到且各自簡單,直接各自呼叫 `MulDiv(10, dpi, 96)` 也可接受,不強制抽象)。
3. **`draw_pane_card` 的「下緣方角」邏輯(`FillRect` 覆蓋下半部圓角、邊框畫直角轉角的部分)全部移除,四個角都用 `RoundRect`/圓角邊框繪製,不再有方角特例。** 這是 PD-030 決策的直接反轉,PD-030 交接區記錄的「下緣鋸齒/方角」相關程式碼(第 1178-1182、1198-1201 行的方角覆蓋與直角轉角繪製)本票要對應刪除。
4. **`SetWindowRgn` 只套用在新增的容器視窗上,不套用在主視窗或其他既有控制項。** 不影響 tab strip、導覽列、地址列等現有子視窗的裁切範圍。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`(候選段落已在覆寫聲明引用的原句,允許新增容器 HWND):
> Pane 卡片若要圓角/陰影,需評估是否需要幫每個 pane 加一層外層容器 HWND 來做圓角裁切或自訂繪製背景卡片,允許新增此容器(大幅修改可接受)

`docs/tickets/PD-030-pane-card-chrome-and-tab-header-restyle.md`(交接區,本票要修改其記錄的下緣方角邏輯):
> Square off the bottom two corners: only the self-drawn header is rounded, the area flush with the Shell view stays rectangular.

## Files to read and trace first

- `src/app_shell/main.cpp` 第 1414-1440 行附近(`apply_layout` 呼叫 `state.explorers[index].initialize(window, rect, ...)` 的位置)——確認目前 `parent` 就是主視窗 `window`,以及 rect 如何計算(要在容器視窗上重現同一個 rect,再讓 `initialize` 改吃容器視窗當 `parent`,rect 相對容器視窗改成 `{0, 0, width, height}`)。
- `src/explorer_host/explorer_host.h`/`.cpp` 的 `initialize`/`set_rect`/`destroy`——確認 `parent_` 欄位與銷毀順序,新增容器視窗後必須確保銷毀順序是「先 `ExplorerHost::destroy()`(內部 view)再銷毀容器 HWND」,不能反過來(對應 `AGENTS.md`「Never destroy a parent HWND while a view is alive」)。
- `src/app_shell/main.cpp` 的 `draw_pane_card`(第 1138-1206 行)——本票要修改的方角覆蓋邏輯所在。
- `docs/tickets/PD-030-pane-card-chrome-and-tab-header-restyle.md` 交接區——PD-030 選定的 `radius`/`shadow_offset`/`outset` 數值,本票延用,不重新設計卡片視覺參數。
- `AppState` 結構(`src/app_shell/main.cpp` 第 300 行附近)——確認是否需要新增一個 `std::array<HWND, kExplorerCount>` 存放容器視窗控制代碼(比照既有 `explorers`/`tab_strips` 陣列風格)。

## Scope

1. `AppState` 新增每個 pane 一個容器 HWND(自訂 window class 或直接用 `L"STATIC"`/一個新註冊的簡單 window class,取決於是否需要攔截訊息——若容器不需要處理任何訊息,`WC_STATICW` 加 `WS_CLIPCHILDREN` 即足夠,不需要新的 window class,依 `AGENTS.md` YAGNI 判斷選最簡單的)。
2. `apply_layout`/`WM_CREATE` 建立容器視窗,`ExplorerHost::initialize` 改吃容器視窗當 `parent`,rect 相對容器視窗歸零。
3. 容器視窗建立與每次 rect 改變時,用 `CreateRoundRectRgn`(依決策 2 的半徑)`SetWindowRgn`。
4. `draw_pane_card` 移除下緣方角覆蓋邏輯,四角一致使用 `RoundRect`。
5. 銷毀順序調整:先 `ExplorerHost::destroy()`,確認完成後再銷毀容器 HWND(若容器隨主視窗一起被系統銷毀,需確認既有 `WM_DESTROY`/程式關閉路徑的呼叫順序沒有反過來)。

## Non-goals

- 不改變 `IExplorerBrowser` 的 `Advise`/`Unadvise`/site 實作,只改它被裝在哪個 HWND 底下。
- 不改變 tab strip、導覽列、地址列的裁切範圍或視覺(它們目前是主視窗的直接子視窗,不搬進新容器)。
- 不做卡片陰影或邊框的其他視覺調整(半徑、顏色沿用 PD-030/PD-033 既有數值)。

## Acceptance

1. 每個 pane 卡片四個角都是圓角,視覺上與 `draw_pane_card` 畫的白色背景圓角完全貼合,沒有裁切邊界與背景圓角對不齊的鋸齒。
2. 真實 Shell 檔案列表內容(icons、選取、右鍵選單)在圓角容器內正常顯示與互動,沒有因為 `SetWindowRgn` 裁到內容或選取高亮異常。
3. 切換 Group、切換版面配置、`WM_DPICHANGED` 後圓角裁切範圍正確跟隨新的 pane rect 與 DPI。
4. 程式正常關閉(`WM_CLOSE`)與系統關機(`WM_ENDSESSION`,PD-032)時沒有因為新增的容器 HWND 導致 `IExplorerBrowser` 未正確 `Destroy` 或發生崩潰。
5. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
6. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "SetWindowRgn|CreateRoundRectRgn" src\app_shell\main.cpp src\explorer_host
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:確認四個 pane 卡片四角皆為圓角、Shell 檔案列表可正常瀏覽/選取/右鍵;
# 切換 Group/layout、改變視窗大小、正常關閉程式,確認沒有崩潰或殘留視窗
```

## Handoff requirements

- 容器 HWND 最終採用的 window class 選擇(是否用 `WC_STATICW` 或新註冊 class)與理由。
- `SetWindowRgn` 套用時機的最終清單(建立時、`WM_SIZE`、`WM_DPICHANGED`,是否還有其他觸發點)。
- 銷毀順序的最終實作方式與驗證結果(是否有在關閉程式時觀察到任何 `IExplorerBrowser` 未正確銷毀或殘留視窗的跡象)。
- 若真實桌面測試發現 `SetWindowRgn` 造成 Shell view 的原生右鍵選單、拖放高亮或捲軸繪製超出裁切範圍出現視覺瑕疵,記錄下來並說明因應方式(或誠實記錄為已知限制,不強行在本票內解決所有邊角案例)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
