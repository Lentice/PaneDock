# PD-219 — session 最後改名失敗時還原主檔

Phase 7 · persistence correctness · Depends on: PD-137

## 來源與約束

2026-09-30 稽核發現：`write_session` 將有效 `session.json` 改名為 `session.json.bak` 後，若 `.tmp` 改名為主檔失敗，函式只刪 `.tmp`；主檔消失，下次讀取會錯誤地回報備份復原。

- `docs/design-spec.md` §10：「寫入：原子替換（`session.json.tmp` 寫入並 flush，再 rename 為 `session.json`），保留上一版為備份」；讀取候選順序為 primary → temporary → backup；「只有退到備份才告知使用者近期變更可能遺失」。
- `docs/development.md`：「Make the smallest change that satisfies the acceptance criteria」；`core` 不可含 HWND、COM 或 `windows.h`。
- `AGENTS.md`：「Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place」；新非平凡邏輯須有 focused runnable test。

## 範圍

先讀並追 `src/core/session.h/.cpp` 的 `write_session`、`read_session` 與所有呼叫者（`src/app_shell/session_writer.cpp`、單元測試），以及 `tests/unit/core_session_test.cpp`。最後改名失敗且已輪替有效主檔時，把備份改名回主檔；若還原也失敗，保留完整 `.tmp` 作為讀取候選。以鎖住暫存檔的 Windows 測試重現最後改名失敗；產品的 `src/core` 不引入 Win32。

非目標：改變 JSON schema、正常寫入的單次 flush 與輪替流程、引入新的持久化檔案。

## 驗收與 Agent checks

1. 最後改名失敗時 `write_session` 回傳 false，舊主檔仍是可讀 primary，不出現虛假的備份復原警告。
2. 正常寫入、備份改名失敗、寫入中斷及損壞文件的既有測試保持通過。

```powershell
cmake --build build
ctest --test-dir build -R '^panedock_core_session$|^panedock_session_writer$' --output-on-failure
git diff --check
```

## 交接區

- 未驗證：OS 直接中斷第二次 rename 後又發生獨立儲存裝置故障的複合情境；既有 `read_session` 會先嘗試完整 `.tmp`，再嘗試備份。
