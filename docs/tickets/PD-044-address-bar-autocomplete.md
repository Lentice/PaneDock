# PD-044 — 網址列輸入時顯示子資料夾自動完成下拉選單

Phase 6 · app_shell · Depends on: PD-020

- Source: 使用者實機操作 `.\build\PaneDock.exe` 後回報,附截圖(2026-08-25)。
- Origin: 「address 沒有自動完成,輸入過程中,應該有 dropdown 顯示下一段路徑(sub folders)」。
- Priority: MEDIUM——易用性缺口,使用者輸入路徑時沒有 Windows 使用者已經習慣的自動完成輔助。

## 已確認的產品決策

1. **改用 Win32 內建的 `SHAutoComplete`(`Shlwapi.h`,連結 `Shlwapi.lib`),對每個 pane 的網址列 `EDIT` 控制項(`state.address_bars[index]`)呼叫一次 `SHAutoComplete(edit_hwnd, SHACF_FILESYS_DIRS)`,不是自己刻一個下拉選單控制項。** 這是 Windows 標準的「輸入路徑時顯示子資料夾下拉建議」機制,檔案總管網址列、「執行」對話框都用同一個 API;`SHACF_FILESYS_DIRS` 限定只建議資料夾(不含檔案),符合使用者說的「sub folders」。這完全符合 `AGENTS.md`「Reach for the standard library and Win32 before adding a dependency」——一行 API 呼叫達成需求,不需要自製 popup listbox、不需要自己查詢子資料夾清單、不需要處理鍵盤上下選取邏輯,Windows 全部處理好。
2. **在每個網址列 `EDIT` 控制項建立完成後呼叫一次 `SHAutoComplete`,不需要每次導覽或內容改變時重新呼叫。** `SHAutoComplete` 掛上去之後是持續生效的輸入行為,只需要初始化一次;不需要在 `refresh_navigation_chrome`/`handle_navigation_complete` 等既有的每次導覽都會呼叫的函式裡重複呼叫。
3. **`SHAutoComplete` 需要 COM 已初始化(`CoInitialize`/`OleInitialize`)才能正常運作其下拉 UI。** 本程式碼庫大量使用 `IExplorerBrowser` 等 COM 介面,`main` 進入點必然已經有 COM 初始化;實作 agent 需要確認呼叫 `SHAutoComplete` 的時間點在 COM 初始化**之後**(通常是在 `WM_CREATE` 建立網址列之後就滿足,但仍需在程式碼裡實際確認呼叫順序,而不是假設)。
4. **`SHAutoComplete` 回傳值若失敗(`HRESULT` 非 `S_OK`),不視為致命錯誤。** 自動完成是輔助功能,失敗時網址列仍可正常手動輸入路徑,不影響核心導覽功能;失敗時不彈錯誤、不記 log(沒有既有的診斷 log 基礎設施可掛),但可以在交接區記錄是否觀察到任何失敗案例。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> File operations go through Shell `IDataObject` and `IFileOperation`. Never assemble a path string and call the filesystem directly.
(本票不受此條款影響——`SHAutoComplete` 是 Shell 官方 API,不是自己組路徑字串呼叫檔案系統,但列出以確認本票沒有踩到這條既有紅線。)

## Files to read and trace first

- `src/app_shell/main.cpp` 建立 `state.address_bars[index]`(`EDIT` 控制項)的位置(`WM_CREATE` 內,搜尋 `address_bars[index] = CreateWindowExW`)——本票要在這裡(或緊接在建立之後)加上 `SHAutoComplete` 呼叫。
- `src/app_shell/main.cpp` 的 `main`/`wWinMain` 進入點——確認 COM 初始化(`CoInitialize`/`CoInitializeEx`)的呼叫時間點,確保早於 `WM_CREATE`。
- `docs/tickets/PD-020-address-bar-and-navigation-buttons.md`——既有網址列 `EDIT` 控制項的建立慣例與既有行為(手動輸入路徑後 Enter 觸發導覽的邏輯),確認本票不改變那個既有行為,只是疊加自動完成建議。
- CMake 建置設定(`CMakeLists.txt` 或對應的 `src/app_shell/CMakeLists.txt`)——確認連結旗標是否已經包含 `Shlwapi`,若沒有需要加入(LLVM-MinGW 工具鏈下的函式庫名稱可能是 `-lshlwapi`)。

## Scope

1. 確認/補上建置設定裡的 `Shlwapi` 連結。
2. 在每個 `state.address_bars[index]` 建立完成後,呼叫 `SHAutoComplete(state.address_bars[index], SHACF_FILESYS_DIRS)`。
3. 確認 COM 初始化時間點滿足決策 3 的要求。

## Non-goals

- 不自己實作下拉選單 UI 或子資料夾查詢邏輯(已確認的產品決策 1 明確排除)。
- 不改變網址列既有的 Enter 觸發導覽、既有的 `refresh_navigation_chrome`/`SetWindowTextW` 行為。
- 不擴充自動完成範圍到檔案(只到資料夾,`SHACF_FILESYS_DIRS`,已確認的產品決策 1)。

## Acceptance

1. 在任一 pane 的網址列輸入部分路徑(例如 `C:\Prog`)時,顯示系統原生自動完成下拉選單,列出符合的子資料夾建議(例如 `C:\Program Files`、`C:\Program Files (x86)`)。
2. 用鍵盤上下鍵選取下拉建議、Tab 或 Enter 完成輸入,行為與 Windows 檔案總管網址列一致(系統原生行為,不需要額外程式碼)。
3. 既有的手動輸入完整路徑後按 Enter 觸發導覽的行為不受影響。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "SHAutoComplete|SHACF_FILESYS_DIRS" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:在網址列輸入部分路徑,確認出現子資料夾自動完成下拉選單且可正常選取、
# 確認既有 Enter 導覽行為不受影響
```

## Handoff requirements

- `Shlwapi` 連結是否需要新增到建置設定,以及最終的 CMake 改動位置。
- `SHAutoComplete` 呼叫的確切位置與是否確認 COM 初始化順序無誤。
- 若真實桌面測試觀察到 `SHAutoComplete` 失敗或下拉選單沒有出現,記錄下來並說明可能原因(不強行在本票內排除所有環境因素)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
