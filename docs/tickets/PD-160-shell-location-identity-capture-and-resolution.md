# PD-160 — 完成 Shell location 三段式 identity 擷取與還原

Phase 7 · Shell location identity correctness · Depends on: PD-006, PD-022, PD-140, PD-157

- Source: 2026-09-01 `improve-codebase-architecture` 收斂審查；四輪反證後唯一保留的 Strong deepening opportunity。
- Priority: HIGH——session schema 雖已保存 `parsing_name`、`known_folder_identity`、`fallback_path`，目前所有建構與導覽完成路徑卻只填第一欄。這違反 binding persistence identity，並讓 known folder 重新導向、一般 filesystem fallback 與不可解析 location 的還原都只依賴單一 parsing string。

## Outcome

深化既有 `shell_core` module：擷取完整 `core::ShellLocation`，並以明確 precedence 將保存的 identity 解析成既有 `ExplorerHost` 可導覽的 Shell target。公開介面仍只傳 value type；COM pointer 與 PIDL 不得跨出 Shell-facing implementation。

完成後：

- exact known folder 保存 canonical braced GUID identity；
- filesystem／UNC location 在 Shell 提供時保存 fallback path；
- restore／retry 優先使用穩定 known-folder identity，再嘗試 parsing name，最後嘗試 fallback path；
- 全部候選均不可解析時，仍保留並嘗試原 `parsing_name`，讓既有 FR-012 error state 顯示、保存並可重試，不得換成預設 location。

## 已確認的現況與根因

- `core::ShellLocation` 與 session JSON 自 PD-004／PD-006 起已有三欄；不需要 schema change 或 migration。
- `shell_core::location(std::wstring)` 目前只做 `return {parsing_name, {}, {}}`。
- `handle_navigation_complete` 經該函式建立 location；`capture_pane_location` 更只覆寫 `tab.location.parsing_name`，因此另外兩欄永遠空白，未來即使填入也可能被留下過期值。
- `ExplorerHost` 只保存字串 `location_`；failed navigation 與 Retry 因而不知道完整 persisted identity。
- PD-157 刻意只建立 value seam，Non-goal 明文把三欄 capture／resolve precedence 與真實 known-folder checks 留給另一張 ticket；本票就是該 follow-up，不覆寫 completed ticket。
- `docs/tickets.md` 的 2026-08-27 audit 曾把空白 known-folder/fallback 列為單一 agent、未開票發現；PD-157 後續建立 `shell_core` seam 並再次明文確認缺口，構成本票的新證據。這不是重開「已否決的方向」。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §9.1：

> | `explorer_host` | 每個已 realize pane 的 `IExplorerBrowser`、site 物件、生命週期、事件 | 產品層決策、持久化 |

> | `shell_core` | `IShellItem`、PIDL、Shell location identity、Shell 變更通知 | 對外傳遞原始 COM 指標 |

> `core` 刻意不含 COM——它是本專案唯一的自動測試 seam。`shell_core` 對外提供 location 與 identity 這類值,而非原始 COM 指標。

`docs/design-spec.md` §10：

> **不得寫入 PIDL 或 COM 指標。** 持久化的 identity 為 parsing name ＋ known-folder identity ＋ fallback path。display name 永不作為 identity。

`docs/design-spec.md` §FR-012／NFR-003：

> 無法解析的 Shell location 在 tab 內顯示可復原錯誤,保留設定,並可重試。不得因此刪除任何已儲存的設定。

> Group 切換、版型切換、tab 切換不得因為某個 Shell location 緩慢或無法連線而凍結 UI。

`docs/development.md`：

> Keep `src/core` free of HWND, COM and `windows.h`.

> Shell APIs re-enter our message loop. Any host-side lock, and any "close the view then wait for its event" sequence, must be reentrancy-safe or it will deadlock.

`AGENTS.md`：

> Never persist a PIDL or a COM pointer. Persisted identity is parsing name plus known-folder identity plus a fallback path. Display names are never identifiers.

> Every persisted config/setting file must be designed for forward extensibility. It carries an explicit schema version from its first version. A read that encounters a field it does not recognize preserves that field rather than silently dropping it on the next write-back.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/testing.md`：

> `explorer_host`, `shell_core` and `file_operations` have no automated tests.

> A test double for `IExplorerBrowser` would assert our assumptions about the COM host contract rather than the contract itself — it would pass while the real integration is broken, which is worse than no test at all.

## Native API facts to preserve

- `IKnownFolderManager::FindFolderFromIDList` maps an absolute IDList to an `IKnownFolder`; `IKnownFolder::GetId` returns its `KNOWNFOLDERID`. Use it only when the completed item itself is an exact registered known folder; do not label arbitrary descendants with an ancestor identity.
- `IShellItem::GetDisplayName(SIGDN_FILESYSPATH)` returns a filesystem path only when the item exposes one; failure for virtual locations is normal and means `fallback_path` stays empty.
- `SHCreateItemFromParsingName` creates a Shell item from a parsing name. A failed candidate is not permission to discard or rewrite the saved `ShellLocation`.
- Official references:
  - <https://learn.microsoft.com/windows/win32/api/shobjidl_core/nf-shobjidl_core-iknownfoldermanager-findfolderfromidlist>
  - <https://learn.microsoft.com/windows/win32/shell/working-with-known-folders>
  - <https://learn.microsoft.com/windows/win32/api/shobjidl_core/ne-shobjidl_core-sigdn>
  - <https://learn.microsoft.com/windows/win32/api/shobjidl_core/nf-shobjidl_core-shcreateitemfromparsingname>

## Files to read and trace first

- `CONTEXT.md`：`Shell location`、`unresolvable location`、`Pinned Location` 的 domain meaning。
- `src/core/model.h`：`ShellLocation` 三欄 value type；不得加入 Windows type。
- `src/core/session.cpp`、`tests/unit/core_session_test.cpp`：既有三欄 serialization、unknown-field preservation 與 round trip；schema bytes 不重設計。
- `src/shell_core/shell_core.h/.cpp`：目前一行 `location`、display helper、COM/PIDL ownership。
- `src/explorer_host/explorer_host.h/.cpp`：`initialize`、`navigate`、`retry_navigation`、`location`、navigation callback、`navigation_complete`、`navigation_failed` 及所有 caller。
- `src/app_shell/main.cpp`：`location` wrapper、`navigate_realized_panes`、`capture_pane_location(s)`、`handle_navigation_complete`、Group/tab/history/address/Pinned Location/layout 切換與 clipboard destination 的全部 navigation caller。
- `CMakeLists.txt`：`panedock_shell_core`／`panedock_explorer_host` dependencies；不得形成循環 target dependency。
- `tests/release/shell_core_boundary_check.ps1`、`shell_reentry_gate_check.ps1`：value seam 與 Shell-call re-entry guard。
- `docs/tickets/PD-006-session-document-persistence.md`、`PD-015-app-shell-core-state-wiring.md`、`PD-022-unresolvable-location-error-and-retry.md`、`PD-111-pinned-locations-menu.md`、`PD-140-shell-call-reentry-shutdown-gate.md`、`PD-157-shell-core-location-boundary.md`。

## Concrete scope

1. Replace the shallow parsing-only helper with the minimum value-oriented operations in `shell_core`:
   - capture a `core::ShellLocation` from a successfully resolved Shell item/parsing name;
   - resolve a saved `core::ShellLocation` to one navigation target using the precedence below.
   Names and exact signatures may follow the existing style, but the public header may expose only `core::ShellLocation`, strings, standard-library value types and error/result values—never COM/PIDL/Win32 ownership types.
2. Capture rules:
   - always retain the canonical `SIGDN_DESKTOPABSOLUTEPARSING` value as `parsing_name`;
   - when `IKnownFolderManager::FindFolderFromIDList` identifies the exact item, store `IKnownFolder::GetId` as a canonical braced GUID string in `known_folder_identity`;
   - when `SIGDN_FILESYSPATH` succeeds, store it in `fallback_path`; otherwise leave fallback empty;
   - any optional identity lookup failure degrades to the fields already obtained; it never turns a successful navigation into failure.
3. Resolution precedence is binding for this ticket:
   1. a syntactically valid, installed `known_folder_identity` resolved through the Known Folder APIs;
   2. `parsing_name` through the normal Shell parser;
   3. `fallback_path` through the normal Shell parser;
   4. if no candidate resolves, return/attempt the original `parsing_name` so the existing recoverable error state retains the requested location.
   Empty, malformed and duplicate candidates are skipped. Do not probe the filesystem directly with `std::filesystem`, `GetFileAttributes`, or path existence checks.
4. Keep browser navigation and error UI in `ExplorerHost`, but make its requested/current location state and navigation callback carry the full `core::ShellLocation` value. `navigate` and Retry must re-run the shared `shell_core` resolution policy, so a reconnected or redirected location can recover without losing its saved identity. `IExplorerBrowser`, PIDL and `IShellItem` ownership stays inside Shell-facing `.cpp` files.
5. `navigation_complete` captures the canonical three-part identity once and sends that value to app_shell. `handle_navigation_complete` records it directly; `capture_pane_location` must stop updating only `parsing_name` and must not leave stale known-folder/fallback fields. Trace Group switching, tab switching, history, address submission, layout realization, Pinned Location addition and retry so every sibling caller uses the same value path.
6. All new Shell calls participate in the existing `ShellCallScope`／deferred-shutdown gate. After any re-entrant capture/resolve call, the caller must abort its continuation when shutdown is deferred or closing. Do not add locks, threads, polling, timers or synchronous network reachability probes.
7. Keep `core::ShellLocation`, schema version, JSON field names, unknown-field preservation and atomic replacement unchanged. Existing parsing-only documents remain valid and are enriched only after a successful capture; no eager startup migration or bulk resolution of inactive tabs.
8. Update the existing source-boundary/re-entry checks only enough to prove:
   - `shell_core` public headers remain value-only;
   - app_shell does not reimplement known-folder/PIDL/Shell identity logic;
   - new call paths retain the shared re-entry guard.
   Do not claim source matching proves runtime Shell behavior.

## Non-goals

- No session schema migration, field rename, destructive reinterpretation or eager rewrite of every saved tab.
- No COM fake, `IExplorerBrowser` abstraction, dependency injection framework, factory or second navigation implementation for tests.
- No PIDL, COM pointer, display name or HWND persisted in the session document.
- No filesystem-direct existence checks or file operations; this ticket changes Shell location identity only.
- No changes to view mode, sort, selection, scroll, Group/pane/tab ownership, layout, file operations, drag/drop or Shell context menus.
- No new UI strings, automatic retry, background worker, async runtime, timeout or polling timer.
- Do not broaden known-folder identity to descendants of a known folder.

## Acceptance criteria

1. A completed exact known folder stores non-empty canonical `parsing_name` and `known_folder_identity`; a filesystem-backed local/UNC location stores non-empty parsing name and fallback path when Shell exposes one; a virtual non-filesystem location may have empty fallback.
2. Restore and Retry use the single shared precedence: valid known folder → parsing name → fallback path → original parsing target for recoverable failure. Malformed/stale optional fields do not discard the saved location or navigate to the default location.
3. Navigation completion, pre-save capture, history entries and Pinned Locations never retain a new parsing name with stale known-folder/fallback fields.
4. A parsing-only v1 session still loads unchanged. It is not bulk-resolved at startup; after a successful navigation, the affected tab alone is enriched and the next normal save preserves all three fields plus unknown JSON fields.
5. `ExplorerHost` still owns `IExplorerBrowser` navigation/lifetime/error UI; `shell_core` owns identity checks; app_shell owns routing and persistence orchestration. No raw COM/PIDL/Win32 ownership type crosses the value seam or enters `core`.
6. Close during capture/resolve cannot continue into navigation, callback installation or persistence after deferred shutdown. Every initialized browser is still `Destroy`ed before its parent HWND.
7. Existing core session round-trip tests, focused source checks, Release build, complete CTest, launch smoke and `git diff --check` pass.
8. Real-desktop evidence separately records all rows below; source inspection or a fake is not accepted as runtime proof:
   - exact Desktop or Documents known folder: captured GUID is non-empty and restart returns to that known folder;
   - ordinary local folder: fallback path is populated and restart returns to it;
   - reachable UNC share: fallback path is populated; after disconnect/restart the same tab shows the recoverable error without losing any identity field; reconnect + Retry succeeds;
   - virtual non-filesystem location such as My Computer: parsing identity restores and empty fallback is accepted;
   - precedence fixture: with a valid known-folder GUID plus deliberately unusable parsing/fallback strings, restore reaches the known folder; original user session files are backed up and restored in `finally`.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
pwsh -NoProfile -File tests/release/shell_core_boundary_check.ps1
pwsh -NoProfile -File tests/release/shell_reentry_gate_check.ps1
git diff --check
```

```powershell
rg -n "known_folder_identity|fallback_path|ShellLocation|navigation_complete|capture_pane_location|retry_navigation|\.navigate\(" src tests
```

The second command is a caller audit, not a PASS by itself. Run `panedock_launch_smoke` with writable `%LOCALAPPDATA%\PaneDock`; an intentional save-failure MessageBox in a restricted session is not a shutdown regression.

## Handoff requirements

- Record the final public value signatures and every changed caller.
- Record the exact capture and resolution precedence, including malformed/empty field handling and the final unrecoverable fallback.
- Record why no schema migration, COM fake, filesystem-direct probe or eager startup resolution was added.
- Record deterministic checks separately from the real-desktop matrix; never report source matching as Shell runtime proof.
- For any real-session edit, record backup/restore paths and confirm the original `session.json`/`.bak` were restored in `finally`.

## 交接區

<!-- 實作 agent 填寫，append-only -->

### 2026-09-01 implementation

- Public value API: `shell_core::capture_location(std::wstring) -> core::ShellLocation` and `shell_core::resolve_location(const core::ShellLocation&) -> std::wstring`; `ExplorerHost::{initialize,navigate,location}` and its completion callback now carry the full `core::ShellLocation` value.
- Changed callers: realized Group panes, pane/tab activation, tab close/add/move, history, address submission, fixed and user Pinned Locations, initialization, Retry, pre-save capture, clipboard destination, and navigation completion.
- Capture stores canonical `SIGDN_DESKTOPABSOLUTEPARSING`, exact `FindFolderFromIDList`/`GetId` braced GUID when available, and `SIGDN_FILESYSPATH` when available. Optional lookup failure leaves the fields already captured.
- Resolution skips empty/malformed/uninstalled candidates and uses installed known-folder GUID → resolvable parsing name → distinct resolvable fallback path → original parsing name. It does not probe the filesystem.
- No schema migration, COM fake, filesystem-direct probe, or eager startup resolution was added: v1 already owns all three fields, and only successful realized navigation enriches one tab.
- Deterministic checks: compilation of all changed objects passed; `shell_core_boundary_check.ps1`, `shell_reentry_gate_check.ps1`, and `git diff --check` passed; 12/13 CTest tests passed. Final executable relink and `panedock_launch_smoke` were blocked because an existing `PaneDock.exe` process held `build\PaneDock.exe` open.
- Real-desktop matrix: not run. No real session file was edited, so there are no backup/restore paths.

### 2026-09-01 verification follow-up

- Release build linked successfully. All 13 CTest tests passed; `panedock_launch_smoke` passed in elevated context with writable real `%LOCALAPPDATA%\PaneDock` storage (1.62 s).
- `shell_core_boundary_check.ps1`, `shell_reentry_gate_check.ps1`, and `git diff --check` passed. The first restricted smoke attempt was correctly classified as a save-failure MessageBox caused by sandboxed Known Folder storage, not a shutdown regression.
- The ticket's separate real-desktop Desktop/Documents, local folder, UNC disconnect/reconnect, virtual location, and precedence matrix remains unrun.
