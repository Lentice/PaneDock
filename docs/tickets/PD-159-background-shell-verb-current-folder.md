# PD-159 — pane 空白處的背景 Shell verb 無法取得目前資料夾

Phase 7 · explorer_host · Depends on: PD-007, PD-014, PD-024

- Source: 使用者於 2026-08-31 的實機回報與同機對照。
- Symptom: 在 `Github` Group 的 `PaneDock` tab（`E:\GitHub\PaneDock`）空白處按右鍵，選 `以 Code 開啟` 或 `Open Git Bash here` 後選單關閉，但兩者都沒有任何反應。
- Priority: HIGH——原生右鍵選單雖能顯示，所有依賴目前資料夾 identity 的 `Directory\Background` verb 都可能失效。

## Outcome

在 PaneDock 的 realized Shell view 空白處執行已安裝的背景 Shell verb 時，verb 取得被點擊 pane／tab 的正確 Shell location，行為與同機 Windows 檔案總管一致。`以 Code 開啟` 與 `Open Git Bash here` 必須在 `E:\GitHub\PaneDock` 正常執行，不能無反應、開錯 location 或取用另一個 pane。

## 已確認證據與診斷邊界

1. 已在目前 Release build 的真實 PaneDock 視窗重現：`E:\GitHub\PaneDock` 空白處選單含 `以 Code 開啟`，點擊後選單關閉；等待 1.5 秒前後 VS Code 視窗集合與標題完全不變。
2. 使用者確認 `Open Git Bash here` 在相同 PaneDock 空白處也失敗。此機器的 registry command 為 `"C:\Program Files\Git\git-bash.exe" "--cd=%v."`，因此已有第二個獨立 verb 證明症狀集中在背景選單的目前資料夾參數，而非 VS Code 特例。
3. 使用者確認同一個 PaneDock 背景選單的 `FileLocator Pro...` 可成功執行。因此 context menu 建立、命令選取與一般 invoke 路徑不是整體失效；失敗邊界進一步縮到需要目前資料夾的 verbs。
4. 使用者確認同一台機器的一般 Windows 檔案總管可順利以相同命令開啟 VS Code。因此 VS Code 安裝／registration 不是首要根因；差異集中在 PaneDock 的 `IExplorerBrowser` host。
5. `ExplorerHost::initialize()` 以 `IUnknown_SetSite(browser_, site_)` 安裝自訂 `Site`。`Site` 宣告 `IServiceProvider`，但 `QueryService()` 對每個 service／IID 都只記錄 service GUID 後回 `E_NOINTERFACE`，且目前未記錄 requested IID。
6. Microsoft 的 `IExplorerBrowser` 契約明載 host 應實作 `IServiceProvider::QueryService` 回應 Shell view 的服務查詢；使用者操作可從 view 觸發 `SID_SExplorerBrowserFrame`／`ICommDlgBrowser*` 查詢。現況的全拒絕與「選單能建立、一般 verb 可執行，但依賴 `%V`／`%v` 的背景 verb 失敗」吻合。
7. 第 6 點是高信心根因方向，不是已量到的最終 IID。實作前必須先記錄實際 `QueryService(service_id, iid)` 序列與命令啟動參數；不得憑猜測直接實作一整套 `IShellBrowser`。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §2.4：
> **不重刻檔案清單。** 檔案檢視一律承載原生 Shell view,原生行為由 Windows 提供。

`docs/design-spec.md` §4.8：
> pane 內部的一切互動由 Shell view 處理:多選手勢、右鍵選單、拖放、就地重新命名、鍵盤操作。PaneDock 不介入。

`docs/design-spec.md` §FR-006：
> 每個已 realize 的 tab 透過 `IExplorerBrowser` 承載 Shell view,提供原生圖示與縮圖、原生右鍵選單與已安裝的 shell extension、多選、就地重新命名。

`docs/design-spec.md` §9.1：
> `explorer_host` | 每個已 realize pane 的 `IExplorerBrowser`、site 物件、生命週期、事件 | 產品層決策、持久化

`docs/design-spec.md` §12.3：
> `explorer_host`、`shell_core`、`file_operations`。為 `IExplorerBrowser` 做 test double 只會驗證我們對 COM 契約的假設而非契約本身,會出現測試通過而真實整合已壞的情況。

`docs/development.md` Change workflow：
> Make the smallest change that satisfies the acceptance criteria. Reuse before adding.

`AGENTS.md`：
> Read the relevant spec section and trace every caller before touching shared code.

`AGENTS.md`：
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`：
> Keep `src/core` free of HWND, COM and `windows.h`.

Microsoft `IExplorerBrowser` documentation：
> The object hosting the ExplorerBrowser object should derive from IServiceProvider and implement QueryService to respond to any service queries.

## Files to read and trace first

- `docs/design-spec.md` §2.4、§4.8、§FR-006、§9.1–9.4、§12.3–12.5、§AC-001。
- `docs/development.md` Architecture rules、COM lifetime rules、Change workflow。
- `docs/testing.md` Single seam、Prototype acceptance protocol item 2、Required test environments。
- `docs/tickets/PD-007-single-explorer-host-and-shutdown.md`——既有 site 與 `IExplorerBrowser` lifetime 契約。
- `docs/tickets/PD-014-explorer-host-ole-and-accelerator-wiring.md`——OLE 與使用者輸入轉發基礎。
- `docs/tickets/PD-024-diagnostic-mode-suppressing-shell-extensions.md`——一般／診斷模式的 extension 對照邊界。
- `src/explorer_host/explorer_host.cpp`——完整讀取 `Site`、`QueryInterface`、`QueryService`、`initialize`、`destroy`，並追蹤 site 的所有 caller／owner。
- `src/explorer_host/explorer_host.h`——site ownership、Shell call scope 與 lifetime state。
- `src/app_shell/main.cpp`——追蹤 `ExplorerHost` 建立、active pane、Group/tab 切換、Shell call re-entry 與 shutdown gate。

## Scope

### Phase A — 釘死背景 verb 執行鏈

1. 以 `E:\GitHub\PaneDock` 空白處的 `以 Code 開啟` 與 `Open Git Bash here` 作原始紅燈；同機 Windows 檔案總管執行相同 verbs 作綠色對照。
2. 暫時把現有 `QueryService` log 擴充為同時記錄 `service_id`、requested `iid` 與回傳 HRESULT，全部加唯一 `[DEBUG-PD159]` 前綴。只記錄這兩條操作期間的查詢。
3. 用 debugger 或 Process Monitor 的 Process Create evidence 擷取 PaneDock 與檔案總管兩條路徑實際是否啟動命令、image、command line 與 working directory。若 PaneDock 完全沒有 Process Create，將 breakpoint 下在 Shell verb invoke 邊界；若有啟動但參數錯誤，記錄 `%V`／目前 location 的實際差異。
4. 沿用已成功的 `FileLocator Pro...` 作不依賴目前資料夾／一般 invoke 對照。只有 `%V`／`%v` verbs 失敗，才支持首要根因。
5. 記錄可使失敗消失的最小 service／interface 與必要方法；一次只改一個 service 回應。若 `QueryService` 序列與失敗無關，先留下反證，再追到實際共享 seam。

### Phase B — 最小修正與回歸

1. 在既有 `Site` 補上 Phase A 證明必要的最小 site contract，讓所有 pane／tab 共用同一修正；不得在 app_shell 針對 VS Code 或 Git Bash 加特例。
2. 沿用 `ExplorerHost::ShellCallScope` 與 deferred shutdown gate；新 callback 必須可在 `detach()`／`Destroy()` 重入期間安全失效，不得破壞 `IUnknown_SetSite(nullptr)` 順序。
3. 重跑兩個原始失敗 verbs、`FileLocator Pro...` 成功對照、檔案項目右鍵、四個 pane、Group/tab 切換與關閉矩陣。
4. 移除全部 `[DEBUG-PD159]` probe 與 throwaway capture code。

## Non-goals

- 不新增 PaneDock 自製的 `Open with Code`／`Open Git Bash here`，不直接啟動外部程式，不讀寫其 registry registration。
- 不攔截、重建或包裝原生 context menu；不改走 `ShellExecute` 或自行展開 `%V`。
- 不為尚未觀察到的 service 實作通用 `IShellBrowser`／`ICommDlgBrowser3` façade，不新增抽象、fake COM、dependency 或 UI automation framework。
- 不修改 `src/core`、session schema、Group/tab/location persistence、file operations 或 Shell view lifetime policy。
- 不改 diagnostic mode，不用 timer、polling、sleep、重試或 focus/activate 掩蓋失敗。

## Scope override — 2026-08-31 實證後修正

原先「不攔截、重建或包裝原生 context menu」的 non-goal 由本節明確覆寫，原因是 runtime evidence 已證明只補 `ICommDlgBrowser`／`QueryService` 不足以修復目前資料夾參數：標準 `CDefView::DoBackgroundContextMenu` 會進入 `shell32!CRegistryVerbsContextMenu::_Execute`，但實際 `CMINVOKECOMMANDINFO` 的 `lpDirectory` 為 null；同一路徑的 `_GetShellItemArray` 回 `0x80070057 (E_INVALIDARG)`。因此背景 verb 在 Shell 內已被選取並 invoke，失敗 seam 是 host 沒有把背景 folder identity 傳到 invoke，而不是 VS Code 或 Git Bash registration。

OpenCode 的公開原始碼研究也找到可工作的 Shell pattern（[ChromaFiler `FolderWindow.cpp`](https://github.com/vanjac/chromafiler/blob/main/src/FolderWindow.cpp)）：由 `IShellView::GetItemObject(SVGIO_BACKGROUND, IID_IContextMenu)` 取得原生背景 menu、以 `IUnknown_SetSite` 將同一個 Shell view 設為 site，再以 `CMINVOKECOMMANDINFO(EX).lpDirectory` 傳入目前資料夾。PD-159 因此只在 `ExplorerHost` 加入這個 host-level bridge：對零選取的背景 `WM_CONTEXTMENU` 使用原生 `IContextMenu`／已安裝 extension，轉發 `IContextMenu2/3` 的 menu message，並以該 host 的 `location_` invoke；檔案項目右鍵仍交給原生 view。這不是 VS Code／Git Bash 特例，也沒有自製 verb、`ShellExecute` 或 registry 讀寫。

## Acceptance criteria

1. 交接區包含修正前 PaneDock FAIL、同機 Windows 檔案總管 PASS、實際 `QueryService` service/IID/HRESULT 與 Process Create 參數；最終根因不可只靠推測。
2. `E:\GitHub\PaneDock` 空白處的 `以 Code 開啟` 與 `Open Git Bash here` 都在該 location 正常執行。
3. 從另一個 pane／tab 的不同 location 執行時使用各自 location，不能誤用其他 pane。
4. `FileLocator Pro...` 仍成功；檔案項目右鍵、其他背景 verbs、installed shell extensions、導覽、Group/tab 切換與四 pane 同時存在仍正常。
5. verb／site callback 期間立即關閉 PaneDock，不 crash、不 deadlock、不在 parent HWND 銷毀後回呼 host；所有 initialized browser 仍先 `Destroy()`。
6. 沒有 VS Code／Git Bash 專屬產品碼、背景執行緒、timer、polling、新 dependency、fake COM 或 `src/core` Win32／COM leakage。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 與 `git diff --check` 全數通過。

這是 live Shell host 契約，沒有可信的 `core` 自動測試 seam；focused runnable check 是 Phase A/B 的真實桌面背景-verb 對照矩陣。不得以 mock `IExplorerBrowser` 或只看選單出現來宣稱 PASS。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "class Site|QueryInterface|QueryService|IUnknown_SetSite|IServiceProvider|ICommDlgBrowser|IShellBrowser|ShellCallScope|detach|destroy" src\explorer_host src\app_shell\main.cpp
rg -n "DEBUG-PD159|Code\.exe|Visual Studio Code|git-bash" src tests
git diff --check
```

```powershell
.\build\PaneDock.exe
# 真實桌面：執行 Phase A/B 的兩個原始失敗 verbs、FileLocator Pro 成功對照、Windows 檔案總管對照、兩個 location、四 pane 與 close/re-entry 矩陣。
```

## Handoff requirements

- 記錄 Windows／PaneDock／VS Code／Git versions、測試 Group/pane/tab/location，以及 Windows 檔案總管對照結果。
- 記錄修正前後的 service ID、requested IID、HRESULT、Process Create image／command line／working directory；敏感路徑須遮蔽。
- 指出最終共享根因與最小 site contract；若不是 `QueryService`，附反證與實際 seam。
- 記錄兩個 location、兩個原始失敗 verbs、`FileLocator Pro...` 成功對照、檔案項目 menu、四 pane 與 close/re-entry 結果。
- 確認所有 `[DEBUG-PD159]` 與 throwaway capture code 已移除，並記錄 build、full CTest、boundary grep、`git diff --check`。

## 交接區

<!-- 實作 agent 填寫，append-only -->

### 2026-08-31 實作交接

- 修正範圍只有 `src/explorer_host/explorer_host.cpp`：既有 `Site` 增加 `ICommDlgBrowser`，`QueryInterface` 回應 `IID_ICommDlgBrowser`；`QueryService` 僅對 `SID_SExplorerBrowserFrame` 將 requested IID 委派回同一個 site，其他 service 維持 `E_NOINTERFACE`。`OnDefaultCommand` 回 `S_FALSE` 讓預設 Shell view 執行命令；`OnStateChange`／`IncludeObject` 回 `S_OK`，不攔截導航、篩選或項目。
- 這是共享的 ExplorerBrowser site contract 修正，沒有加入 VS Code／Git Bash 字串、registry 讀寫、`ShellExecute`、`IShellBrowser` façade、timer、thread、dependency 或 `src/core` 變更；`IUnknown_SetSite(nullptr)`、`ShellCallScope`、`Destroy` 順序未改。
- 靜態根因證據：修正前 `QueryService` 對所有 service／IID 一律回 `E_NOINTERFACE`；Microsoft `IExplorerBrowser` host contract 指定以 `SID_SExplorerBrowserFrame` 提供 `ICommDlgBrowser`。背景命令的 `%v`／`%V` 需要目前資料夾 identity，這與「FileLocator 成功、Code/Git Bash 失敗」邊界一致。因本次可用 Windows session 在驗證期間停留於鎖定畫面，未能取得 Phase A 要求的實際 runtime `service_id`／requested `iid`／HRESULT 或 Process Create capture；因此這仍標為高信心的靜態根因，不能冒充已完成 runtime capture。
- 環境記錄：Windows NT `10.0.26200.0`；PaneDock Release build（基準 commit `8b2c3d1`，無 embedded product version）；VS Code `1.135.0` x64；Git `2.55.0.windows.3`。目標測試為 `Github` Group、`PaneDock` tab、`E:\GitHub\PaneDock`，並需覆蓋另一個 location、另一個 pane/tab 與四 pane。
- 修正前人工結果沿用本票上方證據：PaneDock 空白處 `以 Code 開啟`／`Open Git Bash here` FAIL；同機 Windows 檔案總管以 Code PASS；PaneDock `FileLocator Pro...` PASS。修正後兩個背景 verb、另一 location 的實際命令參數、FileLocator、檔案項目 menu、installed extensions、Group/tab 導航、四 pane、close/re-entry 尚未在鎖定桌面重跑，需解鎖後依矩陣補驗。
- 自動檢查：CMake configure PASS；Release build PASS；提升權限環境完整 CTest `13/13 PASS`（含 `panedock_launch_smoke`）；`panedock_explorer_host_lifetime_check.exe` PASS；ticket boundary `rg` PASS；未留下 `[DEBUG-PD159]` 或 throwaway capture code；`git diff --check` PASS。真實桌面矩陣與 runtime service／Process Create 證據完成後，才可依 tracker 規則結案。

### 2026-08-31 實作交接（根因反證與原生背景 menu bridge）

- 既有 `ICommDlgBrowser`／`SID_SExplorerBrowserFrame` site contract 已保留，但標準 Shell route 的 CDB trace 顯示 `CRegistryVerbsContextMenu::_Execute` 收到的 `CMINVOKECOMMANDINFOEX` `cbSize=0x68`、`fMask=0x24100000`、`command=0x12`、`lpDirectory=null`；`_GetShellItemArray` 同時回 `E_INVALIDARG`。這是「只有依賴目前資料夾的 background verb 失敗」的直接反證，故最終共享 seam 改為背景 `IContextMenu` invoke 參數，而非再擴張 `IShellBrowser` façade。
- `ExplorerHost` 現在在每次 navigation 完成後，對該 `IShellView` 安裝唯一的 `SetWindowSubclass`。只有零選取的 `WM_CONTEXTMENU` 走 `SVGIO_BACKGROUND` 原生 `IContextMenu`；以同一 `IShellView` 設 site，將 `location_` 同時放入 `lpDirectoryW` 與可轉換的 `lpDirectory`，並轉發 `IContextMenu2/3` menu messages。navigation／destroy 先移除 subclass 並清空 menu COM 參照；流程全程使用 `ShellCallScope`，不改 `src/core`、tab／Group persistence 或 Shell view lifetime policy。
- 真實桌面（Windows `10.0.26200.0`、PaneDock Release、VS Code `1.135.0` x64、Git `2.55.0.windows.3`）結果：`Github`／`PaneDock`／`E:\GitHub\PaneDock` 背景選 `以 Code 開啟` 成功，VS Code 出現標題 `main.cpp - PaneDock - Visual Studio Code`；同處 `Open Git Bash here` 成功，Process command line 為 `"C:\\Program Files\\Git\\git-bash.exe" "--cd=E:\\GitHub\\PaneDock."`。切換同一 Group 的 `NimbleRun` tab 後，Git Bash command line 為 `"C:\\Program Files\\Git\\git-bash.exe" "--cd=E:\\GitHub\\NimbleRun."`，未誤用其他 pane；切換 `Misc` 四 pane Group 可正常 realize 四個 Shell view，並已切回原本的 `Github`／`PaneDock` tab。`FileLocator Pro...` 仍成功，項目右鍵仍為原生 item menu，右上 `E:\GitHub\PaneDock\build` 背景 menu 仍保留 Git／Shell extensions。close-while-menu 矩陣尚未完成。
- 自動檢查：CMake configure／Release build PASS；`ctest --test-dir build --output-on-failure -E panedock_launch_smoke` `12/12 PASS`；`panedock_explorer_host_lifetime_check.exe` PASS；boundary `rg` PASS；無 `[DEBUG-PD159]`／throwaway capture code；`git diff --check` PASS。完整 `panedock_launch_smoke` 在目前 medium-integrity sandbox 會因 `%LOCALAPPDATA%\PaneDock` session save 權限跳出「could not save its session」對話框，非產品 Shell invoke 失敗；以可寫入 session 的同等權限隔離啟動測得 graceful close／exit code 0。
