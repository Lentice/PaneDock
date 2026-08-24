# PD-019 — 每個 pane 的 tab 條:新增／關閉／切換,接上 realize-on-activation

Phase 3 · app_shell · Depends on: PD-017

- Source: `AGENTS.md`、`docs/design-spec.md` §4.6／§9.3(realize 順序)／FR-005／NFR-002、`docs/tickets.md` §已否決的方向
- Origin: 2026-08-24,`docs/roadmap.md` Phase 3「Tabs per pane, with realize-on-activation」。
- Priority: HIGH——PD-020(網址列/導覽按鈕)與 PD-021(鍵盤快速鍵)都要接在這裡建立的 tab 條與 UI 狀態上。

## Goal

`src/core/model.h` 的 `PaneState.tabs` 已經是 `std::vector<TabState>`,`add_tab`/`close_tab`/`set_active_tab` 也都已存在且經過測試(PD-004)——**資料模型不缺东西,缺的是 UI**。目前 `src/app_shell/main.cpp` 從來只建立恰好一個 tab(見 `default_application_state`、`new_group_state`、`toggle_layout` 裡的 `unique_tab_id`),使用者完全沒有介面可以開第二個 tab。本 ticket 補上:

1. 每個 pane 上緣的 tab 條 UI(新增、關閉、點擊切換)。
2. 切換 pane 內的 active tab 時,正確地讓同一個 `ExplorerHost`(每個 pane 槽位只有一個)重新導覽到新 active tab 的 location——這就是 realize-on-activation 在「同一 pane、不同 tab」情境下的具體化。

## 已確認的產品決策

1. **tab 條使用原生 `SysTabControl32`(`WC_TABCONTROL`),不是仿 `sidebar.cpp` 的 owner-draw `LISTBOX`。** 原生 tab control 原生就有選取高亮、鍵盤方向鍵切換焦點內 item、`WM_NOTIFY`/`TCN_SELCHANGE` 通知——這正是 rung 4「native platform feature covers it」該用的情境,不需要重新畫選取狀態或處理滑鼠命中測試。側邊欄用 owner-draw 是因為它需要行內改名編輯匣;tab 條不需要改名,不套用同一個模式。
2. **關閉 tab 不做視覺上的「×」按鈕,改用滑鼠中鍵點擊該 tab 關閉**(比照 Chrome/大多數瀏覽器慣例),鍵盤 `Ctrl+W` 留給 PD-021 補上。`docs/design-spec.md` FR-005 只要求「可新增、關閉、切換」,沒有規定觸發方式;畫一顆可點擊、可正確命中且會隨 DPI 縮放的「×」需要 owner-draw 或 subclass 原生 tab control 才能疊加,複雜度不成比例。**這是一個判斷,已在交接時請你確認**——若你或使用者更希望有可見的關閉按鈕,可在後續 UI 打磨 ticket 加。
3. **新增 tab 的觸發是 tab 條最後方一個固定的「+」項目**,點擊後在該 pane 呼叫 `core::add_tab` 建立一個導覽到預設 location(沿用既有 `kDefaultLocations[pane_index]`)的新 tab 並設為 active。這顆「+」項目本身**不是**真正的 tab(它沒有對應 `TabState`),渲染與點擊判斷都要把它從真正的 tab 清單裡排除。
4. **每個 pane 槽位維持只有一個 `ExplorerHost`(既有陣列 `state.explorers[kExplorerCount]`),不因為多 tab 而每個 tab 各配一個 host。** 切換同一 pane 內的 active tab,做法完全比照 PD-015/PD-017 已經在用的「先 `capture_locations` 存回舊 active tab 的 location,再對同一個 `ExplorerHost` 呼叫既有的 `navigate()`」模式——這正是 `docs/design-spec.md` NFR-002「只有可見 pane 的 active tab 持有 live `IExplorerBrowser`」的自然結果:同一個 host 在同一時刻只能顯示一個 tab 的內容,不需要額外程式碼保證「其餘 tab 不是 live 的」,因为它们從來不曾获得任何 host。
5. **tab 條佔用該 pane 矩形的上緣一小段固定高度**(96-DPI 基準值,依現有 `layout_metrics`/`scaled_value` 模式縮放,建議 24px 基準),`ExplorerHost` 的 `rect` 收窄為扣掉這段高度後的剩餘區域。四個 pane 各自的 tab 條是四個獨立的 `SysTabControl32` 控制項(不是一個橫跨全窗的控制項),隨 pane 矩形一起在 `apply_layout`/`WM_SIZE`/`WM_DPICHANGED` 時重新定位。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.6:
> 每個 pane 有一個以上的 tab,可新增、關閉、切換。tab 條在 pane 上緣。

`docs/design-spec.md` FR-005:
> 每個 pane 至少一個 tab。可新增、關閉、切換 tab。關閉 pane 的最後一個 tab 時,該 tab 導覽至預設 location 而非留下空 pane。

`docs/design-spec.md` NFR-002:
> 記憶體由架構決定,不由語言決定。恰有一個條件保證上界:**只有可見 pane 的 active tab 持有 live `IExplorerBrowser`**。其餘 tab 僅以資料存在。

`docs/tickets.md` §已否決的方向(「每個 tab 都保留 live `IExplorerBrowser`」):
> 這是記憶體無上界成長的唯一原因;閒置資源目標與完整 tab 狀態的共存,靠的就是「只有可見 pane 的 active tab 是 live」。

`AGENTS.md`:
> Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs persist as data and are realized on activation.

`AGENTS.md`:
> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups.
（同一原則本 ticket 套用到「切換同一 pane 內的 tab」:不 destroy/recreate `ExplorerHost`,只 `navigate()`。）

## Files to read and trace first

- `src/core/model.h`/`.cpp`——`PaneState.tabs`、`add_tab`、`close_tab`、`set_active_tab`(簽章已經是最終形狀,直接呼叫,不要修改)。
- `src/app_shell/main.cpp`——`AppState`、`active_tab()`、`capture_locations()`、`apply_layout()`、`layout_rects()`、`pane_area()`、`layout_metrics()`、`activate_group()`(切換 Group 時對每個 pane 呼叫 `navigate()` 的既有模式,是本 ticket 切換 tab 時要照抄的模式)、`unique_tab_id()`(可直接重用,分配新 tab id)、`WM_PARENTNOTIFY`(目前用來偵測點擊哪個 pane 並設為 active pane,tab 條點擊事件不會經過這裡,要另外處理 `WM_NOTIFY`)。
- `src/explorer_host/explorer_host.h`/`.cpp`——`navigate()`、`initialize()`、`set_rect()`(tab 條加入後,傳給 `ExplorerHost` 的 rect 要扣掉 tab 條高度,呼叫方式不變)。
- `CMakeLists.txt`——`PaneDock` target 目前連結 `user32 gdi32` 等;`SysTabControl32` 屬於 Common Controls,需要 `comctl32` 並在啟動時呼叫 `InitCommonControlsEx`(確認目前是否已連結,若沒有要加)。

## Scope

1. `AppState` 新增 `std::array<HWND, kExplorerCount> tab_strips{}`,在 `WM_CREATE` 時用 `CreateWindowExW(..., WC_TABCONTROLW, ...)` 建立四個(每個 pane 槽位一個),`WM_CREATE` 一開始呼叫一次 `InitCommonControlsEx`(`ICC_TAB_CLASSES`)。
2. 新增 `void layout_pane_chrome(HWND window, AppState& state)`(或併入既有 `apply_layout`):對每個可見 pane,把該 pane 的矩形拆成「tab 條矩形(上緣固定高度)」與「Shell view 矩形(剩餘部分)」,前者 `SetWindowPos` 到對應 `tab_strips[index]`,後者才是傳給 `ExplorerHost::initialize`/`set_rect` 的 rect。不可見 pane 的 tab 條連同 `ExplorerHost` 一起 `ShowWindow(..., SW_HIDE)`/`set_visible(false)`。
3. 新增 `void refresh_tab_strip(AppState& state, std::size_t pane_index)`:清空並依 `PaneState.tabs` 順序重建 `TCITEMW` 清單(`TCIF_TEXT`,文字用該 tab `location.parsing_name` 的最後一段路徑,或找不到分隔符號時整串——不需要完美的顯示名稱轉換,那是原生 Shell view 內部才有的能力),最後多加一個固定文字 `"+"` 的項目(決策 3);用 `TCM_SETCURSEL` 對齊 `active_tab_id` 對應的 index。每次 `core::add_tab`/`close_tab`/`set_active_tab` 之後都呼叫這個函式,比照既有 `refresh_sidebar` 的呼叫模式。
4. `WM_NOTIFY` 處理(目前 `main.cpp` 沒有這個 case,新增):
   - `TCN_SELCHANGE`:讀 `TCM_GETCURSEL`,若命中「+」項目則呼叫新增 tab 邏輯(見下),否則呼叫「切換 active tab」邏輯(見下)。
   - 判斷是哪個 `tab_strips[index]` 送來的通知,用 `((LPNMHDR)lparam)->hwndFrom` 對照 `state.tab_strips` 陣列找到 index。
5. 新增 `void switch_active_tab(HWND window, AppState& state, std::size_t pane_index, const std::string& tab_id)`:比照 `activate_group` 的模式——`capture_locations`-等价的單一 pane 版本(把該 pane 目前 active tab 的 location 寫回 `PaneState`)、呼叫 `core::set_active_tab`、對 `state.explorers[pane_index]` 呼叫既有的 `navigate(active_tab(pane).location.parsing_name)`、`refresh_tab_strip`、`save_now`。
6. 新增 `void add_tab_to_pane(HWND window, AppState& state, std::size_t pane_index)`:用 `unique_tab_id` 分配 id,建立 `TabState{id, location(kDefaultLocations[pane_index]), {}, {}, true}`,呼叫 `core::add_tab`、`core::set_active_tab`,對該 pane 的 `ExplorerHost` 呼叫 `navigate()`,`refresh_tab_strip`、`save_now`。
7. 新增 `void close_tab_in_pane(HWND window, AppState& state, std::size_t pane_index, const std::string& tab_id)`(掛在 `WM_MBUTTONDOWN` 落在某個 tab 項目矩形內時觸發,用 `TCM_HITTEST` 判斷):呼叫既有 `core::close_tab`(已處理「最後一個 tab 導覽到預設 location」的情況),之後不論是否為 active tab 被關閉,都用 `active_tab(pane).location.parsing_name` 重新 `navigate()` 該 pane 的 `ExplorerHost`(如果關的不是 active tab,location 沒變,`navigate()` 到同一個位置是無害的重複呼叫;若你認為值得避免這次多餘呼叫,只在 `close_tab` 回傳後檢查 `pane.active_tab_id` 是否等於關閉前的 active id 再決定要不要呼叫,實作時自行取捨並記錄理由),`refresh_tab_strip`、`save_now`。

## Non-goals

- 不做網址列、上一頁/下一頁/上層按鈕——PD-020。
- 不做任何鍵盤快速鍵(新增/關閉/切換 tab 的鍵盤路徑)——PD-021。
- 不做可見的「×」關閉按鈕(見決策 2)。
- 不修改 `core::add_tab`/`close_tab`/`set_active_tab` 既有簽章或行為。
- 不做 tab 拖曳排序、跨 pane 拖曳 tab——spec 未要求,YAGNI。
- 不處理 PD-018 的導覽歷史——tab 條本身的新增/關閉/切換不需要歷史,只有 PD-020 的上一頁/下一頁需要。

## Acceptance

1. 每個 pane 上緣顯示一個 tab 條,tab 數量與 `PaneState.tabs.size()` 一致,額外顯示一個「+」項目。
2. 點擊「+」在該 pane 新增一個導覽到預設 location 的 tab 並設為 active,tab 條即時反映;`session.json` 存檔後可在其中看到新 tab。
3. 點擊既有 tab 項目切換該 pane 的 active tab,`ExplorerHost` 正確重新導覽顯示新 active tab 的內容,舊 active tab 的 location 被正確存回(不遺失使用者剛剛在該 tab 內瀏覽到的位置)。
4. 滑鼠中鍵點擊某個 tab 項目關閉該 tab;關閉最後一個 tab 時該 tab 導覽到預設 location 而非留下空白 pane(既有 `core::close_tab` 行為,驗證接線正確)。
5. 四個 pane 各自的 tab 條互不影響,且 `apply_layout` 或視窗縮放/DPI 變更時 tab 條與其 `ExplorerHost` 一起正確重新定位。
6. `cmake --build build`、`ctest --test-dir build --output-on-failure`(既有三個 core 測試)全數通過。
7. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "windows\.h|WC_TABCONTROL|comctl32" src\core
# 預期:src\core 內無命中(tab 條屬於 app_shell,不下放進 core)
git diff --check
```

```powershell
Start-Process .\build\PaneDock.exe
Start-Sleep -Seconds 2
Get-Process PaneDock | Select-Object Responding
# 手動(不涉及鍵盤/滑鼠自動化,留給使用者):點擊「+」新增 tab、點擊 tab 切換、中鍵關閉 tab
```

## Handoff requirements

- tab 條高度的最終基準值(96-DPI)與其縮放公式,供 PD-020 在同一段 chrome 加裝網址列/按鈕時對齊。
- `refresh_tab_strip` 目前用什麼字串當 tab 顯示文字(整段 `parsing_name` 還是取最後一段),若日後要換成 Shell 顯示名稱(需要真正的 `IShellFolder::GetDisplayNameOf`),記錄下目前的簡化版本與差距。
- `WM_NOTIFY`/`TCN_SELCHANGE` 的處理是否會與既有 `WM_PARENTNOTIFY`(判斷點擊哪個 pane 設為 active pane)互相干擾——若點擊 tab 條同時觸發了「這個 pane 變成 active pane」的既有邏輯,說明是刻意保留(通常這是合理行為)還是需要抑制。
- 決策 2(中鍵關閉,無可見「×」)在真實使用上是否顯得不直覺,留給你/使用者手動試用後的回饋,作為後續 UI 打磨 ticket 的輸入。

## 交接區

<!-- 實作 agent 填寫,append-only -->
