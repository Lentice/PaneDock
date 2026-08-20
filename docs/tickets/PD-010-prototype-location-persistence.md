# PD-010 — 原型的位置持久化與還原

Phase 0 · app_shell · Depends on: PD-008

- Source: `AGENTS.md`、`docs/design-spec.md` §9.4／§10、`docs/development.md`
- Origin: 2026-08-20 由 PD-001 拆分。
- Override: 本 ticket 與 PD-007／PD-008／PD-009／PD-011 共同取代 PD-001。
- Priority: 中——驗收協定 step「關閉再開啟後路徑精確還原」需要它,但它不觸及本案的決定性風險。

## Goal

原型關閉時寫下四個 pane 的位置,啟動時讀回並導覽。

用最簡單的檔案格式。**明確不需要**符合 §10 的 session document 契約——schema version、原子寫入、備份保留與遷移都由 PD-006 定義,在這裡預先實作只會產生一份之後要丟掉的實作。

## 已確認的產品決策

1. 檔案格式自選,一行一個路徑的純文字即可。
2. 位置以 parsing name 記錄。**不得**持久化 PIDL 或 COM 指標,也不得用顯示名稱當識別。這條是 `AGENTS.md` 的硬規則,即使原型也不放寬。
3. 檔案位置在 `%LOCALAPPDATA%\PaneDock`。
4. 讀不到或格式壞掉時,回退到預設路徑並繼續啟動;不得因此無法啟動。
   但**路徑存在於檔案卻無法解析**(斷線網路、已移除的磁碟)時,**保留該筆設定字串不覆寫**,pane 呈現可辨識的錯誤,下次關閉仍寫回原字串。`docs/testing.md` step 7 明文要求「its configuration intact」——直接改寫成預設路徑會靜默毀掉使用者的設定。
5. **仍需原子寫入(temp file 加 rename)並保留前一版**。這是 `AGENTS.md` 的無條件硬規則,不是 §10 的契約條款,原型不放寬——而且它只值三行程式碼。可省略的是 schema version、遷移與 §10 的文件結構。
6. 除了四個路徑,也要記錄目前版型(二分割／四宮格)與 active pane 索引。`docs/testing.md` step 6 要求「layout and every tab's location are restored exactly」;Phase 0 沒有 tab,但版型有。
7. **刻意只記錄 parsing name,不記錄 known-folder identity 與 fallback path**。這是對 `AGENTS.md` 三段式識別規則的明示簡化:三段式的價值在於資料夾被移動或重新導向後仍能還原,那是正式 session document(PD-006)要解的問題,而原型檔案可拋棄。若 PD-011 的驗收發現單靠 parsing name 就還原失敗,把案例寫進交接區給 PD-006。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> **Never persist a PIDL or a COM pointer.** Persisted identity is parsing name plus known-folder identity plus a fallback path. Display names are never identifiers.

`AGENTS.md`:
> All user data lives under `%LOCALAPPDATA%\PaneDock`.

`AGENTS.md`:
> Keep `src/core` free of HWND, COM and `windows.h`.

`docs/design-spec.md` §9.4 關閉序列:
> 1. 擷取現行狀態並原子寫入 session document
> 2. **destroy 全部 live `IExplorerBrowser`**
> 3. destroy pane HWND
> 4. destroy 主視窗
> 5. 退出訊息迴圈
> 6. `CoUninitialize`
>
> 順序不可調換。

擷取狀態必須發生在 destroy view **之前**——view 死掉之後就問不到它的目前位置了。

## Files to read and trace first

- `src/app_shell/`(PD-007／PD-008 建立),特別是關閉序列
- `IExplorerBrowser::GetCurrentView` → `IFolderView2` → `GetFolder`,或 `IExplorerBrowserEvents::OnNavigationComplete` 記錄最後位置
- `IShellItem::GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING)`
- `SHGetKnownFolderPath(FOLDERID_LocalAppData)`

## Scope

1. 取得每個 pane 目前位置的 parsing name;無法解析的 pane 沿用其原始設定字串。
2. 關閉序列的第 1 步寫檔,發生在 destroy view 之前。寫法為原子替換:寫入 temp 檔、`MoveFileEx` 覆蓋正式檔、保留前一版。
3. 啟動時讀檔,四個 pane 各自 `BrowseToObject` 到讀回的位置。
4. 讀檔失敗或行數不足時,缺少的 pane 回退到預設路徑,程式正常啟動。路徑無法解析時保留設定字串、pane 顯示錯誤,不覆寫。
6. 記錄並還原版型與 active pane 索引。
7. 一個可執行的 self-check 目標,驗證解析邏輯:正常內容、空檔、行數不足、含無效路徑、含空白行,五種輸入都產出四個項目與一個有效版型而不丟例外,且無效路徑的原始字串被保留。解析邏輯必須可在不建立真實 COM 物件的情況下被測到。

## Non-goals

- 不實作 §10 的 session document 格式、schema version 或遷移(歸 PD-006)。原子寫入與保留前一版**不在**豁免之列,見上。
- 不持久化選取狀態、捲動位置、檢視模式或排序。
- 不持久化捲動位置、檢視模式或排序。
- 不做 Group 或 tab。
- 不做視覺打磨。

## Acceptance

1. 四個 pane 導覽到任意位置後關閉,重新啟動時四個位置精確還原。
2. 刪除持久化檔案後啟動,四個 pane 回退到預設路徑,程式正常運作。
3. 手動把檔案改成亂碼後啟動,程式正常運作,不崩潰。
4. 其中一個路徑改為斷線的網路路徑後啟動:該 pane 顯示錯誤,關閉再開啟時該筆設定仍在檔案裡,未被預設路徑覆寫。
5. 版型與 active pane 索引在重啟後還原。
6. 寫檔為原子替換,且前一版檔案存在於 `%LOCALAPPDATA%\PaneDock`。
7. 持久化檔案位於 `%LOCALAPPDATA%\PaneDock` 之下。
8. 檔案內容為 parsing name;`rg` 檢查程式碼內無 PIDL 或 COM 指標的序列化。
9. 解析邏輯的五種輸入 self-check 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
.\build\PaneDock.exe
# 手動:導覽四個 pane、關閉、重開、確認還原
Get-ChildItem "$env:LOCALAPPDATA\PaneDock"
Get-Content "$env:LOCALAPPDATA\PaneDock\*"
# 手動:刪檔後啟動、寫入亂碼後啟動
```

```powershell
rg -n "AddRef|->Release\(\)" src
git diff --check
```

## Handoff requirements

- 實際採用的檔案格式與檔名,供 PD-006 判斷是否需要為原型檔案寫遷移(預設不需要,原型檔案可拋棄——若結論不同必須寫出理由)。
- 取得目前位置所用的介面路徑,以及它在網路路徑或虛擬資料夾下是否仍可用。
- 虛擬項目(例如「本機」「資源回收筒」)的 parsing name 是否能被 `SHCreateItemFromParsingName` 還原;不能的話列出哪些。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-20 實作交接

- 檔案格式與檔名：`%LOCALAPPDATA%\PaneDock\prototype-state.txt`，UTF-8 純文字六行：第 1 行為 `two` 或 `four`，第 2 行為 active pane 索引，第 3–6 行依序為四個 pane 的 parsing name。空白 location 行及不足的行回退到四個 prototype defaults；其他字串（包括無法解析的網路路徑）原樣保留。暫存檔為同目錄的 `prototype-state.txt.tmp`，前一版備份為 `prototype-state.txt.bak`。這是可拋棄的 prototype 格式，不需要由 PD-006 寫遷移；PD-006 的正式 JSON/session schema 不會讀取它。
- 寫入順序：建立 `PaneDock` 目錄 → 寫入並 flush `.tmp` → 若主檔存在，以 `CopyFileW` 覆蓋 `.bak` → `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` 原子替換主檔；任一步失敗會保留主檔並清理暫存檔。關閉時在 `destroy` 任一 `IExplorerBrowser` 之前擷取狀態並寫檔。
- 目前位置介面路徑：`IExplorerBrowserEvents::OnNavigationComplete` → `SHCreateItemFromIDList` → `IShellItem::GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING)`；成功導覽才更新 host 內的 location。啟動時 `SHCreateItemFromParsingName` 或 `BrowseToObject` 失敗不會 destroy 該 view，而會保留原始字串並在 pane 顯示 `This location is not available. Reconnect the drive and retry.`；關閉時仍寫回原字串。虛擬資料夾能否還原取決於 `SHCreateItemFromIDList` 與 `SIGDN_DESKTOPABSOLUTEPARSING` 對該 Shell namespace 的支援，本環境沒有互動桌面可作逐項人工確認，未宣稱所有虛擬項目皆通過。
- 新增 `src/core/prototype_location_persistence.h` 與 `panedock_prototype_location_persistence_check.exe`。self-check 覆蓋正常內容、空檔、行數不足、無效路徑、空白行，並檢查四項輸出、有效 layout/active index、無效路徑字串保留，以及格式 round-trip；結果 `PASSED: prototype_location_persistence_check`。
- 工具鏈與自動結果：LLVM-MinGW Clang/LLD target `x86_64-w64-windows-gnu`、Ninja；指定 toolchain configure/build 通過，票據 configure/build/CTest 亦通過；`ctest --test-dir build --output-on-failure` 為 `1/1 passed`。既有 `panedock_layout_state_check.exe`、`panedock_quadrant_layout_check.exe`、`panedock_explorer_host_lifetime_check.exe` 與新增 self-check 均 `PASSED`；`git diff --check` 通過。
- Literal grep 證據：`rg -n "windows\.h|HWND|IUnknown" src/core` 無命中；`rg -n "AddRef|->Release\(\)" src` 命中 `src/explorer_host/explorer_host.cpp:59` 的 COM `IUnknown` 介面實作 `Site::AddRef()`。這是 COM contract 本身，不是序列化或 raw interface pointer 的參照計數呼叫；沒有用巨集或拆字規避。位置序列化只處理 `std::wstring` parsing name，不寫入 PIDL 或 COM pointer。
- 互動限制：本 Codex 執行環境沒有可附著的互動桌面；啟動測試可建立 `PaneDock.exe`，但無法可靠取得可操作的視窗狀態，未把它當成手動通過。AC1 的四 pane 導覽後精確還原、AC2 刪除檔案後 defaults、AC3 亂碼檔案啟動、AC4 斷線網路路徑顯示錯誤並關閉重開仍保留、AC5 實際版型/active 重啟還原，以及 AC6 實際 `.bak` 檔案產生，均留待真實互動桌面人工確認；AC7–AC9 有上述程式碼與 self-check 證據。

### 2026-08-20 人工驗收（真實互動桌面）

在真實 Windows 桌面（雙螢幕，`PaneDock.exe` 於第二螢幕）以滑鼠雙擊逐一操作四個 pane、`Process.CloseMainWindow()`/`WaitForExit` 觀察關閉、`Get-Content`/`Get-ChildItem` 檢查 `%LOCALAPPDATA%\PaneDock` 內容，逐一驗證 Agent checks 與 Acceptance 清單：

- **重建與測試**：`cmake` configure + `cmake --build build` 為 no-op（已是最新），`ctest --test-dir build --output-on-failure` 1/1 通過。四個獨立 self-check exe（`panedock_explorer_host_lifetime_check`、`panedock_layout_state_check`、`panedock_prototype_location_persistence_check`、`panedock_quadrant_layout_check`，其中三個未註冊進 ctest）逐一直接執行，全部 `PASSED`，exit code 0。
- **AC1／AC5（精確還原＋版型/active 還原）**：四個 pane 分別導覽到 `C:\Dell\Drivers`、`C:\Windows\Boot`、`C:\Users\<user>`、`C:\Program Files\CMake` 後正常關閉（`CloseMainWindow` 78ms 內結束），檔案內容為 `four` / `3` / 四個 parsing name，與導覽結果完全一致。重新啟動後截圖確認四個 pane 精確還原到相同四個位置，版型仍為 four-pane。**通過**。
- **AC6（原子寫入＋前一版保留）**：第一次關閉時尚無 `.bak`（首次寫入，符合預期）。再次導覽（點擊 pane 0 使其成為 active pane）後第二次關閉，`%LOCALAPPDATA%\PaneDock` 下同時存在 `prototype-state.txt`（active=0，路徑不變）與 `prototype-state.txt.bak`（內容為前一版 active=3，路徑相同）。前一版內容與關閉前的主檔逐位元組一致。**通過**。
- **AC2（刪檔回退）**：手動刪除 `prototype-state.txt` 與 `.bak` 後啟動，程式正常開啟、四個 pane 落回預設路徑（`C:\`、`C:\Windows`、`C:\Users`、`C:\Program Files`），無崩潰、`Process.Responding = True`。**通過**。
- **AC3（亂碼檔案）**：把 `prototype-state.txt` 內容改成任意非格式字串（含符號與中文亂碼）後啟動，程式正常開啟並可互動，無崩潰。**通過**。
- **AC4（斷線網路路徑保留）**：手動寫入 6 行檔案，pane 0 設為 `\\nonexistent-host-xyz123\share`，其餘三行為正常路徑，啟動後 pane 0 顯示「This location is not available. Reconnect the drive and retry.」，其餘三個 pane 正常導覽，無崩潰。關閉後重讀檔案，pane 0 的字串仍是原始的 `\\nonexistent-host-xyz123\share`，未被預設路徑覆寫。**通過**，符合 AGENTS.md／`docs/testing.md` step 7 的硬規則。
- **AC7／AC8**：`Get-ChildItem "$env:LOCALAPPDATA\PaneDock"` 確認檔案位於規定路徑下；獨立重跑 `rg -n "AddRef|->Release\(\)" src` 只命中 `explorer_host.cpp:59` 的 COM `IUnknown::AddRef` 介面實作本身，與交接區描述一致；另讀 `src/core/prototype_location_persistence.h` 全文，序列化／解析只操作 `std::wstring`／`std::wstring_view`，未見任何 COM 型別或 PIDL。**通過**。
- **AC9**：已由 self-check exe 直接執行驗證（見上），涵蓋正常內容、空檔、行數不足、無效路徑、空白行五種輸入。**通過**。

九項驗收條件全數在真實互動桌面上驗證通過，PD-010 判定為 `done`。
