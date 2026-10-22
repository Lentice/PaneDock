# PD-253 — 清單行為對齊 Windows 檔案總管：排序分組、自然排序、隱藏檔、rename 選取範圍

Phase 8 · 目標 1 天

## 來源與交付

來源：使用者 2026-10-22 要求 PaneDock 的使用者體驗與 Windows 檔案總管一致，並指出降冪排序時檔案總管會把資料夾放在最後、rename 只選取主檔名。

交付四個結果：(1) 排序方向會一併反轉「資料夾／檔案」分組；(2) 名稱以自然排序（`file2` 在 `file10` 前）；(3) 遵守檔案總管的「顯示隱藏檔」與「隱藏受保護的作業系統檔案」設定；(4) F2 rename 預設只選取不含副檔名的名稱。

## Overrides

覆寫 PD-228 對排序的決定「降冪時資料夾仍在最前」與名稱以 ordinal 比較。證據：`IShellFolder::CompareIDs` 對名稱、日期、類型、大小四欄皆回傳資料夾小於檔案（升冪資料夾在前），檔案總管降冪即整體反轉；名稱比較使用 `StrCmpLogicalW`。`D:\Downloads` 的 `desktop.ini`（隱藏＋系統）在檔案總管預設不顯示，PaneDock 原先一律以 `SHCONTF_INCLUDEHIDDEN` 列出。

## 綁定限制（引用）

- AGENTS：檔案操作維持 Shell 邊界；本票只改顯示與排序。
- AGENTS：UI 文字英文；不新增持久化欄位（隱藏檔設定讀取 Shell 現行設定，不另存）。
- AGENTS：事件驅動，worker 外不新增鎖或執行緒。

## 具體範圍

1. `pane_managed_list.cpp`：`sort_managed_list_rows`／`property_row_less` 的分組隨方向反轉；名稱主鍵改用 `natural_compare`（`StrCmpLogicalW`），平手時仍以序數與 identity 保持確定性。
2. `list_worker.h/.cpp`：`HiddenFilePolicy`（`hides()` 純函式）與 `read_hidden_file_policy()`（`SHGetSetSettings`）；本機列舉、變更 delta（變成隱藏時移除）、Shell namespace 列舉（`SHCONTF_INCLUDEHIDDEN`／`INCLUDESUPERHIDDEN`）皆套用。
3. `managed_list/rename_text.h`：`rename_selection_length`；`begin_list_rename` 以 `EM_SETSEL` 只選主檔名（資料夾、無副檔名、`.gitignore` 類全選）。
4. 測試：`managed_list_sort_test`（降冪資料夾在後、自然排序）、`managed_list_hidden_policy`、`managed_list_rename_text`。

## Non-goals

不做「依類型分組」、不改欄位集合、不實作「隱藏已知檔案類型的副檔名」設定（`HideFileExt`）、設定變更後的即時重列（需 Refresh）。

## 驗收標準

- [ ] 名稱、大小欄降冪時資料夾排在最後；`file2` 排在 `file10` 前（測試已覆蓋）。
- [ ] `D:\Downloads` 不再列出 `desktop.ini`（項目數 1443 → 1442，已實測）；使用者開啟「顯示受保護檔案」後出現。
- [ ] F2 rename 只選取主檔名（純函式測試已覆蓋；實機待使用者確認）。
- [ ] ctest 全部通過。

## Agent checks

```powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

## 交接區

- 未驗證：F2 實機選取範圍、`HideFileExt=1` 的機器（此時檔案總管 rename 也不含副檔名）、依類型排序時資料夾位置與檔案總管是否一致（目前為文字比較，未用 `CompareIDs` 實測）。
- 結論：名稱／日期／大小欄在 `CompareIDs` 下資料夾都排前；不要把降冪的分組改回固定在前。
