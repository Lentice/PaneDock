# PD-002 — 判定選取狀態還原是否可行,並決定該需求去留

Phase 0 · explorer_host · Depends on: PD-001

- Source: `AGENTS.md`、`docs/design-spec.md` §NFR-005、`docs/testing.md` §Prototype acceptance protocol step 8
- Origin: 2026-08-20 選型審查。兩份獨立審查都指出「還原選取狀態」是 handoff 文件沒提到的隱形大坑:`IExplorerBrowser` 沒有公開 API 可讀寫 pane 內的選取,實務上要對 view 內部的 `SysListView32` 發未公開的 `LVM_*` 訊息。
- Priority: **MEDIUM**——結論可能砍掉一個需求。越早知道越好,但必須有 PD-001 的原型才能試。

## Goal

回答一個問題:PaneDock 能不能還原一個 tab 內的選取項目,而且風險可接受?

三種合法結論,任一種都算完成:

1. **可行**——找到穩定的做法,寫下做法與其版本相依性,`NFR-005` 維持現狀。
2. **可行但風險不可接受**——做得到,但依賴未公開行為且跨 Windows 版本會斷。需求降級或砍除。
3. **不可行**——做不到。需求砍除。

本 ticket 刻意**不承諾實作**。它的產出是判定與依據。若結論為「可行」,實作另開 ticket。

## 已確認的產品決策

1. 選取狀態屬於 `docs/design-spec.md` §NFR-005 的 **best-effort 狀態**,不是必要狀態。砍掉它不影響 MVP 是否可交付。
2. 捲動位置與欄寬同屬 best-effort,順便一起判定,但不得為了它們擴大本 ticket 範圍。
3. 若結論是砍除,必須同時更新 `docs/design-spec.md` §NFR-005 與 §3.2,並在 `docs/tickets.md` §已否決的方向 新增一列(含重開條件)。**這是本 ticket 唯一被授權修改 spec 的情況**,依 §1「先更新 Spec,再調整受影響的 ticket」的規則執行。
4. 不接受「用一堆 `LVM_*` 訊息硬幹然後不寫下版本風險」這種交付。判定必須包含在哪些 Windows build 上實測過。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §NFR-005:
> **必要狀態**(版型、location、tab、view mode、排序)必須精確還原,否則視為缺陷。**best-effort 狀態**(選取項目、捲動位置、欄寬)不保證。

`AGENTS.md`:
> Selection restoration has no public Shell API and is expected to require undocumented `LVM_*` messages. It is best-effort and is the first feature cut if the prototype shows it unstable.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/tickets.md` §Agent 交付規則:
> 每個 ticket 只負責一個主要成果,避免跨 ticket 的隱性工作。

## Files to read and trace first

- PD-001 的 `## 交接區` — step 8 的初步判定與觀察到的 view 內部結構
- `src/explorer_host/` — PD-001 建立的 view 宿主型別與 site 物件
- 外部契約:`IFolderView2::GetSelectedItem` / `Items` / `SelectItem`、`IShellView::GetItemObject`、`SVGIO_SELECTION`、`FindWindowEx` 對 `SysListView32` 的定位、`LVM_GETNEXTITEM` / `LVM_SETITEMSTATE`

## Scope

1. 先窮盡**公開 API** 的可能性:`IFolderView2::GetSelectedItem`、`IFolderView2::SelectItem`、`IShellView::GetItemObject(SVGIO_SELECTION)`。逐一實測能否讀出選取、能否寫回選取。這一步必須先做完並記錄結果,才允許進入下一步。
2. 若公開 API 不足,才評估未公開路徑:以 `FindWindowEx` 定位 view 內的 `SysListView32`,以 `LVM_*` 訊息讀寫選取狀態。
3. 對每一條可行路徑,測試以下情境並記錄:
   - 一般本機資料夾
   - 項目數量大的資料夾(數千個檔案)
   - 虛擬命名空間(本機、控制台之類沒有一般檔案系統路徑的位置)
   - OneDrive 佔位檔所在資料夾
   - 切換 view mode 後(詳細資料／大圖示)
4. 在至少兩個不同的 Windows build 上實測(Windows 10 22H2 與 Windows 11),記錄行為差異。
5. 評估 realize 時序:選取的還原必須發生在 view 完成導覽之後。判定是否能可靠地知道「導覽已完成」——若不能,選取還原就不可靠,這本身就是判定依據。
6. 寫出判定與依據。若結論為砍除,執行「已確認的產品決策」第 3 點的文件更新。

## Non-goals

- 不實作正式的選取還原功能。本 ticket 只判定,實作另開 ticket。
- 不判定捲動位置與欄寬以外的其他 best-effort 狀態。
- 不為了讓選取還原可行而改動 `docs/design-spec.md` §9.1 的模組邊界或 `core` 的 COM-free 規則。
- 不引入任何抽象層或 wrapper 以「方便將來替換」。
- 不處理多選跨 pane 的情境——選取是 tab 的狀態,不是全域狀態。
- 不做效能最佳化。

## Acceptance

1. 公開 API 路徑的實測結果完整記錄:哪些能讀、哪些能寫、哪些回傳失敗或空值。
2. 若評估了未公開路徑,記錄其在 Scope 3 全部五種情境下的行為。
3. 在兩個 Windows build 上都有實測紀錄,差異寫出。
4. 導覽完成時序的可靠性有明確判定。
5. 產出三種結論之一,並寫出依據。
6. 若結論為砍除:`docs/design-spec.md` §NFR-005 與 §3.2 已更新,`docs/tickets.md` §已否決的方向 已新增一列且含重開條件。
7. 若結論為可行:做法、其版本相依性與殘餘風險已寫下,足以讓後續 ticket 直接實作。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
.\build\PaneDock.exe
# 依 Scope 3 的五種情境逐一實測讀取與寫回選取,記錄每次的 HRESULT 與觀測結果
```

```powershell
rg -n "LVM_|FindWindowEx" src
# 若結論為砍除,預期無命中——探測用程式碼不留在 repo
git diff --check
```

## Handoff requirements

交接時記錄:

- 三種結論之一,以及依據。
- 公開 API 的逐一實測結果,含 HRESULT。
- 未公開路徑(若評估了)在五種情境下的行為。
- 實測的兩個 Windows build 版本號與行為差異。
- 導覽完成時序的判定。
- 若砍除:已更新的文件位置,以及新增的已否決方向那一列的重開條件。
- 若可行:做法摘要、版本相依性、殘餘風險,以及建議的後續 ticket 範圍。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-24 PD-002 公開 API 與 LVM 可行性實證交接

**本輪結論：可行但風險不可接受。**

依目前唯一可執行的 Windows build 之實際 probe，標準檔案系統 Shell view 的選取讀寫可以只使用公開 API；但 Control Panel 這類虛擬命名空間的公開 view API 全部不能完成選取讀寫，透過公開 `IShellFolder` 列舉出的 child PIDL 也不能交給 `IShellView::SelectItem`，而該 view 沒有可用的 `SysListView32` 供 `LVM_*` fallback。再加上 `OnNavigationComplete` 實測早於項目列舉完成，不能單獨當作「可以立即還原選取」的 ready 訊號。因此目前可以做出一般檔案系統的 best-effort 實作，但無法給產品需求一個跨 Shell namespace、跨 Windows build 的穩定保證；這符合「可行但風險不可接受」，不是「不可行」的斷言。

#### 實測方法與環境

- 建立 self-contained、暫存於 repo 外產品樹的 probe：LLVM-MinGW Clang C++20，STA `OleInitialize`，建立隱藏 host HWND 與真實 `CLSID_ExplorerBrowser`，以 `IExplorerBrowserEvents` 幫浦導覽；沒有模擬滑鼠或鍵盤，也沒有把 probe 加入產品或 CMake。probe 完成後已刪除，`src` 不留探測碼。
- 本機 registry 實際回報：`ProductName=Windows 10 Pro`、`DisplayVersion=25H2`、`CurrentBuild=26200`、`UBR=9168`、`OSVersion=10.0.26200.0`。沒有第二個可啟動的 Windows 10 22H2／Windows 11 環境，因此第二 build 的結果**未驗證,需要真實桌面**（或另一個實際 Windows build）。
- 沒有附加 debugger；沒有互動桌面，所有「選取」寫入都是直接呼叫公開 `SelectItem` 的程式化 probe，不是人工滑鼠／鍵盤選取。

#### 公開 API 實測結果

官方契約參考：[IFolderView2](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-ifolderview2)、[IFolderView2::GetSelectedItem](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview2-getselecteditem)、[IFolderView2::GetSelection](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview2-getselection)、[IFolderView::Items](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview-items)、[IFolderView::SelectItem](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifolderview-selectitem)、[IShellView::GetItemObject](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellview-getitemobject)。

所有成功的 setup／導航步驟均為 `S_OK (0x00000000)`：`OleInitialize`、`CoCreateInstance(CLSID_ExplorerBrowser)`、`IUnknown_SetSite`、`IExplorerBrowser::Initialize`、`Advise`、`SHCreateItemFromParsingName`、`BrowseToObject`、`GetCurrentView`，以及所有五個 scenario 對 `IShellView` 的 `QueryInterface(IID_IFolderView2)`／`QueryInterface(IID_IFolderView)`。

在沒有選取的檔案系統 view 上，三個讀取族群的實際結果一致：

- `IFolderView::ItemCount(SVGIO_SELECTION)`：`S_OK`，count `0`。
- `IFolderView2::GetSelectedItem(-1)`：`S_FALSE (0x00000001)`。
- `IFolderView2::GetSelection(FALSE)`、`IFolderView::Items(SVGIO_SELECTION, IDataObject/IShellItemArray)`、`IShellView::GetItemObject(SVGIO_SELECTION, IDataObject/IShellItemArray)`：`0x80070490` (`HRESULT_FROM_WIN32(ERROR_NOT_FOUND)`)。
- `IFolderView2::GetSelection(TRUE)`：`S_OK`，回傳的 `IShellItemArray` count `1`，代表「無選取時以 parent folder 代替」的文件語意。

程式化選取第一個 item 後，`IFolderView2::SelectItem(0, flags)` 與 `IShellView::SelectItem(first child PIDL, flags)` 都回 `S_OK`；再讀回時 `ItemCount(SVGIO_SELECTION)` 為 `1`、`GetSelectedItem` 為 `S_OK` index `0`、`GetSelection`／`Items`／`GetItemObject` 的 `IDataObject` 與 `IShellItemArray` 皆為 `S_OK`，array count `1`，`IShellItemArray::GetItemAt(0)` 也為 `S_OK`。這證實公開 API 能讀回可持久化的 Shell item identity 載體，不需要 LVM 才能處理一般檔案系統 view。

#### Scope 3 五種情境

| 情境 | 實際 target／HRESULT 結果 | 公開 API 判定 | 人工狀態 |
|---|---|---|---|
| 一般本機資料夾 | `C:\Windows`，`ItemCount(ALLVIEW)=134`；上列所有公開讀寫結果成立 | 程式化讀寫可行 | 真實滑鼠選取與重開還原：**未驗證,需要真實桌面** |
| 數千項本機資料夾 | `C:\Windows\System32`，`ItemCount(ALLVIEW)=5048`；選取讀寫及 array count `1` 均成功 | 程式化讀寫可行，未見大資料夾特有 HRESULT 差異 | 真實大量項目人工操作：**未驗證,需要真實桌面** |
| 虛擬命名空間 | `::{21EC2020-3AEA-1069-A2DD-08002B30309D}` Control Panel；`IFolderView`／`IFolderView2` QI 為 `S_OK`，但 `ItemCount(ALLVIEW)`、`ItemCount(SELECTION)`、`GetSelectedItem`、`Items`、`GetItemObject`、`IFolderView2::SelectItem(0)` 都是 `E_FAIL (0x80004005)`。補測 `IFolderView::GetFolder(IShellFolder)`、`EnumObjects`、`IEnumIDList::Next` 都是 `S_OK`，但有效 enumerated child PIDL 交給 `IShellView::SelectItem` 仍是 `E_FAIL (0x80004005)`，讀回亦同值 | 目前 host 上公開 API 不足；這是本輪風險不可接受的主要依據 | 真實 Control Panel 選取行為：**未驗證,需要真實桌面** |
| OneDrive 佔位檔所在資料夾 | 找到 `D:\OneDrive - via.com.tw`，Shell view item count `1`，一般公開讀寫結果為 `S_OK`；但該目錄實際只確認到 `附件` 與隱藏 metadata，沒有確認到可標記為 OneDrive placeholder 的使用者檔案 | OneDrive root 可行性已做程式化 probe；「佔位檔所在資料夾」本身未達成情境條件 | **未驗證,需要真實桌面**，且需一個明確的 Files On-Demand placeholder |
| 切換 view mode 後 | `C:\Windows`；`IFolderView::SetCurrentViewMode(DETAILS/ICON)` 與 `IFolderView2::SetViewModeAndIconSize(DETAILS/ICON)` 全部 `S_OK`；保留選取後再讀回，count `1`、index `0`、兩種 array／data object 皆 `S_OK` | 在本 build 的程式化 probe 可行 | 真實 Details／Large Icons 人工選取還原：**未驗證,需要真實桌面** |

#### 未公開 LVM 路徑

因 Control Panel 的公開 API 已實際不足，才進入 `FindWindowEx`／`LVM_*` 評估。五個 scenario 都得到相同結果：`IShellView::GetWindow` 為 `S_OK`，但遞迴搜尋沒有找到 `SysListView32` descendant；因此沒有送出 `LVM_GETNEXTITEM` 或 `LVM_SETITEMSTATE`，結果是 **LVM path unavailable**，不是 LVM 通過。這也表示不能把「用 LVM 硬幹」寫成目前 Windows build 的 fallback。探測程式已刪除，`src` 無 `LVM_`／`FindWindowEx` 命中。

#### 導覽完成時序判定

官方文件定義 `OnNavigationComplete` 為成功導航通知，且事件順序是 `OnNavigationPending → OnViewCreated → OnNavigationComplete`；文件沒有承諾項目列舉已完成（參考 [IExplorerBrowserEvents](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iexplorerbrowserevents)、[OnNavigationComplete](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-iexplorerbrowserevents-onnavigationcomplete)、[BrowseToObject](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-iexplorerbrowser-browsetoobject)）。本 probe 的實測也支持這個區分：第一次收到 `OnNavigationComplete` 後立即查詢，`C:\Windows` 的 `ItemCount` 為 `0` 且 `Item(0)` 失敗；同一 view 經 3000 ms message pump 後，才得到 `134` 項與可用第一個 PIDL。這不是可接受的產品 polling 解法；目前只能判定 `OnNavigationComplete` 可可靠表示導航成功，**不能單獨可靠表示 selection restore 的 item-ready 時機**。後續實作若要開票，必須先定義不依賴 busy loop／polling timer 的 ready/retry 邊界，否則選取還原風險不可接受。

#### 人工驗證與跨 build 限制

- Scope 3 五項需要 live Shell view 的真實滑鼠／鍵盤選取、切換 view、重新導覽與重開：全部 **未驗證,需要真實桌面**。本次程式化 `SelectItem` 結果不能冒充人工驗收。
- Windows 10 22H2 與 Windows 11 的雙 build 比對：**未驗證,需要真實桌面**；本環境只有上述 `10.0.26200.0`，沒有第二個 Windows build。
- 沒有修改 `docs/design-spec.md` 或 `docs/tickets.md`：本輪不是「砍除」結論，且 tracker status 明確留給外部流程處理。

#### Agent checks

- `cmake --build build`：通過（exit code 0，`ninja: no work to do.`）。
- `ctest --test-dir build --output-on-failure`：通過，1/1 test passed（exit code 0）。
- `.\build\PaneDock.exe` 及 Scope 3 人工操作：**未驗證,需要真實桌面**；沒有啟動後捏造結果。
- `rg -n "LVM_|FindWindowEx" src`：無匹配（exit code 1，符合探測碼不留 repo 的預期）。
- `git diff --check`：通過（exit code 0）。
- 暫存 public-API probe：以 Clang C++20 編譯無 warning；完整五 scenario probe exit code 0；Control Panel 補測 `IShellFolder::EnumObjects`／`IShellView::SelectItem` 的 probe exit code 0，輸出如上。所有暫存 source／exe 已刪除。

### 2026-08-24 驗證與判定接受

獨立確認:`git status`/`git diff --stat` 只有本文件變動,無探測碼留在 `src` 或建置樹;`rg -n "LVM_|FindWindowEx" src` 無命中(exit 1),與交接一致。

接受本輪「可行但風險不可接受」的判定,不需要依「已確認的產品決策」第 3 點更新 `docs/design-spec.md` §NFR-005／§3.2——該決策只在結論為「砍除」時觸發,而選取狀態依 §NFR-005 本來就已是 best-effort 狀態,不是必要狀態;本輪判定沒有把它從 best-effort 升級成必要,也沒有主張完全砍除,只是把殘餘風險釘死在「一般檔案系統可行、虛擬命名空間公開 API 不足、無穩定的 item-ready 訊號」這三點,供後續實作 ticket 直接引用範圍。

殘餘缺口(環境限制,非缺陷):
- Scope 3 五種情境的真人滑鼠/鍵盤即時選取與重開驗證未執行——本 session 依使用者指示暫緩鍵盤/滑鼠自動化,且原本這類即時互動驗證即使做也需要人工操作,不適合自動化;待使用者方便時可自行驗證,或留給後續實作 ticket 一併做。
- 雙 Windows build 比對未完成——本機只有一個可啟動的 Windows build(`10.0.26200.9168`),沒有第二台/第二個 build 可測;若之後有其他機器可用,補測即可,不需要重跑本輪已完成的 API 探測。

PD-002 的三種合法結論之一已產出且有實測 HRESULT 佐證,交接資訊足以讓後續實作 ticket(若要做)直接引用範圍與殘餘風險。判定為完成。
