# PD-082 — 非「詳細資料」檢視模式仍顯示 column header

Phase 7 · app_shell · explorer_host · Depends on: PD-079

- Source: 使用者實機截圖回報(2026-08-27)。
- Origin: 使用者原文:「除了『詳細資訊』以外，其他外觀都不應該有 column header」，附截圖顯示大圖示檢視(7-7zip / Adobe / AMD / Application 等資料夾)上方仍有一列「名稱、修改日期、類型、大小」欄位標題。
- Priority: MEDIUM——純視覺缺陷,不影響操作,但與 PD-079 已確認的產品決策直接牴觸,使用者已明確指出。

## 與 PD-079 的關係

`docs/tickets/PD-079-view-mode-menu-eight-items-and-column-header.md` 已確認的產品決策 5:

> Column header 只在「詳細資料」顯示,其餘 7 種檢視不顯示——這是 Windows Shell ListView 在對應 `FOLDERVIEWMODE` 下的原生行為,不是 PaneDock 自己畫的。

PD-079 的交接區記錄這條驗收(該票驗收項 6)**從未實機驗證**:

> | 6 | 未驗證 | 需要逐一切換並截圖 8 種模式的 column header;依協作政策未做連續 UI 操作,亦沒有把原生 Shell 行為推定為實機 PASS。 |

**本票是那個未驗證假設被使用者實機推翻後的後續票**,不是重開 PD-079 的決策,而是本票要找出「原生行為」為何在本專案的宿主環境下沒有成立的根因並修正。PD-079 文件本身依規則不得編輯,狀態異動與本票的存在已足夠反映事實。

## 已確認的現況(有程式碼證據)

`src/app_shell/main.cpp` 目前沒有任何操作 ListView header 可見性的程式碼(`rg -n "Header|LVS_REPORT" src\app_shell\main.cpp src\explorer_host\explorer_host.cpp` 無命中)——header 的顯示與否完全交給真實 `IExplorerBrowser`/Shell ListView 自行決定,PaneDock 只負責呼叫 `IFolderView2::SetViewModeAndIconSize`。

檢視模式套用與擷取的既有路徑(`main.cpp`):

- `apply_pane_view_mode`(第 1079-1089 行):`parse_view_mode(tab.view_mode)` 若解析失敗(`std::nullopt`,例如全新 tab 從未被 `capture_pane_view_mode` 寫入過任何值),**直接跳過整個 `set_view_mode` 呼叫**,不會對 Shell view 下任何指令——此時 Shell view 維持它自己剛建立/剛導覽完成時的原生預設狀態,而不是 PaneDock 認定的某個已知模式。
- `handle_navigation_complete`(第 1773-1791 行):每次導覽完成都呼叫 `apply_pane_view_mode`,時序上在 `refresh_tab_strip`/`save_now` 之前。
- `capture_pane_view_mode`(第 1067-1077 行):讀回 Shell 目前的真實模式寫回 `tab.view_mode`,在 `apply_pane_view_mode` 內於套用之後、以及第 1296 行的另一呼叫點,都會再呼叫一次。

**尚未查證,需要實作 agent 用真實桌面操作查證的假設(至少兩個,不要只驗一個就結案):**

1. **全新 tab / `tab.view_mode` 為空字串的路徑**:`parse_view_mode` 回傳 `nullopt`,`apply_pane_view_mode` 完全不呼叫 `set_view_mode`,Shell view 停留在它自己的預設值。若 Shell 對這個特定資料夾的原生預設剛好是「圖示排列 + header 仍顯示」的中間狀態(例如作業系統記得使用者之前在**這個實體資料夾路徑**用過『詳細資料』,重新開啟時 Explorer 本身會保留 per-folder 設定,但 view mode 與 header 顯示是兩個獨立旗標,沒有同步),就會重現使用者截圖的現象。
2. **從『詳細資料』切換到其他模式的路徑**:`SetViewModeAndIconSize(FVM_ICON, ...)` 呼叫成功,圖示尺寸確實改變(PD-079 已用 `panedock_explorer_host_lifetime_check.exe` 量測過 mode/size 正確 round-trip),但 header 子視窗的可見性是否會被這個 API 呼叫一併處理,PD-079 從未實機驗證過。若 Windows Shell 的行為是「`SetViewModeAndIconSize` 只改變 layout 引擎與圖示尺寸,header 的顯示/隱藏是切換到/離開 `FVM_DETAILS` 時另一個獨立步驟才會觸發」,而目前呼叫時機或參數遺漏了那個步驟,header 就會殘留。

**實作 agent 必須先用單次點擊 + 截圖,分別驗證這兩條路徑各自是否重現,才能判定根因**,不要假設是哪一個就直接動手改。

## 已確認的產品決策

1. **只有「詳細資料」(`FVM_DETAILS`)顯示 column header,其餘 7 種模式一律不顯示**——這是 PD-079 決策 5 的重申,本票不改變這個目標,只是要真正做到。
2. **優先尋找「呼叫 Shell 既有 API 使其原生行為正確發生」的修法,不要自己畫一個假的 header 遮蔽層或用 `ShowWindow`/`SetWindowPos` 硬藏 header 子視窗。** 理由:header 子視窗屬於 Shell 自己的 `SHELLDLL_DefView`/ListView 內部結構,不是 `AGENTS.md` 允許重刻的範圍(「The file list is never reimplemented」)。若查證後發現目前的 API 呼叫方式(呼叫時機、參數、呼叫順序)不足以觸發 Shell 的原生隱藏行為,才調整**我們呼叫既有 Shell API 的方式**,而不是繞過 Shell 自己控制 header。
3. **若查證發現問題出在「全新 tab 從未套用過任何 view mode」這條路徑(假設 1)**,修法方向是確保每個 realized 的 tab 在首次導覽完成時都會明確呼叫一次 `set_view_mode`(即使 `tab.view_mode` 是空字串,也要決定一個明確的預設值並呼叫,而不是完全跳過),而不是引入一個新的「header 可見性」旗標。**具體預設值(例如空字串時預設為 `FVM_ICON` 何種尺寸,或退回真實 Explorer 的『General items』預設)由實作 agent 決定並記錄理由**,但不得破壞既有已儲存的 `view_mode` 字串格式(PD-079 建立的 `core::TabState::view_mode` 持久化格式,見 `docs/tickets/PD-079-view-mode-menu-eight-items-and-column-header.md` 的 Scope 4)。
4. **若查證發現問題出在「切換模式後 header 沒有跟著隱藏」(假設 2)**,查證 `IFolderView2`/`IFolderView`/`IShellView` 是否有對應的顯式呼叫(例如重新導覽同一個 location、或另一個已知的 Shell 慣例)能觸發 header 正確隱藏,並在交接區記錄查證依據(官方文件連結或反向驗證實測),不得憑猜測硬呼叫。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> The file list is never reimplemented.

——見決策 2,header 屬於檔案列表本身的一部分。

`AGENTS.md`:
> Group switching keeps live views alive and re-navigates them.

——本票的修法必須在既有的 realize-on-activation/re-navigate 模型內運作,不得為了套用 view mode 額外破壞或重建 `IExplorerBrowser`。

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/tickets/PD-079-view-mode-menu-eight-items-and-column-header.md` 已確認的產品決策 5(本票延續,不覆寫):
> Column header 只在「詳細資料」顯示,其餘 7 種檢視不顯示——這是 Windows Shell ListView 在對應 `FOLDERVIEWMODE` 下的原生行為,不是 PaneDock 自己畫的。

## Files to read and trace first

- `src/app_shell/main.cpp` `apply_pane_view_mode`(第 1079-1089 行)、`capture_pane_view_mode`(第 1067-1077 行)——套用與擷取的核心邏輯,**先查證假設 1**。
- `src/app_shell/main.cpp` `handle_navigation_complete`(第 1773-1791 行)——套用時機。
- `src/app_shell/main.cpp` 第 1296 行、第 1788 行、第 1945 行、第 2316 行——`apply_pane_view_mode`/`capture_pane_view_mode`/`set_view_mode` 的其他呼叫點,確認是否有路徑遺漏套用。
- `src/explorer_host/explorer_host.cpp` `set_view_mode`/`get_view_mode`(第 388-403 行附近)——目前只包了 `SetViewModeAndIconSize`/`GetViewModeAndIconSize`,**先查證假設 2**:這兩個方法之外,`IFolderView2`/`IShellView`/`IFolderView` 是否有已知會影響 header 可見性的其他方法。
- `docs/tickets/PD-079-view-mode-menu-eight-items-and-column-header.md` 交接區——已查證的 API 簽章、四級圖示尺寸實測數值,直接沿用不要重查。
- `docs/tickets/PD-052-pane-refresh-and-view-mode-switcher.md`——`view_mode` 欄位與 `IFolderView2` 使用慣例的原始脈絡。

## Scope

1. 用單次點擊 + 截圖查證假設 1 與假設 2,判定 header 殘留的實際觸發路徑(可能兩者都成立,或只有其中一個)。
2. 依查證結果,在 `apply_pane_view_mode`/`set_view_mode` 或其呼叫時機上做最小修正,讓「非詳細資料模式一律不顯示 header」的原生行為正確發生。
3. 若決策 3 適用(全新 tab 路徑),決定並記錄一個明確的空 `view_mode` 預設值處理方式。

## Non-goals

- 不改選單的 8 個項目、彈出方式、ID 配置(PD-059/PD-079 已定案)。
- 不改四級圖示尺寸的像素值(PD-079 已用真實 Shell round-trip 驗證過 256/96/48/16)。
- 不改 `core::TabState::view_mode` 的持久化字串格式,除非決策 3 的預設值處理需要(若需要,必須是加法式擴充,沿用 `AGENTS.md` 的設定檔可擴充性規則)。
- 不自行繪製或遮蔽 header 子視窗(決策 2)。
- 不處理「詳細資料」模式本身的欄位內容或欄寬(不在使用者回報範圍內)。

## Acceptance

1. **針對假設 1**:開一個全新 Group 或全新 tab,首次導覽到一個資料夾,在從未手動切換過檢視模式的情況下,確認 header 是否顯示;若目前預設模式不是「詳細資料」,header 不應顯示。
2. **針對假設 2**:在同一個 tab 內,先切到「詳細資料」(header 顯示),再切到「大圖示」/「清單」/其他任一非詳細資料模式,確認 header 消失。
3. 8 種檢視模式各自單獨截圖確認:只有「詳細資料」顯示 header,其餘 7 種都不顯示——比照 PD-079 原本要求但未完成的驗收 6,本票必須真正完成它。
4. 修正後不影響「詳細資料」模式本身欄位標題的正確顯示與 PD-079 已驗證的圖示尺寸切換行為。
5. 切換 Group 再切回來,或重啟程式還原 session,header 顯示邏輯依然正確(不因 realize-on-activation 的重新導覽而回歸)。
6. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
7. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "apply_pane_view_mode|capture_pane_view_mode|set_view_mode|parse_view_mode|view_mode_name" src\app_shell\main.cpp src\explorer_host\explorer_host.h src\explorer_host\explorer_host.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 單次點擊+截圖:全新 tab 導覽到一個資料夾,確認 header 狀態(假設 1);
# 單次點擊+截圖:同一 tab 從詳細資料切到大圖示,確認 header 消失(假設 2)。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,不要用 `Graphics.CopyFromScreen`。

**驗證原則(本專案共同約定):只做單次點擊/操作 + 截圖的驗證由 Agent 或本人執行;需要連續、多步驟操控滑鼠鍵盤的測試(例如逐一切換 8 種模式並各自截圖比對)交給使用者本人執行**,避免電腦操作工具長時間佔用實體滑鼠鍵盤。若某項驗收條件無法用單次動作完成,如實在交接區標記未驗證並說明需要使用者手動驗證的具體步驟。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 假設 1、假設 2 各自的查證結果(重現/不重現,證據為何)。
- 實際根因與修正點(檔案、函式、修改內容)。
- 若涉及決策 3 的空 `view_mode` 預設值處理,寫明選擇的預設值與理由。
- 8 種模式各自的 header 顯示結果(已驗證的用截圖或量測值,未驗證的說明原因與需要使用者驗證的具體步驟)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 實作交接（2026-08-27）

- 假設 1（全新 tab 的 `view_mode` 為空字串）：**修正前重現**。在修正前的 Release binary 中，單次點擊新增 tab 並等候首次導覽完成，該 `C:\` 圖示檢視仍顯示「名稱／修改日期／類型／大小」header；程式碼也確認 `parse_view_mode("")` 使 `apply_pane_view_mode` 完全跳過 setter。**修正後不再重現**：同樣新增 tab 的單次點擊後截圖為 Large icons，第一個 pane 沒有 header。
- 假設 2（模式切換不會同步 header）：**修正前重現**。在同一 tab 先單次選取 Details，截圖顯示欄位 header；再單次選取 Large icons，圖示已切換但 header 仍然存在。這證明 `SetViewModeAndIconSize` 不會獨立更新欄位 header 的可見性。**修正後不再重現**：Details 截圖仍顯示 header，接著切回 Large icons 的截圖沒有 header。
- 實際根因是兩條路徑共同暴露同一個 Shell 狀態缺口：`SetViewModeAndIconSize` 只設定 view mode／圖示尺寸，`FWF_NOCOLUMNHEADER` 是獨立的 folder-view flag；空 `view_mode` 路徑則連前述 Shell API 都沒有呼叫。
- 修正點：`src/explorer_host/explorer_host.cpp` 的 `ExplorerHost::set_view_mode` 在 `SetViewModeAndIconSize` 成功後，透過公開的 `IFolderView2::SetCurrentFolderFlags` 更新 `FWF_NOCOLUMNHEADER`；`FVM_DETAILS` 清除該旗標，其餘模式設定該旗標。這個共享入口同時涵蓋首次套用與 View 選單切換，不操作 Shell 子視窗。API 契約：[SetCurrentFolderFlags](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview2-setcurrentfolderflags)、[SetViewModeAndIconSize](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview2-setviewmodeandiconsize)。
- `src/app_shell/main.cpp` 的 `apply_pane_view_mode` 對空字串明確套用 `FVM_ICON` + `kLargeIconSize`。選擇 Large icons（96 px）是沿用既有 `FVM_ICON` 舊值映射與 PD-079 的已驗證尺寸，之後仍由 `capture_pane_view_mode` 寫回既有 `FVM_ICON:96` 格式；沒有改變持久化 schema 或既有字串格式。

#### Agent checks

```text
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
PASS — configure/generate completed with LLVM-MinGW and Ninja.

cmake --build build
PASS — PaneDock.exe and all targets linked after the stale PID 28308 was gracefully terminated with taskkill (no /F).

ctest --test-dir build --output-on-failure
PASS — 5/5 tests passed.

rg -n "apply_pane_view_mode|capture_pane_view_mode|set_view_mode|parse_view_mode|view_mode_name" src\app_shell\main.cpp src\explorer_host\explorer_host.h src\explorer_host\explorer_host.cpp
PASS — all required view-mode call sites and mappings found; shared set_view_mode now also applies FWF_NOCOLUMNHEADER.

git diff --check
PASS — final handoff append and source diff have no whitespace errors.
```

#### Acceptance evidence

| # | 結果 | 證據 |
|---|---|---|
| 1 | PASS（單次操作+截圖） | 修正後新增 tab 的首次導覽使用 Large icons，截圖與 accessibility tree 均沒有第一個 pane 的 header；空值路徑現在會明確呼叫 `set_view_mode(FVM_ICON, 96)`。 |
| 2 | PASS（單次操作+截圖） | 同一 tab 的 Details 截圖有 header；下一次單次選取 Large icons 後截圖沒有 header。 |
| 3 | 部分驗證 | Large icons 與 Details 已各自用單次選取+截圖驗證；Extra large icons、Medium icons、Small icons、List、Tiles、Content 尚未逐一截圖。依票據協作原則，請使用者分別選取這 6 項並以 `PrintWindow(hwnd, hdc, 2)` 截圖確認無 header。 |
| 4 | PASS（部分） | Details 的 header 在切換流程中仍正確顯示；PD-079 的 256/96/48/16 round-trip self-check 未改動。Tiles/Content 的畫面仍需使用者手動確認。 |
| 5 | 部分驗證 | 修正後重新啟動 binary 並還原既有 session 時，已觀察 Large icons 沒有 header；完整 Group 切換再切回及 Details/其餘模式的還原矩陣尚未做連續操作，請使用者手動補驗。 |
| 6 | PASS | `cmake --build build` 通過；`ctest --test-dir build --output-on-failure` 顯示 5/5 通過。 |
| 7 | PASS | 最終 `git diff --check` 通過。 |

視窗驗證前均重新取得唯一的 PaneDock window、啟用視窗並等待 2 秒；每個操作都在最新觀察後只執行一次，再立即擷取畫面。視覺證據使用 Computer Use 的視窗擷取，未使用 `Graphics.CopyFromScreen`；未完成的 6 種模式與完整 Group/restart 矩陣保留給使用者依上述步驟補驗。暫存視覺 probe 已刪除，沒有納入產品或提交內容。
