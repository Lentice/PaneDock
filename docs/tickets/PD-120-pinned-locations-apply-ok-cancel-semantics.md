# PD-120 — Pinned Locations 管理視窗區分 Apply、OK 與 Cancel

Phase 7 · app_shell · Depends on: PD-119

- Source: 使用者釐清(2026-08-29)。
- Origin: 使用者原文:「Apply寫回不關閉  確定 寫回並關閉 Cancel 放棄並關閉」。
- Override: PD-119 的「Apply 寫回並關閉」語意由本票覆寫；PD-119 的 draft 編輯基礎與 PD-112 的管理功能維持不變。

## Product decision

管理視窗按鈕語意固定為：`Apply` 將目前 draft 寫回並保持視窗開啟；`OK`（確定）將 draft 寫回並關閉；`Cancel` 放棄全部尚未提交的 draft 變更並關閉；標題列 X 等同 `Cancel`。Apply 完成後以正式清單重建 draft baseline，因此後續仍可繼續編輯。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> App UI text must be English. No Chinese strings ship in the binary.

`AGENTS.md`:
> All user data lives under `%LOCALAPPDATA%\\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.

## Files to read and trace first

- `docs/tickets/PD-119-pinned-locations-apply-cancel.md`: previous draft and commit behavior.
- `src/app_shell/main.cpp`: manager button IDs/array, refresh enablement, `WM_COMMAND`, and `WM_CLOSE`/`WM_NCDESTROY` cleanup.
- `src/core/model.h` / `src/core/model.cpp`: existing pinned-location mutations remain reused unchanged.

## Scope

1. Add an `OK` button while keeping `Apply` and `Cancel`.
2. Make Apply commit the draft, refresh the draft baseline/list, and keep the modeless manager open.
3. Make OK commit the draft and close; keep Cancel/X as full discard-and-close.
4. Preserve the existing atomic `save_now` path and the existing manager layout/selection behavior.

## Non-goals

- No new persistence schema, core API, dialog framework, or modal message loop.
- No change to Remove/Move behavior, Pinned Locations menu ordering, or external Add Current Folder synchronization.

## Acceptance

1. The manager displays `Remove`, `Move Up`, `Move Down`, `Apply`, `OK`, and `Cancel`.
2. After Remove/Move, Apply persists the current list but leaves the manager open and ready for more edits.
3. OK persists the current list and closes the manager.
4. Cancel and title-bar X close the manager and discard all uncommitted edits.
5. After Apply, a later Cancel does not undo the already applied changes.
6. Empty/no-selection states remain safe and Apply is disabled when there are no pending changes.
7. `cmake --build build`, `ctest --test-dir build --output-on-failure`, and `git diff --check` pass.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kPinnedLocationsOkId|L\"Apply\"|L\"OK\"|L\"Cancel\"|pinned_locations_draft" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:Remove/Move 後測試 Apply（留在視窗）、再編輯後測試 Cancel；最後測試 OK 與標題列 X。
```

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉，不要 `Stop-Process -Force`。**

## Handoff requirements

- 記錄 Apply/OK/Cancel/X 的最終 commit、baseline 與關閉行為。
- 未能做的互動驗證要如實記錄。

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 實作交接（2026-08-29）

- 管理視窗按鈕為 `Remove`、`Move Up`、`Move Down`、`Apply`、`OK`、`Cancel`。`Apply` 將 draft 寫回正式 `ApplicationState`、呼叫既有 `save_now`，再以正式清單重設 draft baseline、刷新清單並保持視窗開啟。
- `OK` 使用同一提交路徑後清除 draft 並 `DestroyWindow`；`Cancel` 先清除 draft 再關閉；標題列 X 由 `WM_NCDESTROY` 清除 draft，等同放棄未提交變更。Apply 僅在有 pending change 時啟用，OK/Cancel 永遠可用。
- 沒有新增 persistence schema、core API 或 dialog framework。既有 modeless 視窗與 `Add Current Folder` 對 draft 的同步策略維持不變。
- Agent checks：CMake configure PASS；`cmake --build build` PASS；`ctest --test-dir build --output-on-failure` PASS（6/6）；`git diff --check` PASS。
- 未執行實機管理視窗互動驗證；需使用者確認 Apply 留窗、後續 Cancel 不會撤銷已 Apply 的變更，以及 OK/Cancel/X 的關閉語意。
