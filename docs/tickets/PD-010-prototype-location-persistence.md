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
