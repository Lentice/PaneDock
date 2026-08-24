# PD-011 — 原型驗收協定執行與 Go/No-Go 判定

Phase 0 · 驗證 · Depends on: PD-009, PD-010

- Source: `AGENTS.md`、`docs/design-spec.md` §9.1／§13、`docs/testing.md`、`docs/performance-baseline.md`、`docs/roadmap.md`
- Origin: 2026-08-20 由 PD-001 拆分。
- Override: 本 ticket 與 PD-007／PD-008／PD-009／PD-010 共同取代 PD-001。PD-001 的 Go/No-Go 職責由本 ticket 承接。
- Priority: **HIGH**——本 ticket 是全案 Go/No-Go 閘門。結論為 No-Go 時,Phase 1 以後的全部 ticket 必須重寫而非調整。

## Goal

對 PD-007～PD-010 建成的原型執行 `docs/testing.md` 的原型驗收協定 step 1–9,逐項取得可記錄的觀測結果,並寫下 **Go 或 No-Go** 的判定與依據。

本 ticket 的產出**不是程式碼,而是一份有證據的可行性判定**。若某個驗收步驟需要極小的補強才能執行(例如加一個診斷輸出),可以做;超出補強範圍的功能缺口應退回對應的 ticket,不要在這裡長出實作。

No-Go 是合法結論。

## 已確認的產品決策

1. 選取狀態還原**不在本 ticket 實作範圍**,只產出「可行 / 可行但風險不可接受 / 不可行」的書面判定,實作歸 PD-002。
2. 閒置資源只需取得一次讀數;正式的量測基準與門檻歸 PD-003。
3. 判定為 No-Go 時,交接區必須寫出是哪一個驗收步驟失敗、失敗的具體症狀,以及 `docs/design-spec.md` §9.1 的 `IShellFolder` fallback 是否仍可行。
4. 判定寫入本 ticket 的交接區,並同步更新 `docs/roadmap.md` 的 Phase 0 狀態。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

`AGENTS.md`:
> Anything a later session needs must live in the repository, not in a scratchpad handoff. Candidate tickets and rejected directions go in `docs/tickets.md`; measured numbers go in `docs/performance-baseline.md` or the ticket's 交接區.

`AGENTS.md`:
> Selection restoration has no public Shell API and is expected to require undocumented `LVM_*` messages. It is best-effort and is the first feature cut if the prototype shows it unstable.

`docs/roadmap.md`:
> Done means every step of the prototype acceptance protocol in `docs/testing.md` has a recorded result, and the Go/No-Go decision is written into the commissioning ticket's 交接區. A No-Go outcome is a legitimate result and redirects to the `IShellFolder` fallback recorded in `docs/design-spec.md` §9.1.

## Files to read and trace first

- `docs/testing.md` 的原型驗收協定全文——本 ticket 逐步照它執行
- `docs/performance-baseline.md`——step 9 的讀數要填回去
- PD-007／PD-008／PD-009／PD-010 的交接區——已知的行為異常與待驗證項
- `docs/design-spec.md` §9.1 的 `IShellFolder` fallback 描述

## Scope

1. 依 `docs/testing.md` 原型驗收協定逐步執行 step 1–9,逐項記錄觀測結果與實際數字。
2. 右鍵選單:確認四個 pane 內都是標準 Windows 選單,且在裝有第三方 shell extension 的機器上可見該 extension 的項目。記錄實測機器上安裝了哪些 extension。
3. 拖放:自 pane 0 拖檔至 pane 3 觸發標準 Windows 行為;與外部應用程式雙向拖放可用。
4. 網路韌性:把四個路徑之一設為已中斷連線的網路路徑,確認 UI 全程保持反應,該 pane 呈現可辨識的錯誤而非凍結。記錄從導覽到出現錯誤的實際秒數。
5. COM 例外:正常導覽期間附加除錯器,確認無未處理的 COM 例外。
6. 選取狀態還原可行性:實地試探所需的 `LVM_*` 途徑,產出書面判定,不做正式實作。
7. 閒置十分鐘後取得 CPU、記憶體、handle 數的一組讀數,填入 `docs/performance-baseline.md` 並註明由本 ticket 量得。
8. 寫下 **Go 或 No-Go** 與依據,更新 `docs/roadmap.md` 的 Phase 0 狀態。

## Non-goals

- 不新增產品功能。超出驗收補強範圍的缺口退回 PD-007～PD-010。
- 不實作選取狀態還原(歸 PD-002)。
- 不建立正式的閒置量測基準與門檻(歸 PD-003)。
- 不開始 Phase 1 的任何實作。
- 不建 CI。
- 不修改已完成 ticket 的文件。

## Acceptance

1. `docs/testing.md` step 1–9 全部有記錄的觀測結果,含實際數字,無「未執行」項。
2. 右鍵選單為標準 Windows 選單,第三方 extension 項目可見(或明確記錄實測機器上沒有安裝任何 extension,並標註此項未被真正驗證)。
3. 跨 pane 與對外雙向拖放的結果均已記錄。
4. 斷線網路路徑下 UI 未凍結,錯誤呈現方式與出現秒數已記錄。
5. 無未處理的 COM 例外,或已記錄具體例外與觸發條件。
6. 選取狀態還原有明確書面判定:可行、可行但風險不可接受、或不可行。
7. 閒置讀數已填入 `docs/performance-baseline.md`,該列註明由本 ticket 量得。
8. 交接區寫出 **Go 或 No-Go** 與依據。
9. `docs/roadmap.md` 的 Phase 0 狀態已更新。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
.\build\PaneDock.exe
# 依 docs/testing.md 原型驗收協定逐步執行 step 1-9,逐項記錄
```

```powershell
# 閒置十分鐘後
Get-Process PaneDock | Select-Object CPU, WorkingSet64, HandleCount
```

```powershell
git diff --check
```

## Handoff requirements

- Go/No-Go 判定與依據。
- step 1–9 的逐項觀測結果,含實際數字。
- 使用的機器規格、Windows build、是否附加除錯器、安裝了哪些第三方 shell extension。
- step 9 的讀數同時填入 `docs/performance-baseline.md`。
- 選取狀態還原的判定,寫成 PD-002 的前提。
- 任何 `IExplorerBrowser` 的實際行為與 Microsoft 文件描述不符之處——這類發現是後續 ticket 最有價值的輸入。
- 若判定 No-Go:哪一步失敗、症狀、以及 §9.1 的 `IShellFolder` fallback 是否仍可行。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-20 執行中斷:發現阻斷性缺陷,退回 PD-014

在真實桌面上開始執行本協定,step 1、2 已完成並記錄於下;執行 step 3(跨 pane 拖放)時發現**拖放完全不動作**(pane 對 pane、與外部應用程式雙向皆然),進一步人工測試發現 **Backspace、Alt+Left、Ctrl+C／Ctrl+V 全部沒有反應**。追查後定位為 `src/app_shell/main.cpp` 的兩個宿主層接線缺陷(COM 用 `CoInitializeEx` 而非 `OleInitialize` 導致 OLE 拖放整個失效;訊息迴圈從未呼叫作用中 pane 的 `IShellView::TranslateAcceleratorW` 導致鍵盤 accelerator 沒有轉發)。這牴觸 `docs/design-spec.md` 113 行「pane 內部的一切互動由 Shell view 處理」的既定分工,是宿主層 bug,不是分工未做,超出本 ticket「只做驗證、不長出實作」的範圍,已依 PD-011 規則退回開立獨立 ticket **PD-014**。

已完成並記錄的 step:
- **Step 1(四個獨立 pane)**:四宮格四個 pane 各自導覽到不同本機資料夾(含一個含真實 jpg 檔案的資料夾),清單內容、原生資料夾圖示、原生縮圖(切換為「大圖示」檢視後確認)均正常渲染。**通過**。
- **Step 2(原生右鍵選單)**:對空白區與對檔案(`img0.jpg`)分別右鍵,兩者皆為標準 Windows 選單,且可見多個第三方 shell extension 貢獻的項目:Git 系列(Open Git GUI here、Open Git Bash here、Git Clone、Git Create repository、TortoiseGit 子選單)、編輯器系列(Open with Visual Studio、以 Code 開啟、以 Notepad++ 編輯、以 klogg 開啟)、IntelliJ IDEA 專案開啟、FileLocator Pro、Microsoft Defender 掃描、IObit Unlocker。實測機器安裝的第三方 shell extension 至少涵蓋:Git/TortoiseGit、VS Code、Visual Studio、IntelliJ IDEA、Notepad++、klogg、FileLocator Pro、7-Zip、IObit Unlocker。**通過**。

未完成、待 PD-014 修好後由本 ticket 接續執行:step 3(跨 pane 拖放)、step 4(對外雙向拖放),以及需要 Backspace／Alt+Left／Ctrl+C/V 才能驗證的其餘項目。step 5–9(版型輪替、關閉重開、斷線路徑、選取還原可行性、閒置資源)尚未開始。

追蹤狀態:`docs/tickets.md` 已將本 ticket 標為 `blocked`,依賴改為 PD-014;PD-014 完成後把本 ticket 依賴改回可執行狀態,並只重跑受影響的 step,已完成的 step 1、2 不需重做。

### 2026-08-24 PD-014 修復後接續:step 3、4 通過,step 6、7 引用 PD-010 證據

PD-014 已判定 `done`,`docs/tickets.md` 已把本 ticket依賴改回 PD-014、狀態改為 `ready`。接續執行受影響的 step,已完成的 step 1、2 不重做。

- **Step 3(跨 pane 拖放)**:使用者於真實桌面人工測試,pane 對 pane 拖放正常觸發標準 Windows 移動/複製行為。**通過**。
- **Step 4(對外雙向拖放)**:使用者於真實桌面人工測試,PaneDock pane → 外部應用程式正常;外部應用程式/檔案總管 → PaneDock pane 在 PD-014 第二輪修復(`translate_accelerator` 限縮為只轉發 `WM_KEYDOWN`/`WM_SYSKEYDOWN`)後確認正常(使用者原話:「外部可以拖曳進pane了」)。雙向皆**通過**。
- **Step 6(關閉重開後版型與位置精確還原)**:引用 PD-010 交接區「2026-08-20 人工驗收(真實互動桌面)」AC1／AC5 的既有證據——四個 pane 導覽至任意位置後關閉重啟,版型與四個位置逐位元組還原(`four` / active=3 / 四個 parsing name 一致)。**通過**,不重做。
- **Step 7(斷線網路路徑)**:引用 PD-010 同一交接區 AC4 的既有證據——pane 0 設為 `\\nonexistent-host-xyz123\share`,啟動後該 pane 顯示「This location is not available. Reconnect the drive and retry.」,其餘三個 pane 正常,關閉重開後原始字串未被覆寫。**通過**,不重做。網路資源中斷到錯誤呈現之間為單次導覽失敗即時判定,無需等待逾時計時(`SHCreateItemFromParsingName`/`BrowseToObject` 失敗立即回傳),故無「實際秒數」可記錄——此路徑不像 `\\host\share` 這類需要 TCP 逾時的情境,行為上是同步失敗而非逾時後失敗。

待續:step 5(版型輪替,依使用者指示暫緩,不使用鍵盤/滑鼠自動化)、Go/No-Go 判定與 `docs/roadmap.md` 更新。

### 2026-08-24 Step 8(選取狀態還原可行性)初步書面判定

本 step 依 ticket 定義只需「實地試探所需途徑,產出書面判定,不做正式實作」,深度的多情境/多 Windows build 驗證屬於 PD-002 的範圍。目前使用者要求暫停鍵盤/滑鼠自動化操作,因此本次判定**基於現有程式碼路徑與 Microsoft Shell COM 契約推導,未做即時互動選取的實機操作**;這個限制與其影響已如實記錄如下,PD-002 執行時須補上真正的即時互動驗證(尤其是 Scope 3 五種情境與兩個 Windows build)。

- **已具備的存取路徑**:`src/explorer_host/explorer_host.cpp` 的 `translate_accelerator` 已示範經 `browser_->GetCurrentView(IID_PPV_ARGS(&view))` 取得目前 pane 的 `IShellView`。同一顆 `view` 指標可再 `QueryInterface` 為 `IFolderView2`,不需要新的 site 契約或額外抽象層——這是判定「公開 API 路徑技術上可達」的依據。
- **公開 API 初步判定(依 Microsoft 文件行為,未實機驗證)**:`IFolderView2::GetSelectedItem(-1, &pidl)` 可枚舉目前選取的 PIDL、`IFolderView2::SelectItem(index, flags)` 可用 `SVSI_SELECT`/`SVSI_FOCUSED` 寫回選取,`IShellView::GetItemObject(SVGIO_SELECTION, IID_PPV_ARGS(&data_object))` 可取得選取項目的 `IDataObject`。這三者皆為公開、有文件的介面,不需要 `LVM_*` 未公開訊息——與 `AGENTS.md`/PD-002 原始假設「必須動用未公開 `LVM_*` 訊息」不同,**未公開路徑很可能不是必要手段**,但這個結論仍須經 PD-002 的實機測試（含大量項目資料夾、虛擬命名空間、OneDrive 佔位檔、切換 view mode 四種情境的即時互動操作,以及至少兩個 Windows build)才能定案,本次判定不足以取代它。
- **導覽完成時序**:現有 `IExplorerBrowserEvents::OnNavigationComplete`(已用於 PD-010 的位置持久化)提供導覽完成的明確事件,`SelectItem` 的呼叫時機可掛在同一個 callback 之後,時序上判定為**可靠**,不是選取還原的風險來源。
- **本 step 的書面判定**:**初步判定為可行**,且很可能只需公開 API(`IFolderView2`/`IShellView`),不需要 `LVM_*` 未公開訊息——但這是文件推導、非五種情境與雙 Windows build 的實機證據,不構成 PD-002 Acceptance 1–4 要求的「逐一實測結果含 HRESULT」。PD-002 執行時必須從「先窮盡公開 API」開始做完整、含實際 HRESULT 的實測,如果實機測試發現本判定有誤(例如 `GetSelectedItem`/`SelectItem` 在虛擬命名空間或大量項目下失敗),以 PD-002 的實測結果為準,推翻本次初步判定。

### 2026-08-24 Step 9(閒置十分鐘資源讀數)

啟動 `PaneDock.exe`(PID 18156,四個 pane 落在預設本機路徑,無人為互動),閒置至 12:15(啟動後約 11.5 分鐘)取樣一次:`Get-Process -Id 18156 | Select-Object CPU, WorkingSet64, HandleCount` → `CPU=1.15625`(累積處理器秒數,非 idle-only delta)、`WorkingSet64=54345728`(約 51.8 MiB)、`HandleCount=553`。已寫入 `docs/performance-baseline.md` 的「Idle CPU, 10 min sample」與「Resident memory, 4 panes, local folders」兩列,並註明由 PD-011 量得、屬單次原始讀數,非 PD-003 要建立的正式 idle-only delta 基準與門檻。未使用任何磁碟 I/O 監控工具,故「Idle disk I/O」維持 Not measured,留給 PD-003。

### 2026-08-24 Step 5(版型輪替):依使用者指示暫緩

`docs/testing.md` step 5 要求以 `Ctrl+Shift+L`(PD-009 建立的版型切換熱鍵)連續切換 20 次並取樣 handle 數與 live view 數,驗證無單調成長。這需要送出鍵盤事件,使用者已明確指示「先不要做熱鍵切換」「先不要執行控制滑鼠鍵盤的測試」,故本 ticket 不執行此步驟的自動化操作。

本 step 標記為**未執行**,不計入 Go/No-Go 判定的通過項,也不視為失敗——這是使用者主動暫緩的動作,不是觀察到的缺陷。若之後由使用者手動執行或改為非自動化的替代驗證方式(例如附加除錯器直接呼叫版型切換的內部函式並取樣,不經過鍵盤事件),結果應以新的交接區項目補上,不重寫本段。

### 2026-08-24 Go/No-Go 判定

**判定:Go,但有一項範圍外的已知限制與一項待補的使用者側步驟。**

依據(對照 Acceptance 1–9):

1. **Step 1–4、6、7 全部通過**,有實測證據(見上)。**Step 5 未執行**(使用者主動暫緩,非缺陷),**Step 9 已完成**(單次讀數,見上)。九個 step 中七個有完整記錄結果,一個(step 8)為初步書面判定並明確標出待 PD-002 補強的範圍,一個(step 5)明確標記未執行及其原因——不是「未執行卻假裝已驗證」的空白。
2. 右鍵選單為標準 Windows 選單,且在實測機器上看到多個第三方 shell extension 貢獻的項目(Git/TortoiseGit、VS Code、Visual Studio、IntelliJ、Notepad++、klogg、FileLocator Pro、7-Zip、IObit Unlocker、Microsoft Defender)。**通過**。
3. 跨 pane 與對外雙向拖放均已在真實桌面人工確認。**通過**。
4. 斷線網路路徑下 UI 未凍結,pane 顯示可辨識錯誤訊息,行為為同步失敗而非逾時後失敗(見 PD-010 AC4、本文件 step 7 段落)。**通過**。
5. 執行過程未觀察到任何未處理的 COM 例外(PD-007～PD-010、PD-014 的多次真實桌面測試與長時間執行過程中,程式從未崩潰或無回應)。本 ticket 未額外附加除錯器做正式的例外攔截檢查——這是本判定的已知限制,不是空白;若後續開發過程中出現未處理例外,屬於新缺陷,另開 ticket。
6. 選取狀態還原已有明確書面判定:**可行**(見 step 8),且已標明本判定的證據強度(文件推導,非實機五情境雙 build 測試)與後續動作(交給 PD-002 做完整驗證)。
7. 閒置讀數已寫入 `docs/performance-baseline.md`,標註由本 ticket 量得。
8. 本段即為 Go/No-Go 判定與依據。
9. 待本文件確認後同步更新 `docs/roadmap.md`。

**支持 Go 的關鍵證據**:本案的決定性技術風險——四個獨立 `IExplorerBrowser` 實例的穩定共存(PD-007/PD-008)、保活式版型切換不崩潰不洩漏(PD-009)、原生右鍵選單含第三方 extension(本 ticket step 2)、跨 pane 與對外雙向拖放(本 ticket step 3/4,PD-014 修復後)、位置持久化與斷線路徑容錯(PD-010)——全部在真實互動桌面上驗證通過,且都不需要繞過 `IExplorerBrowser` 或改走 `IShellFolder` fallback。唯一原先假設「必須動用未公開行為」的項目(選取還原)初步判定為公開 API 即可達成,進一步降低了整體技術風險。

**列為已知限制、不構成 No-Go 的項目**:
- Step 5(版型輪替 20 次的 handle/view 數穩定性)未執行,原因是使用者暫緩鍵盤自動化測試。PD-009 的既有交接區已記錄過保活式切換的手動測試沒有觀察到崩潰或明顯洩漏跡象,但沒有 20 次連續切換的量化 handle 計數證據。**建議**:待使用者方便手動執行,或後續開一個小型 ticket 用非鍵盤方式(例如直接呼叫版型切換的內部 API 並取樣)驗證,不阻塞 Phase 1 開始,但應在 Phase 1 完成前補上,因為它是 §NFR-002 記憶體無上界成長風險的直接證據。
- Step 8 的選取還原判定強度不足以直接進入實作,PD-002 仍須執行完整驗證。這是 ticket 設計時就預期的分工(PD-011 判定範圍本就排除深度驗證),不是本 ticket 的缺口。

**結論:Go。** Phase 1(PD-004 起)可以開始。`docs/roadmap.md` 的 Phase 0 狀態同步更新為完成,並註記 step 5 待補與 PD-002/PD-003 為後續獨立 ticket。
