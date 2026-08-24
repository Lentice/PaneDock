# PD-017 — Group 側邊欄:建立、重新命名、複製、刪除、重新排序、切換

Phase 2 · sidebar · Depends on: PD-015

- Source: `AGENTS.md`、`docs/design-spec.md` §4.1／§4.2／§9.1／FR-001／FR-002、`docs/development.md`、`CONTEXT.md`
- Origin: 2026-08-24,`docs/roadmap.md` Phase 2 清單的「Group sidebar with create / rename / duplicate / delete / reorder」。`docs/roadmap.md` 明訂 Phase 2 完成的定義是「a user can create Groups, switch between them, and see the correct pane arrangement restored each time」——這張票是唯一真正讓「建立第二個 Group」變成可能的 ticket,PD-015 只接了單一 Group 的管線。
- Priority: HIGH——沒有它,Phase 2 的完成定義就達不到,PaneDock 仍然只是一個能記住自己狀態的四宮格檔案總管,不是「Group」產品。

## Goal

在主視窗左側加入固定寬度的 Group 側邊欄:列出全部 Group、單擊切換、新增、重新命名、複製、刪除(二次確認)、拖曳或按鈕重新排序。全部操作透過 PD-004 已經寫好且測過的 `core::add_group`／`rename_group`／`duplicate_group`／`delete_group`／`reorder_group`,側邊欄本身不持有 Group 資料的權威狀態。

## 已確認的產品決策

1. **側邊欄是一個原生 Win32 `ListBox`(owner-draw),不是自訂繪製的完整控制項。** `docs/design-spec.md` §9.1 把 `sidebar` 列為獨立模組,只負責「Group 列表繪製與切換」;`docs/development.md` 同一張表加了一句「Group list rendering, selection input」。owner-draw `ListBox` 是能在最小程式碼下达到「可捲動清單、單擊選取」需求的原生控制項,不需要自己處理捲軸、鍵盤上下鍵導覽這些 `ListBox` 內建就有的行為。若之後要做圖示、顏色(見 `docs/tickets.md` §候選「Group 圖示與顏色」,目前觸發條件未到)才需要換成更重的自訂控制項,不在這裡預先做。
2. **側邊欄寬度是寫死的常數,不是使用者可拖曳調整、也不持久化。** `docs/design-spec.md` §4.1:「兩者之間有可拖曳的分隔線,寬度屬於全域設定而非個別 Group 的狀態」——這句話字面上要求「可拖曳」,但 `docs/tickets.md` §候選已有一條「側邊欄寬度的全域設定持久化」,觸發條件寫明「若使用者回報每次啟動都要重拖再開;目前預設值可接受」,代表這個決策已經被有意延後過一次。本 ticket **維持這個既有的延後決策**,不重新開放它:側邊欄寬度用一個具名常數(例如 200 像素,依 DPI 縮放),側邊欄與 pane 區域之間**沒有**可拖曳分隔線。如果要重開,需要先有「使用者實際回報」這個新證據,依 `docs/tickets.md` 的規則不能在這裡自己順手加上。
3. **新增 Group 的預設狀態複製自目前 active Group 的版型,但 location 全部重設為預設路徑。** Spec 沒有明講「新增」要從哪裡起始;選「複製目前版型」比「永遠是 single」更符合「使用者剛才在忙什麼版型,大概還想繼續用」的直覺,且不需要新的預設值表——直接呼叫 `core::switch_layout` 相同的預設 location 產生邏輯(PD-015/PD-016 已經在用)。
4. **刪除的二次確認用原生 `MessageBoxW`(`MB_YESNO | MB_ICONWARNING`)**,不做自訂對話框。FR-001 只要求「需要二次確認」,沒有規定樣式;`MessageBoxW` 是達到這個需求最小的做法。
5. **重新排序用兩個方向鍵按鈕(「上移」／「下移」),不做拖放排序。** 側邊欄清單项目通常不多(使用者不會有幾百個 Group),按鈕排序比實作 `ListBox` 的拖放排序(需要自己處理 `WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/插入指示線繪製)簡單得多,且不影響 `core::reorder_group` 的呼叫方式——之後如果要換成拖放,只是側邊欄內部的輸入處理改變,`core` 呼叫不用動。
6. **複製(Duplicate)產生的新 Group 名稱是「原名稱 + `" copy"`」**(例如 `Group 1` → `Group 1 copy`),不彈輸入框讓使用者立刻改名——複製之後使用者原本就可以用「重新命名」再改,不需要為了這一步再多一個對話框。
7. **切換 Group 時,PD-015 已建立的 `save_now` 在切換完成後呼叫一次**,把新的 `active_group_id` 存檔;新增/重新命名/複製/刪除/重新排序也各自在成功後呼叫 `save_now`,沿用 PD-015 決策 4 訂下的「mutation 後立即存檔」慣例。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.1:
> 單一頂層視窗。左側為固定寬度的 Group 側邊欄,右側為 pane 區域。兩者之間有可拖曳的分隔線,寬度屬於全域設定而非個別 Group 的狀態。

`docs/design-spec.md` §4.2:
> 側邊欄單擊即切換。切換必須感覺即時——實作上以保活既有 view 並重新導覽達成,不重建 HWND。

`docs/design-spec.md` FR-001:
> 使用者可新增、重新命名、複製、刪除、重新排序 Group。刪除需二次確認。複製會複製完整狀態但產生新的 identity。

`docs/design-spec.md` FR-002:
> 選取 Group 時還原:版型、分隔比例、每個 pane 的 tab 集合、每個 tab 的 Shell location、view mode、排序欄位與方向、active pane、每個 pane 的 active tab。

`docs/design-spec.md` §9.1 模組責任表:
> `sidebar` 負責「Group 列表繪製與切換」,不得負責「Group 資料的權威狀態」。

`AGENTS.md`:
> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups — that is both the crash surface and a CPU/disk spike from simultaneous folder enumerations.

`CONTEXT.md`:
> **sidebar**: The persistent left-hand region listing Groups. It is always visible and is not part of any Group's state.
> _Avoid_: navigation pane, tree, panel

`docs/development.md`:
> No Chinese text in the binary... Examples of the expected register: `New Group`, `Duplicate Group`, `Delete Group`.

`docs/tickets.md` §已否決的方向 / §候選(側邊欄寬度持久化):
> 若使用者回報每次啟動都要重拖再開;目前預設值可接受。

## Files to read and trace first

- `docs/tickets/PD-015-app-shell-core-state-wiring.md` 的交接區——`AppState` 的最終形狀、`save_now` 的簽章、目前 pane 區域佔用主視窗 client area 的方式(側邊欄要從這個 client area 切一塊出來,pane 區域的可用寬度會變小)。
- `docs/tickets/PD-016-splitters-five-layouts-and-dpi-scaling.md` 的交接區(若已完成)——DPI 縮放輔助函式是否可以直接複用給側邊欄寬度換算。
- `src/core/model.h`/`model.cpp`——`add_group`、`rename_group`、`duplicate_group`、`delete_group`、`reorder_group`、`ApplicationState.groups`、`ApplicationState.active_group_id` 的確切簽章與回傳語意(全部回傳 `bool`,失敗時原狀態不變)。
- `src/app_shell/main.cpp`(PD-015/PD-016 改寫後)——`WM_CREATE`/`WM_SIZE`/`WM_DPICHANGED` 現有的 client area 配置邏輯,新增側邊欄後這裡要改成「先切一塊給側邊欄,剩下的給 `compute_layout_rects`」。
- Win32 `ListBox` owner-draw 參考:`LBS_OWNERDRAWFIXED`、`WM_DRAWITEM`、`WM_MEASUREITEM`、`LB_ADDSTRING`/`LB_DELETESTRING`/`LB_GETCURSEL`/`LB_SETCURSEL`。

## Scope

1. 新增 `src/sidebar/`(對照 §9.1,啟用 `CMakeLists.txt` 內既有註解掉的 `add_library(panedock_sidebar STATIC src/sidebar/...)`)。內容:一個包住 `ListBox` HWND 的薄封裝型別,提供:建立(`CreateWindowExW` with `WC_LISTBOXW`, `LBS_OWNERDRAWFIXED | LBS_NOTIFY | WS_VSCROLL`)、`set_groups(const std::vector<GroupSummary>&)`(`GroupSummary` 只含 `id` 與顯示用的 `name`,不含完整 `GroupState`——sidebar 不持有權威狀態)、`selected_index()`、`set_selected_index(std::size_t)`、owner-draw 的 `WM_DRAWITEM` 處理(畫出目前選取列的高亮)。
2. 側邊欄下方或上方放四個原生按鈕(`New`、`Duplicate`、`Rename`、`Delete`)與兩個排序按鈕(`Up`、`Down`)——用 `CreateWindowExW` with `WC_BUTTONW`,英文文字比照 `docs/development.md` 的既有範例(`New Group`、`Duplicate Group`、`Delete Group`;`Rename Group`、`Move Up`、`Move Down` 依同樣風格命名)。
3. 主視窗版面配置:`WM_SIZE`/`WM_DPICHANGED` 先切出左側固定寬度(DPI 縮放後)給側邊欄與按鈕列,剩餘 client area 才交給 `compute_layout_rects`。
4. 側邊欄清單的資料來源是 `ApplicationState.groups`(依現有陣列順序顯示,不重新排序)。每次任何 Group mutation 成功後,重新呼叫 `set_groups` 並把 `set_selected_index` 對齊 `active_group_id`。
5. 按鈕行為:
   - `New`:呼叫 `core::add_group`,新 Group 內容依已確認的產品決策 3 產生,新 id 用簡單的遞增字串(例如 `"group-" + 目前最大數字後綴 + 1"`,若解析失敗退回時間戳字串——不需要複雜的 UUID)。
   - `Duplicate`:對目前選取的 Group 呼叫 `core::duplicate_group`,新名稱依決策 6。
   - `Rename`:彈一個簡單的原生輸入對話框(可用 `DialogBoxParamW` 搭配一個最小的 `.rc` 對話框資源,或用 `EDIT` 控制項疊加 `ListBox` 就地編輯——實作時擇一,擇一原則是「用最少新程式碼達到能輸入一行文字並確認/取消」)呼叫 `core::rename_group`。
   - `Delete`:`MessageBoxW` 二次確認後呼叫 `core::delete_group`;若刪除後 `groups` 清空,套用 PD-004 已定義的「空狀態」語意(`active_group_id` 為空字串),此時 pane 區域顯示什麼由 Scope 6 決定。
   - `Up`/`Down`:對目前選取的 Group 呼叫 `core::reorder_group`,索引 ±1(邊界時按鈕應該被禁用或呼叫後不動作,擇一)。
6. **空狀態(刪光所有 Group)的 UI 行為**:pane 區域顯示一個簡單的原生訊息(例如置中的 `STATIC` 文字「No Group. Click New Group to get started.」),全部 `ExplorerHost` 保持 `set_visible(false)` 且不 `initialize`。這是本 ticket 需要新增的顯示狀態,PD-004 的 `ApplicationState`(空 `groups` + 空 `active_group_id`)已經是合法狀態,只是 app_shell 之前從未處理過它(PD-015 假設恰好一個 Group)。
7. 側邊欄單擊選取一列時:若選到的 id 不是目前 `active_group_id`,呼叫 PD-015/PD-016 已有的版型套用邏輯(保活既有 `ExplorerHost`、重新導覽到新 Group 的每個 pane location,不 destroy/recreate),對照 §4.2 的「感覺即時」與 `AGENTS.md` 的保活式切換規則。

## Non-goals

- 不做側邊欄寬度的拖曳調整或持久化(見已確認的產品決策 2)。
- 不做拖放排序(見已確認的產品決策 5)。
- 不做 Group 圖示或顏色(`docs/tickets.md` §候選,觸發條件未到)。
- 不做 tab(Phase 3)。
- 不修改 `core::model.h` 的既有 Group mutation 函式簽章;若發現不夠用,在交接區記錄,不要為了這個 ticket 順手改 `core`。
- 不做鍵盤全域熱鍵切換 Group(側邊欄本身的 `ListBox` 已有內建的上下鍵/首字母跳轉導覽,足夠)。

## Acceptance

1. 側邊欄顯示全部 Group 的名稱,單擊任一列即切換到該 Group,pane 區域正確還原該 Group 的版型、比例與四個(或對應數量)location,且沒有任何 `ExplorerHost` 被 destroy 又重建(可用交接區記錄的程式碼路徑論證,或在偵錯建置下用中斷點/log 確認)。
2. `New Group` 建立一個新 Group 並立即顯示在清單裡,側邊欄選取狀態跟著移到新 Group。
3. `Duplicate Group` 複製目前選取 Group 的完整狀態(版型、比例、location),新 Group 名稱依決策 6。
4. `Rename Group` 可以修改名稱並反映在清單上。
5. `Delete Group` 需要二次確認,確認後該 Group 從清單消失;刪到剩最後一個時仍可正常刪除並進入 Scope 6 的空狀態,不當機。
6. `Move Up`/`Move Down` 正確改變清單順序,重開程式後順序保持(因為 `reorder_group` 之後有 `save_now`)。
7. 任一 Group mutation 之後,`%LOCALAPPDATA%\PaneDock\session.json` 立即反映新狀態(手動在操作後直接讀檔確認,不需要關閉程式)。
8. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過(新增的 `sidebar` 程式庫本身不含自動測試,因為它是純 Win32 UI,交接區需說明用什麼人工檢查取代,同 `docs/development.md` 第 4 條的既有規則)。
9. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "windows\.h|HWND|ComPtr" src\core
# 預期:無命中——sidebar 的 UI 邏輯不得洩漏進 core
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:New/Duplicate/Rename/Delete/Move Up/Move Down 各操作一次,單擊切換至少兩個 Group,
# 刪到只剩一個再刪光確認空狀態,關閉重開確認全部 Group、順序、active Group 精確還原
Get-Content "$env:LOCALAPPDATA\PaneDock\session.json"
```

## Handoff requirements

- `sidebar` 模組的最終公開介面(型別與函式簽章),供之後如果要加圖示/顏色的 ticket 參考擴充點。
- 側邊欄寬度的具名常數值與其 DPI 縮放方式。
- Rename 對話框的最終實作方式(`DialogBoxParamW` 對話框資源,或就地 `EDIT` 疊加)與其取捨理由。
- 新 Group id 的產生方式,以及是否有跟 PD-015/PD-016 產生的 pane/tab id 衝突的可能性(理論上命名空間不同,但要在這裡明確排除)。
- 空狀態(零個 Group)的實際 UI 呈現與程式碼路徑,供之後任何要美化這個狀態的 ticket 參考。
- 若在真實桌面上發現 owner-draw `ListBox` 在高 DPI 下有已知的原生渲染問題(常見於某些 Windows 版本的 owner-draw 列高計算),記錄下來。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-24 實作交接

#### 完成內容

- 新增 `panedock_sidebar` 靜態程式庫與 `src/sidebar/sidebar.h`／`sidebar.cpp`。它以原生 `LISTBOX`（`LBS_HASSTRINGS | LBS_OWNERDRAWFIXED | LBS_NOTIFY | WS_VSCROLL`）承載 Group 清單，由 `WM_MEASUREITEM`／`WM_DRAWITEM` 繪製系統色選取高亮與 focus rectangle；資料只是一份由 `ApplicationState.groups` 投影出的 `GroupSummary`，權威狀態仍在 core。
- `AppState` 新增 `Sidebar`、六個 button HWND 與空狀態 STATIC HWND；`WM_CREATE` 建立英文 UI `New Group`、`Duplicate Group`、`Rename Group`、`Delete Group`、`Move Up`、`Move Down`，`WM_COMMAND` 分別接到既有 `core::add_group`／`duplicate_group`／`rename_group`／`delete_group`／`reorder_group`。每個成功 mutation 都會 `refresh_sidebar` 並在該次操作完成後呼叫 `save_now(AppState&) noexcept`。
- Up／Down 在邊界禁用；其餘非 New 按鈕在無 Group 時禁用。Delete 使用 `MessageBoxW(..., MB_YESNO | MB_ICONWARNING)` 二次確認。Duplicate 名稱固定為來源名稱加 `L" copy"`。
- Group 選取切換會先 `capture_locations` 保存舊 active Group，改寫 `active_group_id`，然後讓新 Group 的每個可見且已 realized host 呼叫新增的 `HRESULT ExplorerHost::navigate(std::wstring_view location)`；尚未 realized 的可見 host 才交給 `apply_layout` 初始化，超出新 layout pane 數的 host 只 `set_visible(false)`。切換路徑不呼叫 `destroy()`，因此保留 PD-015 的 live-host 模式；`navigate` 也會在非同步 Shell 導覽前先記住要求的 parsing name，緊接著的 `save_now` 不會把舊 Group location 寫回新 Group。
- pane rect、splitter rect、pane hit-test 共用扣除 sidebar 後的 `pane_area`。垂直 splitter drag 的 numerator 改為 `point.x - pane_area.left`，denominator 使用 pane area width 減 divider；水平 drag 同理使用 pane area 的 y/height。因此左側新增偏移不會扭曲既有比例計算，且 sidebar 點擊不會命中 pane/splitter。

#### `sidebar` 最終公開介面

```cpp
inline constexpr int kSidebarWidth = 200;
inline constexpr UINT kRenameCommitMessage = WM_APP + 1;

struct GroupSummary final {
    std::string id;
    std::wstring name;
};

class Sidebar final {
public:
    bool create(HWND parent, int control_id) noexcept;
    void set_rect(const RECT& rect, UINT dpi) noexcept;
    void set_groups(const std::vector<GroupSummary>& groups);
    std::optional<std::size_t> selected_index() const noexcept;
    void set_selected_index(std::size_t index) noexcept;
    bool measure_item(MEASUREITEMSTRUCT* item, UINT dpi) const noexcept;
    bool draw_item(const DRAWITEMSTRUCT* item) const noexcept;
    bool begin_rename();
    std::optional<std::wstring> take_rename_text() noexcept;
    HWND window() const noexcept;
};
```

`kSidebarWidth` 是 96-DPI logical pixels；app shell 用 `MulDiv(kSidebarWidth, GetDpiForWindow(window), 96)` 得到實際寬度。8 px margin、4 px gap、28 px button/list-row 高度與 owner-draw 文字 inset 也依同一 DPI 比例換算。此寬度沒有 splitter，也不持久化。

Rename 採用在選取 ListBox item 上疊加單行原生 `EDIT` 的方式：Enter commit、Escape 或失焦 cancel。這比新增 `.rc` 與 `DialogBoxParamW` 資源更少程式與建置面積，且仍提供明確確認／取消；commit 透過 `kRenameCommitMessage` 回到 app shell，sidebar 本身不修改 core 狀態。

#### identity 與空狀態

- 新 Group id 掃描所有 `group-<純數字>` id 的最大後綴並加一；遇到 `group-` 命名空間內無法解析的後綴時，退回 system-clock milliseconds，若同一毫秒仍碰撞再追加 discriminator。既有 `default` 不屬於數字 Group id 且不妨礙遞增。Group id、`pane-*`、`tab-*` 分別用在不同欄位／命名空間；Group 產生器不拿新 Group id 當 pane/tab id，因此不會與 PD-015/PD-016 的 pane/tab identity 衝突。
- New 在有 active Group 時只沿用其 `LayoutTemplate`，divider 使用該 template 的預設比例，所有可見 pane location 依既有表重設為 `C:\`、`C:\Windows`、`C:\Users`、`C:\Program Files`；從零 Group 建立時使用既有四 pane default。Duplicate 則由 core 複製來源完整狀態。
- 刪除最後一個 Group 後，core 留下合法的空 `groups`／空 `active_group_id`。`apply_layout` 先配置永遠可見的 sidebar，再把所有 ExplorerHost `set_visible(false)`（不 initialize／destroy），並顯示 pane area 中央的原生 STATIC：`No Group. Click New Group to get started.`。message loop、F6、hotkey、pane/splitter hit-test 與 session capture 均有空狀態 guard；從空狀態按 New 會重新 realize 新 Group 可見的 panes。

#### 驗證結果與限制

- Release configure 成功：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`。
- 完整建置成功：`cmake --build build`。
- `ctest --test-dir build --output-on-failure`：3/3 通過（`panedock_core_model`、`panedock_core_layout`、`panedock_core_session`）。
- `rg -n "windows\.h|HWND|ComPtr" src\core`：無命中，sidebar/Win32 未洩漏進 core。
- `rg -n "LayoutState|PrototypeLocationState|quadrant_layout|prototype_location_persistence" src tests CMakeLists.txt`：無命中，舊 prototype 邊界仍保持清除。
- `git diff --check`：通過。
- 靜態 code-path 檢查涵蓋：每個按鈕的 control id → `WM_COMMAND` → 對應 core mutation → `refresh_sidebar`／`save_now`；ListBox `LBN_SELCHANGE` → 舊 Group capture → realized-visible-only `navigate` → `apply_layout` realize/hide → active pane/focus → save；Delete-to-zero 與 New-from-zero 不會呼叫 `active_group` 的 assert 路徑；sidebar offset 同時用於 layout、splitter hit-test 與 drag math。
- 目前執行環境沒有互動式 Windows desktop，因此無法誠實驗證實際 button click、ListBox 單擊／鍵盤選取、inline EDIT 輸入與 Enter/Escape、MessageBox 確認、owner-draw 實際視覺、高 DPI monitor 切換、真實 Shell location 切換，亦無法在操作後人工讀取 `%LOCALAPPDATA%\PaneDock\session.json` 或 close/reopen 核對 round-trip。這些 Acceptance 1–7 的人工項目仍需在真實桌面依 Agent checks 執行。也因未能視覺檢查，目前沒有可回報的 Windows 版本特定 owner-draw 高 DPI 渲染問題；28 logical-pixel row height 同時由 `WM_MEASUREITEM` 與 `LB_SETITEMHEIGHT` DPI 縮放，僅能由程式路徑確認。

### 2026-08-24 驗證

獨立重跑 build 與完整 `ctest`(3/3 通過)、兩個邊界 `rg`、`git diff --check`,結果一致。`git status` 確認變動範圍為 `CMakeLists.txt`、`src/app_shell/main.cpp`、`src/explorer_host/explorer_host.{h,cpp}`、新增的 `src/sidebar/`,與 PD-017 的 scope 一致。逐行讀過 `sidebar.h`／`sidebar.cpp`(owner-draw ListBox、inline EDIT rename、`WM_KILLFOCUS`/Escape 取消、Enter 提交)與 `main.cpp` 全文(`activate_group`、`add_group`、`duplicate_group`、`delete_group`、`move_group`、空狀態的 `has_active_group` 守衛)：邏輯正確,`activate_group` 對每個仍在新 Group pane 數量範圍內、且已 realize 的 host 呼叫新增的 `ExplorerHost::navigate`,確保切換 Group 後畫面與 model 不會分岔(這比 PD-016 的版型內切換更完整,因為它明確重新導覽而非仰賴 host 保留舊內容)。`ExplorerHost::navigate` 的新增是 PD-015 交接區已預先授權的擴充,不算範圍外變更。

真實桌面上做了不涉及鍵盤/滑鼠點擊的最小驗證:啟動最新 build,四個 pane 正常渲染、`Responding=True`,正常關閉無當機,關閉後 `session.json` 內容不受側邊欄程式碼新增影響(`active_group_id=default`、`groups[0].name=Group 1`,與加入側邊欄前一致)。實際點擊 New/Duplicate/Rename/Delete/Move Up/Move Down、side bar 選取切換 Group、owner-draw 視覺與高 DPI 下的呈現,依目前政策不使用鍵盤/滑鼠自動化驗證,留待你方便時手動確認。程式碼層面與可自動化的部分已核實無誤,判定為完成。
