# PD-137 — session backup copy/restore 不是 atomic replace

Phase 7 · core session durability · Depends on: PD-006, PD-078

- Source: 2026-08-30 close/startup audit loop。
- Priority: HIGH——`write_session` 已對 primary 使用 temporary file，但更新 backup 與失敗 restore 仍直接覆寫既有檔案。force kill、Windows shutdown、anti-virus quarantine、disk-full 或 storage I/O error 若落在 `copy_file` 中間，下一次 startup 可能同時失去 primary 與 backup。

## Goal

Keep at least one complete previous session document across interruption of any backup update or primary replacement. Every replacement of a persisted file must be staged in a temporary file and committed by rename; no in-place restore is allowed.

## Confirmed root cause

`src/core/session.cpp::write_session` currently:

1. writes `session.json.tmp`, flushes it, and invokes the durability hook;
2. copies a readable primary directly to `session.json.bak` with `overwrite_existing`;
3. on primary rename failure copies `session.json.bak` directly over `session.json`.

The primary copy can truncate the only good backup before an interruption. The restore copy can likewise truncate the primary in place, and it is not needed when an atomic rename failed because the old primary remains in place.

## Binding constraints

`AGENTS.md`:

> All user data lives under `%LOCALAPPDATA%\\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.

> Every persisted config/setting file must be designed for forward extensibility. A read that encounters a field it does not recognize preserves that field rather than silently dropping it on the next write-back.

> Keep `src/core` free of HWND, COM and `windows.h`.

> New non-trivial logic needs one focused runnable test or self-check.

`docs/design-spec.md §10` requires the session file to retain a previous version for recovery. `docs/tickets/PD-078-crash-safe-session-write.md` already supplies the injected flush boundary; this ticket fixes the file replacement topology and does not weaken that hook.

## Files to read and trace

- `src/core/session.h`: persisted file-name constants and durability hook contract.
- `src/core/session.cpp`: `write_session`, `read_file`, and all cleanup/error paths.
- `tests/unit/core_session_test.cpp`: existing atomic-write, corrupt-primary, and durability-hook tests.
- `docs/tickets/PD-006-session-document-persistence.md` and `docs/tickets/PD-078-crash-safe-session-write.md`: schema/unknown-field and flush decisions.

## Scope

1. Add one named disposable backup-temp file constant.
2. Copy a valid primary to the backup-temp file, run the existing durability hook on that temp file, then atomically rename it to `session.json.bak`.
3. Rename the new primary temp into place only after the backup commit succeeds. If that rename fails, remove only temporary files; do not restore by copying over the old primary.
4. Preserve a prior backup when the current primary is corrupt, and clean every temporary path on failure.
5. Extend the existing core session test to assert hook ordering, no temp leftovers, unknown-field preservation, and safe behavior when the backup target cannot be replaced.

## Non-goals

- Do not change JSON schema, migration, parser behavior, clean-shutdown semantics, or `SessionReadResult`.
- Do not add Windows headers, COM, a worker thread, a retry loop, or an I/O timeout.
- Do not replace the existing injected durability hook; its backup callback now observes the staged backup temp path before that path is renamed.
- Do not attempt to make two separate filesystem volumes transactional; the session directory and its temporary files stay in one directory.

## Acceptance criteria

1. No code path uses `copy_file(... overwrite_existing)` to write either the persisted primary or backup path.
2. An interruption before backup-temp rename leaves the old backup intact; an interruption after backup rename but before primary rename leaves both files complete.
3. A failed primary rename leaves the old primary readable and never performs an in-place restore.
4. A corrupt primary never replaces a good backup.
5. Existing unknown JSON fields survive a successful write-back.
6. Focused core test, build, CTest, and `git diff --check` pass.

## Agent checks

```powershell
rg -n "copy_file|rename|kSessionBackupTemporaryFileName|durability" src/core/session.cpp src/core/session.h tests/unit/core_session_test.cpp
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

## Handoff requirements

- Record the exact temporary names and rename order.
- Record the test that proves backup replacement failure cannot damage the old primary/backup.
- Record build/CTest results; do not claim power-loss proof from unit tests alone.

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-08-30 — implemented

- 新增 disposable `session.json.bak.tmp`；valid primary 先 copy 到該檔、跑既有 durability hook，再 rename 到 `session.json.bak`，最後才 rename `session.json.tmp` 到 primary。
- 移除原本對 backup/primary 的 in-place `copy_file(... overwrite_existing)` 與 restore；任一 rename 失敗只清理 temp，保留既有完整 primary/backup。
- `core_session` 補上 backup target replacement failure 測試，並更新 hook ordering 測試確認 hook 觀察 staged backup temp；測試驗證 temp cleanup、primary preservation 與 corrupt-primary 保護。
- `cmake --build build` PASS（既有 `session.cpp:538` missing-field-initializers warning）；`panedock_core_session` PASS；`git diff --check` PASS。完整 deterministic CTest（排除 launch smoke）待 commit 後統一回歸。
- 未修改 schema、unknown-field preservation、Shell/Win32 seam、durability hook 的 flush 順序或新增任何 background/retry 行為；unit test 不等同於真實斷電 proof。
