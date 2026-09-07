# Audit: PaneDock

## Summary

本次有界稽核確認三項 High，已依使用者後續授權直接修正。最大的架構風險是「仍存活的 Shell view」與「目前 model 的工作情境」並不等價；本次兩項正確性問題都來自這個交界，最直接的結果是新 Group 沿用已刪除 Group 的資料夾。

PaneDock 由 `wWinMain` 啟動，以 `app_shell` 協調 Group、Pane、Shell view 與關閉流程；`core` 管理純資料與 session JSON，`SessionWriter` 負責儲存時機，資料位於 `%LOCALAPPDATA%\PaneDock`。未新增相依性、schema 或介面。

## Findings

### [High] 刪除最後一個 Group 後，新 Group 沿用舊 view（已修正）

- **Location**: [main.cpp:1623](../../src/app_shell/main.cpp#L1623)。
- **Mechanism**: `delete_group` → `apply_layout` 的無 Group 分支原本只隱藏 view；之後 `add_group` 的 `was_empty` 分支經同 ID 的 `activate_group` 直接套用版型，已 realized 的 pane 不會重新初始化或導覽。
- **Failure**: 在唯一 Group 開啟資料夾 A，刪除該 Group，再新增 Group；新 Group 的預設位置應為 My Computer，卻仍顯示 A，後續 `capture_location` 將 A 寫回新 Group。
- **No guard**: 已檢查 `delete_group`、`add_group`、`activate_group` 與 `apply_layout` 的 realized 分支，沒有補做導覽。
- **Direction / applied**: 無 Group 的共用版面分支改呼叫既有 `Pane::derealize()`，仍置於 `ShellCallScope` 內，保留 pane HWND。新 Group 因而正常 realize 預設位置，也恢復只有可見 pane 持有 live view 的不變量。

### [High] 導覽未完成或失敗時，舊 view 設定覆蓋目的分頁（已修正）

- **Location**: [explorer_host.cpp:682](../../src/explorer_host/explorer_host.cpp#L682)、[pane.cpp:896](../../src/app_shell/pane.cpp#L896)。
- **Mechanism**: `navigate` 先更新 requested location；`save_now` → `capture_locations` → `Pane::capture_location` 仍呼叫 `get_view_mode`／`get_sort`，而 Shell 可能仍提供前一資料夾的 view。
- **Failure**: 從圖示模式的分頁切換至不可解析、原本保存 Details／另一排序的分頁，接著自動儲存或關閉，目的分頁的必要設定被舊 view 的值取代。
- **No guard**: 呼叫端只檢查 realized 與 HRESULT；Group 的 capture suppression 在發出導覽後就解除，不能涵蓋非同步等待與失敗狀態。
- **Direction / applied**: 記錄最後成功完成的 navigation generation。四個共用 view-mode／sort 讀寫入口在其與最新請求不符時回傳 `E_PENDING`；只有最新成功完成事件才解除限制。

### [High] 跨 pane 搬移分頁會遺失未知 session 欄位（已修正）

- **Location**: [session.cpp:418](../../src/core/session.cpp#L418)。
- **Mechanism**: `finish_tab_drag` → `core::move_tab` 保留 tab ID；`serialize_session` 原本僅在目的 pane 的舊 JSON 依 tab ID 找保留資料。
- **Failure**: 載入含未知 tab／shell-location 欄位的 session，將該分頁拖到另一 pane，再儲存；兩層未知欄位均被丟棄，違反前向相容規則。
- **No guard**: 已檢查搬移呼叫端、`move_tab`、`SessionDocument` 與 `SessionWriter`；沒有其他欄位搬移機制。
- **Direction / applied**: 目的 pane 找不到舊 tab 時，改在同 Group 的舊 panes 依 ID 尋找；沿用既有 JSON 保留方式，不修改 schema。

## Observations

- 導覽 completion 仍以 FIFO 配對 Shell 事件；本次沒有取得事件亂序的實機證據，不將它列為缺陷，也未重寫此機制。
- 無 Group 分支的回歸檢查是來源接線檢查，不能取代「刪除最後 Group → 新增 Group」的桌面驗收。
- 原始碼與文件已有顯著演進；AGENTS.md 的 greenfield 敘述已不符合現況，本次未擴大修改歷史／階段文件。

## Recommended order

1. 三項程式修正與對應檢查已完成。
2. 下次有可寫且可供測試的 session 環境時，執行最後 Group 刪除／重建，以及慢速位置切換後關閉重開的桌面驗收。

## Coverage

- **Budget / read**: 76 個追蹤原始碼檔（66 個 C++／header／Python，另 10 個 PowerShell）；預算 15 主選＋8 追查。本次閱讀函式本體共 **15 主選＋7 追查＋0 超額**，包含重點區段閱讀，並非每個大檔逐行覆蓋。
- **Plan**: `src/app_shell/{main.cpp,pane.cpp,pane.h,session_writer.cpp,session_writer.h}`、`src/core/{model.cpp,model.h,session.cpp,session.h,shutdown.cpp,navigation.h}`、`src/explorer_host/{explorer_host.cpp,explorer_host.h}`、`src/shell_core/shell_core.cpp`、`src/file_operations/file_operations.cpp`。
- **Reserve**: `src/core/shutdown.h`、`src/app_shell/pane_host.h`、`tests/unit/{core_session_test.cpp,explorer_host_lifetime_check.cpp,test_util.h}`、`tests/release/{shell_reentry_gate_check.ps1,launch_smoke.ps1}`。
- **Flows traced**: 分頁導覽／失敗／設定擷取；Group／版型切換；session 寫入／備份復原；剪貼簿操作與關閉閘門。
- **Map**: 已讀根與 tests CMake、toolchain、設計／開發／測試文件；沒有 README。最大檔為 `main.cpp` 4,329 行、`pane.cpp` 1,414 行、`explorer_host.cpp` 1,094 行。近六個月 churn 以 `main.cpp` 181 次、`explorer_host.cpp` 29 次為主要原始碼熱點。
- **Ruled out within coverage**: 正常關閉路徑先 Destroy views 再 DestroyWindow；已存在 Shell／drag 閘門與備份原子替換、損壞 primary 不覆盖良好 backup 的處理，未將這些已處理事項重報。
- **Read, no independent finding**: `core/model.*`、`core/shutdown.*`、`core/navigation.h`、`app_shell/pane.h`、`app_shell/pane_host.h`、`app_shell/session_writer.*`、`shell_core/shell_core.cpp`、`file_operations/file_operations.cpp`、`tests/unit/test_util.h`。
- **Unreached**: sidebar／dialog／tab-strip gesture 的完整流程、混合 DPI、第三方 extension、斷線網路與十分鐘 idle 量測；本次限於上述重要流程，未宣稱全 repo 無缺陷。
- **Worktree**: 進場時只有未追蹤 `.claude/`；保留不動。沒有 commit 或 push。

### Validation

- LLVM-MinGW Release configure/build 成功。首次重建 `session.cpp` 出現原有的 `TabState::history` 缺省 aggregate initializer warning；本次沒有更動該初始化行。
- 修正前：新增的 session 搬移案例 2 個斷言失敗；真實 ExplorerHost lifetime check 中新增的設定存取斷言 12 個失敗；空 Group 的來源接線檢查失敗。
- 修正後：上述三項皆通過；`ctest --test-dir build --output-on-failure -E '^panedock_launch_smoke$'` **26/26 通過**，含真實 ExplorerHost 的建立／失敗導覽／Destroy 檢查。
- `panedock_launch_smoke` **未執行**：它使用 repository 外的實際 session，且失敗清理包含 `Stop-Process -Force`，不符合本次資料範圍與不可略過 `IExplorerBrowser::Destroy` 的限制。因此不是完整 27 項 CTest 全綠，也未完成本次桌面操作驗收。
- `git diff --check` 通過。
