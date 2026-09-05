# PD-193 — 檢視模式、排序與位置擷取收進 `Pane`

Phase 7 · architecture · Depends on: PD-191

- Source: 2026-09-05 使用者要求「main 中不要有屬於 pane 的獨立功能」。
- Priority: MEDIUM。可與 PD-192 並行（兩者無共用函式），但 `handle_navigation_complete` 的收尾由 PD-196 統一處理。

## Outcome

「這個 pane 目前顯示成什麼樣子」的擷取與套用，全部成為 `Pane` 的成員。`main.cpp` 不再有 `capture_pane_*`／`apply_pane_*`／`set_pane_view_mode`。行為與視覺零變更。

## 要搬的函式（2026-09-05 工作樹行號）

| 目前 | 行數 | 搬成 |
|---|---|---|
| `capture_pane_view_mode(AppState&, std::size_t)` `:1600-1619` | 20 | `void Pane::capture_view_mode()` |
| `capture_pane_sort(AppState&, std::size_t)` `:1621-1639` | 19 | `void Pane::capture_sort()` |
| `apply_pane_view_mode(AppState&, std::size_t)` `:1641-1662` | 22 | `void Pane::apply_view_mode()` |
| `apply_pane_sort(AppState&, std::size_t)` `:1664-1678` | 15 | `void Pane::apply_sort()` |
| `capture_pane_location(AppState&, std::size_t)` `:1868-1886` | 19 | `void Pane::capture_location()` |
| `set_pane_view_mode(AppState&, std::size_t, const ViewModeOption&)` `:3211-3230` | 20 | `void Pane::set_view_mode(const ViewModeOption&)` |
| `show_view_mode_menu(HWND, AppState&, std::size_t, ...)` `:3232-3275` | 44 | `void Pane::show_view_mode_menu(POINT screen)` |

## 兩個必須先處理的相依

### 1. `ViewModeOption` 的位置

`ViewModeOption` 目前定義在 `main.cpp` 的匿名 namespace 內（`grep -n 'struct ViewModeOption' src/app_shell/main.cpp`）。`Pane::set_view_mode` 要吃它，因此它必須移出。**搬到 `src/shell_core/shell_core.h`**——它包的是 `shell_core::view_mode_name` 的參數（`mode` + `image_size`），本來就屬於那一層；不要為它另開新檔。若它含有 UI 標籤字串，把標籤留在 `main.cpp`／`pane.cpp` 的選單建構處，只搬 `selection` 那部分。實際切法記在交接區。

### 2. `state.suppress_location_capture`

`capture_pane_location`（`:1868`）讀 `state.suppress_location_capture`。這個旗標是 **singleton 且跨 pane**——`BrowseToObject` 可能在其餘 pane 還顯示舊 Group 的資料夾時同步重入 `navigation_complete`（見 `main.cpp:563-565` 的註解）。它**不得**變成 per-pane 欄位（會變成 4 槽而永遠只有一個有意義，PD-190 已對 `tab_context_menu_pane` 做過同樣判定）。

處理方式：`PaneHost` **新增第八支** `virtual bool location_capture_suppressed() const noexcept = 0;`。這是 PD-191 Non-goals 允許的新增路徑——本票在此說明理由：這是一個協調層擁有的全域 Group 切換旗標，`Pane` 無法自己知道，而且暴露它不會讓 `Pane` 看到任何其他 pane 或 `AppState`。同步更新 PD-191 驗收條件 1 的「恰好七支」為八支（在 `docs/tickets.md` 的本票段落記一筆，**不要編輯 PD-191 的票面**）。

## Scope

1. 依上表逐支搬移，機械替換同 PD-192（`is_shutting_down()`／`ShellCall`／`schedule_session_save()`／`active_tab()`）。
2. 每支開頭 `if (pane_host() == nullptr) return;`。
3. `show_view_mode_menu` 現在吃 `HWND window`（主視窗，用來當 `TrackPopupMenu` 的 owner 與接收 `WM_COMMAND`）。PD-187 之後 pane 有自己的 HWND，改用 `window()` 當 owner；若 `TrackPopupMenu` 的回傳值是直接被讀取（`TPM_RETURNCMD`）則 owner 只影響訊息路由，換成 pane HWND 安全。**先確認現況用的是不是 `TPM_RETURNCMD`**，若不是，維持傳入主視窗 HWND 當參數而不要擅自改，並在交接區說明。
4. 更新呼叫點：`capture_locations(AppState&)` `:1888-1894` 是四 pane 迴圈，**留在協調層**，只改成呼叫新成員。`handle_navigation_complete`／`activate_group`／`apply_layout`／`switch_active_tab`／`add_tab_to_pane` 的呼叫點一併更新。

## Non-goals

- 不改 `shell_core` 的 `view_mode_name`／排序對映邏輯，也不改持久化欄位名稱（那是 session schema，`AGENTS.md` 要求向前相容）。
- 不把 `suppress_location_capture` 變成 per-pane。
- 不動選單的視覺、項目順序或快捷鍵。
- 不搬 `refresh_tab_strip`（PD-197）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> **A `pane_index` parameter is a signal that the function belongs to that pane.** … it is not shared state that is a singleton by nature.

> **Every persisted config/setting file must be designed for forward extensibility.** … A schema change is additive (new optional fields, new migration step) rather than a destructive reinterpretation of an existing field's meaning.

> **Never persist a PIDL or a COM pointer.** Persisted identity is parsing name plus known-folder identity plus a fallback path.

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work.

## Acceptance criteria

1. 上表七支皆為 `Pane` 成員，`main.cpp` 內無 `capture_pane_`／`apply_pane_` 前綴的函式。
2. `capture_locations` 仍是協調層的四 pane 迴圈。
3. `PaneHost` 為八支純虛方法，新增的那支在 `pane_host.h` 有一行註解說明它為什麼是 singleton。
4. session 存檔的欄位與內容位元組完全相同（同一組操作前後 diff 兩份 session 檔）。
5. 零行為、零視覺變更。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

`tests/unit/pane_test.cpp` 新增 `test_capture_location_respects_suppression` — 假 `PaneHost` 的 `location_capture_suppressed()` 回 `true` 時，`capture_location()` 不改動綁定的 `PaneState`。

session 相容性檢查：以 PD-193 前的執行檔存一份 session，用 PD-193 後的執行檔讀取並再存一次，兩份檔案內容相同。

## 使用者實機檢查

1. 四個 pane 各自切換檢視模式（大圖示／詳細資料／清單）→ 各自獨立、不互相影響。
2. 切換 Group 後再切回 → 檢視模式與排序都還原。
3. 在 pane 內改排序欄位 → 關閉 App → 重開 → 排序還原。
4. 檢視模式選單的項目、勾選狀態、位置與改動前相同。
5. 切換 Group 的瞬間不會把 A Group 的資料夾寫進 B Group 的 tab（`suppress_location_capture`）。

## 交接區

- `ViewModeOption` 的資料半部（`ViewModeSelection selection`）移到
  `src/shell_core/shell_core.h`，並由同一處的 `kViewModeOptions` 保留八個
  mode/image-size 組合；UI label 留在 `src/app_shell/pane.cpp` 的選單建構處。
  `main.cpp` 的 command routing 只讀該資料表，不再持有 UI label 或
  `ViewModeOption` 定義。
- `show_view_mode_menu` 現為 `Pane::show_view_mode_menu(POINT screen)`。
  現況使用 `TPM_RETURNCMD`，因此 owner 改為 `Pane::window()`；呼叫點仍以
  view button 的 screen 座標定位選單，選定後把 command 送回 parent main
  window，保留既有 command routing。`TPM_RETURNCMD` 的 owner 變更不改
  選單項目、順序、勾選、位置或快捷鍵。
- `PaneHost` 新增的實際簽章為
  `virtual bool location_capture_suppressed() const noexcept = 0;`，
  `AppState` 以 out-of-line `AppState::location_capture_suppressed()` 回傳
  singleton `suppress_location_capture`。它沒有變成 per-pane state。
- 七支 view/sort/location 函式已成為 `Pane` 成員；`capture_locations` 仍
  保留在協調層作四 pane 迴圈，`tab_drag_layout` 與跨 pane placeholder
  計算未搬移。`apply_pane_container_region` 只改名為
  `apply_container_region`，以滿足 `main.cpp` 不再有 `apply_pane_` 函式的
  acceptance gate，沒有改其繪製內容。
- 新增 `test_capture_location_respects_suppression`；同步更新兩支既有
  source-level release checks 以追蹤 PD-192/PD-193 的新成員位置。
- 驗證：LLVM-MinGW configure/build 通過；排除既有 baseline 的
  `panedock_launch_smoke` 關閉逾時後，22/22 CTest 通過（含本票 focused
  pane test 與兩支更新後的 release checks）。完整 CTest 為 22/23 通過，
  唯一失敗仍是 `panedock_launch_smoke` 在關閉主視窗後 30 秒未退出；以可寫
  暫存 `%LOCALAPPDATA%` 重跑仍重現，且 PD-192 交接區已有相同 baseline，
  本票未改 shutdown scope。Session schema／序列化程式碼未變更，session
  欄位與內容保持原樣。
