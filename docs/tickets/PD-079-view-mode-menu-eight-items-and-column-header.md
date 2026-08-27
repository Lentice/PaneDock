# PD-079 — 檢視模式選單擴充為 8 項,對齊真實檔案總管;僅「詳細資料」顯示欄位標題

Phase 7 · app_shell · explorer_host · Depends on: PD-059

- Source: 使用者附真實 Windows 檔案總管「檢視」選單截圖(2026-08-27)。
- Origin: 使用者原文第 2 項:「切檔案的檢視要有這幾項〔超大圖示、大圖示、中圖示、小圖示、清單、詳細資料、並排、內容〕,跟正常的檔案總管一樣,且只有『詳細資料』會顯示 column header(名稱、日期、大小等),其餘的檢視不會顯示 column header。」
- Priority: MEDIUM——功能已存在但選項數量與真實檔案總管不一致,使用者慣用的顯示密度(例如中圖示)無法選到。

## 覆寫聲明

**本票覆寫 `docs/tickets/PD-059-view-mode-dropdown-menu.md` 已確認的產品決策 3。** PD-059 原決策:

> 選單項目為使用者原文列出的四項……`Large icons`(`FVM_ICON`)、`Small icons`(`FVM_SMALLICON`)、`List`(`FVM_LIST`)、`Details`(`FVM_DETAILS`)。

當時的依據是 PD-052 決策 2「額外模式(`FVM_TILE`/`FVM_CONTENT` 等)是否加入由實作 agent 決定,不強制」——四項是「最低限度」而非「使用者最終要的範圍」。**新證據:使用者附上真實 Windows 檔案總管「檢視」選單截圖,明確要求對齊該選單的完整 8 個項目**,不再是可選範圍,是本票的強制驗收項目。PD-059 的其餘決策(`TrackPopupMenu` 彈出模式、錨定按鈕左下角、radio check、ID 配置慣例、`cycle_view_mode` 移除)全部沿用,不重開。

## 已確認的現況(有程式碼證據)

PD-059 剛完成的實作(`commit 818eae8`)在 `src/app_shell/main.cpp` 定義:

```cpp
constexpr std::array<ViewModeOption, 4> kViewModeOptions{{
    {FVM_ICON, L"Large icons"},
    {FVM_SMALLICON, L"Small icons"},
    {FVM_LIST, L"List"},
    {FVM_DETAILS, L"Details"},
}};
```

以及對應的 `view_mode_name`/`parse_view_mode` 只認識這 4 個字串化列舉名稱。`src/explorer_host/explorer_host.h` 的 `set_view_mode`/`get_view_mode` 只包了 `IFolderView2::SetCurrentViewMode`/`GetCurrentViewMode`(`explorer_host.cpp` 第 388-403 行),沒有處理圖示大小。

使用者截圖列出的 8 項,對照真實檔案總管與公開 Shell API,可分兩類:

1. **有直接對應的 `FOLDERVIEWMODE` 列舉值,只是目前沒接上:** 清單(`FVM_LIST`,已接)、詳細資料(`FVM_DETAILS`,已接)、並排(`FVM_TILE`)、內容(`FVM_CONTENT`)。
2. **超大圖示/大圖示/中圖示/小圖示——這四項在真實檔案總管裡不是四個不同的 `FOLDERVIEWMODE`。** 現代 Windows Shell(Vista 以後)把這四種都視為同一個檢視模式(圖示型)配上不同的圖示像素尺寸,由 `IFolderView2::SetViewModeAndIconSize(FOLDERVIEWMODE ViewMode, int iImageSize)` / `GetViewModeAndIconSize(FOLDERVIEWMODE*, int*)` 控制(`shobjidl.h`,`IFolderView2` 既有介面的另一個方法,本專案已經在用同一個介面取 `folder_view`,新增呼叫不需要新 include)。**`FVM_SMALLICON` 這個列舉值是舊版相容用途,不等於截圖裡的「小圖示」——這是實作 agent 必須先查證、不能假設的地方。**

## 已確認的產品決策

1. **選單項目改為 8 項,順序與文字對齊使用者截圖(繁體中文原文供比對,UI 顯示文字仍是英文,`AGENTS.md` 硬規則):**
   - 超大圖示 → `Extra large icons`
   - 大圖示 → `Large icons`
   - 中圖示 → `Medium icons`
   - 小圖示 → `Small icons`
   - 清單 → `List`(`FVM_LIST`)
   - 詳細資料 → `Details`(`FVM_DETAILS`)
   - 並排 → `Tiles`(`FVM_TILE`)
   - 內容 → `Content`(`FVM_CONTENT`)
2. **前四項(超大/大/中/小圖示)的精確實作方式,由實作 agent 查證 Windows SDK 標頭與 `IFolderView2::SetViewModeAndIconSize` 的行為後決定,不得憑猜測的像素值硬編。** 至少要確認:
   - 該方法在本專案已經使用的 Windows SDK 版本(LLVM-MinGW 目前使用的 SDK,見 `cmake/llvm-mingw.cmake`)裡是否存在、簽章為何。
   - 四個尺寸級距對應的 `iImageSize` 實際數值(真實檔案總管的四級並非任意值,需要查證或反向驗證——例如啟動真實 `explorer.exe` 切換四種圖示大小並讀取 `GetViewModeAndIconSize` 的回傳值作為依據,比自己猜測可靠)。
   - 若查證後發現 `SetViewModeAndIconSize` 在目前 SDK/工具鏈下不可用或行為不如預期,**如實記錄在交接區,並選擇次佳方案(例如四項全部呼叫 `SetCurrentViewMode(FVM_ICON)` 但退化為只有一種圖示尺寸,同時明確標註哪些項目的「大小切換」未真正生效)**,不得為了選單看起來完整而假造沒有真正作用的選項。
3. **`core::TabState::view_mode` 的持久化格式需要能表達「檢視模式 + 圖示尺寸」這個組合,不能只存 `FOLDERVIEWMODE` 的字串化列舉名稱。** 由實作 agent 決定精確形狀(例如在既有字串後接一個尺寸後綴,或擴充成一個小型巢狀結構),但必須符合 `AGENTS.md` 的持久化可擴充性規則(新欄位用加法方式擴充,不得破壞舊版 `session.json` 的可讀性——舊版檔案裡只有 4 種模式字串,升級後的程式必須還能正確讀回並映射到新的 8 選項之一)。
4. **選單 ID 配置沿用 PD-059 已建立的慣例**(每 pane 一組固定基底 + 偏移量解碼回 pane 與模式索引),範圍從 4 項擴充到 8 項時,連帶檢查 `kViewModeMenuIdCount`(PD-059 定為 `16` = 4 pane × 4 模式)是否需要對應擴大為 `32`(4 pane × 8 模式),以及緊鄰的 ID 區段(`kLayoutButtonIdBase = 400`)是否被佔用衝突——**必須先重新 grep 全部 `constexpr int k*Id*` 常數確認,不得假設 PD-059 當時保留的區段還夠用。**
5. **Column header 只在「詳細資料」顯示,其餘 7 種檢視不顯示——這是 Windows Shell ListView 在對應 `FOLDERVIEWMODE` 下的原生行為,不是 PaneDock 自己畫的。** 實作 agent **必須先用目前程式碼實際切到 8 種模式各自截圖驗證**,確認這件事是否已經因為使用真實 `IExplorerBrowser`/`IFolderView2` 而自動成立,才決定要不要寫任何額外程式碼。**很可能完全不需要新程式碼**(真實檔案總管本身就是這樣運作,PaneDock hosts 的就是同一個 Shell view),先驗證再動手,不要假設有落差就直接寫程式碼補。若實測發現例外(例如某個模式意外顯示了 header),才需要進一步調查根因並處理。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> **App UI text must be English.** No Chinese strings ship in the binary.

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`AGENTS.md`:
> **Every persisted config/setting file must be designed for forward extensibility.** ……A schema change is additive (new optional fields, new migration step) rather than a destructive reinterpretation of an existing field's meaning.

`docs/tickets/PD-059-view-mode-dropdown-menu.md` 已確認的產品決策 6(ID 配置規則,本票沿用):
> 選單 ID 的配置必須避開既有的 ID 區段。實作 agent 必須先 grep 全部 `constexpr int k*Id*` 常數,選一個沒被佔用的區段。

`docs/tickets/PD-052-pane-refresh-and-view-mode-switcher.md` 已確認的產品決策 1:
> 檢視模式透過 `IExplorerBrowser::GetCurrentView(IID_PPV_ARGS(&folder_view))` 取得 `IFolderView2`,呼叫 `IFolderView2::SetCurrentViewMode`/`GetCurrentViewMode` 切換與查詢,不是自己重新實作檢視樣式渲染。

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `kViewModeOptions`、`view_mode_name`、`parse_view_mode`、`show_view_mode_menu`(PD-059 剛新增,函式實際名稱以 PD-059 交接區記載為準)——本票要擴充的選單資料與邏輯。
- `src/explorer_host/explorer_host.h`/`.cpp` 的 `set_view_mode`/`get_view_mode`——需要擴充或新增方法以支援圖示尺寸。
- `src/core/model.h` 的 `TabState::view_mode`——需要能表達模式+尺寸組合的持久化欄位。
- `src/core/session.cpp` 的序列化/反序列化——確認新格式的加法式擴充與舊版檔案的向後相容讀取。
- `docs/tickets/PD-059-view-mode-dropdown-menu.md`——完整交接區,尤其是 ID 配置決策與 modal 重入驗證結果。
- `docs/tickets/PD-052-pane-refresh-and-view-mode-switcher.md`——`view_mode` 欄位的原始設計脈絡。
- Windows SDK 標頭(`shobjidl.h` 或等效)裡 `IFolderView2::SetViewModeAndIconSize`/`GetViewModeAndIconSize` 的宣告,確認 LLVM-MinGW 工具鏈下可用。

## Scope

1. 選單項目從 4 項擴充為 8 項,對齊使用者截圖的順序與命名。
2. 實作「超大/大/中/小圖示」四級圖示尺寸切換(透過 `IFolderView2::SetViewModeAndIconSize` 或查證後的替代方案)。
3. 新增「並排」(`FVM_TILE`)、「內容」(`FVM_CONTENT`)兩個模式。
4. 擴充 `TabState::view_mode` 的持久化格式以容納模式+尺寸組合,並確保舊版 session 檔案可正確遷移/映射。
5. 重新核對並視需要擴大選單 ID 區段。
6. 驗證(不預設需要新增程式碼)column header 僅在「詳細資料」顯示。

## Non-goals

- 不新增 8 種以外的檢視模式。
- 不改選單的彈出方式、錨定位置、radio check 繪製邏輯(PD-059 已定案)。
- 不改 View 按鈕本身的圖示或位置(PD-059 決策 7 沿用)。
- 不重寫 `ExplorerHost` 既有簽章以外的部分。
- 若 column header 驗證後發現不需要任何程式碼改動,不得為了「有做事」而畫蛇添足加自訂 header 繪製邏輯。

## Acceptance

1. 點擊任一 pane 的 View 按鈕,選單顯示 8 個項目,順序與文字為:Extra large icons / Large icons / Medium icons / Small icons / List / Details / Tiles / Content。
2. 選單中目前生效的模式(含圖示尺寸級距)有 radio 標記。
3. 選擇「超大/大/中/小圖示」任一項,Shell view 的圖示尺寸實際改變(截圖比對四種尺寸確實不同,不是選了但畫面沒變)。
4. 選擇「並排」「內容」,Shell view 實際切換成對應版面。
5. 切換 Group 再切回來(或重啟程式),模式與圖示尺寸正確還原,包含用舊版(僅 4 選項時代)`session.json` 開啟本版程式,不崩潰、能映射到合理的對應模式。
6. 只有「詳細資料」模式顯示欄位標題(名稱/日期/大小等),其餘 7 種模式截圖確認不顯示。
7. 選單 ID 區段重新核對不與既有常數衝突(見決策 4)。
8. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
9. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kViewModeOptions|SetViewModeAndIconSize|GetViewModeAndIconSize|FVM_TILE|FVM_CONTENT|view_mode_name|parse_view_mode" src\app_shell\main.cpp src\explorer_host\explorer_host.h src\explorer_host\explorer_host.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動或單次點擊+截圖:對每個 pane 開 View 選單,確認 8 項與 radio 標記;
# 逐一選取,PrintWindow 截圖比對圖示尺寸/版面實際改變;
# 確認僅「詳細資料」顯示 column header。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`。`TrackPopupMenu` 選單是獨立 top-level 視窗(class `#32768`),需另外截圖。

**驗證原則(2026-08-27 起本專案的共同約定):只做單次點擊/操作 + 截圖的驗證由 Agent 或本人執行;需要連續、多步驟操控滑鼠鍵盤的測試交給使用者本人執行**,避免電腦操作工具長時間佔用實體滑鼠鍵盤,影響使用者正常使用電腦。若某項驗收條件無法用單次動作完成,如實在交接區標記未驗證並說明需要使用者手動驗證的具體步驟,不要嘗試用自動化工具連續操控。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉。**

## Handoff requirements

- `IFolderView2::SetViewModeAndIconSize` 查證結果(是否可用、簽章、四個尺寸級距的實際數值依據)。
- 四級圖示尺寸若有任何一級無法真正實作,明確記錄哪一級、為什麼、退化行為是什麼。
- `TabState::view_mode` 最終的持久化形狀,以及舊版(4 選項)session.json 的映射/遷移規則。
- 選單 ID 區段最終配置與是否需要擴大。
- Column header 驗證結果:是否需要任何新程式碼,或純粹是既有 Shell view 行為已經正確。
- 逐項 acceptance 的驗證結果,明確標示哪些用單次動作完成、哪些交由使用者本人驗證。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 實作交接（2026-08-27）

- `IFolderView2` API 查證：目前工具鏈標頭 `E:\Dev\LLVM-MinGW\include\shobjidl.h` 的宣告是 `SetViewModeAndIconSize(FOLDERVIEWMODE uViewMode, int iImageSize)` 與 `GetViewModeAndIconSize(FOLDERVIEWMODE *puViewMode, int *piImageSize)`；本專案以 `ComPtr<IFolderView2>` 直接呼叫，沒有加入 include、依賴或 `core` 的 COM/HWND 型別。Windows SDK 與官方契約：[SetViewModeAndIconSize](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview2-setviewmodeandiconsize)（`-1` 使用該 view 的預設圖示尺寸）、[GetViewModeAndIconSize](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview2-getviewmodeandiconsize)（回傳目前 mode 與像素尺寸）。
- 四級圖示選項現在都使用 `FVM_ICON`，尺寸為 Windows File Explorer 的 96-DPI 四級值：`Extra large icons=256`、`Large icons=96`、`Medium icons=48`、`Small icons=16`；這些是傳給 `SetViewModeAndIconSize` 的 image pixels，不再把 `FVM_SMALLICON` 當成獨立的現代小圖示模式。`panedock_explorer_host_lifetime_check.exe` 在真實 Windows Shell view 的輸出為：`requested=256 actual=256 mode=1`、`96→96 mode=1`、`48→48 mode=1`、`16→16 mode=2`；16px 時 Shell 回報相容列舉 `FVM_SMALLICON`，但尺寸仍確實為 16。相同 self-check 亦確認 `List`/`Details`/`Tiles`/`Content` 分別回報 `FVM_LIST`/`FVM_DETAILS`/`FVM_TILE`/`FVM_CONTENT`。
- 選單資料已擴為 8 項且順序/英文文字為 `Extra large icons`、`Large icons`、`Medium icons`、`Small icons`、`List`、`Details`、`Tiles`、`Content`。popup ID 由每 pane 8 個擴為 `360–391`（`kViewModeMenuIdBase=360`、`kViewModeMenuIdCount=32`），已 grep 全部 ID 常數；`kLayoutButtonIdBase=400` 未衝突。
- 持久化沿用既有 `core::TabState::view_mode` 字串欄位，採加法式格式：圖示模式為 `FVM_ICON:<image_size>`，非圖示模式為 `FVM_LIST`/`FVM_DETAILS`/`FVM_TILE`/`FVM_CONTENT`。舊值映射為 `FVM_ICON→FVM_ICON:96`、`FVM_SMALLICON→FVM_ICON:16`、其餘既有兩值照原模式；因此舊版 session 可讀，且沒有破壞既有 schema 或 unknown-field preservation。core session self-check 已覆蓋 `FVM_ICON:48` round-trip 與舊 `FVM_SMALLICON` 字串可讀。
- `capture_pane_view_mode` 改讀 `GetViewModeAndIconSize`，將 Shell 回報的 mode/size 正規化後保存；套用與 radio check 都比較 mode+尺寸，`Tiles`/`Content` 走同一條 Shell view 路徑。Column header 沒有新增自訂繪製；PaneDock 承載原生 Shell view，預期僅 Details 由 Shell 顯示欄位標題。

#### Agent checks

```text
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
PASS — configure/generate completed; build rules invoke E:\Dev\LLVM-MinGW\bin\clang++.exe and Ninja.

cmake --build build
PASS — PaneDock.exe and all test targets linked with LLVM-MinGW.

ctest --test-dir build --output-on-failure
PASS — 4/4 tests passed.

rg -n "kViewModeOptions|SetViewModeAndIconSize|GetViewModeAndIconSize|FVM_TILE|FVM_CONTENT|view_mode_name|parse_view_mode" src\app_shell\main.cpp src\explorer_host\explorer_host.h src\explorer_host\explorer_host.cpp
PASS — all required symbols and mappings found.

git diff --check
PASS — no whitespace errors before this handoff append; rerun after append before commit.
```

#### Acceptance evidence

| # | 結果 | 證據 |
|---|---|---|
| 1 | 未驗證 | 需要單次點擊 View 按鈕後截取獨立 `#32768` popup；Computer Use native pipe 初始化、重試與 reset 均失敗，沒有送出點擊，也沒有用 `CopyFromScreen` 或其他 UI automation 替代。 |
| 2 | 未驗證 | 沒有取得 popup 截圖；程式碼以目前 tab 的 mode+image size 對 8 項執行 `MF_CHECKED`/`CheckMenuRadioItem`。 |
| 3 | 未驗證 | 逐一選取四級圖示並做畫面比對屬多步驟 UI；未自動執行。Shell API self-check 已實測 256/96/48/16 全部 round-trip，但這不是畫面 acceptance 的替代證據。 |
| 4 | 未驗證 | `ExplorerHost` self-check 已實測四個 FOLDERVIEWMODE setter/getter 回報正確；未做 Tiles/Content 畫面截圖。 |
| 5 | 未驗證 | 需要切換 Group 或重啟並觀察還原，屬多步驟互動；core 字串 round-trip 與程式碼路徑已檢查，但沒有真實桌面還原證據。 |
| 6 | 未驗證 | 需要逐一切換並截圖 8 種模式的 column header；依協作政策未做連續 UI 操作，亦沒有把原生 Shell 行為推定為實機 PASS。 |
| 7 | PASS | `rg`/原始碼核對證明 popup IDs 為 `360–391`，與 `kLayoutButtonIdBase=400` 及其他既有 ID 區段不衝突。 |
| 8 | PASS | LLVM-MinGW/Ninja build 成功；CTest 顯示 4/4 通過；額外 `panedock_explorer_host_lifetime_check.exe` 成功。 |
| 9 | PASS | `git diff --check` 在 handoff 寫入前通過；會在 commit 前對最終 diff 再跑一次。 |
| 10 | 未驗證 | 使用者額外要求的 `C:\Windows` Ctrl+A responsiveness 與 idle 0% CPU/無 disk I/O 需要互動/長時間量測；依政策未執行，沒有宣稱 PASS。 |

因 acceptance 1–6、10 沒有政策允許的真實畫面/量測證據，`docs/tickets.md` 的 PD-079 狀態維持 `ready`。需要人類在解鎖桌面後手動確認 popup、四級尺寸、Tiles/Content、Group/restart 還原、各模式 column header，以及 `C:\Windows` Ctrl+A responsiveness；idle NFR 則需依 `docs/testing.md` 的量測流程執行。
