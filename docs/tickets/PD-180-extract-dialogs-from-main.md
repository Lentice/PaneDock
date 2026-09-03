# PD-180 — 把三個對話框搬出 `main.cpp`，並收斂 window class 註冊樣板

Phase 7 · architecture · Depends on: PD-179

- Source: 同 PD-178（2026-09-03 使用者重構需求）。
- Priority: MEDIUM——**誠實標註**：本票減少的是 `main.cpp` 的檔案大小與導覽成本，**不是模組間耦合**。這三個對話框本來就幾乎不碰 pane 狀態，是「每個模組管理自己的內容」風險最低的一個示範，但不要把它算進解耦的帳上。若時間有限，本票是整個系列裡最可以延後的一張。

## Outcome

`pinned locations manager`、`startup notification`、`transfer close dialog` 三個對話框各自搬進獨立的 `src/app_shell/*.h/.cpp`，各自擁有自己的 window class 註冊、window proc、子控制項與版面計算。`register_window_class` 中四份逐字重複的 `WNDCLASSEXW` 填寫收斂成一個 helper。約 780 行離開 `main.cpp`。

行為與視覺零變更。

## 已確認的現況（2026-09-03 工作樹）

- 三個對話框在 `src/app_shell/main.cpp`：
  | 對話框 | 主要行範圍 | 內容 |
  |---|---|---|
  | pinned locations manager | 2510-2786 | `refresh_pinned_locations_manager_buttons`、`refresh_pinned_locations_manager`、`layout_pinned_locations_manager`、`pinned_locations_window_proc`（2586，151 行）、`destroy_pinned_locations_manager`、`show_pinned_locations_manager`（2742） |
  | startup notification | 4707-4886 | `layout_startup_notification`、`startup_notification_height`、`startup_notification_proc`（4759）、`show_startup_notification`（4841） |
  | transfer close dialog | 4887-5024 | `transfer_close_dialog_proc`（4887，102 行）、`show_transfer_close_dialog`（4989） |
- 三個 `show_*` 函式（`2742-2786`、`4841-4885`、`4989-5023`）是同一段「已開啟就早退 → `GetDpiForWindow` + `MulDiv` 縮放 → `CreateWindowExW(..., &state)` → 相對 owner 置中 → `SetWindowPos(HWND_TOP, ..., SWP_NOACTIVATE|SWP_SHOWWINDOW)` → `UpdateWindow`」的三份複製，其中置中那段約 10 行 ×3（差別只在用 client rect 還是 window rect）。
- `register_window_class`（`main.cpp:6288-6329`）把同一組 7 個 `WNDCLASSEXW` 欄位填了四次（pinned locations、transfer close、startup notification、主視窗）。
- 三個對話框各自的控制項 ID 是自己的區域編號（`kPinnedLocationsListId = 1` … `kPinnedLocationsCancelId = 7`；`kTransferCloseKeepOpenId = 1` … `kTransferCloseStatusId = 4`；`kStartupNotificationTextId = 1`、`kStartupNotificationOkId = 2`），彼此獨立，搬移後不需重編。
- 三者都透過 `GWLP_USERDATA` 取得 `AppState*`。**transfer close dialog 與 shutdown 流程耦合**：它由 `run_shutdown_action`（`main.cpp:5054`）叫起，其結果會回饋進 `ShutdownSequence`。`tests/release/shutdown_state_check.ps1` 以 `-SourcePath main.cpp` 掃描相關字串。
- `AppState` 中屬於這三者的欄位：`pinned_locations_window`、`pinned_locations_list`、`pinned_locations_buttons`、`pinned_locations_draft`、`pinned_fixed_labels`、`startup_notification`、`transfer_close_dialog`、`startup_error_message`、`startup_warning_message`。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> **App UI text must be English.** No Chinese strings ship in the binary.

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`docs/design-spec.md` §9.4：關機順序

> 順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。

`docs/design-spec.md` §9.1：

> | `app_shell` | WinMain、STA 初始化、訊息迴圈、主視窗、命令路由 | 資料模型計算、Shell 呼叫 |

`AGENTS.md`：

> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`docs/tickets.md` Agent 交付規則：

> 預設每個 ticket 的實作範圍為半天至兩天;若超過,先拆 ticket。

## Files to read and trace first

- `src/app_shell/main.cpp:2510-2786`、`4707-5024`：三個對話框的完整程式碼。
- `src/app_shell/main.cpp:5054-5143`：`run_shutdown_action`、`begin_shutdown`、`complete_deferred_close`——transfer close dialog 的呼叫者與回饋路徑。
- `src/app_shell/main.cpp:6288-6329`：`register_window_class`。
- `src/app_shell/main.cpp:2465-2509`：`append_startup_warning`、`save_now`、`schedule_session_save`——startup notification 的資料來源。
- `src/core/shutdown.h`：`ShutdownEvent` / `ShutdownAction` / `ShutdownSequence`，確認對話框回饋的事件名稱。
- `tests/release/shutdown_state_check.ps1`、`tests/release/startup_frame_order_check.ps1`：它們掃描的字串是否會隨搬移而消失。
- `src/sidebar/sidebar.h/.cpp`：本專案既有的「一個 UI 模組擁有自己的 HWND 與 proc」寫法，作為新檔案的範本。

## Scope

1. 新增三組檔案，命名與既有 `sidebar` 一致的風格：
   - `src/app_shell/pinned_locations_dialog.h/.cpp`
   - `src/app_shell/startup_notification.h/.cpp`
   - `src/app_shell/transfer_close_dialog.h/.cpp`
   加進 `CMakeLists.txt:98` 的 `PaneDock` target。
2. 每個模組**擁有自己的**：window class 名稱常數、註冊函式、window proc、控制項 ID、版面計算、DPI 縮放、`show` / `destroy`。
3. 對外介面遵循本系列的模組契約：**單向依賴，不持有回呼介面、不持有 coordinator 反向指標。** 具體做法：
   - 輸入以純資料傳入（例如 pinned locations 傳入 `core::ApplicationState` 的複本作為 draft）。
   - 輸出以回傳值或「可查詢的結果」表達（例如 `transfer_close_dialog` 回傳使用者選了哪個選項的 enum），由 `main.cpp` 的協調層據此推進 `ShutdownSequence`。
   - **若某個對話框無法在不引入回呼的前提下搬移**（最可能是 transfer close 與 shutdown reducer 的往返），就把該對話框留在 `main.cpp`，在交接區寫出具體阻礙，本票只交付另外兩個。**不要**為了搬完而引入一個只有單一實作的介面——`docs/development.md` 明文禁止無實測需要的抽象。
4. 新增 `register_simple_window_class(const wchar_t* name, WNDPROC proc, HINSTANCE, HBRUSH background)` helper，四處註冊改為呼叫它。helper 放在 `main.cpp` 匿名 namespace 或一個小的共用標頭，選 diff 較小者。
5. 新增共用的 `center_over_owner(HWND dialog, HWND owner, int width, int height)`，取代三份置中複製。若 client rect 與 window rect 的差異無法用同一個函式表達，就用一個布林參數，不要拆成兩個函式。
6. 同步更新受影響的 PowerShell 測試的 `-SourcePath`（可指向多個檔案，或改掃 `src/app_shell/`）。**不得**以放寬 pattern 讓它剛好還是綠的。

## Non-goals

- 不改任何對話框的版面、字串、尺寸、按鈕順序或 DPI 縮放數值。
- 不改 shutdown 的事件序列或 `ShutdownSequence` 的狀態機。
- 不搬 pane 相關的任何東西（PD-182／PD-183 處理）。
- 不搬 `explorer_host` 的 error window（PD-181 處理）。
- 不為了搬移而引入回呼介面或反向指標。
- 不重開「以獨立 process 隔離第三方 shell extension」。

## Acceptance Criteria

1. 至少兩個對話框已離開 `main.cpp`；若第三個留下，交接區寫出具體阻礙。
2. `main.cpp` 行數下降 ≥ 600。
3. `WNDCLASSEXW` 的欄位填寫在整個 `src/` 中只剩 helper 一處（`explorer_host.cpp:533` 的 error window class 除外，那屬 PD-181）。
4. 三個對話框的置中程式碼只剩一份。
5. PowerShell 測試全綠，且 `-SourcePath` 已更新到正確位置。
6. `ctest --test-dir build --output-on-failure` 全綠，含 `panedock_launch_smoke`。
7. 視覺與行為零變更。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# WNDCLASSEXW 填寫只剩一處
Select-String -Path src/app_shell/*.cpp -Pattern 'WNDCLASSEXW\s+\w+' | Measure-Object
# 新模組不得洩漏中文字串
Select-String -Path src/app_shell/pinned_locations_dialog.cpp,src/app_shell/startup_notification.cpp,src/app_shell/transfer_close_dialog.cpp -Pattern '[一-鿿]'
```

第二條必須無結果。

```powershell
# 關閉不殘留
$p = Start-Process build\PaneDock.exe -PassThru; Start-Sleep 3
$p.CloseMainWindow() | Out-Null
if (-not $p.WaitForExit(5000)) { throw 'process survived graceful close' }
```

```powershell
git diff --check
```

## 交給使用者的實機檢查清單

1. Pinned locations manager：開啟、新增、刪除、上移、下移、Apply、OK、Cancel，確認每個按鈕與清單行為不變；關閉後再開一次確認狀態正確。
2. Startup notification：以一個含不可解析位置的 Group 啟動（例如把某個 tab 指向已拔除的隨身碟），確認通知出現、文字正確、OK 可關閉。
3. Transfer close dialog：啟動一個大檔複製，複製途中關閉主視窗，確認三個選項（Keep open／Close after transfer／Cancel and close）各自行為與既有 build 一致。
4. 三個對話框都在多 DPI 螢幕上各開一次，確認縮放與置中位置正確。

## Handoff requirements

在 `## 交接區` 記錄：`main.cpp` 前後行數、三個對話框各自搬出的行數（若有留下的，寫出具體阻礙與需要什麼條件才能搬）、PowerShell 測試 `-SourcePath` 的更新內容、以及使用者實機檢查的回報結果。

## 交接區

### 實作與行數

- `src/app_shell/main.cpp`：6601 行降為 5988 行（-613），超過本票要求的 -600。
- 從原始 `main.cpp` 搬出的完整區段：pinned locations manager 281 行（2474-2753）、startup notification 175 行（4643-4817）、transfer close dialog 135 行（4818-4952）。三者均已實作於各自的 `.h/.cpp`，沒有留下空殼。
- `PinnedLocationsDialog` 持有 `core::ApplicationState` draft 與顯示文字資料，以 `take_result()` 回傳 Apply/OK 的結果；主協調層在訊息派送後更新 application 並排程儲存。
- `StartupNotification` 持有 warning message、子控制項、版面與 DPI 邏輯；`AppState` 僅保留物件與相容的 warning message reference。
- `TransferCloseDialog` 只持有具體 UI 狀態，以 `Result`/`take_result()` 輸出選項。為保留檔案操作期間取消的即時性，模組用既有 Win32 owner message 通知主視窗立即查詢結果；沒有 `AppState`、`ShutdownSequence` 或 callback interface。Shutdown event 仍只由 `main.cpp` 協調層送入 reducer。
- `register_simple_window_class` 與 `center_over_owner` 集中在 `src/app_shell/window_helpers.h/.cpp`；四個 class registration 都經由 helper，client/window rect 差異由 `center_over_owner` 的布林參數表達。`src/app_shell/*.cpp` 的 `WNDCLASSEXW` 填寫只剩 helper 一處。

### PowerShell source path

- `tests/CMakeLists.txt` 的 `panedock_shutdown_state` 改掃 `main.cpp,transfer_close_dialog.cpp`。
- `tests/CMakeLists.txt` 的 `panedock_startup_frame_order` 改掃 `main.cpp,startup_notification.cpp`。
- 兩支 script 的 `SourcePath` 仍是單一明確參數，內容以逗號分隔後逐檔讀取；既有 pattern 維持具體語意，startup class/cleanup pattern 改為對應新模組，沒有放寬匹配。

### 自動檢查

- Agent configure：PASS（LLVM-MinGW Clang/LLD + Ninja）。
- Agent build：PASS。
- `ctest --test-dir build --output-on-failure`：PASS，20/20，含 `panedock_shutdown_state`、`panedock_startup_frame_order`、`panedock_launch_smoke`。
- `Select-String -Path src/app_shell/*.cpp -Pattern 'WNDCLASSEXW\s+\w+' | Measure-Object`：PASS，Count = 1。
- 三個新 dialog `.cpp` 的中文字串掃描：PASS，無結果。
- ticket 的 graceful-close Agent Check（可寫桌面環境）：PASS；launch smoke：PASS。
- `git diff --check`：PASS，無結果。
- 受限 sandbox 內第一次 launch smoke 因 `%LOCALAPPDATA%\PaneDock` 拒絕寫入而觸發既有 save-failure MessageBox；以可寫桌面環境重跑後 PASS，並非保留中的程式殘留。

### 使用者實機檢查

尚未收到使用者對 pinned locations、startup notification、transfer close 三個流程及多 DPI 的實機回報；目前標記為待使用者依本票清單驗證。自動 launch smoke 只覆蓋啟動與 graceful close，不能取代上述視覺／互動檢查。
