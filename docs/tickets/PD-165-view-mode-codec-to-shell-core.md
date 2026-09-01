# PD-165 — 把檢視模式字串 codec 移進 `shell_core`

Phase 7 · architecture · Depends on: PD-052, PD-079, PD-153, PD-156, PD-157

- Source: 2026-09-01 `improve-codebase-architecture` 架構審查，結論由背景 Codex 唯讀核對（判定 **VALID**）。
- Priority: LOW——目前功能正確，但這是 session document 的 persistence codec 卻住在視窗層，且 round trip 的另一半（`core::session`）已有測試，這一半沒有。

## Outcome

`view_mode_name` / `parse_view_mode` 這組「session 字串 ↔ `FOLDERVIEWMODE` + icon size」的 codec 移入 `shell_core`，並加上 round-trip 測試（含未知字串路徑）。選單標籤與 owner-draw 留在 `app_shell`。行為零變更。

## 已確認的現況（2026-09-01 工作樹，經 Codex 唯讀核對）

- `kViewModeOptions` 位於 `src/app_shell/main.cpp:250`。**它是 menu metadata**（label + mode + image size），不是 codec 本身。
- 真正的 codec 是 `view_mode_name`（`main.cpp:1527`）與 `parse_view_mode`（`main.cpp:1543`）。
- 呼叫點：`main.cpp:1575`（capture）、`:1603`（apply）、`:3524`／`:3538` 一帶（`show_view_mode_menu`）、`:5499-5503`（選單命令路由）。
- `TabState::view_mode` 是 `core` 的 persisted string（`src/core/model.h:32`），由 session serializer 讀寫（`src/core/session.cpp:414`）。
- `shell_core` 目前**沒有** view-mode codec；`ExplorerHost` 只提供 raw `FOLDERVIEWMODE` API（`src/explorer_host/explorer_host.h:62`）。
- PD-157 已建立 `shell_core` target 與 value-oriented API 慣例，可直接沿用。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §9.1：

> | `shell_core` | `IShellItem`、PIDL、Shell location identity、Shell 變更通知 | 對外傳遞原始 COM 指標 |

`docs/design-spec.md` §9.1：

> | `app_shell` | WinMain、STA 初始化、訊息迴圈、主視窗、命令路由 | 資料模型計算、Shell 呼叫 |

`AGENTS.md`：

> **Every persisted config/setting file must be designed for forward extensibility.** … A read that encounters a field it does not recognize preserves that field rather than silently dropping it on the next write-back.

`AGENTS.md`：

> Keep `src/core` free of HWND, COM and `windows.h`.

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/tickets.md` Agent 交付規則：

> 每個非平凡邏輯至少新增一個 focused runnable test 或 self-check。

## Files to read and trace first

- `src/app_shell/main.cpp:230-258`（`ViewModeSelection`、`ViewModeOption`、`kViewModeOptions`、icon size 常數）。
- `src/app_shell/main.cpp:1527-1560`（`view_mode_name`、`parse_view_mode`）。
- `src/app_shell/main.cpp:1570-1615`（`capture_pane_view_mode`、`apply_pane_view_mode`）。
- `src/app_shell/main.cpp:3510-3570`（`show_view_mode_menu`）與 `:5490-5510`（選單命令路由）。
- `src/shell_core/shell_core.h/.cpp`：PD-157 建立的既有 API 風格與 target。
- `src/core/model.h:32`（`TabState::view_mode`）、`src/core/session.cpp:414`（序列化）。
- `docs/tickets/PD-157-shell-core-location-boundary.md`、`PD-153-default-view-mode-details.md`、`PD-156-shell-sort-state-restore.md`（同類的「擷取／持久化／還原」先例）。

## Scope

1. 把 `ViewModeSelection`、`view_mode_name`、`parse_view_mode` 與其依賴的 icon size 常數（`kExtraLargeIconSize`、`kLargeIconSize`、`kMediumIconSize`、`kSmallIconSize`）移入 `src/shell_core/shell_core.h/.cpp`。
2. `kViewModeOptions` 的 **label 留在 `app_shell`**（UI 文字），`mode` 與 `image_size` 引用 `shell_core` 的型別。若拆開會造成無謂的重複，可整表留在 `app_shell` 並只引用 `shell_core` 的 codec 函式——擇一，在交接區寫出選擇理由。
3. 未知字串路徑必須維持既有語意：`parse_view_mode` 對不認識的字串回傳空 `optional`，呼叫端沿用既有 fallback（不得改成丟棄或覆寫該欄位——`AGENTS.md` 的前向相容規則）。
4. 新增 `tests/unit/shell_core_view_mode_test.cpp`（或加入既有 shell_core 測試）並註冊到 `tests/CMakeLists.txt`：
   - `kViewModeOptions` 全部八個選項的 `view_mode_name` → `parse_view_mode` round trip 完全相等。
   - 未知字串（如 `"nonsense"`、空字串）回傳空 `optional`。
   - `view_mode_name` 對同一 mode 但不同 image size 產生可區分的字串。
5. `app_shell` 不再自行定義該 codec；`main.cpp` 只保留 UI 標籤與選單路由。

## Non-goals

- 不改 session document 的 schema、欄位名稱或已寫入檔案的字串值——既有 `session.json` 必須照舊讀得回來。
- 不改任何預設檢視模式（PD-153 的 details 預設不變）。
- 不改排序 codec（PD-156 的範圍），也不順手一併搬移。
- 不改 `ExplorerHost::set_view_mode` / `get_view_mode` 的簽章。
- 不新增檢視模式選項、不改選單順序或標籤文字。

## Acceptance Criteria

1. `view_mode_name` 與 `parse_view_mode` 只存在於 `src/shell_core`；`Select-String -Path src/app_shell/main.cpp -Pattern 'FOLDERVIEWMODE'` 僅剩必要的型別引用（或無）。
2. 新測試涵蓋 Scope 第 4 點全部案例並通過。
3. 以既有 `%LOCALAPPDATA%\PaneDock\session.json` 啟動，全部 tab 的檢視模式與既有 build 完全一致（含 icon size）。
4. `ctest --test-dir build --output-on-failure` 全綠，含 `shell_core_boundary_check` 與 `panedock_launch_smoke`。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
Select-String -Path src/app_shell/main.cpp -Pattern 'view_mode_name|parse_view_mode'   # 只應剩呼叫，無定義
Select-String -Path src/shell_core/shell_core.* -Pattern 'view_mode_name|parse_view_mode'
```

```powershell
# 既有 session 的檢視模式還原不變
Copy-Item "$env:LOCALAPPDATA\PaneDock\session.json" "$env:TEMP\session-before.json"
$p = Start-Process build\PaneDock.exe -PassThru; Start-Sleep 4
$p.CloseMainWindow() | Out-Null; $p.WaitForExit(5000) | Out-Null
# 比對兩份 session 的 view_mode 欄位應完全相同
```

## Handoff requirements

在 `## 交接區` 記錄：`kViewModeOptions` 的拆或不拆決定與理由、round-trip 測試的八個字串值、以及既有 `session.json` 還原前後的 `view_mode` 欄位比對結果。

## 交接區

### 2026-09-01 implementation

- `ViewModeSelection`, the four icon-size constants, `view_mode_name`, and
  `parse_view_mode` now live in `shell_core`; `app_shell` keeps only the eight
  English menu labels and routes selections through the value API. The table
  was not split into a second shell-core metadata list because keeping labels
  beside the menu command wiring avoids duplicate UI metadata.
- Round-trip values are `FVM_ICON:256`, `FVM_ICON:96`, `FVM_ICON:48`,
  `FVM_ICON:16`, `FVM_LIST`, `FVM_DETAILS`, `FVM_TILE`, and `FVM_CONTENT`.
  `nonsense` and the empty string return an empty `optional`; icon sizes 48 and
  96 encode differently.
- No session schema or persisted string changed. The new focused test covers
  the codec; existing session restore remains value-compatible. A real-session
  before/after launch comparison was not run in the sandbox, so no runtime
  result is claimed here.
- Verification: `cmake --build build` passed; focused CTest for the new codec,
  `shell_core_boundary`, and `shell_reentry_gate` passed (3/3); `git diff
  --check` passed. The sandbox-sensitive launch smoke is recorded separately
  from these deterministic checks.
