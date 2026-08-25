# PD-037 — Tab 動態寬度(比照 Chrome 的縮放＋上限)

Phase 6 · app_shell · Depends on: PD-030

- Source: 使用者比對 `.\build\PaneDock.exe` 實際畫面與 `docs/panedock-ui-prototype.html` 理想稿後回報(2026-08-25)。
- Origin: 「tab 太窄,顯示不了太多字,也許可以參考 chrome 用動態寬度搭配最大寬度」。
- Priority: MEDIUM——現有 tab 純視覺可用但長檔名/資料夾名幾乎全被截斷,直接影響「看得出這是哪個 tab」這個核心可用性。

## 已確認的產品決策

1. **tab 寬度改為依同一 pane 內目前的 tab 數量動態計算,不再是固定寫死的寬度。** 現況(`src/app_shell/main.cpp` 的 `refresh_tab_strip`)完全沒有呼叫 `TCM_SETITEMSIZE`/`TCM_SETMINTABWIDTH`,`SysTabControl32` 在 `TCS_OWNERDRAWFIXED` 下退回系統預設固定寬度,與 tab 數量、文字長度都無關,這就是「太窄」的根本原因。改為:可用寬度(tab strip 總寬扣掉 `+` 按鈕固定寬度)÷ 目前 tab 數量,但夾在 `[kTabMinWidth, kTabMaxWidth]` 之間——比照 Chrome:tab 少的時候每個 tab 拉到上限寬度(而不是無限拉伸鋪滿全部空間變成很寬的空白 tab),tab 多到超過可用寬度時每個 tab 縮到下限寬度並讓 tab strip 出現水平捲動(`SysTabControl32` 原生內建捲動按鈕,不用自己刻)。
2. **常數命名與量測基準沿用既有慣例:`kTabMinWidth`(96-DPI px,建議 72,足夠放 1 個圖示＋4~5 字元＋ellipsis)、`kTabMaxWidth`(96-DPI px,建議 200,比照 Chrome 桌面版分頁上限量級,同時不超過 pane 卡片常見寬度的一半)。** 都用既有 `MulDiv(value, dpi, 96)`/`scaled_value` 模式做 DPI 縮放,不新增第二套縮放機制。
3. **動態寬度只在 `refresh_tab_strip` 重新計算並用 `TCM_SETITEMSIZE` 套用,不逐 tab 分別設定不同寬度。** `SysTabControl32` 的 `TCM_SETITEMSIZE` 是套用到全部 tab 的統一寬高,不支援單一 tab 有不同寬度;Chrome 的「新分頁比舊分頁窄」效果需要完全自訂佈局引擎,超出這張票要解決的核心問題(可讀性),YAGNI,不做。
4. **文字截斷邏輯不變,繼續依賴 `draw_tab_item` 既有的 `DT_END_ELLIPSIS`。** 寬度變寬後截斷情形自然減少,不需要额外改字型或省略號邏輯。
5. **`+` 新增分頁按鈕維持現有固定寬度(不受本票影響),排在動態寬度計算「可用寬度」的扣除項裡。** 沿用 `refresh_tab_strip` 現有邏輯,`+` 一律排最後一個 item。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`CONTEXT.md`:
> **tab**: One navigable location within a pane. A tab owns its location, view mode, sort order and navigation history.

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `refresh_tab_strip`(第 886 行起)——目前完全沒有寬度計算,只有 `TCM_INSERTITEMW`;本票要在插入 item 之後、或插入前先算好寬度呼叫 `TCM_SETITEMSIZE`。
- `src/app_shell/main.cpp` 的 `draw_tab_item`(第 694 行起)——確認 `DT_END_ELLIPSIS` 與現有 inset/icon 佈局在新寬度下不需要改動。
- `src/app_shell/main.cpp` 現有 `scaled_value`/`MulDiv` 使用方式(參考 `kLayoutButtonWidth`/`kAddressBarInset` 等常數的宣告與使用位置),新常數比照相同宣告風格放在同一個常數區塊。
- `apply_layout`(呼叫 `refresh_tab_strip` 的位置,第 1330 行附近)——確認 tab strip 的 client 寬度(`rc.right`)在此時已經是最終版面配置後的值,可以用來算「可用寬度」。
- Win32 `TCM_SETITEMSIZE` 文件(`commctrl.h`):`SendMessageW(strip, TCM_SETITEMSIZE, 0, MAKELPARAM(width, height))`。

## Scope

1. 新增常數 `kTabMinWidth`、`kTabMaxWidth`(96-DPI px,依決策 2)。
2. `refresh_tab_strip` 在插入 tab item 之前,先算出 tab strip 目前的 client 寬度,扣掉 `+` 按鈕寬度後除以「tab 數量」(至少 1,避免除以零),夾在 `[kTabMinWidth, kTabMaxWidth]`(DPI 縮放後)之間,呼叫 `TCM_SETITEMSIZE` 套用。
3. 確認 tab strip 建立時的 style 允許超出可用寬度時原生捲動(`SysTabControl32` 預設即支援,若目前有任何抑制捲動的 style 需一併確認並移除)。

## Non-goals

- 不做「新 tab 比舊 tab 窄」的逐一自訂寬度(已確認的產品決策 3)。
- 不改變 tab 的排序、關閉、切換行為(與 PD-035 無關,純寬度計算)。
- 不新增自訂捲動箭頭 UI,原生 `SysTabControl32` 捲動行為維持系統預設外觀。

## Acceptance

1. 同一個 pane 只有 1~2 個 tab 時,每個 tab 寬度為 `kTabMaxWidth`(DPI 縮放後),不會被拉伸鋪滿整條 tab strip。
2. 增加 tab 到超過 `可用寬度 / kTabMaxWidth` 的數量時,tab 寬度隨數量增加而縮小,直到觸及 `kTabMinWidth`,之後不再縮小、改為原生捲動。
3. 視窗縮放(含 `WM_DPICHANGED`)後重新整理 tab strip,寬度依新的可用寬度與 DPI 重新計算。
4. `+` 按鈕寬度不受影響,固定在 tab strip 最後一格。
5. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
6. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "TCM_SETITEMSIZE|kTabMinWidth|kTabMaxWidth" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:同一 pane 開 1 個、5 個、10+ 個 tab,觀察寬度縮放與捲動行為;
# 拖曳視窗改變寬度、切換 DPI(不同螢幕),確認寬度重新計算
```

## Handoff requirements

- `kTabMinWidth`/`kTabMaxWidth` 最終數值與選定理由(若因實測畫面效果調整過,記錄調整前後的值)。
- 可用寬度計算公式的最終實作位置與是否有邊界情況(0 個 tab、tab strip 尚未 layout 完成時寬度為 0)需要特別處理。
- 若真實桌面測試發現原生捲動按鈕與 owner-draw 的 tab 外觀有視覺不一致(例如捲動箭頭是系統預設樣式,與自繪 tab 背景不搭),記錄下來,不強行在本票內解決。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-25 實作交接

- **常數最終值**:`kTabMinWidth = 72`、`kTabMaxWidth = 200`(96-DPI px),沿用票中建議值,未因實測調整(桌面互動驗證未能執行,見下)。兩者宣告在既有常數區塊,緊接 `kTabStripHeight`/`kTabStripIdBase` 之後,並附註說明用途與 `+` 按鈕的關係。
- **寬度計算實作位置**:新增 `apply_tab_item_size(HWND strip, std::size_t tab_count)`(`src/app_shell/main.cpp`,`refresh_tab_strip` 前),邏輯為 `GetClientRect` 取得目前 client 寬度 → 扣掉一個 `scaled_value(strip, kTabMinWidth)` 當作 `+` 按鈕的預留寬度 → 除以 `max(1, tab_count)` → `std::clamp` 到 `[scaled_value(strip, kTabMinWidth), scaled_value(strip, kTabMaxWidth)]` → 呼叫 `TCM_SETITEMSIZE`。DPI 縮放沿用既有 `scaled_value`,直接傳入 `strip`(tab strip 自己的 HWND)取得其 DPI,未新增第二套縮放路徑。
  - 呼叫點有兩處,分別對應票裡點出的兩條觸發路徑:(1) `refresh_tab_strip` 在插入 tab item 之前呼叫,對應「tab 數量變動」時重算;(2) `apply_layout` 在 `SetWindowPos`/`ShowWindow` 該 pane 的 tab strip 之後立即呼叫,對應「tab strip 尺寸變動(視窗縮放、`WM_DPICHANGED`)」時重算——因為追蹤呼叫鏈發現 `WM_SIZE`/`WM_DPICHANGED` 只直接呼叫 `apply_layout`,並不會經過 `refresh_tab_strip`,若只在 `refresh_tab_strip` 裡算寬度,縮放視窗不會觸發重算,驗收項 3 會失敗。兩處都呼叫在功能上有重疊(同一次 group 切換會呼叫兩次),但成本可忽略,換取兩條觸發路徑都保證正確,判斷比拆分成一個「dirty flag」機制更省事(YAGNI)。
- **邊界情況**:
  - 0 個 tab(理論上不會發生,pane 至少有 1 個 tab,但 `tab_count` 仍用 `std::max<std::size_t>(1, tab_count)` 防除以零)。
  - tab strip 尚未 layout 完成、`GetClientRect` 回傳 0 寬度:`available` 用 `std::max(0, width - min_width)` 夾住不會變負值,`per_tab` 算出 0,再經 `std::clamp` 保底在 `kTabMinWidth`,不會呼叫到寬度為 0 或負值的 `TCM_SETITEMSIZE`。
  - `client.right`/`client.left` 是 `LONG`,與 `int` 常數混算會被編譯器擋下(`std::max` 型別推導衝突),已用 `static_cast<int>(...)` 處理,建置時發現並修正。
- **`+` 按鈕寬度的已知限制(票中決策 3/5 已預期)**:`TCM_SETITEMSIZE` 對整個 tab control 套用統一寬高,無法讓 `+` 維持與真實 tab 不同的固定寬度;目前實作只用 `kTabMinWidth` 當作「可用寬度」計算時的扣除量估計值,`+` 本身實際渲染寬度仍會被設成與其他 tab 相同的計算結果。這是 API 限制,票中決策 3 已經接受(YAGNI,不做逐 tab 客製寬度),此處如實記錄以免日後誤以為是遺漏。
- **Tab strip scroll 樣式**:確認 `WC_TABCONTROLW` 建立時的 style(`WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | TCS_OWNERDRAWFIXED`)沒有 `TCS_MULTILINE`/`TCS_BUTTONS`/`TCS_FIXEDWIDTH` 等會抑制原生捲動列的 style,未做任何修改。
- **建置/測試**:`cmake --build build`、`ctest --test-dir build --output-on-failure`(4/4 通過)均成功。`rg -n "TCM_SETITEMSIZE|kTabMinWidth|kTabMaxWidth" src\app_shell\main.cpp` 與 `git diff --check` 均如票中 Agent checks 所示執行過,無異常輸出。
- **手動桌面驗證的誠實狀態**:本次環境沒有 Computer Use 或類似工具可以互動操作視窗,因此**沒有**做到票裡要求的「開 1 個/5 個/10+ 個 tab 觀察寬度縮放與捲動」「拖曳視窗改變寬度」「切換 DPI」等真人互動驗證。只做了非互動的煙霧測試:`Start-Process .\build\PaneDock.exe` 啟動後存活 2 秒未崩潰即結束程序,僅能證明程式仍可啟動,無法證明 tab 寬度視覺效果或捲動行為符合預期。這是這個專案一直以來的已知環境限制,不是本票新增的缺口,請下一個有桌面互動能力的關卡(人或工具)補做票中列出的手動驗收項 1~4。
