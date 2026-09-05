# PD-195 — 位址列與釘選位置收進 `Pane`

Phase 7 · architecture · Depends on: PD-191, PD-192

- Source: 2026-09-05 使用者要求「main 中不要有屬於 pane 的獨立功能」。
- Priority: LOW-MEDIUM。本群最小（約 106 行），可獨立捨棄而不影響其餘票。

## Outcome

位址列輸入與釘選位置下拉這兩個 pane 自己的互動成為 `Pane` 的成員（子控制項 subclass proc 除外，見 Scope 2）。

## 要搬的函式（2026-09-05 工作樹行號）

| 目前 | 行數 | 搬成 |
|---|---|---|
| `submit_address(Pane&, AppState&)` `:3347-3359` | 13 | `void Pane::submit_address()` |
| `address_edit_proc(HWND, UINT, WPARAM, LPARAM, ...)` `:3361-3386` | 26 | 留在 `main.cpp`，見 Scope 2 |
| `add_current_folder(AppState&, std::size_t)` `:3277-3294` | 18 | `void Pane::pin_current_folder()` |
| `show_pinned_locations_menu(HWND, AppState&, std::size_t, ...)` `:3296-3344` | 49 | `void Pane::show_pinned_locations_menu(POINT screen)` |

## 兩個相依

### 1. 釘選位置清單是 app 層級的，不是 pane 層級的

`show_pinned_locations_menu` 讀 `state.application`（使用者的釘選清單，持久化）與 `state.pinned_fixed_labels`（固定項目的顯示名稱快取）。`add_current_folder` 寫 `state.application` 並開 `state.pinned_locations_dialog`。

這些**不得**變成 per-pane。處理方式：`PaneHost` 新增兩支——

- `virtual std::span<const PinnedLocation> pinned_locations() const noexcept = 0;`（唯讀，供建選單）
- `virtual void pin_location(core::ShellLocation location) = 0;`（寫入＋排程存檔，由協調層做）

固定項目的標籤（`pinned_fixed_labels`）併進 `pinned_locations()` 回傳的資料，或另加一支唯讀 accessor，實作者自選並記在交接區。實際型別以 `main.cpp` 現況為準（`grep -n 'pinned_fixed_labels\|kPinnedFixedParsingNames' src/app_shell/main.cpp`）。

`show_pinned_locations_manager` 這個對話框（`:2321-2330`）與 `apply_pinned_locations_dialog_result`（`:2312-2319`）**留在協調層**——那是 app 設定 UI，不是 pane 的功能。`Pane::pin_current_folder` 只呼叫 `pane_host()->pin_location(...)`。

### 2. `address_edit_proc` 為什麼留在 `main.cpp`

它是位址列 EDIT 控制項的 subclass proc，形狀與既有的 `tab_strip_proc`／`group_list_proc` 一致：協調層的自由函式，從視窗資料取得指標。PD-189 已確立這個模式並明確寫過它**不**違反模組契約 (1)。本票沿用：`address_edit_proc` 留在 `main.cpp`，函式體收斂成「取得 `Pane&` → 呼叫 `pane.submit_address()`」。若 PD-189 之後它已改成從 pane HWND 取得 `Pane*`，則本票只改那一行呼叫。

## Scope

1. 依上表搬移，機械替換同 PD-192（`is_shutting_down()`／`ShellCall`／`schedule_session_save()`／`active_tab()`／`navigate_to()`）。
2. `Pane::submit_address` 要讀位址列文字並解析成 `ShellLocation`。目前用的解析路徑（`location(std::wstring)` `main.cpp:656` → `shell_core`）**不要改**，只搬呼叫。
3. `show_pinned_locations_menu` 的 `TrackPopupMenu` owner 處置同 PD-193 Scope 3：先確認是否 `TPM_RETURNCMD`，是則可用 `window()`，否則保留主視窗 HWND 參數並在交接區說明。
4. PD-044 的 `SHAutoComplete`（`main.cpp` 內對位址列 EDIT 呼叫一次）**不動**——它是建立控制項時的一行設定，下拉 UI 由 Windows Shell 負責，沒有可搬的自訂邏輯。這一點 PD-190 已判定過，本票不重新評估。
5. 更新呼叫點：`handle_pane_command`（`:4642`）、`draw_pane_control`（`:4797`）中的釘選按鈕分支。

## Non-goals

- 不改釘選位置的持久化格式（session schema，`AGENTS.md` 要求向前相容且新欄位為 additive）。
- 不把釘選清單變成 per-pane。
- 不動 `PinnedLocationsDialog`（`src/app_shell/pinned_locations_dialog.*`，PD-180 已抽出）。
- 不動 `SHAutoComplete`。
- 不改位址列的視覺、字型、PD 既有的「首次點擊未聚焦 pane 的位址列時全選」行為（commit `052b586`）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> **Never persist a PIDL or a COM pointer.** Persisted identity is parsing name plus known-folder identity plus a fallback path. Display names are never identifiers.

> **Every persisted config/setting file must be designed for forward extensibility.** … A read that encounters a field it does not recognize preserves that field rather than silently dropping it on the next write-back.

> File operations go through Shell `IDataObject` and `IFileOperation`. Never assemble a path string and call the filesystem directly.

> **A `pane_index` parameter is a signal that the function belongs to that pane.** … it is not shared state that is a singleton by nature.

`docs/tickets.md` 2026-09-04 PD-189 條目：

> pane proc 是協調層的自由函式（比照既有的 `tab_strip_proc`，從視窗資料取得 `AppState*`），`Pane` 這個型別本身仍然沒有 `AppState*`／回呼／`std::function` 成員。

## Acceptance criteria

1. `submit_address`／`add_current_folder`／`show_pinned_locations_menu` 皆為 `Pane` 成員。
2. `address_edit_proc` 仍在 `main.cpp`，且函式體不再直接觸碰 `AppState` 的釘選或導覽欄位。
3. `PaneHost` 新增的兩支在 `pane_host.h` 各有一行註解說明為什麼是 app 層級。
4. 釘選位置的 session 檔位元組不變。
5. 零行為、零視覺變更。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

既有的 `panedock_address_bar_failure` 源碼守門測試必須通過；若它以函式名稱當錨點，更新錨點但**不得**放寬它守的不變式，並在交接區寫出新舊錨點。

`tests/unit/pane_test.cpp` 新增 `test_submit_address_is_a_no_op_while_shutting_down` — 假 `PaneHost` 的 `is_shutting_down()` 回 `true` 時不發生導覽。

## 使用者實機檢查

1. 在位址列輸入路徑按 Enter → 該 pane 導覽，其他 pane 不動。
2. 輸入不存在的路徑 → 錯誤處理與改動前相同。
3. 位址列自動完成下拉仍出現（`SHAutoComplete` 未被破壞）。
4. 首次點擊未聚焦 pane 的位址列 → 全選（commit `052b586` 行為）。
5. 釘選按鈕下拉 → 項目、順序、固定項目標籤與改動前相同；點選任一項導覽正確。
6. 「加入目前資料夾」→ 新項目出現在清單中，重開 App 後仍在。

## 交接區

- `PaneHost` 新增的 `PinnedLocation` 同時攜帶 `core::ShellLocation` 與顯示標籤。`AppState::pinned_locations()` 回傳 `pinned_location_menu_items` 的唯讀 span；快取依序放兩個固定項目（Desktop、This PC）與最多 64 個 application-level 自訂項目。固定標籤仍由既有的 `pinned_fixed_labels` 在 startup Shell lookup 後填入，再由 `rebuild_pinned_location_menu` 組成 Pane 使用的資料；管理對話框提交與新增釘選後都會重建快取。
- `AppState::pin_location()` 保留原有的 `core::add_pinned_location` 去重與 64 項上限，更新管理對話框、重建快取並呼叫既有的 debounce 存檔。`Pane::pin_current_folder()` 只負責 capture 目前 tab location 後呼叫此 host service；持久化格式與 `PinnedLocationsDialog` 未改動。
- `Pane::show_pinned_locations_menu(POINT)` 使用 `TPM_RETURNCMD`，因此 `TrackPopupMenu` 的 owner 使用 pane 自己的 `window()`；選擇後把 `WM_COMMAND` 送回其 parent main window。按鈕螢幕座標由 `handle_pane_command` 取得，固定／自訂項目的導覽仍由既有 coordinator command router 執行。
- `address_edit_proc` 的舊錨點 `submit_address(Pane&, AppState&)` 已移除，Enter 現在直接呼叫 `Pane::submit_address()`；`panedock_address_bar_failure` 沒有以該函式名稱作錨點，既有的新舊檢查錨點（`Pane::navigation_failed` 與 `refresh_navigation_chrome`）維持不變。`shutdown_state_check` 的 debounce 錨點由 `add_current_folder` 更新為 `Pane::pin_current_folder`，並驗證其透過 `PaneHost::pin_location` 委派持久化。
- 新增 `test_submit_address_is_a_no_op_while_shutting_down`。Pinned menu 依賴 Win32 HWND／TrackPopupMenu 與 AppState 的跨層快取，無法放入 `core` seam；其自動檢查由 build、source gates、`panedock_pane`、完整 CTest 與下方實機清單涵蓋。
- 2026-09-05 Agent checks：指定 LLVM-MinGW configure/build 通過；原始碼守門通過；正常 Win32 執行環境完整 CTest 23/23 通過（含 `panedock_pane` focused test 與 `panedock_launch_smoke`，6.24 秒）。受限 sandbox 內的 smoke 曾於關閉後 30 秒逾時，但不代表產品回歸。
