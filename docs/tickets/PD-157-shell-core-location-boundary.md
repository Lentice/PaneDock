# PD-157 — 建立 `shell_core` 並移出 app_shell 的 Shell location/value 操作

Phase 7 · architecture · Depends on: PD-006, PD-022, PD-140, PD-146

- Source: 2026-08-31 spec／實作差異審查；使用者決策「`shell_core` 和 `file_operations` 分離：拆」。
- Priority: MEDIUM——目前功能可運作，但 `app_shell/main.cpp` 直接解析 Shell item、取得 display name 與 known-folder path，違反既有 module ownership，並讓後續 location identity 工作只能繼續堆進 app_shell。

## Outcome

建立可編譯的 `panedock_shell_core` 模組，讓 app_shell 只傳遞／接收 `core::ShellLocation`、字串與 filesystem path 等值；目前位於 app_shell 的 Shell location display/value 操作移入該模組，app_shell 不再直接呼叫對應 Shell identity APIs。

本票是現有行為的邊界整理，不宣稱已完成 `known_folder_identity + fallback_path` 的全部恢復策略。

## 已確認的現況

- `CMakeLists.txt` 保留被註解的 `panedock_shell_core` placeholder，但沒有 target 或來源目錄。
- `src/app_shell/main.cpp::session_directory` 直接呼叫 `SHGetKnownFolderPath(FOLDERID_LocalAppData, ...)`。
- `src/app_shell/main.cpp::display_text_for_parsing_name` 直接呼叫 `SHCreateItemFromParsingName` 與 `IShellItem::GetDisplayName`。
- `src/app_shell/main.cpp::location` 建立只有 parsing name 的 `core::ShellLocation`。
- `ExplorerHost::navigate` 自己解析目的 Shell item；這是 browser hosting responsibility，不是本票要搬動的 app-shell value helper。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §9.1：

> | `shell_core` | `IShellItem`、PIDL、Shell location identity、Shell 變更通知 | 對外傳遞原始 COM 指標 |

`docs/design-spec.md` §9.1：

> `core` 刻意不含 COM——它是本專案唯一的自動測試 seam。`shell_core` 對外提供 location 與 identity 這類值,而非原始 COM 指標。

`docs/design-spec.md` §10：

> 不得寫入 PIDL 或 COM 指標。持久化的 identity 為 parsing name ＋ known-folder identity ＋ fallback path。display name 永不作為 identity。

`docs/development.md` Module boundaries：

> | `shell_core` | `IShellItem`, PIDL, Shell location identity, change notification | Handing raw COM pointers to callers above it |

`AGENTS.md`：

> Keep `src/core` free of HWND, COM and `windows.h`.

`AGENTS.md`：

> Never persist a PIDL or a COM pointer. Persisted identity is parsing name plus known-folder identity plus a fallback path. Display names are never identifiers.

`AGENTS.md`：

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

## Files to read and trace first

- `CMakeLists.txt` 的 shell-facing target 與 link libraries。
- `src/app_shell/main.cpp`：`session_directory`、`location`、`display_text_for_parsing_name` 及所有 caller；追蹤其 `ShellCallScope`／shutdown guard。
- `src/core/model.h`：`ShellLocation` value type。
- `src/core/session.cpp`：三欄 identity 的既有 persistence；不得改 schema。
- `src/explorer_host/explorer_host.h/.cpp`：browser navigation ownership，避免把 ExplorerHost 搬進 shell_core。
- `tests/release/shell_reentry_gate_check.ps1`：現有 source-level re-entry assertion。
- `docs/tickets/PD-006-session-document-persistence.md`、`PD-022-unresolvable-location-error-and-retry.md`、`PD-140-shell-call-reentry-shutdown-gate.md`、`PD-146-display-name-shell-reentry-guard.md`。

## Scope

1. 新增 `src/shell_core/shell_core.h/.cpp` 與 `panedock_shell_core` static target；只連結既有 Windows libraries，並讓 `PaneDock` 連結該 target。
2. 提供最小 value-oriented API，涵蓋：
   - 由 parsing name 建立 `core::ShellLocation`；
   - 取得 virtual parsing name 的正常顯示文字，失敗時回傳原 parsing name；
   - 取得 `%LOCALAPPDATA%\PaneDock` directory。
3. API 不得把 `IUnknown*`、`IShellItem*`、PIDL、`ComPtr` 或 ownership-sensitive handle 傳回 app_shell。可在 `.cpp` 內暫時使用 PIDL／COM，離開函式前完整釋放。
4. app_shell 的每個 Shell call 仍必須落在既有 `ShellCallScope` 內；移動函式不能繞過 PD-140／PD-146 的 deferred shutdown 判斷。需要 wrapper 時只在共享 caller seam 加一次。
5. `ExplorerHost::navigate`／`Destroy`、session atomic-write、file operations、drag/drop 與 UI 行為保持原位且行為不變。
6. 新增一個 CTest source-boundary check，至少證明 `src/app_shell/main.cpp` 不再直接出現 `SHGetKnownFolderPath`、location-display 用的 `SHCreateItemFromParsingName`／`GetDisplayName` 實作，且 `src/core` 不包含 Windows headers。
7. 既有 session JSON byte semantics、unknown-field preservation、fallback behavior 與錯誤提示不得改變。

## Non-goals

- 不在本票填滿 `known_folder_identity` 或 `fallback_path`，不設計 migration；該功能需以另一張 ticket 定義 capture／resolve precedence 與真實 known-folder checks。
- 不把 `ExplorerHost`、`IExplorerBrowser`、`IFileOperation`、clipboard 或 OLE drag/drop 搬進 shell_core。
- 不新增 interface/factory、dependency injection、COM fake、背景執行緒或 async runtime。
- 不重新命名 `core::ShellLocation` 欄位，不改 session schema version。
- 不改 display text、sidebar/tab UI 或 navigation history。

## Acceptance criteria

1. `panedock_shell_core` 為獨立 target，app_shell 經由 value API 取得 location、display text 與 session directory。
2. app_shell 不再直接持有上述 location/value Shell API 的 COM implementation；沒有 raw COM pointer 從 shell_core 跨出。
3. virtual location 顯示、普通 path 顯示、session directory 與 startup/recovery 行為與拆分前一致。
4. PD-140／PD-146 的 re-entry/shutdown source checks 仍通過；close/startup smoke 無退化。
5. `src/core` 保持 platform-free，session schema 與 persisted bytes 沒有非必要改動。
6. Release build、完整 CTest、focused boundary check、launch smoke 與 `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
pwsh -NoProfile -File tests/release/shell_reentry_gate_check.ps1
git diff --check
```

```powershell
rg -n "SHGetKnownFolderPath|SHCreateItemFromParsingName|GetDisplayName" src/app_shell/main.cpp
# 只允許與本票 non-goal 明確相符的剩餘 caller；交接區逐項解釋。
```

## 交接區

<!-- 實作 agent 填寫，append-only -->

### 2026-08-31 — implemented

- 新增 `panedock_shell_core` static target 與 `src/shell_core/shell_core.h/.cpp`；公開 API 只傳遞 `core::ShellLocation`、字串、`std::filesystem::path` 與 `std::optional`。`SHGetKnownFolderPath`、virtual-location 的 `SHCreateItemFromParsingName`／`GetDisplayName` 已移出 app_shell，沒有 raw COM pointer、PIDL 或 ownership-sensitive handle 跨出模組。
- app_shell 保留單一薄 wrapper：非 virtual parsing name 繼續走原本無 Shell call 的 fast path；virtual display lookup 與 startup session-directory lookup 仍由既有 `ShellCallScope` 包住，回傳後檢查 deferred shutdown。`ExplorerHost`、session schema／bytes、file operations 與 drag/drop 未更動。app_shell 唯一剩餘的 `SHCreateItemFromParsingName` 位於 `perform_clipboard_paste`，屬本票明確排除的 file-operation destination 建立。
- 新增並註冊 `panedock_shell_core_boundary` source check；它驗證 app_shell 不持有 location/value Shell API、public header 不洩漏 COM/PIDL、`src/core` 保持 platform-free，且只允許上述 clipboard caller。既有 `panedock_shell_reentry_gate` 已改為跨 `shell_core.cpp` 驗證同一條 guarded display lookup。
- LLVM-MinGW Release configure/build PASS；focused boundary check PASS；shell re-entry check PASS；非 launch CTest 11/11 PASS；`git diff --check` PASS。`panedock_launch_smoke` 在目前 sandbox 連跑兩次皆 FAIL：主視窗關閉後 30 秒內未退出；測試腳本清理後無殘留 PaneDock process，未手動終止任何使用者 process。此 sandbox desktop smoke 結果不宣稱為產品 shutdown regression，需在可互動／elevated desktop 重跑確認。

### 2026-08-31 — 主代理審查與最終驗證

- 審查 module API、COM ownership、`ShellCallScope` 與 source-boundary checks，未發現需擴張修改的缺口。公開 header 只含 value types；COM allocation 與 `CoTaskMemFree` 均留在 `.cpp` 並於所有分支釋放。
- Release configure/build PASS；sandbox 內非 launch CTest 11/11 PASS；focused boundary 與 shell re-entry checks PASS；`git diff --check` PASS。依 PD-149 已知 `%LOCALAPPDATA%` sandbox 權限限制，在 elevated context 重跑 `panedock_launch_smoke` 1/1 PASS（1.06 秒）。

### 2026-08-31 review correction

- Tracker status returned to `in_progress` pending the ticket's real-desktop virtual-location, ordinary-path, startup, and recovery behavior matrix. Automated boundary and launch checks do not prove that matrix by themselves.

### 2026-09-01 — real-desktop location/startup/recovery matrix

- 在實際 `build\\PaneDock.exe` Release 視窗的 `Group 1` 四窗格中，將左上 tab 導覽至 `shell:MyComputerFolder`；tab 與位址列均顯示友善名稱 `本機`，內容列出 3 個本機磁碟。這確認 virtual location display/value path 未把 parsing name 泄漏到 UI。
- 將同一 tab 導回普通路徑 `C:\\Windows`；tab 顯示 `Windows`、位址列顯示 `C:\\Windows`，內容正常載入。普通 path 顯示與 virtual location 均可在同一 Group 內切換。
- 以標題列正常關閉後，fresh app listing 找不到 `process:E:\\GitHub\\PaneDock\\build\\PaneDock.exe`；再啟動同一實際 exe 只出現一個 PaneDock 視窗，`Group 1`、`Windows` tab、四窗格與先前 session 內容（含 PD-158 disposable fixture 的 moved item）均恢復，無空白主窗或 startup/recovery error。
- 上述桌面矩陣與既有 boundary、re-entry、Release build／CTest／launch smoke 證據合併，完成本票 review correction；未改 session schema 或 persisted bytes。

### 2026-09-01 — final automated verification

- `cmake --build build` PASS（Ninja reports no work）；提升環境完整 `ctest --test-dir build --output-on-failure` `13/13 PASS`，包含 `panedock_launch_smoke`。
- `shell_core_boundary_check.ps1`、`shell_reentry_gate_check.ps1`、`live_view_count_parse_check.ps1` 與 `git diff --check` 均 PASS。
