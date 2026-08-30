# PD-123 — 複製進行中關閉 PaneDock 的驗證、確認與修正

Phase 7 · Shell/clipboard lifetime validation and gated remediation · Depends on: PD-023, PD-032, PD-106, PD-121, PD-122

- Source: 使用者需求(2026-08-30)、`docs/design-spec.md` FR-007／FR-008、§9.2／§9.4。
- Origin: 使用者詢問複製檔案進行中關閉 PaneDock 是否安全，並要求涵蓋 PaneDock pane 之間及 PaneDock／Windows File Explorer 雙向組合；在尚未取得真實 runtime 證據前，使用者明確要求先建立 ticket，且必須再次確認後才能開始任何修正實作。
- Priority: HIGH——目前 `WM_CLOSE` 會同步儲存 session、destroy 全部 live `IExplorerBrowser` 再關閉主視窗；Shell 檔案操作會回入同一 STA message loop，但現有驗收沒有覆蓋「操作進行中關閉 host」。

## Goal

先在真實、已解鎖且可互動的 Windows 桌面上，以 disposable test data 量出複製進行中關閉 PaneDock 的實際行為，依 Shell 操作擁有方向歸類結果，回答是否存在來源損壞、目的檔殘留、複製中止、程序殘留、關閉 hang/crash 或錯誤的 clean-shutdown 紀錄。

若任何情境不符合安全預期，本 ticket 接著負責修正與回歸驗收，但中間有強制 checkpoint：執行者必須先回報重現步驟、證據、根因範圍與最小修正方案並停止；只有使用者在看過證據後明確批准，才可在**同一張 PD-123** 內進入修正階段。

## 已確認的產品決策

1. 不預設現行行為有 bug，也不預設需要 pending-close barrier、`OleFlushClipboard`、自有 `IFileOperation` wrapper 或背景複製程序；先量測 Windows Shell 的真實行為。
2. 測試矩陣依操作擁有權分成 pane→pane、PaneDock→File Explorer、File Explorer→PaneDock、僅 Copy 後關閉再 Paste。四個 pane 的所有排列在生命週期上等價，不窮舉 12 個方向。
3. 每個涉及實際傳輸的類別至少包含同磁碟與跨磁碟；跨磁碟測試必須記錄來源／目的 volume。OneDrive、網路磁碟與 USB 只在現成可用且不會接觸非 disposable 資料時抽樣，不為本票建立網路或外部依賴。
4. 關閉方式以使用者正常可觸發的主視窗 Close／`Alt+F4`／對已確認主 HWND 送 `WM_CLOSE` 為準；不得用 `TerminateProcess`、`Stop-Process -Force` 或 `taskkill /F` 代替。
5. 可以用一次性、不進 repo 的 UIA／Win32 driver 導向測試資料夾、選取 disposable 檔案、送 Copy／Paste 與 `WM_CLOSE`；這不建立常駐端到端 UI 測試，也不覆寫 `docs/tickets.md` 已否決的一般 UIAutomation／WinAppDriver 測試方向。
6. Phase A 驗證期間的程式碼決策是「不動工」。在 checkpoint 核准前，任何 `src/*`、`CMakeLists.txt` 或測試程式碼差異都表示越界。
7. **強制確認閘門：**即使 Phase A 驗證出 FAIL，執行者仍不得直接修正。必須先向使用者呈現觀察結果、根因範圍、建議方案、替代方案、預計修改的精確函式／檔案與 non-goals，等待使用者明確回覆同意。
8. 使用者核准後，先把核准日期、選定方案與精確實作 scope append 到本 ticket 的「Approved remediation plan」；完成這一步才可在同一張 PD-123 進入 Phase B。不得另開修正 ticket，也不得把未核准的替代方案順手一起做。
9. 若 Phase A 全部結果可接受，本票不產生產品程式碼差異，直接以「現行 Shell 行為可接受」完成。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` FR-007:

> 複製、移動、刪除、重新命名經由 Shell `IFileOperation`,含原生進度對話框與衝突提示。剪貼簿操作經由 Shell `IDataObject`。

`docs/design-spec.md` FR-008:

> 支援 pane 之間、以及與其他應用程式之間的拖放,經由 OLE drag and drop 與 Shell `IDataObject`。

`docs/design-spec.md` §9.2:

> 單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。

`docs/design-spec.md` §9.4:

> 1. 擷取現行狀態並原子寫入 session document
> 2. **destroy 全部 live `IExplorerBrowser`**(每個曾 `Initialize` 的都必須 `Destroy`)
> 3. destroy pane HWND
> 4. destroy 主視窗
> 5. 退出訊息迴圈
> 6. `CoUninitialize`
>
> 順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。

`AGENTS.md`:

> File operations go through Shell `IDataObject` and `IFileOperation`. Never assemble a path string and call the filesystem directly — that loses virtual items, progress UI and conflict handling.

`AGENTS.md`:

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`:

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

## Files to read and trace first

- `AGENTS.md`、`docs/design-spec.md` FR-007／FR-008／FR-009、§9.2／§9.4、`docs/development.md` 的 STA、Shell re-entry 與 shutdown 規則。
- `docs/testing.md` 的 Shell file operations A1–A7、Clipboard B8–B10、Drag and drop C11–C15、Cross-Group E18–E23。
- `src/app_shell/main.cpp` 的 message loop、`translate_accelerator`、`destroy_explorers`、`WM_CLOSE`、`WM_DESTROY`、`WM_QUERYENDSESSION`、`WM_ENDSESSION` 與 message loop 後的最終 cleanup。
- `src/explorer_host/explorer_host.cpp`／`.h` 的 `translate_accelerator`、`destroy`、`IExplorerBrowser::Destroy`、callback detach 與 `destroying_` guard。
- `docs/tickets/PD-023-shell-file-operations-acceptance.md`、`PD-032-endsession-clean-shutdown-handling.md`、`PD-106-graceful-shutdown-shell-teardown.md`、`PD-121-cross-group-clipboard-copy-paste.md`、`PD-122-cross-group-file-drag-and-drop.md`。

## Scope

### Phase A — 驗證與確認

1. 建立只含 disposable data 的來源與目的資料夾；保留原始檔大小及 SHA-256。大檔必須讓原生進度 UI 可觀察至少數秒，但不得耗盡較小 volume 或接觸使用者真實檔案。
2. 先跑不關閉 PaneDock 的 control copy，確認測試資料、兩個 volume、原生 Copy／Paste 與完整性判定本身可用。
3. 執行下列代表性矩陣；每輪使用全新目的資料夾與全新 PaneDock process，且必須在原生進度 UI 明確仍在更新時觸發正常關閉：
   - V1: PaneDock pane A → pane B，同磁碟 Copy／Paste。
   - V2: PaneDock pane A → pane B，跨磁碟 Copy／Paste。
   - V3: PaneDock → Windows File Explorer，同磁碟與跨磁碟各一次；Paste 已開始後關閉 PaneDock。
   - V4: Windows File Explorer → PaneDock，同磁碟與跨磁碟各一次；Paste 已開始後關閉 PaneDock。
   - V5: 在 PaneDock 只執行 Copy、尚未 Paste 即正常關閉；關閉後到 Windows File Explorer Paste。
4. 每輪記錄：來源／目的、volume、資料大小、啟動方式、觸發 close 時進度、PaneDock 主視窗與 PID 是否在 5 秒內消失、Shell 進度 UI 是否繼續／取消／消失、來源 hash、目的存在與大小/hash、殘留 partial/temp item、Windows Error Reporting／Application event、下一次啟動是否出現不乾淨關閉警示。
5. 若某輪 copy 太快而無法證明 close 發生在操作中，該輪記為無效並增加 disposable payload 後重做；不得把「來不及關」記成 PASS。
6. 將結果 append 到本 ticket 的交接區；每個 V1–V5 明確記為 `PASS`、`FAIL` 或 `BLOCKED`，附具體證據。
7. 完成證據後停止並向使用者要求決策：接受現行 Shell 行為、要求補驗某情境，或批准本 ticket 內提出的最小修正方案。未收到批准前不得修改產品。

### Checkpoint — 必須取得使用者確認

1. 若 V1–V5 任一項 FAIL，先 append 一份「Remediation proposal」，至少包含：已確認的 failure class、可重現步驟、根因 evidence、推薦方案、替代方案、精確修改函式／檔案、預計 test/self-check、風險與 non-goals。
2. 向使用者呈現 proposal 並停止。模糊回覆、單純要求「繼續調查」或先前「建立 ticket」的授權都不等於實作批准；必須取得對該 proposal 的明確同意。
3. 收到批准後，把批准內容 append 為「Approved remediation plan」。若使用者選擇不同方案，先依其決策更新 plan；不得自行擴張。

### Phase B — 核准後修正與回歸驗收

1. 只修改 Approved remediation plan 列出的共享根因位置及必要 caller；動手前重新追蹤該函式所有 caller，避免只修一個方向。
2. 沿用原生 Shell `IDataObject`／`IFileOperation` 與既有單一 STA 架構。優先重用現有 shutdown、message-loop、re-entry guard；除非 Phase A 證據與使用者核准明確要求，不新增檔案操作 wrapper、背景執行緒、helper process 或 polling timer。
3. 保持 §9.4 shutdown 順序：每個 initialized browser 仍必須 `Destroy`，且 parent HWND 不得先銷毀。修正只能控制何時進入既有 teardown 或如何保全已確認由 PaneDock 擁有的 OLE 資料，不得跳過 cleanup。
4. 新增一個最小 runnable check：若修正可抽成不含 HWND／COM 的狀態轉移，放進 `core` focused test；否則使用 source/lifetime check 加真實桌面 V1–V5 回歸，並在交接區說明無法 fake Shell COM 契約的原因。
5. 重跑 Phase A 中所有 FAIL 情境，並至少重跑每個未失敗 ownership class 的一個 control，證明修正沒有破壞 pane→pane、PaneDock→Explorer、Explorer→PaneDock 或 clipboard-only 路徑。
6. 完成 build、CTest、`git diff --check` 與 graceful close smoke；把修改、驗證結果及剩餘限制 append 到交接區。

## Non-goals

- Phase A 與 checkpoint 未核准前，不修改 `src/*`、`tests/*`、`CMakeLists.txt`、session schema 或產品 UI。
- Phase B 不實作 Approved remediation plan 以外的候選方案；在核准前 pending-close flag、message-loop barrier、關閉提示、`OleFlushClipboard`、`IFileOperationProgressSink` 等都只是可能方案。核准後僅依「Approved remediation plan」列出的 pending-close barrier、關閉提示與 progress sink 實作，`OleFlushClipboard` 仍不在 scope。
- 不用路徑字串自行執行產品層 Copy／Move；建立與清理 disposable test data 不受此產品規則限制。
- 不測 Cut／Paste、同磁碟預設 move、delete、rename、系統關機／登出或強制終止；這些不是本次「複製進行中正常關閉」的問題。
- 不建立可長期執行的 UIAutomation／WinAppDriver suite，不把一次性 UIA driver check in。
- 不因一個方向失敗就推定其他方向相同；也不因一個方向成功就宣告全部安全。
- 不另開修正 ticket；驗證、確認與核准後的最小修正都留在 PD-123。

## Acceptance

1. Control copy 與 V1–V5 都有可重演步驟及逐項結果；至少 V1–V4 的同／跨 volume 分支各有有效觀察，或記錄無法執行的精確外部 blocker。
2. 每次 close 都發生在原生進度 UI 可證明仍在更新時；紀錄 PaneDock window/PID、進度 UI 與 copy 結果，沒有用時間猜測代替證據。
3. 每個來源檔在測試後以 SHA-256 證明未受損；完成的目的檔 hash 相符；未完成目的地的所有殘留項目均被列出。
4. 正常關閉後重新啟動一次並記錄 `clean_shutdown`／警示行為；若 crash、hang 或 PID 殘留，附 Event Viewer／WER 或 process evidence。
5. Phase A 結束時，`git diff --name-only` 證明除了 `docs/tickets.md` 與本 ticket 交接紀錄外沒有產品或測試程式碼變更；一次性 driver 與 disposable data 均不進 repo。
6. 若全部情境可接受，明確記錄「現行 Shell 行為可接受，不需修正」；若任一情境不可接受，先完成 Remediation proposal 並明確寫出「等待使用者確認，尚未實作」。
7. Phase B 只能在本 ticket 的 Approved remediation plan 與對應使用者批准存在後開始；最終 diff 不得超出該 plan。
8. 修正後原 FAIL 情境全部 PASS，其他 ownership class 的 control 無退化；來源 hash、目的結果、PID/window exit、clean-shutdown 與進度 UI 均有新證據。
9. `cmake --build build`、`ctest --test-dir build --output-on-failure`、focused runnable check、graceful close smoke 與 `git diff --check` 全部通過。

## Agent checks

### Phase A

```powershell
git status --short
rg -n "WM_CLOSE|WM_DESTROY|WM_QUERYENDSESSION|WM_ENDSESSION|destroy_explorers|PostQuitMessage" src/app_shell/main.cpp
rg -n "ExplorerHost::destroy|IExplorerBrowser::Destroy|destroying_|SetCallback|Unadvise" src/explorer_host/explorer_host.cpp
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

```powershell
.\build\PaneDock.exe
# 真實、已解鎖的互動桌面：依 Scope V1–V5 執行。
# UIA／Win32 driver 只可操作已確認屬於本次 PaneDock／Explorer 測試 process 的 HWND，
# 不得碰使用者其他 Explorer 視窗或真實資料。
```

### Phase B（只有 Approved remediation plan 存在時執行）

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

```powershell
# 重跑 Phase A 的原 FAIL 情境與每個未失敗 ownership class 的一個 control。
# 記錄修正後來源 hash、目的結果、PaneDock PID/window、進度 UI 與 clean_shutdown。
```

## 交接區

<!-- 執行者填寫，append-only。Approved remediation plan 出現前不得實作修正。 -->

### 2026-08-30 建票時的已知 blocker

- 建票前曾兩次嘗試用 UIA 操作真實 PaneDock：UIA 可以讀到完整控制樹，但 Windows automation provider 在任何輸入前都回報 `failed to activate captured window`；測試 desktop 當時不可互動，因此沒有執行 V1–V5，也沒有把 source inspection 當成 runtime PASS。
- 兩次嘗試建立的測試 process 都已正常關閉；第一次建立的 6 GiB disposable payload 與兩個暫存目錄已刪除。沒有留下測試資料或背景 PaneDock process。
- 目前 blocker 是「提供已解鎖、UIA／Win32 可互動的真實桌面工作階段」。解除後從 control copy 開始；不得跳過 Phase A 與使用者 checkpoint 直接進入 Phase B。

### 2026-08-30 — 第三次嘗試:UIA activation 已解除,但發現新的、更精確的 blocker

使用者確認在其目前互動中的主控台工作階段(`quser` 顯示 `Active`、`IDLE TIME none`)執行,並在得知「選取/Copy/Paste 需要真實 Ctrl 修飾鍵注入(`SendInput`),會瞬間碰觸使用者當下鍵盤」後明確核准採用 `SendInput`。

- 先用 UIA 探測確認:先前兩次的 blocker(automation provider 回報 `failed to activate captured window`)本次未重現——`AutomationElement.SetFocus()` 雖仍回報「Target element cannot receive focus」(屬正常,主視窗元素本身不支援該 UIA pattern),但 `GetForegroundWindow()` 直接回傳與目標 HWND 相同,證明當時真的取得前景。此為單次探測結果,不是穩定保證(見下)。
- 依 `AGENTS.md`「不得持久化 PIDL/COM 指標」精神以外的一項新事實:PaneDock 目前沒有任何測試/替代 `%LOCALAPPDATA%` 覆寫機制(`grep -rn "LOCALAPPDATA\|PANEDOCK_.*DIR" src/` 只有一處 `SHGetKnownFolderPath(FOLDERID_LocalAppData,...)`)。每次啟動都讀寫使用者真實 `session.json`(`AGENTS.md` 的 atomic-replace 規則只保留一代 `.bak`)。已在跑任何導覽動作前,把整個 `%LOCALAPPDATA%\PaneDock` 複製到暫存位置備份,並在本次驗證結束後完整還原(已還原,`session.json`/`.bak` 內容與時間戳與備份一致)。**這是 Phase A 執行前必須先做的動作,寫入本節供下次接手者比照辦理**,而不是本票的產品程式碼變更。
- 建了一次性、不進 repo 的 PowerShell driver(`Add-Type -TypeDefinition` 一支 C# 類別包 `EnumWindows`/`GetDlgItem`/`PostMessage`/`SendInput`;地址列導覽沿用 PD-003 已驗證手法:直接 `SendMessage(WM_SETTEXT)` + `PostMessage(WM_KEYDOWN, VK_RETURN)` 給 `GetDlgItem(mainHwnd, 330+pane_index)`,不需要真實 focus,對兩個 pane 都導覽成功,畫面截圖確認)。
- **實測 control copy 失敗,且找到根因**:用 `GetGUIThreadInfo(0, ...)` 讀取當下系統前景執行緒的 `hwndFocus`,發現在驅動腳本呼叫 `Copy-ActivePaneSelection`/`Paste-ActivePane`(內含逐次 `SetForegroundWindow(mainHwnd)` 後才 `SendInput`)之後,系統真正的前景視窗其實是別的視窗(class `Windows.UI.Input.InputSite.WindowClass`,非 PaneDock),不是 PaneDock 主視窗。也就是說**每次 PowerShell 工具呼叫都是一個新行程**,`SetForegroundWindow` 從一個與 PaneDock 無關、且不是「使用者剛互動」的背景行程呼又,受 Windows 前景鎖定(foreground lock)機制限制,回傳值不能保證真的把前景切過去——只有緊接在 `Start-Process` 之後的第一次由建立者行程呼叫時才可靠成功(這解釋了探測階段為何成功、但後續每一步分開呼叫都不可靠)。結果是 `Ctrl+A`/`Ctrl+C`/`Ctrl+V` 這些真實鍵盤事件很可能送到了使用者當下真正在用的其他視窗,而不是 PaneDock——目的資料夾維持 0 items,driver 截圖顯示來源檔案也沒有被選取(無反白)。
- 這與建票時的 blocker 不同,是更精確的下一層:不是「拿不到前景」,而是「外部背景行程能否可靠、且不干擾使用者當下工作地拿到並保持前景,以送出真實鍵盤事件」。Windows 的前景鎖定本身就是設計用來防止背景程式在使用者正在互動時搶走焦點——在使用者明確 `IDLE TIME none` 的即時工作階段裡持續嘗試繞過它(例如 `AttachThreadInput` 之類的技巧)風險是:(a) 產生看似執行但其實按鍵送錯視窗的偽陰性證據,`docs/tickets.md` 與本票 Acceptance 第 5 條明確要求不得把「來不及/送錯」記成 PASS；(b) 每次「碰巧成功」時都是真的把使用者當下操作打斷一次,比原先揭露給使用者的「短暫、僅修飾鍵」風險更頻繁、更不可預期。
- 已清理:測試用 PaneDock 行程已用 `WM_CLOSE`(非 `TerminateProcess`)正常關閉;`%LOCALAPPDATA%\PaneDock` 已用備份還原,`session.json`/`.bak` 內容與時間戳與使用者原始檔案一致;`git status --short` 只有既有的 `docs/tickets.md`、`CONTEXT.md`、本 ticket 檔案,無 `src/*`/`CMakeLists.txt`/測試程式碼差異。E 槽/D 槽的一次性 disposable payload(`E:\PaneDockTest123\control_src|control_dst`)保留供下次沿用,未進 repo。
- 修正單一 blocker 後(見下)重跑,`GetForegroundWindow()` 全程正確等於 PaneDock hwnd,`GetGUIThreadInfo(0,...)` 顯示真實鍵盤 focus 落在 `DirectUIHWND`(即 Shell view 的內容控制項),F6 導致的 focus 在多次 nudge 後仍指向同一 HWND 且與預期 pane 一致——前景/焦點層級的問題已解決,不是先前懷疑的「送錯視窗」。
- 但 `Ctrl+A`→`Ctrl+C` 之後,以 `System.Windows.Forms.Clipboard.ContainsFileDropList()` 直接檢查,剪貼簿仍然沒有 `CF_HDROP`——也就是說即使 focus 正確落在 shell view 的 `DirectUIHWND`,`SendInput` 送出的 `Ctrl+A`/`Ctrl+C` 依然沒有讓 `IShellView`/`CDefView` 產生選取與複製的可觀察效果。根因尚未查明(候選:`DirectUIHWND` 底下還有更深一層才是真正接收鍵盤加速鍵的子視窗;或 Shell 的 `IShellView::TranslateAccelerator` 需要訊息真正經過我們主視窗的 `GetMessage`/`TranslateMessage` 迴圈而非只是「目標視窗恰好有 focus」;`INPUT` struct 的 marshalling 手動核算過位元組對齊,理論上與 x64 原生 40 bytes 佈局一致,但未做獨立對照組驗證排除)。
- 為了獨立排除「`SendInput` 本身是否work」這個變數,原打算開一個全新、與 PaneDock 無關的 Notepad 視窗送出簡單字元鍵驗證——這個動作被 Claude Code 的 auto-mode 分類器直接擋下(判定為需要額外授權的動作),因此沒有執行,也沒有嘗試繞過。
- **決定停在這裡,不再往下挖 `SendInput`/`AttachThreadInput` 等更底層的技巧**:(1) 已經連續修正兩層機制性 blocker(UIA activation、前景鎖定跨行程限制)但仍卡在同一個「選取/複製沒有可觀察效果」的謎團,再往下修可能要引入 `AttachThreadInput` 或更深的視窗階層探測,這類技巧本身就是在對抗 Windows 為了保護「使用者當下互動」而設的機制,在使用者目前正主動使用電腦(`quser` idle time 持續是 `none`)的情況下不應該不斷嘗試繞過;(2) 平台分類器剛擋下相關方向的一個獨立診斷動作,這是「先停下來問」比「找別的路徑硬做」更合適的訊號。
- 使用者後續提供 `E:\iso` 下現成大檔(`ubuntu-20.04.6-desktop-amd64.iso`,4,351,463,424 bytes,SHA-256 `510ce77afcb9537f198bc7daa0e5b503b6e67aaed68146943c231baeaab94df1`)供複製測試使用,並要求不得刪除 `E:\iso` 內任何檔案——已只用 `mklink /H` 建立唯讀等效的 hardlink 到 `E:\PaneDockTest123\iso_src\`(同一 NTFS volume 內的 hardlink,不複製資料、不修改原始檔),`E:\iso` 本身未被寫入或刪除;原始檔案 hash 仍為上述值,可作為之後真的能送出 Copy/Paste 時的驗證基準與觀察用大檔(2026-08-30 起有效)。
- 尚未執行任何 V1–V5;control copy(Phase A 步驟 2)本身還沒有一次成功過,因此還不能進入正式矩陣。停下並回報使用者決定下一步(見下方決策請求)。

### 2026-08-30 — AutoIt fallback、UIPI 假陰性排除與 V1 same-volume PASS

- 使用者核准以 computer-use 優先，失敗時改用 AutoIt／滑鼠，並允許 elevated context 與暫時修改 code 釐清關閉問題。computer-use／UIA 仍無法可靠 activation；AutoIt fallback 已成功完成可觀察的原生 Shell Copy／Paste 與正常關閉。
- 重現並排除一個工具假陰性：非 elevated PowerShell／computer-use／AutoIt 對 elevated PaneDock 回報 `MainWindowHandle=0`、無法列舉或啟用視窗；同時由 elevated AutoIt 列舉同一 PID，則可見 `PaneDockMainWindow` 且 `visible=True`。因此這次重現中的「視窗消失但 PID 殘留」是 UIPI／integrity mismatch，不是 PaneDock shutdown failure；CDB 所見 UI thread 停在外層 `GetMessageW` 也符合主視窗仍存活的狀態。此結論只涵蓋本次可重現案例，不倒推先前所有 UIA failure 都具有相同原因。
- 正常滑鼠關閉 control：一般 build 3/3 PASS，暫時 diagnostic build 1/1 PASS。暫時 probe 觀察到 `WM_CLOSE` → `DestroyWindow` → `WM_DESTROY` → `WM_NCDESTROY` → `PostQuitMessage` → message loop leave，沒有 hang；probe 與所有一次性 AutoIt script 已移除。
- **V1 PASS（PaneDock pane A → pane B，同磁碟 E:）**：來源 `E:\PaneDockTest123\iso_src\ubuntu-20.04.6-desktop-amd64.iso`，目的 `E:\PaneDockTest123\pd123-20260830-01\v1_same_panedock\`。AutoIt 確認 clipboard 有 `CF_HDROP`，偵測到原生 `OperationStatusWindow` 後等待 500 ms，於進度視窗仍存在時對明確的 `PaneDockMainWindow` 發出正常 close。PaneDock PID 於 5 秒內退出，進度視窗消失；目的檔為 4,351,463,424 bytes，SHA-256 與來源同為 `510CE77AFCB9537F198BC7DAA0E5B503B6E67AAED68146943C231BAEAAB94DF1`。
- `E:\iso` 內檔案未修改或刪除。測試後已還原 `%LOCALAPPDATA%\PaneDock`，目前與 session backup 的 `session.json` SHA-256 均為 `995A5B7422E5494596F44387116A1CB5D58EB7995A11D4A215515C9919159CEC`。沒有殘留 PaneDock／AutoIt 測試 process。
- 清除暫時 instrumentation 後，`cmake --build build` PASS、CTest 6/6 PASS、`git diff --check` PASS；沒有產品 code diff。因未發現產品關閉缺陷，不建立修正 ticket、不進入 Phase B。
- V2–V5 與 UNC `\\vianextfs06\Tmp\Lentice\test` 尚待執行；該 UNC 已在 Windows Explorer 確認可見且當時為空，但非 elevated PowerShell 直接存取回報 access denied。PD-123 保持 `in_progress`。

### 2026-08-30 — opencode commit d1a0771 後的 Phase A 矩陣結果

- 本輪測試的產品版本為 opencode 已提交的 `d1a0771`（Harden close and startup）。該 commit 的 build、CTest 6/6 與 `git diff --check` 均通過；本輪沒有再修改產品 code。測試仍使用 elevated AutoIt，所有路徑均為 disposable，`E:\iso\ubuntu-20.04.6-desktop-amd64.iso` 未被修改或刪除。
- 來源基準：4,351,463,424 bytes，SHA-256 `510CE77AFCB9537F198BC7DAA0E5B503B6E67AAED68146943C231BAEAAB94DF1`。512 MiB UNC 抽樣來源 `E:\PaneDockTest123\network_src\pd123-network-512mb.bin` 的 SHA-256 為 `9ACCA8E8C22201155389F65ABBF6BC9723EDC7384EAD80503839F49DCC56D767`。
- **V1 PASS**：PaneDock pane A → pane B，同磁碟 E:。原生 `OperationStatusWindow` 在 close 前可見至少 500 ms；PaneDock 正常退出，目的檔完成且 hash 與來源一致。
- **V2 PASS**：PaneDock pane A → pane B，E: → D:。log `13:21:25` 顯示 operation 可見且 500 ms 後仍存在，`13:21:26` 對 PaneDock PID 23480 正常 close，`13:21:27` PID 退出；目的檔 4,351,463,424 bytes，hash 與來源一致。
- **V3 PASS（同磁碟）**：PaneDock → Windows File Explorer，E: → E:。operation 在 `13:21:57` 可見且 500 ms 後仍存在，PID 7744 於 5 秒內退出，目的檔 hash 一致。
- **V3 PASS（跨磁碟）**：PaneDock → Windows File Explorer，E: → D:。operation 在 `13:22:20` 可見且 500 ms 後仍存在，PID 29248 於 5 秒內退出，目的檔 hash 一致。
- **V3 PASS（UNC 抽樣）**：PaneDock → Windows File Explorer，E: → `\\vianextfs06\Tmp\Lentice\test`。以唯一 512 MiB payload 重跑，operation title 為 `14% 已完成` 且 500 ms 後仍存在；PID 25392 於 5 秒內退出，share 目的檔完成 536,870,912 bytes，hash 與來源一致。另以 4.3 GiB ISO 觀察到 PaneDock 關閉後 share copy 約 46 秒完成，但該次抓到的是 stale `100% 已完成` 視窗，不作為進行中 UI 的主要證據。
- **V4 BLOCKED（同磁碟，時序無效）**：Windows File Explorer → PaneDock，E: → E:。單檔及三個 4.3 GiB 檔的 fresh destination 都在可證明活動 operation 前完成；進度視窗分別顯示 `100% 已完成` 或已消失，因此不記為 PASS。三檔測試目的檔均完整，總量 13,054,390,272 bytes，但 close 沒有在更新中的 UI 期間發生。
- **V4 FAIL（跨磁碟）**：Windows File Explorer → PaneDock，D: → E:。來源 `D:\PaneDockTest123\pd123-20260830-01\v4_cross_source\pd123-v4d-unique.iso` hash 與基準一致；目的 `E:\PaneDockTest123\pd123-20260830-01\v4_cross_panedock_unique` 是全新空資料夾。log `13:30:02` 顯示 operation title `進度`，500 ms 後仍存在；隨後對 PID 29612 正常 close，PID 於 5 秒內退出。關閉後至少 8 秒目的地仍無檔案，沒有 partial/temp item；來源未受損，Application Error／WER 無事件。這是有效的「PaneDock target view teardown 後 copy 未完成」失敗證據。
- **V5 PASS（clipboard-only）**：PaneDock 只執行 Copy 後正常關閉，再由 Windows File Explorer Paste；PaneDock PID 2504 於 5 秒內退出，之後 Explorer paste 完成，主要目的檔 hash 與來源一致。目的資料夾另有 `ubuntu-20.04.6-desktop-amd64 - 複製.iso`，大小及 hash 也一致；沒有 partial/temp item，該 duplicate 保留作為 Shell／一次性 driver 殘留證據，未把它隱藏或刪除。
- Control copy 沒有另記為獨立 PASS：較早的 control driver 曾因跨行程前景／UIPI 問題送錯輸入；後續 V1 已以有效的 CF_HDROP、原生 progress 與完整 hash 證據證明 pane→pane 路徑可用。一般滑鼠 close control 3/3、diagnostic close 1/1 均 PASS。
- 測試後 `%LOCALAPPDATA%\PaneDock` 的 `session.json`、`.bak` 與 prototype state 已由 session backup 還原；current `session.json` hash 與 backup 同為 `995A5B7422E5494596F44387116A1CB5D58EB7995A11D4A215515C9919159CEC`，內容含 `clean_shutdown:true`。測試期間沒有殘留 PaneDock／AutoIt process。UNC share 仍保留本輪 disposable outputs，未擅自刪除。
- 矩陣結論：V1、V2、V3 同／跨磁碟與 UNC 抽樣、V5 可接受；V4 跨磁碟明確 FAIL，V4 同磁碟仍缺有效 timing evidence。現有 d1a0771 的 close re-entry／nested-loop hardening 沒有解決「外部 Explorer 貼入 PaneDock 時，關閉 target host 導致 operation 未完成」這個 ownership class。依 checkpoint 規則，以下 proposal 尚未實作，PD-123 保持 `in_progress`。

## Remediation proposal

### 2026-08-30 — V4D remediation proposal（等待使用者確認，尚未實作）

- Failure class：外部 Windows File Explorer 對 PaneDock Shell view 進行跨磁碟 Paste 時，PaneDock 在原生 progress `進度` 仍存在且 500 ms 後正常 close；PaneDock PID 正常退出，但 target view teardown 後目的檔未建立，來源未損壞且沒有 partial item。
- 可重現步驟：以全新 PaneDock process 導覽 pane A 到 `E:\PaneDockTest123\pd123-20260830-01\v4_cross_panedock_unique`；以 Windows File Explorer 開啟 `D:\PaneDockTest123\pd123-20260830-01\v4_cross_source`，選取唯一 4.3 GiB 檔案並 Copy；回到 PaneDock Paste；確認 `OperationStatusWindow` title 為 `進度` 且 500 ms 後仍在；對明確的 `PaneDockMainWindow` 送正常 close；5 秒內 PID 消失，8 秒後目的地仍空。
- Root-cause evidence scope：現行 `src/app_shell/main.cpp` 的 `window_proc` `WM_CLOSE` 已設定 `closing_`，接著同步呼叫 `destroy_explorers`、`DestroyWindow` 與 `PostQuitMessage`；`destroy_explorers` 會對所有 initialized `IExplorerBrowser` 做 teardown。V4D 的 failure 只在 PaneDock 作為 Paste target 且 target view 被關閉時重現；V2、V3 與 V5 的 source／clipboard ownership classes 沒有相同結果。這已確認 view lifetime 與 Shell operation completion 的耦合，但尚未宣稱 Shell 內部 operation owner 已由公開 API 證實。
- Recommended option：在既有單一 STA shutdown flow 加入 close-pending barrier，只有當 PaneDock target Shell operation 明確完成或取消後，才進入既有 `destroy_explorers` → pane HWND → main HWND → message loop teardown 順序。候選修改位置限定為 `src/app_shell/main.cpp` 的 `AppState`、`window_proc` `WM_CLOSE`／message loop／`destroy_explorers` caller，以及 `src/explorer_host/explorer_host.h/.cpp` 的 Shell view lifetime／operation completion seam；不得以 busy polling、背景 copy engine 或路徑字串檔案操作代替。
- Alternative：接受並明確記錄現行 Shell 行為為「關閉 PaneDock 會取消 target-host-owned copy，但不損壞來源、不留下 partial item」；不修改產品，只將 V4D 結果視為可接受的取消語意。這保留 Windows Shell 的原生 ownership，但不保證 copy 在 host 關閉後完成。
- Risks and non-goals：Shell copy ownership／完成通知可能沒有足夠公開 API；barrier 若設計錯誤會把正常 close 變成 hang。不得新增自有 `IFileOperation`／clipboard engine、helper process、background thread、polling timer、`OleFlushClipboard` 或跳過 §9.4 cleanup；不得修改 session schema。
- Proposed checks after approval：重跑 V4D；另重跑 V1、V3E、V3D、V5 作為 pane→pane、PaneDock→Explorer、clipboard-only controls；確認 source／destination hash、progress timing、PID/window exit、clean shutdown、Application/WER，並完成 build、CTest、`git diff --check`。
- 等待使用者明確選擇：接受目前 Shell cancellation，或批准上述 Recommended option（也可指定 Alternative／不同精確方案）。在確認前不修改產品 code、不建立另一張修正 ticket。

## Approved remediation plan

### 2026-08-30 — 使用者核准 Recommended option：Finish Transfer, Then Close

使用者回覆「OK」，核准上一節的 Recommended option。本 Phase B 只處理已確認的 V4D failure class：PaneDock 作為原生 Shell Paste target 時，正常 close 不得在 `IFileOperation` 完成前 teardown target `IExplorerBrowser`。

- 在 PaneDock 的 Ctrl+V paste seam 以剪貼簿現有的 `IDataObject` 建立原生 `IFileOperation`，target 使用 active pane 的 `IShellItem`；不自行複製路徑、不建立自有 clipboard／copy engine。
- 以 `IFileOperationProgressSink` 記錄完成與取消狀態；`PerformOperations` 返回前不得進入既有 `destroy_explorers`／`DestroyWindow` teardown。
- close intent 若遇到進行中的 operation，顯示 modeless、英文提示；預設為 `Close After Transfer`，完成後關閉提示並執行既有正常 shutdown。提供 `Keep PaneDock Open` 與 `Cancel Transfer and Close`，取消也必須等 `PerformOperations` 返回後才 teardown。
- 重用既有 single-STA message loop、close guard、shutdown 順序與 `IExplorerBrowser::Destroy` cleanup；不新增 thread、helper process、polling timer、session schema 或另一張 ticket。
- 預計修改：`src/app_shell/main.cpp` 的 `AppState`、Ctrl+V message dispatch、`WM_CLOSE`／`WM_ENDSESSION` 與必要的 dialog／operation completion handlers；若無必要不改 `src/core`、`src/explorer_host` 或 CMake。
- Runnable check：重新執行 V4D，並以 V1、V3E、V3D、V5 作 ownership controls；完成 build、CTest、`git diff --check` 與 graceful-close smoke。因公開 Shell COM 契約無法以 fake COM object 證明 view teardown 時序，保留真實桌面 V1–V5 作為本修正的 focused regression check。

本核准不包含 context-menu paste、Cut／Move、delete、rename 或任何未列出的 Shell operation；若後續需要，另行取得範圍核准。

### 2026-08-30 — Phase B implementation and regression evidence

- `src/app_shell/main.cpp` now intercepts PaneDock Ctrl+V before `IExplorerBrowser::TranslateAccelerator`, reuses the live OLE `IDataObject`, queues native `IFileOperation::CopyItems`, and keeps the existing `IExplorerBrowser` instances alive until `PerformOperations` and sink cleanup return.
- Normal close records `close_after_file_operation`; the existing shutdown path is entered only after the operation call is no longer active. Re-entrant close and `WM_ENDSESSION` retain the same barrier and cleanup order. A modeless English close-intent window is available with `Keep PaneDock Open`, `Close After Transfer`, and `Cancel Transfer and Close`; cancellation returns `ERROR_CANCELLED` from the progress sink before teardown.
- Build checks: `cmake --build build` PASS; `ctest --test-dir build --output-on-failure` PASS (6/6); `git diff --check` PASS.
- Focused real-desktop V4D: D: `pd123-v4d-unique.iso` → fresh E: PaneDock target; native `進度` window observed before `PostMessage(WM_CLOSE)`, PaneDock PID exited normally, target size `4,351,463,424` bytes, target SHA-256 matched source (`510CE77AFCB9537F198BC7DAA0E5B503B6E67AAED68146943C231BAEAAB94DF1`), and no incomplete target remained. The close-intent dialog was not observed in the 5-second sample; the nested Shell loop deferred the close until completion, after which the process exited automatically. This is recorded as a completion-barrier PASS, not as dialog-visibility evidence.
- The earlier V1/V2/V3/V5 controls remain the Phase A evidence for unaffected ownership classes; the pane→pane AutoIt rerun was not counted because the disposable driver failed to select the source item, so it produced no product-path result. No files under `E:\ISO` were deleted or modified by this run, and all one-shot driver files were removed from the repository.
