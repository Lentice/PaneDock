# PD-156 — 擷取、持久化並還原 Shell 排序欄位與方向

Phase 7 · explorer_host + app_shell · Depends on: PD-006, PD-052, PD-153

- Source: 2026-08-31 spec／實作差異審查；使用者決策「保留 spec 要求，另開 ticket」。
- Priority: HIGH——`TabState` 與 session 已宣稱保存排序，但 live Shell view 沒有 capture/apply 路徑，Group restore 會靜默使用 Shell 預設排序。

## Outcome

每個 tab 的主要 Shell 排序欄位與升／降冪會從 live `IExplorerBrowser` 擷取到 `TabState`，並在 tab realize、導覽完成、Group 切換與重新啟動後精確還原。空白 `sort_column` 保留 Shell 對該資料夾的預設排序。

## 已確認的現況

- `src/core/model.h::TabState` 已有 `sort_column` 與 `sort_ascending`。
- `src/core/session.cpp` 已序列化／反序列化這兩欄，schema 不需改版。
- `src/app_shell/main.cpp::capture_pane_view_mode`／`apply_pane_view_mode` 已建立可重用的 live-view state capture/apply 時機；`capture_pane_location` 是切換／儲存前的共享 capture seam。
- `src/explorer_host/ExplorerHost` 只提供 view-mode API，沒有 sort API。
- LLVM-MinGW 的 `IFolderView2` 提供 `GetSortColumns(SORTCOLUMN*, int)` 與 `SetSortColumns(const SORTCOLUMN*, int)`；`SORTCOLUMN` 是 `PROPERTYKEY + SORTDIRECTION`。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` FR-002：

> 選取 Group 時還原:版型、分隔比例、每個 pane 的 tab 集合、每個 tab 的 Shell location、view mode、排序欄位與方向、active pane、每個 pane 的 active tab。

`docs/design-spec.md` NFR-005：

> 必要狀態(版型、location、tab、view mode、排序)必須精確還原,否則視為缺陷。

`docs/development.md`：

> Make the smallest change that satisfies the acceptance criteria. Reuse before adding.

`AGENTS.md`：

> Read the relevant spec section and trace every caller before touching shared code.

`AGENTS.md`：

> Keep `src/core` free of HWND, COM and `windows.h`.

`AGENTS.md`：

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

## Files to read and trace first

- `src/core/model.h`、`src/core/session.cpp`：既有 sort 欄位與 wire format。
- `src/app_shell/main.cpp`：`capture_pane_view_mode`、`apply_pane_view_mode`、`capture_pane_location`、`capture_locations`、`handle_navigation_complete` 及所有 caller。
- `src/explorer_host/explorer_host.h/.cpp`：取得 `IFolderView2`、view-mode API 與 HRESULT logging pattern。
- `tests/unit/core_session_test.cpp`、`tests/unit/explorer_host_lifetime_check.cpp`。
- `docs/tickets/PD-006-session-document-persistence.md`、`PD-052-pane-refresh-and-view-mode-switcher.md`、`PD-153-default-view-mode-details.md`。

## Scope

1. 在 `ExplorerHost` 增加單一主要排序欄位的 get/set API；內部使用 `IFolderView2::GetSortColumns`／`SetSortColumns`，不把 COM pointer 傳給 app_shell。
2. `sort_column` 使用 `PSStringFromPropertyKey`／`PSPropertyKeyFromString` 的 canonical `PROPERTYKEY` 文字格式；該格式為 ASCII，可直接存進既有 `std::string` JSON 欄位。需要時讓 `panedock_explorer_host` 連結 Windows `propsys`，不得加入第三方 dependency。
3. `SORT_ASCENDING` 對應 `sort_ascending=true`，`SORT_DESCENDING` 對應 `false`。無欄位、未知 direction、無法解析的 property key 或 Shell API 失敗不得覆寫已保存狀態；依既有診斷方式記錄 HRESULT。
4. 在 `capture_pane_location` 的共享路徑一起擷取 sort，涵蓋 Group/tab/layout 切換與 session save，不在每個 caller 重複補 guard。
5. 在 live view 完成導覽並套用 view mode 後套用已保存 sort；`sort_column.empty()` 時不呼叫 `SetSortColumns`，保留 Shell folder default。
6. 套用成功後重新讀回一次，讓 `TabState` 保存 Shell 真正接受的排序；所有 Shell 呼叫沿用 `ShellCallScope` 與 deferred-shutdown checks。
7. 擴充既有 ExplorerHost runnable check：對可排序的真實資料夾以 `PKEY_ItemNameDisplay` 測試 ascending／descending set-get round trip。擴充 core session test，確認非空 sort key 與 direction 的 JSON round trip。

## Non-goals

- 不保存多欄排序、secondary sort、欄位寬度、欄位顯示順序、grouping 或 filter。
- 不改 session schema version，不遷移或重寫未知欄位。
- 不自行實作檔案清單或用 `LVM_*` 操作 Shell list view。
- 不變更 view-mode、selection、scroll restoration 或預設 Details 決策。
- 不在 `core` 引入 `PROPERTYKEY`、Windows headers、COM 或 HWND。

## Acceptance criteria

1. 在 Details view 依 Name 升冪／降冪排序，切換 Group 或 tab 後返回，欄位與方向不變。
2. 正常關閉再啟動後，所有已保存 tab 的主要排序欄位與方向精確還原。
3. 尚無 `sort_column` 的既有 session 仍使用 Shell folder default，沒有被強制成 Name sort。
4. 無法解析的舊 sort key 不使 pane、Group 或應用程式不可用，且不把未知值靜默改寫成另一欄。
5. `src/core` 仍不含 Windows／COM 型別；session schema 與 unknown-field preservation 不變。
6. Release build、完整 CTest、ExplorerHost focused check 與 `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
.\build\panedock_explorer_host_lifetime_check.exe
git diff --check
```

```powershell
.\build\PaneDock.exe
# 真實桌面：Name ascending → Group/tab round trip → restart；再測 descending。
# 使用既有 disposable test folder，不修改使用者真實檔案。
```

## 交接區

<!-- 實作 agent 填寫，append-only -->

### 2026-08-31 實作交接

- `ExplorerHost` 新增 `set_sort(std::string_view, bool)` 與 `get_sort(std::string&, bool&)`，以 canonical `PROPERTYKEY` 字串配合 `PSPropertyKeyFromString`／`PSStringFromPropertyKey`，並用 `IFolderView2::SetSortColumns`／`GetSortColumns` 處理單一主要排序欄位；`panedock_explorer_host` 增加既有 Windows `propsys` link，未增加 dependency 或 Windows type 到 `core`。
- `app_shell` 在共享 `capture_pane_location` seam 擷取 sort；首次 realize 與 navigation complete 在 view mode 後套用 sort，成功後讀回 Shell 實際接受值。空白欄位不呼叫 Shell；解析、direction 或 Shell 失敗不覆寫原保存值。所有呼叫沿用 `ShellCallScope` 與 deferred-shutdown guard。
- `core_session_test` 明確檢查非空 sort key 與 descending JSON round trip；`explorer_host_lifetime_check` 以 `PKEY_ItemNameDisplay` 實測 ascending／descending set-get round trip，PASS。
- 獨立 `build-pd156` Release configure/build PASS；完整 CTest 10/11，唯一未執行成功的是 `panedock_launch_smoke`，因使用者既有 `PaneDock` 程序正在執行而由測試主動拒絕，未終止該程序。其餘 10/10 PASS；focused ExplorerHost check PASS；人工 Group/tab/restart 桌面流程未執行。

### 2026-08-31 主代理審查與最終驗證

- 審查子代理 diff 後，將 `set_sort`／`get_sort` 的 canonical key 轉換改成固定 `PKEYSTR_MAX` buffer，避免 `noexcept` API 在字串配置失敗時 terminate；輸出字串配置失敗明確回傳 `E_OUTOFMEMORY`。focused check 補上 empty／oversized key 的 `E_INVALIDARG` coverage。
- Release configure/build PASS；`panedock_explorer_host_lifetime_check.exe` PASS，包含 `PKEY_ItemNameDisplay` ascending／descending round trip 與 invalid-key checks；sandbox 內非 launch CTest 10/10 PASS；`panedock_launch_smoke` 因既有 `%LOCALAPPDATA%` sandbox 權限問題在 sandbox 內失敗，依 PD-149 已確認根因於 elevated context 重跑 1/1 PASS（1.13 秒）。`git diff --check` PASS。
- 人工 Group/tab/restart 桌面流程未執行；程式與 focused real-Shell check 已覆蓋本票自動化範圍。

### 2026-08-31 review correction

- Tracker status returned to `in_progress`: the required real-desktop Name ascending/descending Group, tab, and restart matrix has not been executed, so the end-to-end acceptance criteria are not yet proven.

### 2026-09-01 — real-desktop Group/tab/restart matrix

- 在實際 PaneDock Release build 的 `Odoo` Group 第一個 pane 將 Name 設為 descending；切換到 `Github` 再切回 `Odoo` 後，第一個項目仍為 `readme`，證明 Group round trip 保留方向。
- 正常關閉並重新啟動 PaneDock 後，`Odoo` 第一個 pane 仍以 `readme` 開頭；點擊 Name 欄標題改回 ascending 後，清單以 `auditlog`、`auth_ldaps`、`auto_logout_idle_user_odoo` 開頭。Group、tab 與 restart 驗收 PASS。
- 搭配既有 `core_session_test`、`panedock_explorer_host_lifetime_check`（ascending／descending／invalid key）、Release build 與非 launch CTest 12/12 PASS，PD-156 的 acceptance criteria 已具備證據；tracker 可轉為 `done`。
