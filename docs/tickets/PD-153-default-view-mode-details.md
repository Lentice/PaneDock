# PD-153 — 新 tab 預設使用詳細資料檢視

Phase 7 · app_shell · Depends on: PD-052, PD-079, PD-082

- Source: 使用者要求「把預設的檢視改成 詳細資料」（2026-08-31）。
- Priority: LOW——既有檢視模式可手動切換且會正確還原；本票只調整新 tab 的初始值。

## Outcome

`view_mode` 尚未設定的新 tab 首次 realize 與導覽完成後使用 Shell 的 `FVM_DETAILS`；既有 tab 已保存的檢視模式仍精確還原。

## 已確認的現況

- `src/explorer_host/explorer_host.cpp::ExplorerHost::initialize` 已用 `FVM_DETAILS` 初始化 Shell view。
- `src/app_shell/main.cpp::apply_pane_view_mode` 隨後把空 `view_mode` 明確覆寫成 `FVM_ICON`／96 px；這是目前預設成大型圖示的唯一根因。
- 同一共用函式涵蓋首次 realize 與每次導覽完成。非空 `view_mode` 由 `parse_view_mode` 還原，不應改動。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-002：
> 選取 Group 時還原:版型、分隔比例、每個 pane 的 tab 集合、每個 tab 的 Shell location、view mode、排序欄位與方向、active pane、每個 pane 的 active tab。

`docs/design-spec.md` §10：
> 必要狀態(版型、location、tab、view mode、排序)必須精確還原,否則視為缺陷。

`docs/development.md` Change workflow：
> Make the smallest change that satisfies the acceptance criteria. Reuse before adding.

`AGENTS.md`：
> Read the relevant spec section and trace every caller before touching shared code.

`AGENTS.md`：
> Keep `src/core` free of HWND, COM and `windows.h`.

## Files to read and trace first

- `src/app_shell/main.cpp`: `parse_view_mode`、`capture_pane_view_mode`、`apply_pane_view_mode`、其所有 caller、手動檢視模式切換路徑。
- `src/explorer_host/explorer_host.cpp`: `initialize`、`set_view_mode`、`get_view_mode`。
- `src/core/model.h`、`src/core/model.cpp`: 新 tab 的空 `view_mode` 建立語意。
- `tests/unit/core_session_test.cpp`: 非空 `view_mode` 的 persistence coverage。

## Scope

1. 只把 `apply_pane_view_mode` 對空 `view_mode` 的 fallback 改為 `FVM_DETAILS`。
2. 保留非空 `view_mode` 的既有解析、套用、擷取與持久化行為。
3. 執行完整 Release build 與 CTest；以真實桌面人工確認新 tab 與既有已保存 tab。

## Non-goals

- 不修改 `core::TabState`、session schema、既有 session 文件或 migration。
- 不變更八種檢視模式選單、column header 規則、排序、欄寬或 Shell view 生命週期。
- 不新增常數、helper、抽象、dependency 或 `IExplorerBrowser` fake。

## Acceptance criteria

1. 新安裝的預設 Group、全新 tab 與新增 layout pane 的初始模式皆為 Details。
2. 已保存為 icons、list、tiles 或 content 的 tab 重新啟動後仍維持原模式。
3. `cmake --build build`、完整 CTest 與 `git diff --check` 通過。

本次是既有分支中常數的單行替換，沒有新增非平凡邏輯；依 `docs/testing.md` 不為 live Shell view 增加 fake。真實行為以既有 ExplorerHost self-check 與人工檢查驗證。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
.\build\panedock_explorer_host_lifetime_check.exe
git diff --check
```

## 交接區

<!-- 實作 agent 填寫，append-only -->

### 2026-08-31 實作交接

- `src/app_shell/main.cpp::apply_pane_view_mode` 的空 `view_mode` fallback 已由 `FVM_ICON`／96 px 改為 `FVM_DETAILS`。非空值仍走既有 `parse_view_mode`，session schema、既有資料與 `core` 均未修改。
- Release configure/build PASS；CTest 在 sandbox 內為 10/11，只有 `panedock_launch_smoke` 因關窗後程序退出逾時失敗；同一測試在 sandbox 外重跑為 1/1 PASS（1.06 秒）。
- `panedock_explorer_host_lifetime_check.exe` PASS，initial view mode 為 `4`（`FVM_DETAILS`），且八種底層 mode/size 切換檢查通過；`git diff --check` PASS。
- 此環境未執行人工切換截圖。程式碼證據確認空值唯一 fallback 已改成 Details，非空保存值的分支未動；若要視覺留證，於真實桌面新增 tab，並重啟一個已保存為 icons 的 tab 各檢查一次即可。
