# PD-138 — startup 將 filesystem status error／backup-only recovery 當成無需提示

Phase 7 · core session recovery · Depends on: PD-025, PD-078

- Source: 2026-08-30 close/startup audit loop。
- Priority: HIGH——disk slow、anti-virus deny/quarantine、權限變更或 path error 讓 `std::filesystem::exists(..., error_code)` 回報 error 時，`read_session` 目前把它當成不存在，回傳 default state 且 `recovered_from_corruption=false`。若 primary 缺失但 backup 可讀，也會使用 backup 卻不提示。startup 隨後可能把 default/crash marker 寫回，靜默覆蓋使用者的 Groups。

## Goal

Make every fallback caused by a missing/unreadable/invalid persisted session visible to the existing startup warning flow, while preserving the valid backup recovery path and the no-file first-run path.

## Confirmed root cause and callers

- `src/core/session.cpp::read_session` checks `exists(primary, error)` and only tries primary when `!error`; it then overwrites the same error code while checking backup.
- The default return computes `recovered_from_corruption` from only `primary_exists || backup_exists`, so a status error can be reported as a clean first run.
- A valid backup return currently sets the flag from `primary_exists`; when primary is absent, the app still uses backup but reports no recovery.
- `src/app_shell/main.cpp::wWinMain` uses `recovered_from_corruption` plus `SessionSource` to show the existing English “previous good version” or “default Group” warning before deferred Shell realization. No new dialog is required; the signal is wrong.

## Binding constraints

`AGENTS.md`:

> Every persisted config/setting file must be designed for forward extensibility. A read that encounters a field it does not recognize preserves that field rather than silently dropping it on the next write-back.

> All user data lives under `%LOCALAPPDATA%\\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.

> Keep `src/core` free of HWND, COM and `windows.h`.

> New non-trivial logic needs one focused runnable test or self-check.

`docs/design-spec.md FR-013` requires corrupt/unreadable session recovery to fall back safely and tell the user. `docs/design-spec.md §9.3` keeps session read before window creation; this ticket does not move that boundary or add asynchronous I/O.

## Files to read and trace

- `src/core/session.cpp`: `read_session` and `read_file` error/fallback paths.
- `src/core/session.h`: `SessionReadResult` and `SessionSource` contract.
- `src/app_shell/main.cpp`: `wWinMain` recovery-warning branches and startup `save_now` call.
- `tests/unit/core_session_test.cpp`: `test_read_fallbacks` and first-run fixtures.
- `docs/tickets/PD-025-crash-recovery-path.md`, `docs/tickets/PD-078-crash-safe-session-write.md`, `docs/tickets/PD-133-session-save-failure-is-silent.md`.

## Scope

1. Track session-directory and primary/backup status errors separately from the boolean existence results.
2. Mark `recovered_from_corruption=true` whenever startup uses backup or default because a persisted path was missing unexpectedly, unreadable, invalid, or had a filesystem status error.
3. Keep an entirely absent session directory/files as the normal first-run case with no recovery warning.
4. Add focused core tests for backup-only recovery and a non-directory/error path; preserve existing invalid-primary and no-files assertions.

## Non-goals

- Do not change JSON schema, parser, unknown-field preservation, atomic writes, backup rotation, or clean-shutdown meaning.
- Do not retry filesystem operations, add polling, background threads, I/O timeouts, or a custom AV/permission detector.
- Do not add another UI dialog or change the existing English warning text.
- Do not treat a valid primary read as recovery merely because a stale backup exists.

## Acceptance criteria

1. Valid primary → `SessionSource::primary`, no recovery warning.
2. Valid backup with missing, unreadable, invalid, or status-error primary → `SessionSource::backup`, `recovered_from_corruption=true`.
3. Invalid/unavailable primary and unavailable/invalid backup → default state with `recovered_from_corruption=true`.
4. No primary and no backup, with no filesystem error → default state with `recovered_from_corruption=false`.
5. Existing startup warning branches receive the corrected signal; no persisted data is silently treated as a clean first run.
6. Focused core test, build, CTest, and `git diff --check` pass.

## Agent checks

```powershell
rg -n "read_session|recovered_from_corruption|SessionSource::backup|exists\(" src/core/session.cpp src/app_shell/main.cpp tests/unit/core_session_test.cpp
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

## Handoff requirements

- Record how `error_code` is preserved across the primary and backup checks.
- Record the exact first-run vs recovery test cases and results.
- State that filesystem calls that never return remain an OS/driver boundary; do not claim a timeout was added.

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-08-30 — implemented

- `read_session` 先驗證 session directory；directory status error 或 path 不是 directory 時直接回 default + recovery flag。primary/backup `exists` 的 error code 也各自保存，不再被下一次查詢覆蓋。
- 只要實際採用 backup 就標記 recovery（包含 primary 缺失）；default 僅在完全沒有檔案且沒有 filesystem error 時維持 first-run 的 false。
- `core_session` 補上 primary 缺失/backup-only 與 non-directory path 測試；原有 invalid-primary、invalid-backup、無檔案案例仍通過。
- `cmake --build build` PASS（保留既有 `session.cpp:538` missing-field-initializers warning）；focused CTest PASS；`git diff --check` PASS。
- 未新增 retry、timeout、thread、schema 或 UI dialog；既有 `wWinMain` recovery warning 會接收到修正後的 signal。filesystem call 永不返回仍是 OS/driver 邊界。
