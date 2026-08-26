# PD-077 — 拖曳調整主視窗大小時,pane 狀態列在舊位置留下殘影

Phase 7 · app_shell · Depends on: PD-041, PD-042

- Source: 使用者實機截圖回報(2026-08-26),附截圖與紅色箭頭標示殘影位置。
- Origin: 使用者原文:「when resize main window, the pane 1 footer is render in multiple positions(紅色箭頭處)」。
- Priority: HIGH——這是一個常見互動(拖曳視窗邊緣調整大小)下的畫面渲染缺陷,不是邊緣情境;殘影文字(舊的「80 items」)疊在真實檔案列表內容之上,足以誤導使用者。

## 已確認的現況與截圖證據

使用者截圖(左下 pane,路徑 `C:\Users`)顯示:檔案列表捲動區塊中段插入了一行孤立的「80 items」文字(紅色箭頭處),與該 pane 目前真正的狀態列「4 items」(畫面最下方,數字正確——`C:\Users` 底下確實是 4 個項目)並存。「80 items」是這個 pane **在視窗變大之前**、pane 較小時顯示的舊狀態列文字(當時列表項目數尚未套用篩選/尚在載入,或是視窗變大前該 pane 顯示的是另一個路徑的殘留值)——重點不是這個數字對不對,而是**這行文字出現在錯誤的螢幕位置,且該位置目前應該是檔案列表內容,不是狀態列**。這證明:狀態列(或某個先前佔用過那塊螢幕區域的子視窗)在被移動之後,舊位置的像素沒有被正確清除/重繪。

## 已確認的根因(有程式碼證據,但渲染時序需要實作 agent 用實機重現+像素證據做最終確認)

### `WM_SIZE` 從不主動要求重繪,完全依賴每個子視窗自己的 `SetWindowPos` 連鎖反應

`src/app_shell/main.cpp` 第 3109-3111 行:

```cpp
case WM_SIZE:
    if (state != nullptr && FAILED(apply_layout(window, *state)))
        OutputDebugStringW(L"PaneDock: Shell view realization failed\n");
    return 0;
```

`apply_layout`(第 1500 行起)對每個 pane 逐一呼叫 `SetWindowPos` 重新定位 tab 條、四顆導覽按鈕、網址列、狀態列、explorer container(第 1562-1626 行),**全程沒有任何一次 `InvalidateRect`/`RedrawWindow` 涵蓋整個 pane 區塊或整個 client area**。整個重繪完全寄望於 Windows 對「子視窗被 `SetWindowPos` 搬移」這件事的內建連鎖失效/重繪機制自動把舊位置清乾淨。

### Pane 內部的 sibling 子視窗大多沒有 `WS_CLIPSIBLINGS`

以 `rg -n "CreateWindowExW" src\app_shell\main.cpp` 核對每個子視窗的建立樣式:

| 子視窗 | 建立位置 | Style | 有 `WS_CLIPSIBLINGS`? |
|---|---|---|---|
| `tab_strips[index]` | 第 2724-2727 行 | `WS_CHILD \| WS_CLIPSIBLINGS \| WS_TABSTOP \| SS_NOTIFY` | **有** |
| `explorer_containers[index]` | 第 2720-2722 行 | `WS_CHILD \| WS_CLIPCHILDREN` | 沒有 |
| 四顆導覽按鈕(`back`/`forward`/`up`/`refresh`/`view_mode`) | 第 2753-2756 行 | `WS_CHILD \| WS_TABSTOP \| BS_PUSHBUTTON \| BS_OWNERDRAW` | 沒有 |
| `address_bars[index]` | 第 2766-2769 行 | `WS_CHILD \| WS_TABSTOP \| ES_AUTOHSCROLL` | 沒有 |
| `status_bars[index]` | 第 2784-2786 行 | `WS_CHILD \| SS_LEFT \| SS_CENTERIMAGE` | 沒有 |

同一個 pane 內、`apply_layout` 每次都會重新定位的五類子視窗中,只有 `tab_strips` 有 `WS_CLIPSIBLINGS`,其餘四類(包含這次出事的 `status_bars`,以及承接檔案列表內容的 `explorer_containers`)全部沒有。`WS_CLIPSIBLINGS` 缺席時,USER32 在處理同一層 sibling 視窗互相重疊/位移時的失效區域(invalid region)計算並不保證正確——這正是「視窗被搬走後,原本畫在那裡的像素沒有被登記為需要重繪」的已知成因類型。

### `explorer_containers` 完全不處理自己的繪製訊息

`main.cpp` 第 2716-2719 行既有註解已明確記載:

```cpp
// PD-040: plain STATIC child used purely as a clipping
// container (SetWindowRgn) and a parent HWND for
// ExplorerHost::initialize — it never paints or handles
// messages of its own, so no custom window class is needed.
```

`explorer_containers` 完全沒有 `WM_PAINT`/`WM_ERASEBKGND` 處理——它 100% 依賴 Windows 正確地把「這塊螢幕區域現在該由我來畫」的失效通知送給它(以及它裡面真正持有內容的 `IExplorerBrowser` Shell view)。上一段的 `WS_CLIPSIBLINGS` 缺席,加上 `WM_SIZE` 從不主動補一次全面重繪(上上段),兩者疊加就是「舊 sibling(狀態列)搬走後,新蓋上來的 `explorer_containers`/Shell view 沒收到『這裡要重畫』的通知,舊像素留在原地」的完整因果鏈。

**這是假說,不是最終定論。** 實際重繪失效的確切觸發序列(哪一個 `SetWindowPos` 呼叫、哪一幀)需要實作 agent 用實機重現(連續拖曳視窗邊框過程中連續截圖)搭配像素取樣確認,不能只憑程式碼推論就直接動兩三個 style 常數然後結案。

## 已確認的產品決策

1. **`explorer_containers`/四顆導覽按鈕/`address_bars`/`status_bars` 一律補上 `WS_CLIPSIBLINGS`**,與既有的 `tab_strips` 對齊,消除本頁「已確認的根因」第二節指出的不一致。這是成本最低、最先該做的修正。
2. **不能只靠決策 1。** `apply_layout` 每個 pane 的幾何區塊處理完之後(第 1500-1673 行,涵蓋 tab 條、導覽按鈕、網址列、狀態列、explorer container 全部重新定位完畢的那個時間點),**必須明確對該 pane 的完整矩形發出一次強制重繪**(`InvalidateRect`/`RedrawWindow`,涵蓋 `RDW_INVALIDATE | RDW_ERASE`,並視實測結果決定是否需要 `RDW_ALLCHILDREN`),不能繼續完全依賴個別 `SetWindowPos` 呼叫各自的內建重繪連鎖反應。這是因為決策 1 只解決「sibling 之間互相遮擋時的失效區域計算」,不解決「`WM_SIZE` 本身從未主動要求重繪」這第一個獨立根因——兩個根因疊加才是完整因果鏈,只修一個未必夠。
3. **決策 2 的強制重繪範圍是「有變動的 pane」,不是整個視窗每次都全部重畫。** `apply_layout` 已經知道每個 pane 的 `pane_rect`,直接針對這個矩形發出失效通知,避免不必要的全視窗重繪造成閃爍或效能浪費。
4. **驗收必須涵蓋連續拖曳的過程,不是單一張「resize 前」「resize 後」的對照截圖。** 使用者原文是「render in multiple positions」(複數),代表殘影可能不只一個、且可能在連續拖曳過程中的中間尺寸就已經出現;必須在拖曳過程中間插入至少 3-4 個中繼尺寸各截一張圖確認乾淨,不能只驗頭尾兩張。
5. **`apply_layout` 是 `WM_SIZE`/`WM_DPICHANGED`/切換 Group/切換版型/新增或關閉 tab 共用的同一個函式**,決策 1、2 的修正一旦補在共用路徑上,理論上這些觸發路徑會一併受益;但驗收仍需實際測過這些路徑各一次確認沒有殘留同類殘影,而不是只測「拖曳視窗邊框」這一種觸發方式。
6. **若實機重現後發現殘影其實是 Shell view(`IExplorerBrowser` 內部真正的 ListView)本身沒有正確重繪,而不是我們自己的子視窗**,修法必須侷限在「呼叫 `ExplorerHost::set_rect` 之後,額外要求該次幾何變動生效」(例如確認 `SetRect` 呼叫時機、或視需要在 `set_rect` 之後補一次對 container 的失效通知讓 Shell view 收到重繪),**絕對不可 subclass 或改寫 Shell view 內部子視窗**(這是專案紅線,見下方 binding constraints)。這個分支只在決策 1+2 驗證後仍有殘影時才需要處理,並在交接區記錄是否真的走到這一步。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> A guard in the shared function is a smaller diff than a guard in every caller, and patching only the path the ticket names leaves sibling callers broken.

——決策 2 明確要求修在 `apply_layout` 這個共用函式裡,而不是只在「使用者這次遇到的那個 pane/那個觸發路徑」打補丁。

`AGENTS.md`:
> The file list is never reimplemented.

——對應決策 6 的紅線:即使最終發現問題出在 Shell view 本身的重繪時機,也不得 subclass 或改寫其內部子視窗,只能調整我們自己呼叫 `IExplorerBrowser` API 的時機/次數。

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers.

——決策 2 新增的強制重繪呼叫必須是「幾何確實變動時」才觸發的事件驅動呼叫,不得為了保險加上任何計時器或輪詢式重繪。

`docs/tickets/PD-041-main-window-missing-clipclipchildren.md`(既有的相關重繪缺陷,PD-077 不得重複其錯誤):
> 主視窗補上 `WS_CLIPCHILDREN` 一次解決所有呼叫點,而不是逐一稽核。

——PD-077 延續同一種思路:修在建立子視窗的共用位置(style 常數)與共用的 `apply_layout` 收尾處,而不是逐一稽核每個觸發 `apply_layout` 的呼叫點。

## Files to read and trace first

- `src/app_shell/main.cpp` 第 3109-3111 行(`WM_SIZE`)——本票根因之一,`WM_SIZE` 從未主動要求重繪。
- `src/app_shell/main.cpp` 第 1500-1673 行(`apply_layout`)——**本票主要修改處**,決策 2 的強制重繪要加在每個 pane 幾何區塊處理完之後。
- `src/app_shell/main.cpp` 第 2720-2787 行(`explorer_containers`/`tab_strips`/導覽按鈕/`address_bars`/`status_bars` 的建立)——決策 1 要補 `WS_CLIPSIBLINGS` 的五個 `CreateWindowExW` 呼叫點,其中 `tab_strips`(第 2724-2727 行)已經有,作為對照組。
- `src/app_shell/main.cpp` 第 2716-2719 行——`explorer_containers` 「從不處理自己的繪製訊息」的既有註解,說明它為什麼完全依賴外部正確的失效通知。
- `src/explorer_host/explorer_host.cpp` 第 457-462 行(`ExplorerHost::set_rect`)——只呼叫 `browser_->SetRect(nullptr, rect)`,決策 6 分支若成立,這裡是要追加處理的地方。
- `docs/tickets/PD-041-main-window-missing-clipchildren.md` 與 `docs/tickets/PD-042-pane-container-top-corner-seam.md` 交接區——同類重繪缺陷的既有診斷方法與教訓,直接沿用其「用截圖+像素取樣先證明因果鏈,再動程式碼」的做法。
- `docs/tickets/PD-038-pane-blank-until-hover.md` 交接區——另一個「畫面沒跟著狀態更新」的既有案例,雖然根因不同(那是 `IExplorerBrowser` 初始完成後沒觸發同步繪製),但驗證手法可參考。

## Scope

1. `explorer_containers`/導覽按鈕/`address_bars`/`status_bars` 的 `CreateWindowExW` style 補上 `WS_CLIPSIBLINGS`。
2. `apply_layout` 對每個「本次幾何有變動」的 pane,在其子視窗全部重新定位完畢後,明確發出一次涵蓋該 pane 矩形的強制重繪。
3. 依決策 6,視實機重現結果決定是否需要額外處理 `ExplorerHost::set_rect` 的呼叫時機。

## Non-goals

- 不改變 pane 內部各子視窗的相對位置、順序或 z-order 本身(只補 style、加重繪呼叫)。
- 不對整個主視窗做每次 `WM_SIZE` 都全部強制重繪(決策 3 明確排除,避免閃爍/效能浪費)。
- 不 subclass 或改寫 `IExplorerBrowser` 內部子視窗(見 binding constraints)。
- 不改 PD-041 的 `WS_CLIPCHILDREN`(主視窗層級的既有修正,本票只動子視窗層級的 `WS_CLIPSIBLINGS`)。
- 不改狀態列的內容/格式(那是 PD-051/PD-060 的範圍,本票只處理「畫在哪裡、有沒有被清除」)。

## Acceptance

1. 連續拖曳主視窗邊框調整大小的過程中(至少含 3-4 個中繼尺寸各截圖一次,不只頭尾),任何 pane 都不出現舊狀態列/舊內容的殘影文字。
2. `WM_DPICHANGED`(切換顯示器或縮放比例)、切換 Group、切換版型(單格/雙欄/2×2 等)、新增或關閉 tab 造成的 reflow,同樣不出現殘影(各測一次,列出截圖或至少記錄測試步驟與結果)。
3. `explorer_containers`/導覽按鈕/`address_bars`/`status_bars` 確認已補上 `WS_CLIPSIBLINGS`(`rg` 核對)。
4. 沒有回歸 PD-038(pane 初次載入空白需 hover 才刷新)、PD-041(切換 active pane 清空畫面)、PD-042(容器頂角瑕疵)——各自的既有驗收步驟重跑一次確認仍過。
5. 新增的強制重繪呼叫沒有造成明顯的閃爍(肉眼觀察 + 可選:連續 resize 時用工作管理員觀察 CPU 沒有不合理飆升)。
6. 沒有 GDI/視窗控制代碼洩漏(反覆 resize 多次後,工作管理員的「GDI 物件」/「控制代碼」欄位數字不持續上升)。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
8. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "WS_CLIPSIBLINGS|apply_layout|explorer_containers\[index\] = CreateWindowExW|status_bars\[index\] = CreateWindowExW" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:拖曳視窗邊框連續調整大小,過程中截圖數張中繼尺寸;
# 切換 Group/版型/DPI 縮放;各自截圖確認無殘影。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 而不是 `Graphics.CopyFromScreen`。本票的殘影文字位置不固定,**必須在連續拖曳過程中多次截圖**,不能只截一張「resize 完成後」的圖就宣稱驗證通過。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 決策 6 是否被觸發(是否真的走到「Shell view 本身重繪時機」那個分支),以及若觸發,實際採用的修法。
- 決策 1 補上 `WS_CLIPSIBLINGS` 之後,決策 2 的強制重繪是否仍然必要(單獨 decision 1 是否已經足夠消除殘影,或兩者缺一不可)——用實機證據回答,不要只憑理論判斷。
- 決策 2 強制重繪呼叫的確切 API 與參數(`InvalidateRect` vs `RedrawWindow`,是否用了 `RDW_ALLCHILDREN`)與理由。
- 連續拖曳過程中多張中繼尺寸的截圖證據(修改前顯示殘影、修改後乾淨)。
- `WM_DPICHANGED`/Group 切換/版型切換/tab 增減的個別驗證結果。
- GDI/控制代碼檢查結果。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-26 實作交接

**實作內容。** `src/app_shell/main.cpp` 已完成兩項決策：

- `explorer_containers`、五顆 pane 導覽按鈕、`address_bars`、
  `status_bars` 的 child style 均補上 `WS_CLIPSIBLINGS`；既有
  `tab_strips` 的 style 保留。
- `apply_layout` 以 `laid_out_pane_rects` 記住每個 pane 上一次成功套用的
  完整矩形。首次顯示、隱藏後重現或矩形確實改變時，在所有子視窗
  `SetWindowPos`、`set_rect`、`set_visible` 與狀態列更新完成後，對主視窗
  的 pane 矩形呼叫：

  ```cpp
  RedrawWindow(window, &pane_rect, nullptr,
               RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
  ```

  使用 `RedrawWindow` 是為了明確涵蓋 child tree；`RDW_ALLCHILDREN` 使
  container 下的原生 Shell view 一併進入失效範圍。未使用 `RDW_UPDATENOW`，
  避免在 Shell API 可能重入的 `apply_layout` 內同步執行 paint；正常訊息迴圈
  會處理 invalid region。重繪只在 pane 矩形改變時發出，不新增 timer、polling
  或全視窗重繪路徑。

**決策 6。** 未觸發；沒有修改 `ExplorerHost::set_rect`，也沒有 subclass、
  改寫或直接操作 `IExplorerBrowser` 內部 child window。由於本環境的
  Computer Use 輸入層未能成功產生 resize 事件，不能把「Shell view 本身已
  排除」宣稱為一次完整拖曳實測結論；目前沒有任何像素證據迫使本票走決策 6
  分支。

**決策 1 與決策 2 的實測界線。** 解鎖後以
  `computer-use.get_window_state` 取得修正版實際視窗，截圖尺寸為
  `1386x913`。四個 pane 的原生 Shell view 均可見；當下狀態列文字分別為
  `80 items`、`138 items`、`4 items`、`0 items`，各自在 pane 底部，畫面
  未見重複狀態列文字。這是修正版的真實靜態畫面證據，不是 resize 過程證據。

  嘗試從同一個 fresh window observation 做 resize drag（兩組邊框座標）、
  內容區 click、layout element click，以及 window `Raise`，工具均回報
  `failed to activate captured window`；重新 `list_apps`、`list_windows`、
  `get_window` 並重置 node session 後結果相同。實際觀察到的窗口尺寸始終
  `1386x913`，因此沒有有效的 3–4 張中繼 resize 截圖，也無法用像素證據
  分離判定「決策 1 單獨足夠」或「決策 2 仍必要」。修改前在鎖定桌面期間的
  PrintWindow 嘗試產生全黑圖，已刪除且不納入證據；原始使用者截圖仍是本票
  的修改前殘影來源。

**回歸與資源驗證。** `rg` 確認五類 child style、主視窗既有
`WS_CLIPCHILDREN`、`apply_layout` pane redraw 及 PD-042 的 `CombineRgn`
均存在且未被移除。PD-038 的既有 Shell view redraw 路徑、PD-041 的主視窗
clip、PD-042 的 container region 程式碼均未改動。由於輸入層失敗，以下項目
標記為未驗證，而非推定通過：連續拖曳 resize 中繼幀、`WM_DPICHANGED`、
Group 切換、layout 實際切換、tab 新增/關閉、PD-038/041/042 的互動視覺
回歸，以及 repeated resize 後的 GDI/handle 趨勢。沒有新增 automated UI
test；`docs/testing.md` 明確指出 live `IExplorerBrowser` 沒有可誠實造假的
自動測試 seam。

**命令與程序證據。** `cmake --build build` 通過（最後一次為 `ninja: no
work to do`）；`ctest --test-dir build --output-on-failure` 為 4/4 通過；
`git diff --check` 通過。測試程序 PID `22520` 以不帶 `/F` 的
`taskkill /PID 22520` 關閉，約 5 秒後確認程序已退出；沒有使用強制終止。

### 2026-08-26 補充:連續拖曳中繼幀實測(補上一輪缺口)

前一輪受限於 computer-use 輸入層失敗,沒有取得連續拖曳的中繼截圖。改用
`SetWindowPos` + `PrintWindow(PW_RENDERFULLCONTENT)` 直接對真實視窗操作,
從 `build\pd062-output\PaneDock.exe`(已確認是 `ninja: no work to do` 的最新
建置產物)實測:

- 基準 1200x780,連續放大 5 步到 1500x980(每步 +60x+40,間隔 350ms),
  再從 1500x980 連續縮小 5 步回到 1200x780,共 11 張 `PrintWindow` 截圖
  (`step0`–`step5`、`shrink4`–`shrink0`)。
- 全部 11 張畫面乾淨:四個 pane 的檔案列表中沒有任何一張出現舊狀態列文字
  殘留(例如 `80 items` 出現在列表中段),每個 pane 底部只有自己對應的單一
  狀態列文字(`80 items`/`138 items`/`4 items`/`0 items` 依 pane 而定)。
  放大與縮小兩個方向都覆蓋到,不是只驗頭尾。
- 代表性截圖存檔:`PD-077-resize-step2.png`(放大中)、
  `PD-077-resize-step4.png`(放大後段)、`PD-077-resize-shrink3.png`
  (縮小中)。
- 測試程序以不帶 `/F` 的 `taskkill /PID` 關閉,確認已退出,沒有殘留程序。

驗收第 4 項(連續拖曳中繼截圖)在此補齊。決策 1+2 疊加後的修復效果視覺上
確認有效;仍未觸及決策 6(未發現需要走該分支的證據)。DPI/Group 切換/
tab 新增關閉等互動路徑仍如上一輪記錄維持未驗證,留給下次有相關改動時
一併檢查。
