# PD-059 — 檢視模式按鈕改為下拉選單,取代目前的單向循環切換

Phase 7 · app_shell · Depends on: PD-052

- Source: 使用者實機操作後回報(2026-08-26)。
- Origin: 使用者原文第 4 項:「pane 裡面的切換檢視按鈕,按下時應該是下拉選單,選擇圖示大小或清單或詳細資料,而不是只能固定切換成一種。」
- Priority: MEDIUM——功能已存在但互動方式錯誤,使用者要切到特定模式必須反覆點擊猜測。

## 已確認的根因(有程式碼證據,不是猜測)

PD-052 已經完成底層能力:`ExplorerHost::set_view_mode(FOLDERVIEWMODE)` / `get_view_mode(FOLDERVIEWMODE&)` 透過 `IFolderView2::SetCurrentViewMode`/`GetCurrentViewMode` 實作,`core::TabState::view_mode` 也已經以字串化列舉名稱(`"FVM_ICON"`/`"FVM_SMALLICON"`/`"FVM_LIST"`/`"FVM_DETAILS"`)持久化並在還原時套用。

**缺口在 UI 互動層:** PD-052 的交接區記載「View 按鈕循環四種模式並立即 `save_now`」——也就是 `cycle_view_mode` 這個 `WM_COMMAND` 處理只做單向循環。使用者無法直接選到想要的模式,只能一直點到猜中為止,而且按鈕上沒有任何指示目前是哪一種模式。

## 已確認的產品決策

1. **改用 `TrackPopupMenu` 彈出下拉選單,不自繪選單。** `TrackPopupMenu` 是 Win32 標準做法,本專案已經在 Group 清單的右鍵選單用過同一套模式(`src/app_shell/main.cpp` 第 3024-3048 行:`CreatePopupMenu` → `AppendMenuW` → `SetForegroundWindow` → `TrackPopupMenu(TPM_RETURNCMD | ...)` → `DestroyMenu` → 把回傳值當成 `WM_COMMAND` 送回自己)。**直接照抄那一段的結構,不要發明新做法。**
2. **選單錨定在 View 按鈕的左下角**(`GetWindowRect(view_button)` 取得螢幕座標,用 `rect.left`/`rect.bottom`),使選單看起來是從按鈕「掉下來」的,而不是出現在游標位置。
3. **選單項目為使用者原文列出的四項,UI 文字用英文**(`AGENTS.md`:App UI text must be English):`Large icons`(`FVM_ICON`)、`Small icons`(`FVM_SMALLICON`)、`List`(`FVM_LIST`)、`Details`(`FVM_DETAILS`)。順序照這個排,與 Windows 檔案總管的慣例一致。
4. **目前生效的模式在選單中以 radio check 標記**(`AppendMenuW` 帶 `MF_CHECKED`,並用 `CheckMenuRadioItem` 讓標記呈現為圓點而非勾號)。目前模式從該 pane 的 active tab 的 `TabState::view_mode` 讀取(不是從 `IFolderView2` 即時查詢——`TabState` 已經由 PD-052 的 `capture_pane_view_mode` 維護,是單一事實來源,而且在 pane 尚未 realize 時也有值)。
5. **`cycle_view_mode` 這個循環函式在本票中移除**,不保留為隱藏功能或鍵盤捷徑。使用者明確說「不要只能固定切換成一種」,保留循環邏輯只會留下死碼。
6. **選單 ID 的配置必須避開既有的 ID 區段。** 既有的 ID 常數在 `src/app_shell/main.cpp` 檔首(`kLayoutButtonIdBase`、`kBackButtonIdBase`、`kForwardButtonIdBase`、`kUpButtonIdBase`、`kRefreshButtonIdBase = 340`、`kViewModeButtonIdBase = 350` 等)。實作 agent 必須先 grep 全部 `constexpr int k*Id*` 常數,選一個沒被佔用的區段,並在該處加註解說明區段用途。**每個 pane 需要四個模式 ID,共 4 × `kExplorerCount` 個,或改用「一個共用的模式 ID 集合 + 記住是哪個 pane 開的選單」——後者較省 ID,由實作 agent 決定,但要在交接區說明選擇理由。**
7. **View 按鈕的圖示不因目前模式改變。** 目前是四個小方塊(`draw_navigation_icon_button` 的 `case 4`),保持不變即可;模式指示由選單裡的 radio check 提供。改成隨模式變化的圖示超出本票範圍。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> **App UI text must be English.** No Chinese strings ship in the binary.

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.
>
> (相關:`TrackPopupMenu` 是 modal 的,會跑自己的訊息迴圈。實作 agent 必須確認選單開啟期間若有 Shell 導覽完成回呼進來,不會造成狀態不一致或當機。)

`docs/design-spec.md` FR-002 / NFR-005(view mode 是必要還原狀態,本票不得破壞 PD-052 已建立的還原行為):
> 必要狀態(版型、location、tab、view mode、排序)必須精確還原,否則視為缺陷。

## Files to read and trace first

- `src/app_shell/main.cpp` 第 3024-3048 行(Group 清單的 `WM_CONTEXTMENU` 右鍵選單)——**本票要照抄的 `TrackPopupMenu` 模式範本。**
- `src/app_shell/main.cpp` 的 `cycle_view_mode` 與 `kViewModeButtonIdBase` 的 `WM_COMMAND` 處理——本票要取代的邏輯。
- `src/app_shell/main.cpp` 的 `view_mode_name(FOLDERVIEWMODE)` / `parse_view_mode(std::string_view)`——PD-052 已建立的字串↔列舉對應,直接重用。
- `src/app_shell/main.cpp` 的 `capture_pane_view_mode` / `apply_pane_view_mode`——PD-052 已建立的捕獲/套用路徑,本票選單選擇後要走同一條路。
- `src/explorer_host/explorer_host.h` 第 41-42 行(`set_view_mode`/`get_view_mode`)——不需要改。
- `src/app_shell/main.cpp` 檔首的全部 `constexpr int k*Id*` 常數——選單 ID 選段前必須先看過。
- `docs/tickets/PD-052-pane-refresh-and-view-mode-switcher.md`——底層能力的來源票與其交接區。

## Scope

1. View 按鈕的 `WM_COMMAND` 處理改為彈出 `TrackPopupMenu` 下拉選單,列出四種檢視模式並標記目前模式。
2. 選單選擇後套用該模式到該 pane 的 Shell view、寫回 `TabState::view_mode`、`save_now`。
3. 移除 `cycle_view_mode` 循環邏輯。

## Non-goals

- 不新增 `FVM_TILE`/`FVM_CONTENT` 等第五種以上的模式。
- 不改 View 按鈕的圖示或位置。
- 不改 `ExplorerHost` 的任何簽章(PD-052 已足夠)。
- 不改排序欄位/方向的還原邏輯。
- 不把檢視模式文字顯示在狀態列(PD-052 的 non-goal,本票沿用)。

## Acceptance

1. 點擊任一 pane 的 View 按鈕,在按鈕下方彈出含四個項目的下拉選單:Large icons / Small icons / List / Details。
2. 選單中目前生效的模式有 radio 標記。
3. 選擇任一項後,該 pane 的 Shell view 立即改為該檢視模式,選單關閉。
4. 選擇後切換 Group 再切回來(或重啟程式),該 tab 的檢視模式正確還原(PD-052 的還原行為未回歸)。
5. 在多 pane 版型下,對不同 pane 開選單,各自反映與修改自己的檢視模式,不會互相影響。
6. 按 Esc 或點選單以外的地方可以關閉選單而不改變模式。
7. 選單開啟期間程式不當機、不卡死。
8. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
9. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "TrackPopupMenu|cycle_view_mode|view_mode_name|parse_view_mode|CheckMenuRadioItem" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:對每個 pane 開 View 選單,確認四項與 radio 標記;各選一種確認 Shell view 實際改變;
# 切換 Group 再切回來確認還原;重啟程式確認還原。
# 本環境已具備 PrintWindow 截圖與 SetCursorPos/mouse_event 點擊模擬能力,請實際驗證。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`。**注意:`TrackPopupMenu` 的選單是獨立的 top-level 視窗,`PrintWindow` 主視窗截不到它。** 要驗證選單外觀需另外找到選單的 HWND(class name `#32768`)單獨截圖,或改用全螢幕 `CopyFromScreen`——後者需確認桌面已解鎖且 PaneDock 在最前景。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 選單 ID 的配置方式(每 pane 四個 ID,或共用 ID 集合 + 記住來源 pane)與選擇理由。
- 目前模式的讀取來源最終是 `TabState::view_mode` 還是 `IFolderView2`,以及為何。
- `TrackPopupMenu` 的 modal 訊息迴圈與 Shell 導覽回呼的重入測試結果。
- 四種模式逐一切換 + 還原的實機驗證結果。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 實作交接（2026-08-27）

- `src/app_shell/main.cpp` 已將 View button 的 `WM_COMMAND` 路徑改為 `TrackPopupMenu`：`CreatePopupMenu` → 四次 `AppendMenuW` → `SetForegroundWindow` → `TrackPopupMenu(TPM_RETURNCMD | TPM_RIGHTBUTTON, button_rect.left, button_rect.bottom, ...)` → `DestroyMenu` → 用回傳值送回 `WM_COMMAND`。選單項目依序為 `Large icons` / `Small icons` / `List` / `Details`，對應 `FVM_ICON` / `FVM_SMALLICON` / `FVM_LIST` / `FVM_DETAILS`。
- 已先盤點檔首既有 ID：`kTabStripIdBase=200`、`kBackButtonIdBase=300`、`kForwardButtonIdBase=310`、`kUpButtonIdBase=320`、`kAddressBarIdBase=330`、`kRefreshButtonIdBase=340`、`kViewModeButtonIdBase=350`、`kLayoutButtonIdBase=400`，以及 Group IDs `100–106`。新增 `kViewModeMenuIdBase=360`、`kViewModeMenuIdCount=16`，明確保留 `360–375` 給 View-mode popup。採用每 pane 四個 ID：`base + pane_index * 4 + mode_index`；這比共用 ID 加可變的來源 pane 狀態多 12 個 ID，但不需要在 `TrackPopupMenu` 的 modal/re-entrant 期間維護共享來源欄位，回傳 command 本身即可決定 pane，故多 pane 與 Shell callback 重入時較安全。
- radio check 的目前值直接讀 `active_tab(active_group(...).panes[pane_index]).view_mode`，經既有 `parse_view_mode` 轉為列舉；沒有為 popup 去查詢 `IFolderView2`。選擇後用 `ExplorerHost::set_view_mode` 套用，成功才寫回同一個 `TabState::view_mode` 並呼叫既有 `save_now`。既有 `capture_pane_view_mode` 仍只負責導覽完成/存檔時的同步捕獲。
- `cycle_view_mode` 已完全移除；`rg` 不再找到該符號。未新增 `ExplorerHost`、`core` 或依賴。這段 UI/Shell 行為不能在本專案唯一的 `core` 自動測試 seam 以 fake 有意義地驗證（`docs/testing.md` 明確排除 `IExplorerBrowser` fake 與 flaky UI automation），因此以建置、既有 CTest 和實機檢查取代；本次實機檢查被鎖定桌面阻塞，詳如下。

#### Modal re-entry review

- `TrackPopupMenu` 期間沒有新增 lock、wait、timer 或共享的來源 pane 狀態。四個 command ID 已編碼 pane；若既有 UI thread 上的 Shell navigation-complete callback 在 menu modal loop 中到達，它只會走原有 `handle_navigation_complete` → `apply_pane_view_mode` → `save_now` 路徑，popup 返回後再依 command ID 套用對應 pane。
- **實機重入結果：未驗證。** 本次用 Windows Computer Use 啟動剛建置的 `build\pd062-output\PaneDock.exe` 並取得視窗狀態時，桌面顯示 Windows 鎖定畫面（時鐘/日期）；依安全規則未送出滑鼠/鍵盤輸入，沒有在 menu 開啟期間觸發 navigation-complete，也沒有宣稱「不卡死/不當機」通過。因而沒有產生 `#32768` menu HWND 的截圖；未使用會受鎖定畫面污染的 `CopyFromScreen`。

#### Agent checks

```text
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
PASS — configure completed; CMAKE_GENERATOR=Ninja, LLVM-MinGW Clang.

cmake --build build
PASS — linked build\pd062-output\PaneDock.exe.

ctest --test-dir build --output-on-failure
PASS — 4/4: panedock_diagnostic_flag, panedock_core_model,
      panedock_core_layout, panedock_core_session.

rg -n "TrackPopupMenu|cycle_view_mode|view_mode_name|parse_view_mode|CheckMenuRadioItem" src\app_shell\main.cpp
PASS — TrackPopupMenu/CheckMenuRadioItem and existing view-mode mappings found;
       cycle_view_mode produced no match.

git diff --check
PASS — no whitespace errors.
```

#### Acceptance evidence

| # | 結果 | 證據 |
|---|---|---|
| 1 | 未驗證 | 實機輸入因 Windows 桌面鎖定未執行；程式碼已建立四項 popup 並以 View button 左下角座標定位。 |
| 2 | 未驗證 | 未能取得實際 popup 畫面；程式碼對該 pane 的四個 item 呼叫 `MF_CHECKED` + 正確 pane 子範圍的 `CheckMenuRadioItem`。 |
| 3 | 未驗證 | 未能點選四項並觀察 Shell view；setter/save 路徑已建置通過。 |
| 4 | 未驗證 | 未能執行 Group switch/restart 的實機流程；`TabState::view_mode` 既有 persistence/restore 路徑未被改動。 |
| 5 | 未驗證 | 未能在多 pane 實機逐 pane 操作；每 pane 四個 ID 的靜態解碼範圍已覆蓋 4 panes。 |
| 6 | 未驗證 | 未能送 Esc 或點擊 menu 外部；`TrackPopupMenu` 回傳 `0` 時不送 `WM_COMMAND`，但未作實機觀察。 |
| 7 | 未驗證 | 未能在 popup 開啟中觸發 Shell navigation-complete；只完成上述程式碼層 modal re-entry review。 |
| 8 | PASS | LLVM-MinGW/Ninja configure、build 與 CTest 4/4 實際通過。 |
| 9 | PASS | `git diff --check` 實際通過。 |

因 acceptance 1–7 尚無真實桌面證據，`docs/tickets.md` 的 PD-059 狀態刻意維持 `ready`，沒有改成 `done`。
