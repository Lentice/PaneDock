# PD-049 — Pane tab 條改為自繪控制項,取代原生 `SysTabControl32`

Phase 6 · app_shell · Depends on: PD-037, PD-030

- Source: 使用者比對 `docs/panedock-ui-demo-01-refined-quiet-header.html` 目標畫面與實機截圖後回報(2026-08-25)。
- Origin: 「pane tabs 不要用 win32 tabs,應該自己做,並且允許動態寬度」「pane tab 的 add 按鈕要在右邊」。
- Priority: MEDIUM——視覺與互動能力雙重落差,且是後續 PD-050(重接拖曳邏輯)的前置票。

## 覆寫的既有決策(必須先讀,這是本票存在的理由)

`docs/tickets/PD-019-tab-strip-and-realize-on-activation.md` 決策 1(2026-08-24):

> tab 條使用原生 `SysTabControl32`(`WC_TABCONTROL`),不是仿 `sidebar.cpp` 的 owner-draw `LISTBOX`。原生 tab control 原生就有選取高亮、鍵盤方向鍵切換焦點內 item、`WM_NOTIFY`/`TCN_SELCHANGE` 通知——這正是 rung 4「native platform feature covers it」該用的情境。

**本票明確覆寫這個決策,理由是新證據:**

1. **`TCM_SETITEMSIZE` 只能設定「全部 tab 統一寬度」,無法讓每個 tab 依文字長度有不同寬度**(PD-037 交接區已經記錄這個限制,`apply_tab_item_size` 目前是「用同一個公式反推一個統一寬度」,不是真正的動態逐一寬度)。使用者這次明確要求的「允許動態寬度」是逐一 tab 依內容長度,不是統一寬度公式——`SysTabControl32` 的訊息介面不支援這個需求,不是實作沒做好,是控制項本身的能力上限。
2. **`SysTabControl32` 的 item 是依插入順序線性排列,無法把最後一個「+」item 獨立固定在控制項的最右緣**(PD-043~046 investigation 已確認:目前「+」item 是最後一個 `TCITEMW`,只是插入順序上的最後,不是版面配置上釘住右緣;當 tab 沒有塞滿整個 strip 寬度時,「+」會跟著最後一個 tab 貼在一起,而不是靠齊控制項右邊界)。使用者要求的「add 按鈕要在右邊」需要把「+」從 tab 序列裡抽離,變成獨立版面配置的固定按鈕,這在單一 `SysTabControl32` 控制項內做不到。
3. **目標畫面(`docs/panedock-ui-demo-01-refined-quiet-header.html`)的 tab 視覺(圓角矩形分頁、選中態實色填底、資料夾圖示、關閉態呈現)與 Windows 原生 `SysTabControl32` 的視覺語言本來就不同**,PD-030 已經在用 owner-draw 模擬部分視覺,但仍受限於原生控制項的版面配置模型(統一寬高、固定 item 順序)。

**要重開(即目前的 `SysTabControl32` 方案)的條件維持 PD-019 那句話所述:如果日後發現自繪版面/命中測試/無障礙支援的維護成本高過這裡列出的三個具體限制,可以再開票討論退回,但必須提出新證據,不能只憑「原生比較省事」的一般性理由。**

## 已確認的產品決策

1. **每個 pane 槽位的 tab 條改為一個自繪的子視窗(plain `WC_STATIC`+`WS_CLIPCHILDREN` 或自訂視窗類別皆可,由實作 agent決定,只要不是 `WC_TABCONTROLW`),自行處理繪製、滑鼠命中測試、點擊選取、hover 狀態。** 命名與既有欄位相容:`AppState::tab_strips`(`std::array<HWND, kExplorerCount>`,`src/app_shell/main.cpp` 第 319 行)的用途不變,只是其建立方式(`WM_CREATE` 內原本 `CreateWindowExW(..., WC_TABCONTROLW, ..., TCS_OWNERDRAWFIXED)`,第 2517-2519 行附近)改成建立新的自繪視窗類別。
2. **每個 tab 的寬度依文字內容量測(`GetTextExtentPoint32W`或等效 API),夾在 `kTabMinWidth`/`kTabMaxWidth`(已存在常數,PD-037)之間,逐一 tab 各自計算,不再套用「總寬度除以數量」的統一公式。** 若全部 tab 的量測總寬超過 strip 可用寬度,才依比例縮減(可沿用 PD-037 交接區描述的「Chrome 式縮放」精神,但這次是逐一寬度而非統一寬度的縮放)。
3. **「+」新增分頁按鈕從 tab 序列抽離,改為獨立的固定寬度子視窗或繪製區塊,永遠釘在 tab 條的最右緣**,不隨 tab 數量或捲動狀態移動位置。tab 區域(可捲動或收縮的部分)佔用「+」按鈕以外的剩餘寬度。
4. **維持每個 tab 顯示的資訊與既有邏輯相容:文字(`tab_display_text`,既有函式)、選中狀態對應 `pane.active_tab_id`、點擊切換呼叫既有的 tab 切換邏輯(`handle tab selection`/`WM_COMMAND`/`WM_NOTIFY` 目前的處理路徑,實作 agent 需要先讀懂現有 `TCN_SELCHANGE` 的處理位置並改為新控制項的等效通知機制,例如自訂 `WM_COMMAND`/`WM_NOTIFY` 或直接在自繪視窗的 `WM_LBUTTONDOWN` 裡呼叫既有的選取函式)。**
5. **不在本票內重新實作拖曳排序(PD-035)、拖曳懸停自動切換(PD-034)。** 這兩個既有功能建立在 `TCM_HITTEST`/`TCM_SETCURSEL`/`TCN_SELCHANGE` 等 `SysTabControl32` 專屬訊息上,換成自繪控制項後這些呼叫全部失效;本票只保證「基本點擊選取正常」,PD-034/035 的行為在本票完成後預期會壞掉(這是已知、刻意接受的暫時狀態),由 PD-050(依賴本票)接手重新接上。**如果本票的實作 agent有餘力且不超出票的預期範圍,可以順手接上,但不是本票的驗收項目**,以免單票工作量爆炸(遵守 `AGENTS.md` 的 ticket 拆分精神)。
6. **tab 的視覺樣式(圓角、選中態填色、資料夾圖示)在本票內只需要「不比目前的 `SysTabControl32` owner-draw 版面(PD-030)差」,不強制要求完全比照目標畫面的圓角分頁視覺。** 視覺精修(圓角、圖示、hover 態微調)如果需要在後續 round 由使用者截圖驗收後再迭代,本票的核心驗收是「控制項換掉了、動態寬度生效、+ 按鈕釘在右邊」,不是像素級視覺還原。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Read the relevant spec section and trace every caller before touching shared code. A guard in the shared function is a smaller diff than a guard in every caller, and patching only the path the ticket names leaves sibling callers broken.

`AGENTS.md`:
> Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs persist as data and are realized on activation. This is what keeps memory bounded.
(本票不改變這個既有架構,新控制項只是「顯示與挑選 tab」的視覺層,tab 資料模型與 realize-on-activation 邏輯完全不動。)

`docs/tickets/PD-037-dynamic-tab-width.md`(既有的統一寬度公式限制,本票要解除的限制):
> `TCM_SETITEMSIZE` cannot give it a different width than the real tabs.

## Files to read and trace first

- `src/app_shell/main.cpp` 第 2510-2525 行附近(`WM_CREATE` 內建立 `tab_strips[index]` 的 `CreateWindowExW(..., WC_TABCONTROLW, ...)` 呼叫)——本票要替換的建立程式碼。
- `src/app_shell/main.cpp` 的 `refresh_tab_strip`/`refresh_tab_strips`(第 980-1015 行)——目前用 `TCM_DELETEALLITEMS`/`TCM_INSERTITEMW`/`TCM_SETCURSEL` 重建 tab 清單的邏輯,改用自繪控制項後這個函式要改成「重建自繪視窗內部的 tab 資料清單並觸發重繪」。
- `src/app_shell/main.cpp` 的 `apply_tab_item_size`(第 963-978 行)——本票要替換成逐一量測寬度的版本。
- `src/app_shell/main.cpp` 找出所有 `TCM_HITTEST`/`TCN_SELCHANGE`/`TCM_GETCURSEL`/`WM_NOTIFY` 對 `tab_strips` 的呼叫點(`rg "TCM_|TCN_" src\app_shell\main.cpp`),逐一列出並確認每個呼叫點在新控制項下的對應處理方式或明確標記為「PD-050 待重接」。
- `docs/tickets/PD-030-pane-card-chrome-and-tab-header-restyle.md` 交接區——確認目前 tab header 的 owner-draw 視覺實作(圖示、選中態)如何被 `WM_DRAWITEM` 分派,新控制項要延用同一套繪製邏輯(可重用既有的繪製函式,不必重寫視覺)。
- `docs/tickets/PD-034-drag-hover-auto-switch.md`、`PD-035-tab-drag-reorder.md` 交接區——理解目前這兩個功能實際依賴哪些 `SysTabControl32` 專屬 API,才能在本票的 Non-goals 裡準確列出「哪些行為預期會暫時壞掉」。

## Scope

1. `AppState::tab_strips` 的建立方式從 `WC_TABCONTROLW` 改為自繪子視窗,自行處理 `WM_PAINT`/`WM_LBUTTONDOWN`(點擊選取)/`WM_MOUSEMOVE`(hover 態,選用)。
2. `apply_tab_item_size` 改為逐一 tab 依文字量測寬度的版本(仍尊重 `kTabMinWidth`/`kTabMaxWidth`)。
3. `refresh_tab_strip`/`refresh_tab_strips` 改為操作新控制項的內部資料結構(例如每個 tab 條需要一個 `std::vector<TabVisual>` 之類的資料,記錄每個 tab 的顯示矩形與文字,供繪製與命中測試共用)。
4. 「+」新增分頁按鈕從 tab 序列抽離,固定於 tab 條最右緣。
5. 點擊 tab 觸發既有的 tab 切換邏輯;點擊「+」觸發既有的新增分頁邏輯。兩者都要找到目前 `WM_NOTIFY`/`TCN_SELCHANGE`(或 `WM_COMMAND`)的既有處理程式碼並改接到新控制項的通知方式。

## Non-goals

- 不在本票內修正/重接 PD-034(拖曳懸停自動切換)、PD-035(拖曳排序)——留給 PD-050。
- 不要求 tab 視覺樣式完全比照目標畫面的圓角分頁(見已確認的產品決策 6),但也不能刻意讓視覺比現狀(PD-030)倒退。
- 不改變 tab 資料模型(`core::TabState`)、realize-on-activation 邏輯、`ExplorerHost` 生命週期。
- 不新增第三方 UI 函式庫依賴(`AGENTS.md`:reach for stdlib/Win32 first——自繪控制項用純 GDI + 一個一般子視窗即可,不需要引入依賴)。

## Acceptance

1. 每個 pane 的 tab 條不再是 `SysTabControl32`(可用 `GetClassNameW` 或程式碼審查確認),而是自繪子視窗。
2. 多個 tab 且文字長度不同時,每個 tab 的顯示寬度依其文字內容各自計算,不是全部統一寬度(可用不同長度的資料夾名稱手動驗證,或以自動化檢查驗證量測邏輯正確)。
3. 「+」新增分頁按鈕固定顯示在 tab 條最右緣,tab 數量或寬度變化時,「+」的位置不移動(除非 tab 全部塞滿導致沒有剩餘空間,此時行為由實作 agent 決定,例如把「+」推到緊接在最後一個可見 tab 之後,只要文件裡誠實記錄這個邊界情況)。
4. 點擊任一 tab 能正確切換 active tab(既有行為不受影響)。
5. 點擊「+」能正確新增分頁(既有行為不受影響)。
6. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
7. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "WC_TABCONTROLW|TCS_OWNERDRAWFIXED|TCM_|TCN_" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:開啟多個長度不同的資料夾路徑分頁,確認每個 tab 寬度依內容不同、
# + 按鈕固定在右邊、點擊 tab 與 + 按鈕行為正常。
# 本環境已具備螢幕截圖與滑鼠點擊/移動模擬能力(PowerShell + System.Drawing/
# user32.dll SetCursorPos/mouse_event),請盡量實際啟動並截圖驗證,
# 而不是只做非互動 smoke check。
```

## Handoff requirements

- 新控制項的視窗類別/訊息處理方式(自訂 `WNDCLASS` vs. 沿用 `WC_STATIC` 子類化)與理由。
- 逐一寬度量測與縮減演算法的具體實作方式。
- 明確列出哪些 `TCM_*`/`TCN_*` 呼叫點已經改接、哪些刻意留給 PD-050(依 Non-goals)。
- 真實桌面測試(點擊 tab、點擊 +、開啟多個不同長度分頁)的實際結果與截圖觀察。
- 若發現 PD-034/035 的既有行為在本票之後明顯壞掉(預期中),簡述壞掉的具體現象,供 PD-050 的實作 agent 快速定位。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 實作交接

2026-08-25：已將 tab strip 改為 `WC_STATIC` 子視窗加 `SetWindowSubclass`，不新增自訂全域 `WNDCLASS`；subclass 以 `WM_PAINT` 使用 GDI 繪製，並在 `WM_LBUTTONDOWN` 透過 `WM_APP + 49` 將 pane index 與 tab index（`-1` 代表 `+`）送回主視窗。`AppState` 新增每個 pane 的 `std::vector<TabVisual>`（文字與顯示矩形）及固定右側的 `tab_add_rects`，既有 `tab_strips` HWND 陣列與 tab model 不變。

寬度以 `GetTextExtentPoint32W` 量測每個 tab 文字，加上 padding 後夾在 `kTabMinWidth`/`kTabMaxWidth`；總寬超出「strip 寬度減固定 `+` 按鈕寬度」時按比例縮減，最後將超出可用區的 tab 矩形裁到 `+` 按鈕左側，讓 `+` 永遠靠右。`refresh_tab_strip` 重建 visuals，`apply_layout` 重新量測矩形。

所有 `TCM_*`/`TCN_*` 選取、插入、刪除與 hit-test 呼叫已移除：選取與新增改由自繪 subclass 的訊息接回既有 `switch_active_tab`/`add_tab_to_pane`；中鍵關閉與 drag-hover hit-test 改用共用的 `TabVisual.rect` 命中測試。PD-035 拖曳排序尚未重接，預期因不再建立 drag capture 狀態而失效，留給 PD-050；PD-034 的 hover hit-test 已改用新矩形，但尚未做真實桌面互動確認。

本次尚未能提供解鎖桌面的截圖或手動點擊結果；建置、測試與非互動 smoke check 的結果由本次交接後續補記。

驗證補記：LLVM-MinGW/Ninja 建置成功，`ctest --test-dir build --output-on-failure` 的 4/4 測試通過，`rg` 與 `git diff --check` 通過。已啟動 `build\PaneDock.exe` 且程序回報 Responding；`CloseMainWindow` 已送出但 5 秒內未退出，後續 `taskkill /PID` 同樣回報 `Access denied`，因此未使用 `/F`。桌面互動截圖仍受目前 locked/access-denied 狀態限制。
