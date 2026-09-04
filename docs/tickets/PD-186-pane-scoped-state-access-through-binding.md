# PD-186 — 以 `Pane` 的綁定指標取代約 85 處 `active_group(state).panes[i]` 與重複的兩段式 guard

Phase 7 · architecture · Depends on: PD-185

- Source: 2026-09-04 使用者重構討論（延續 PD-178～PD-183 的同一輪需求）。使用者要求「pane 只知道自己，不知道別人的 state」。本票是 PD-185 的**收成票**：PD-185 建立了綁定，本票才讓呼叫點真的用它。
- Priority: MEDIUM——機械性改動，零行為變更，但這是整串重構「耦合真的下降」的可量測成果。

## Outcome

協調層取用某個 pane 的資料時，不再從「整個應用程式狀態 → 找到 active Group → 索引到第 i 個 pane」走一遍，而是直接向該 `Pane` 要它綁定的 `core::PaneState*`。

同時消滅一組逐字重複的 guard。目前約 20 處寫著：

```cpp
if (!has_active_group(state) || pane_index >= active_group(state).panes.size() || ...)
    return;
```

本票之後是單一 null 檢查：

```cpp
auto* pane_state = state.panes[pane_index].pane_state();
if (pane_state == nullptr) return;
```

行為與視覺零變更。

## 已確認的現況（2026-09-04 工作樹）

- `main.cpp` 內對 `GroupState::panes` 的取用約 **85 處**（另有 `core` 內 18 處不在本票範圍）。集中在：
  - 導覽與請求識別：`begin_navigation`（`:754`）、`navigation_request_is_current`（`:775,781`）、`navigate_realized_panes`（`:812`）
  - 導覽 chrome：`refresh_navigation_buttons`（`:1576,1585`）、`refresh_navigation_chrome`（`:1601,1604`）
  - view mode／sort 擷取與套用：`capture_pane_view_mode`（`:1611,1625`）、`capture_pane_sort`（`:1630,1641`）、`apply_pane_view_mode`（`:1648,1650`）、`apply_pane_sort`（`:1670,1672`）
  - tab strip：`apply_tab_item_size`（`:1841,1842`）、`refresh_tab_strip`（`:1880,1887`）、`paint_tab_strip`（`:3810,3811,3896,3898`）、`tab_strip_proc`（`:4004,4011,4023,4041,4062,4064`）
  - location 擷取：`capture_pane_location`（`:1905,1909`）、`capture_locations`（`:1921`）
  - tab 操作：`switch_active_tab`（`:3101,3102`）、`cycle_active_tab`（`:3171`）、`add_tab_to_pane`（`:3191,3195`）、`close_tab_in_pane`（`:3218,3219`）、`navigate_tab_history`（`:3240,3242`）、`navigate_up`（`:3260`）、`refresh_pane`（`:3269`）
  - 其他：`set_pane_view_mode`（`:3281,3291`）、`show_view_mode_menu`（`:3300,3309`）、`add_current_folder`（`:3345,3351`）、`show_pinned_locations_menu`（`:3365`）、`submit_address`（`:3417`）、`handle_pane_command`（`:4739`）、`draw_pane_control`（`:4782,4789`）、`handle_context_menu`（`:4950,4956`）、`window_proc`（`:5266,5267,5914`）
- 兩段式 guard（`has_active_group(state)` ＋ `pane_index >= active_group(state).panes.size()`）逐字重複出現於至少 `:1576`、`:1601`、`:1611`、`:1630`、`:1648`、`:1670`、`:1880`、`:3099-3101`、`:3134-3135`、`:3143-3144` 等處。
- `active_group`（`:699-717`）每次呼叫都對 `groups` 做一次 `find_if` 字串比對。上列 85 處中多數在同一個函式內呼叫它兩次以上。
- PD-185 之後 `Pane::pane_state()` 已存在，且 `rebind_panes` 保證：**指標為非 null 恰好等價於「這個插槽目前有一個屬於 active Group 的可見 pane」**——也就是兩段式 guard 的完整語意。
- **跨 pane 的操作**：`finish_tab_drag` 的跨 pane 分支（`:3665-3695`，呼叫 `core::move_tab`）同時觸及來源與目標兩個 `PaneState`。依 2026-09-03 模組契約 (3)，它留在協調層。

## Binding constraints — quoted, do not go looking for them

`docs/tickets.md` 2026-09-03 條目，模組契約（本票**不**覆寫任何一條）：

> (1) **單向依賴**——協調層 → `Pane`，`Pane` 不得持有 `AppState*`、回呼介面或 `std::function` 成員。
> (3) **跨 pane 的拖曳狀態留在協調層**，因為它天生跨越來源與目標兩個 pane。

`docs/tickets.md` 2026-09-03 條目的診斷結論：

> 問題不是 `main.cpp` 6748 行，而是 `AppState`…這個約 130 成員的 god struct 被 **118 個函式**以 `AppState&` 接受。把檔案切開但簽章不變，行數搬走了、耦合一分沒少…每張票的共同必辦項是「順手把碰到的函式簽章從 `AppState&` 收窄成實際需要的型別」。

`AGENTS.md`：

> Read the relevant spec section and trace every caller before touching shared code. A guard in the shared function is a smaller diff than a guard in every caller, and patching only the path the ticket names leaves sibling callers broken.

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`docs/design-spec.md` §9.1：

> | `app_shell` | WinMain、STA 初始化、訊息迴圈、主視窗、命令路由 | 資料模型計算、Shell 呼叫 |

`docs/tickets.md` Agent 交付規則：

> 預設每個 ticket 的實作範圍為半天至兩天;若超過,先拆 ticket。

**若清點後發現改動超過兩天**，依上述規則先拆票：建議切法是以本票「現況」清單的分組為界（導覽／chrome／view mode+sort／tab strip／tab 操作／其他），一張票吃一到兩組，並在 `docs/tickets.md` 說明拆法。

## Files to read and trace first

- `src/app_shell/pane.h`（PD-185 產出的 `bind`／`unbind`／`pane_state()`）。
- `src/app_shell/main.cpp:699-729`：`active_group`、`has_active_group`、`active_pane_index`。
- `src/app_shell/main.cpp` 上列「現況」清單的每一個函式。
- `src/app_shell/main.cpp:3639-3696`：`finish_tab_drag`（跨 pane 分支留在協調層，但同 pane 分支可用綁定指標）。
- `docs/tickets/PD-185-pane-binds-core-pane-state.md`：綁定的維護點與 null 語意。
- `docs/tickets/PD-184-panestate-address-stability-invariant.md`：**`TabState` 的位址不穩定**，本票不得建立任何長期持有的 `TabState*`。
- `tests/release/shell_reentry_gate_check.ps1:61`：以字面 regex 斷言 `navigate_realized_panes(AppState& state, const panedock::core::GroupState& group) noexcept` 的簽章。若本票改動該簽章，必須同步更新此斷言，且改動前先確認舊字面確實存在（同義重寫，不得放寬判定）。

## Scope

1. 把「現況」清單中每一處 `active_group(state).panes[pane_index]` / `group.panes[pane_index]`（**pane 局部**的取用）改為 `state.panes[pane_index].pane_state()`。
2. 把兩段式 guard 改為單一 null 檢查。**必須逐處確認語意等價**：`pane_state() != nullptr` ⟺ 有 active Group 且該索引小於 `panes.size()`（PD-185 的 `rebind_panes` 不變式）。任何一處若還多帶了其他條件（例如 `realized()`、`closing_`），那些條件**原樣保留**。
3. **不得**改動的取用：
   - 需要整個 `GroupState` 的（`navigate_realized_panes`、`apply_layout`、`refresh_sidebar`、`capture_locations` 的 Group 層迴圈、`active_pane_index`）
   - 跨 pane 的（`finish_tab_drag` 的 `core::move_tab` 分支）
   - `core` 內的 18 處
4. 取得的指標**只在當次函式呼叫內使用**，不得存進任何成員或 static。若函式中間跨越 `ShellCallScope`／可能重入 Shell 的呼叫，指標本身仍有效（PD-184），但 `PaneState` 的**內容**可能已被重入路徑改動——因此凡是跨越 Shell 呼叫後還要再用的地方，必須重新讀取欄位而不是沿用先前讀出的區域複本。逐處檢查並在交接區列出跨越 Shell 呼叫的函式清單。
5. **不得**在任何地方保存 `TabState*`。既有的 `active_tab(pane)` 用法維持「取用後立即使用」。
6. 把本票碰到的函式簽章從 `AppState&` 收窄成實際需要的型別，並在交接區回報收窄前後的 `AppState&` 函式數量（PD-183 完成時為 118 → PD-185 值 → 本票值）。
7. 若 `navigate_realized_panes` 的簽章因收窄而改變，同步更新 `tests/release/shell_reentry_gate_check.ps1:61`，並在交接區附上「改動前舊字面存在」的驗證證據。

## Non-goals

- **不**把 tab 操作包成 `Pane::add_tab()` / `Pane::close_tab()` 這類轉呼叫方法。這些函式的本體是協調（`capture_pane_location` → `ShellCallScope` → `navigate` → `refresh_tab_strip` → `schedule_session_save`），只有中間一行是資料變更；包一層只會多一層轉呼叫而耦合不變。**觸發條件**：若日後某個 tab 操作變成**純**資料變更（不再需要導覽或 session 排程），再把該支包進 `Pane`。
- **不**為每個 pane 註冊獨立 window class 或 `WNDPROC`（`docs/tickets.md` §候選 的既有登記項，觸發條件未變）。
- **不**動跨 pane 拖曳（契約 (3)）。
- **不**改任何 Shell 呼叫閘門、shutdown reducer、realize/derealize 政策或導覽 generation 機制。
- **不**改 `core` 的任何函式、型別或測試。
- **不**改 session schema 或持久化行為。
- **不**改視覺或任何使用者可見行為。

## Acceptance Criteria

1. 「現況」清單中屬於 pane 局部取用的每一處都已改用 `state.panes[i].pane_state()`；交接區附上逐處清單與未改動者的理由。
2. 兩段式 guard 在 pane 局部路徑上已不存在；`Select-String` 檢查（見下）為空或僅剩清單中列明的合法例外。
3. 沒有任何長期持有的 `TabState*`。
4. `AppState&` 函式數量較 PD-185 完成時下降，數字記於交接區。
5. `ctest --test-dir build --output-on-failure` 全綠，含 `panedock_launch_smoke` 與 `panedock_shell_reentry_gate`。
6. 若動到 `shell_reentry_gate_check.ps1:61`，已附「舊字面存在 → 同義重寫 → 測試仍綠」的三段證據。
7. 行為與視覺零變更。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 兩段式 guard 應已消失於 pane 局部路徑
Select-String -Path src/app_shell/main.cpp -Pattern 'has_active_group\(state\)\s*\|\|\s*pane_index'
```

必須無結果（若有殘留，交接區須逐條說明為何是合法例外）。

```powershell
# 不得長期持有 TabState*
Select-String -Path src/app_shell/main.cpp,src/app_shell/pane.h -Pattern 'TabState\s*\*\s*\w+_\s*(=|;)'
# Pane 仍不得反向依賴協調層
Select-String -Path src/app_shell/pane.h -Pattern 'AppState|std::function|callback'
```

兩條都必須無結果。

```powershell
# AppState& 計數（填進交接區）
(Select-String -Path src/app_shell/main.cpp -Pattern 'AppState&\s+state').Count
```

```powershell
$p = Start-Process build\PaneDock.exe -PassThru; Start-Sleep 3
$p.CloseMainWindow() | Out-Null
if (-not $p.WaitForExit(5000)) { throw 'process survived graceful close' }
```

```powershell
git diff --check
```

## 交給使用者的實機檢查清單

本票是機械性改動但觸及面廣，請完整執行：

1. 每個 pane 都做一次：新增 tab、關閉 tab、切換 tab、拖曳排序 tab。
2. 跨 pane 拖曳 tab 3 次（本票明確不動這條路徑，用來確認沒有被誤傷）。
3. 導覽：位址列輸入、上一頁、下一頁、上一層、Refresh，各 3 次。
4. 檢視模式下拉選單切換 3 種模式，關閉再開啟確認還原。
5. 排序欄位切換後關閉再開啟，確認還原。
6. Pinned Locations 選單開啟並選一個位置。
7. 在檔案區與空白處各開一次右鍵選單。
8. 版型 1 → 4 → 1 切換 3 次，Group 切換 5 次。
9. 關閉再重啟，確認狀態完整還原；工作管理員無殘留 `PaneDock.exe`。

## Handoff requirements

在 `## 交接區` 記錄：逐處改動清單（含刻意未改動者與理由）、跨越 Shell 呼叫因而需要重新讀取欄位的函式清單、`AppState&` 計數的前後值、`shell_reentry_gate_check.ps1` 是否改動及其三段證據、`ctest` 全量結果、以及使用者實機檢查 9 項的逐項回報。

## 交接區

- **未拆票。** 實際盤點後，所有 pane-local 取用都集中在 `src/app_shell/main.cpp`，可在原票的半天至兩天規模內完成；未新增後續票。
- 逐處改動（行號為完成後）：導覽／請求識別 `begin_navigation` (`:768`)、`navigation_request_is_current` (`:789`)；chrome `refresh_navigation_buttons` (`:1590`)、`refresh_navigation_chrome` (`:1612`)；view mode／sort `capture_pane_view_mode` (`:1624`)、`capture_pane_sort` (`:1645`)、`apply_pane_view_mode` (`:1665`)、`apply_pane_sort` (`:1688`)；tab strip `apply_tab_item_size` (`:1787`)、`refresh_tab_strip` (`:1892`)、`register_tab_drag_hover_targets` (`:3165`) 的 callbacks、`close_tab_at_point` (`:3605`)、`tab_item_at_point` (`:3624`)、`update_tab_drag` (`:3734`)、`paint_tab_strip` (`:3839`)、`tab_strip_proc` (`:4017`)；location `capture_pane_location` (`:1923`)；navigation completion `handle_navigation_complete` (`:2465`)；tab 操作 `switch_active_tab` (`:3129`)、`cycle_active_tab` (`:3204`)、`add_tab_to_pane` (`:3220`)、`close_tab_in_pane` (`:3250`)、`navigate_tab_history` (`:3275`)、`navigate_up` (`:3295`)、`refresh_pane` (`:3304`)（僅改資料取用，未新增任何 `Pane::add_tab`／`Pane::close_tab` 轉呼叫）；其他 pane-local 路徑 `set_pane_view_mode` (`:3314`)、`show_view_mode_menu` (`:3335`)、`add_current_folder` (`:3380`)、`show_pinned_locations_menu` (`:3399`)、`submit_address` (`:3449`)、`finish_tab_drag` 的同 pane reorder 分支 (`:3674`)、`handle_pane_command` (`:4747`)、`handle_global_command` (`:4809`)、`handle_context_menu` (`:4978`)、`window_proc` 的 tab selection 分支 (`:5188`)、`wWinMain` 的 Ctrl+W 路徑 (`:5698`)。所有上述 pane-local guard 均以綁定指標的 null 語意取代。
- 刻意未改動：`rebind_panes` (`:724-731`) 是綁定本身的唯一維護點；`navigate_realized_panes` (`:818`) 需要整個 `GroupState` 規劃 realization；`capture_locations` (`:1943`) 是 Group 層迴圈；`apply_layout` 的 realize 路徑 (`:2748`) 需要整個 Group 與 layout；`new_group_state` (`:2947`) 尚未綁定；`set_active_pane` (`:3101`) 要修改 Group 的 active pane identity；`finish_tab_drag` 的跨 pane `core::move_tab` 分支 (`:3699-3700`) 同時需要來源與目標，完全未改。`core` 零改動。
- Shell 重入稽核：需要重讀／快照欄位的函式為 `refresh_navigation_chrome`（呼叫顯示名稱 Shell API 前快照 parsing name）、`capture_pane_view_mode` 與 `capture_pane_sort`（Shell 返回後重新取得 `pane_state()` 與 active tab）、`apply_pane_view_mode` 與 `apply_pane_sort`（呼叫前複製傳入 Shell 的 view/sort 欄位，返回後由 capture 函式重讀）、`refresh_tab_strip`（所有 parsing names 先快照，不讓 label Shell 查詢跨越 `TabState` 參照）、`capture_pane_location`（兩次 capture Shell 呼叫後重新取得 `pane_state()` 與 active tab）、`switch_active_tab`／`add_tab_to_pane`／`close_tab_in_pane`（`capture_pane_location` 返回後重新取得綁定與欄位）、`set_pane_view_mode`（Shell 返回後重新取得綁定與 active tab）、`add_current_folder`（capture 返回後重新取得綁定位置）。其餘本票碰到且跨 Shell 的 `navigate_tab_history`、`navigate_up`、`refresh_pane`、`show_view_mode_menu`、`show_pinned_locations_menu`、`submit_address`、`handle_pane_command`、`handle_global_command` 在 Shell 呼叫後不再使用先前讀出的 pane 欄位；`paint_tab_strip` 的跨 pane placeholder 改直接使用已存的 `tab_visuals()`，因此繪製途中不再做顯示名稱 Shell 查詢。
- 簽章收窄：`tab_strip_index`、`address_bar_has_focus`、`tab_item_at_point` 改吃 pane array；`tab_viewport_rect`、`tab_scroll_button_at_point`、`tab_scroll_step` 改吃單一 `Pane`。`AppState& state` 字面計數由 PD-185 完成後工作樹的 **123** 降為 **116**。
- `tests/release/shell_reentry_gate_check.ps1` **未改動**；`navigate_realized_panes(AppState& state, const panedock::core::GroupState& group) noexcept` 簽章維持原字面，因此不適用「舊字面 → 同義重寫」三段證據。
- Agent checks：LLVM-MinGW configure 通過；build 通過；提升環境（可寫 `%LOCALAPPDATA%\\PaneDock`）的最終 `ctest --test-dir build --output-on-failure` **22/22 通過**，含 `panedock_shell_reentry_gate` 與 `panedock_launch_smoke`，總時間 4.90 秒。受限環境第一次為 21/22，僅 `panedock_launch_smoke` 因無法完成 session 儲存提示而逾時，且檢查確認無殘留程序；依專案指引改在可寫 session 環境重跑後全綠。獨立 `Start-Process`／`CloseMainWindow` smoke exit code 0。兩段式 guard、長期 `TabState*`、`Pane` 反向依賴三項 `Select-String` 均 0 筆；合法 Group/cross-pane 例外如上；`git diff --check` 通過。
- 使用者實機檢查：Agent checks 不包含互動畫面操作，九項均留待使用者執行：① 四 pane 各自新增／關閉／切換／排序 tab；② 跨 pane 拖曳三次；③ 五種導覽各三次；④ 三種 view mode 與重啟還原；⑤ sort 與重啟還原；⑥ Pinned Locations；⑦ 檔案區／空白區右鍵；⑧ layout 1→4→1 三次與 Group 切換五次；⑨ 關閉重啟還原與無殘留程序。Agent 已完成第⑨項中的啟動／優雅關閉／無殘留程序自動 smoke。
