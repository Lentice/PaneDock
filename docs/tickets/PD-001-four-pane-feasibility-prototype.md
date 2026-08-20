# PD-001 — 建立四分割 `IExplorerBrowser` 可行性原型並產出 Go/No-Go 結論

Phase 0 · explorer_host · Depends on: —

- Source: `AGENTS.md`、`docs/design-spec.md` §9.1／§9.3／§9.4／§13、`docs/development.md`、`docs/testing.md`
- Origin: 2026-08-20 專案建立。選型審查(`docs/adr/0001`)確認本案的決定性風險是多實例 `IExplorerBrowser` 的宿主整合,兩份獨立審查都要求在任何產品開發前先以原型驗證。
- Priority: **HIGH**——本 ticket 是全案 Go/No-Go 閘門,結論為 No-Go 時 Phase 1 以後的全部 ticket 必須重寫而非調整。

## Goal

做出一個最小的原生 Win32 視窗,以四宮格承載四個獨立的 `IExplorerBrowser` 實例,並依 `docs/testing.md` 的原型驗收協定逐項取得可記錄的觀測結果。

本 ticket 的產出不是產品程式碼,而是**一份有證據的可行性判定**。原型可以醜、可以沒有 Group、沒有 tab、沒有側邊欄、沒有持久化格式;但它必須能回答「多個 Shell view 在同一個 process 內能否穩定共存」。

刻意是四分割而不是二分割:被測的風險就是「多實例」,二分割測不出來。

## 已確認的產品決策

1. 原型不需要 Group、tab、側邊欄或 JSON schema。位置持久化用最簡單的方式(單一檔案存四個路徑字串)即可,**不需要**符合 §10 的 session document 契約——該契約由 PD-006 定義。
2. 不需要視覺打磨、不需要 active pane 指示以外的任何 UI 樣式。
3. 選取狀態還原**不在本 ticket 範圍**,只在 step 8 產出「可行性判定」,實作歸 PD-002。
4. 閒置資源只需在 step 9 取得一次讀數;正式的量測基準與門檻歸 PD-003。
5. No-Go 是合法結論。若判定 No-Go,交接區必須寫出是哪一個驗收步驟失敗、失敗的具體症狀,以及 `docs/design-spec.md` §9.1 的 `IShellFolder` fallback 是否仍可行。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`:
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/design-spec.md` §9.4 關閉序列:
> 1. 擷取現行狀態並原子寫入 session document
> 2. **destroy 全部 live `IExplorerBrowser`**(每個曾 `Initialize` 的都必須 `Destroy`)
> 3. destroy pane HWND
> 4. destroy 主視窗
> 5. 退出訊息迴圈
> 6. `CoUninitialize`
>
> 順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。

`docs/design-spec.md` §9.2:
> 單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。

`docs/development.md`:
> Use `Microsoft::WRL::ComPtr` for every interface pointer. Raw `AddRef`/`Release` pairs are not acceptable in new code.

## Files to read and trace first

本 ticket 是本 repo 的第一段程式碼,沒有既有檔案可追。需先讀的外部契約:

- `IExplorerBrowser`(`shobjidl_core.h`):`Initialize`、`Destroy`、`SetRect`、`BrowseToObject`、`SetOptions`
- `IExplorerBrowserEvents`:navigation 與 view-created 通知
- `IFolderView2`:`SetCurrentViewMode`、`SetSortColumns`
- `IServiceProvider`:site 物件必須實作的查詢
- `IShellItem` / `SHCreateItemFromParsingName`
- `SetProcessDpiAwarenessContext` 與 `WM_DPICHANGED`

## Scope

1. 建立 `src/explorer_host/` 與 `src/app_shell/`,以及能產出單一 `PaneDock.exe` 的 `CMakeLists.txt` 目標。
2. `app_shell`:`wWinMain`、`CoInitializeEx(COINIT_APARTMENTTHREADED)`、Per-Monitor-V2 DPI awareness、主視窗類別與訊息迴圈。
3. 四宮格矩形計算:視窗 client area 均分為四,固定 0.5/0.5 比例(可拖曳分隔線**不在**本 ticket 範圍)。`WM_SIZE` 與 `WM_DPICHANGED` 時重算並對四個 view 呼叫 `SetRect`。
4. `explorer_host`:一個型別包住一個 `IExplorerBrowser` 及其 site 物件,以 `ComPtr` 持有所有介面指標,並在解構時保證 `Destroy` 被呼叫恰好一次。site 物件實作 `IServiceProvider`(以及 `IExplorerBrowserEvents`,用於記錄導覽事件到診斷輸出)。
5. 四個 pane 各自 `BrowseToObject` 到一個可由命令列或硬編碼給定的路徑。
6. active pane 指示:記錄哪個 pane 為 active,並以最低成本的方式呈現(邊框繪製即可)。點擊 pane 設為 active。
7. 二分割／四分割切換:提供一個快速鍵在「只顯示 pane 0」與「四宮格」之間切換。**切換時保活 view 並重新 `SetRect`,不 destroy 後重建**——這正是要驗證的路徑。
8. 位置持久化:關閉時寫下四個路徑,啟動時讀回並導覽。用最簡單的檔案格式。
9. 關閉序列嚴格依 §9.4 實作,並加入一個 self-check:在 `Destroy` 全部 view 之後、destroy 主視窗之前,斷言 live view 計數為 0。
10. 一個可執行的 self-check 目標(見 Agent checks),驗證四宮格矩形計算在退化尺寸下不產生零或負值的矩形。

## Non-goals

- 不實作 Group、側邊欄、tab、tab 條、網址欄、上一頁／下一頁。
- 不實作 §10 的 session document 格式、schema version 或原子寫入契約(歸 PD-006)。
- 不實作選取狀態的讀寫(歸 PD-002)。
- 不實作可拖曳分隔線。
- 不實作 `IFileOperation` 或剪貼簿(拖放由 Shell view 自行提供,無須我們接線)。
- 不實作五種版型;只要「單一」與「四宮格」兩種,足以測 view 生命週期。
- 不做視覺打磨、不做主題、不做圖示。
- 不建 CI(見 `docs/tickets.md` §計畫決策紀錄 的刻意不做事項)。
- 不為 `IExplorerBrowser` 加抽象層以便測試(見 §已否決的方向)。

## Acceptance

1. 四宮格下四個 pane 各自可獨立導覽,原生圖示與縮圖正常顯示。
2. 四個 pane 內的右鍵選單為標準 Windows 選單,且在裝有第三方 shell extension 的機器上可見該 extension 的項目。
3. 自 pane 0 拖檔至 pane 3 觸發標準 Windows 行為;與外部應用程式雙向拖放可用。
4. 在單一／四宮格之間切換 20 次後:live view 計數回到基準、焦點仍落在 active pane、handle 數未單調成長。
5. 關閉再開啟後四個 pane 的路徑精確還原。
6. 四個路徑之一為已中斷連線的網路路徑時,UI 全程保持反應,該 pane 呈現可辨識的錯誤而非凍結。
7. 正常導覽期間無未處理的 COM 例外。
8. 針對選取狀態還原,產出明確的書面判定:可行、可行但風險不可接受、或不可行。
9. 閒置十分鐘後取得 CPU、記憶體、handle 數的一組讀數。
10. 交接區寫出 **Go 或 No-Go**,以及依據。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
.\build\PaneDock.exe
# 依 docs/testing.md §Prototype acceptance protocol 逐步執行 step 1-9,逐項記錄觀測結果
```

```powershell
rg -n "AddRef|->Release\(\)" src
# 預期:無命中。全部介面指標應由 ComPtr 持有。
rg -n "windows\.h|HWND|IUnknown" src/core
# 預期:無命中(本 ticket 尚未建立 src/core,命中即為誤置)
git diff --check
```

## Handoff requirements

交接時記錄:

- Go/No-Go 判定與依據。
- `docs/testing.md` 原型驗收協定 step 1–9 的逐項觀測結果,含實際數字。
- 使用的機器規格、Windows build、是否附加除錯器、安裝了哪些第三方 shell extension。
- step 9 的讀數同時填入 `docs/performance-baseline.md`,並在該列註明由本 ticket 量得。
- 選取狀態還原的判定寫入 PD-002 的前提。
- 任何 `IExplorerBrowser` 的實際行為與 Microsoft 文件描述不符之處——這類發現是後續 ticket 最有價值的輸入。
- 若判定 No-Go:哪一步失敗、症狀、以及 §9.1 的 `IShellFolder` fallback 是否仍可行。

## 交接區

<!-- 實作 agent 填寫,append-only -->
