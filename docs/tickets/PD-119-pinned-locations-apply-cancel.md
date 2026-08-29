# PD-119 — Pinned Locations 管理視窗改用 Apply / Cancel

Phase 7 · app_shell · Depends on: PD-112

- Source: 使用者要求(2026-08-29)。
- Origin: 使用者原文:「For manager quick paths dialog it should have a apply and a cancel button」
- Override: PD-112 的「Remove/Move 立即更新 `ApplicationState` 並 `save_now`」與 `Close` 按鈕決策由本票覆寫。

## Product decision for this ticket

`Remove`、`Move Up`、`Move Down` 只修改管理視窗的暫存清單。`Apply` 將全部暫存變更寫回共享 `ApplicationState`、呼叫既有 `save_now`，並關閉視窗；`Cancel` 放棄本次暫存變更並關閉視窗；標題列的 X 視為 Cancel。這是本次需求在未另行指定關閉語意下的最小一致解讀。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Every persisted config/setting file must be designed for forward extensibility... A schema change is additive...

`AGENTS.md`:
> All user data lives under `%LOCALAPPDATA%\\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.

`CONTEXT.md`:
> Pinned Location ... App-wide: shared across all Groups and panes, not part of any Group's state.

## Files to read and trace first

- `docs/tickets/PD-112-pinned-locations-manage-dialog.md`, especially its handoff and current immediate-save behavior.
- `src/app_shell/main.cpp`: `AppState`, manager window procedure, manager refresh/layout functions, `add_current_folder`, and all manager destruction call sites.
- `src/core/model.h` / `src/core/model.cpp`: reuse existing index-based remove/reorder operations; do not add a second mutation API.

## Scope

1. Keep the existing modeless owned manager window and list.
2. Add a private draft application/location list initialized when a new manager window opens.
3. Make Remove/Move operate on the draft and refresh the list/selection without persisting.
4. Replace `Close` with `Apply` and `Cancel`; Apply commits the draft and uses the existing atomic session-save path, while Cancel and X discard it.
5. If `Add Current Folder` is invoked while the modeless manager is open, mirror that new location into the draft so Apply cannot accidentally lose it.

## Non-goals

- No new persistence schema or file format.
- No changes to the Pinned Locations menu ordering, fixed Desktop/My Computer entries, or core mutation signatures.
- No drag sorting, rename UI, multi-select, or modal message loop.

## Acceptance

1. Manager shows `Remove`, `Move Up`, `Move Down`, `Apply`, and `Cancel`; it no longer relies on `Close`.
2. Remove/reorder changes are visible immediately in the manager but do not change the menu or session file before Apply.
3. Apply commits all pending changes, persists them through `save_now`, refreshes later Pinned Locations menus, and closes the manager.
4. Cancel and title-bar X discard all pending manager changes and leave the previously committed list intact.
5. Empty/no-selection states remain safe; Apply is disabled when there is no pending change and Cancel remains usable.
6. Reopening an already visible manager preserves its draft instead of silently replacing it.
7. `cmake --build build`, `ctest --test-dir build --output-on-failure`, and `git diff --check` pass.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "pinned_locations_draft|kPinnedLocationsApplyId|kPinnedLocationsCancelId|L\"Apply\"|L\"Cancel\"" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:管理視窗中 Remove/Move 後分別用 Apply 與 Cancel 驗證提交/放棄，並用標題列 X 驗證等同 Cancel。
```

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉，不要 `Stop-Process -Force`。**

## Handoff requirements

- draft 的儲存位置、建立/提交/捨棄時機，以及 modeless 視窗下 Add Current Folder 的同步策略。
- Apply/Cancel 的按鈕狀態規則與視窗關閉語意。
- 未能做的互動驗證要如實記錄。

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 實作交接（2026-08-29）

- draft 放在 `AppState::pinned_locations_draft`，開新管理視窗時複製目前 `ApplicationState`；Remove/Move 只呼叫既有 core index API 修改 draft。Apply 將 draft 的 `pinned_locations` move 回正式 application，再走既有 `save_now`；Cancel、X 與視窗銷毀都清掉 draft。
- 因管理視窗維持 modeless，`Add Current Folder` 若在管理視窗開啟期間成功加入正式清單，也同步加入 draft，避免 Apply 覆蓋掉該筆外部新增。
- 管理按鈕最終為 `Remove`、`Move Up`、`Move Down`、`Apply`、`Cancel`；Apply 僅在 draft 與正式清單不同時啟用，Cancel 永遠可用。Apply/Cancel 都關閉視窗，X 等同 Cancel。
- Agent checks：CMake configure PASS；`cmake --build build` PASS；`ctest --test-dir build --output-on-failure` PASS（6/6）；`git diff --check` PASS。
- 未執行實機管理視窗互動驗證；需使用者確認 Remove/Move 的暫存、Apply 提交、Cancel/X 放棄與重啟持久化。
