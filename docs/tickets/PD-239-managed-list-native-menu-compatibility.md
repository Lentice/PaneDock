# PD-239 — 原生選單目標相容性接線

Phase 8 · managed list migration

## 來源與交付

2026-10-16 使用者回報偶發 tab 列表刷新延遲，明確決定自行管理列表，並確認 21 個切片的粒度與依賴。
功能來源：[自建列表規格](../research/2026-10-16-managed-list-spec.md)。本票的狀態與阻擋關係以 Ticket 總覽為唯一來源。

**What to build：**替換 Shell view 後，使用者仍可對目前選取或資料夾背景開啟原生 Windows 選單，功能及呈現策略維持。

## Overrides

使用者明確排除本次選單改造；本票僅避免移除 IShellView 造成既有選單消失，不將先前基本選單建議當需求。

## 綁定限制（引用與適用方式）

- design-spec NFR-003：「Group 切換、版型切換、tab 切換不得因為某個 Shell location 緩慢或無法連線而凍結 UI。」
- design-spec FR-007：「複製、移動、刪除、重新命名經由 Shell IFileOperation,含原生進度對話框與衝突提示。剪貼簿操作經由 Shell IDataObject。」
- design-spec NFR-005：「必要狀態(版型、location、tab、view mode、排序)必須精確還原」。跨重啟 selection／scroll／column widths 仍是 best-effort；本系列要求操作中的 identity／anchor 穩定，不擅自升級所有持久化保證。
- development：「A pane-only feature is implemented in Pane or one of its sub-modules, not in the coordinator.」跨 pane 協調、存檔與 shutdown 仍由 coordinator 掌管。
- development：「Use Microsoft::WRL::ComPtr for every interface pointer.」「Never destroy a parent HWND while a hosted view is alive.」遷移期間所有 initialized IExplorerBrowser 仍先 Destroy。
- AGENTS：「Keep src/core free of HWND, COM and windows.h.」「Event-driven idle path only. No busy loops, no polling timers.」「New non-trivial logic needs one focused runnable test or self-check.」
- AGENTS：「Never persist a PIDL or a COM pointer.」既有 session schema version、未知欄位保存及 atomic replace／backup 契約不變。
- 使用者最新授權覆寫舊「不重刻列表」前提，並要求 UX → CPU／memory／power → 架構／可維護性。背景工作只限新列表所需；UI STA 與既有 Shell re-entry 驗證仍保留，不以跨 thread 存取 UI／COM 繞過慢呼叫。
- 使用者明確要求本次不處理右鍵選單；只允許替換列表必要的原生相容性接線。這不授權 custom menu／filter／timeout／process isolation。
- 不新增 telemetry、服務、driver、第三方 runtime 或管理員需求；現有 namespace／network location 支援不等於新增應用程式網路功能。

## 閱讀與追查入口

ExplorerHost show_folder_context_menu_at／handle_context_menu_message、IShellView GetItemObject 的舊依賴、Pane selection snapshot、WM_CONTEXTMENU／keyboard routing、folder footer entry。

追查上列 shared functions 的所有 caller，涵蓋滑鼠、鍵盤、restore、deferred replay 與 shutdown；改動前核對兩份 ADR 及來源規格。本系列內容模組的新增入口可能由前票建立，應沿其實際實作閱讀，不為滿足票面名稱另建一套模組。

## Commander 參考提示

- **來源事實**：[Double Commander 官方 Basic Help](https://doublecmd.github.io/doc/en/help.html)說明 Windows 使用系統選單；[OneCommander 作者](https://groups.google.com/g/onecommander/c/9YHPT1BxcAQ)說明隱藏原生項目仍可能付出載入成本，歷史自有基本選單也有使用者不接受的功能取捨。
- **本票提示**：將列表選取目標與原生 menu 建立／執行拆開，保留日後替換的整合位置；相容性檢查只確認目前選取、背景、keyboard／mouse 目標與 verbs 正確。
- **範圍護欄**：參考這些案例是為避免把列表重寫誤報成 menu 加速；不因此新增自有 menu、藏項目、封鎖 extension、timeout 或 helper process。

上述為第一手文件／作者回覆所記錄的做法與本票的工程建議，未經 PaneDock 效能實測。完整來源及未確認事項見[Commander 研究](../research/2026-10-16-commander-performance.md)。本節不增加功能、阻擋關係或額外驗收門檻。

## 範圍

- 將 selection／background identity snapshot 接回原生 Shell menu acquisition，mouse／keyboard／footer background 三入口共用正確目標契約。
- menu COM／HMENU／IContextMenu2/3 message forwarding／verb invocation lifetime 留在 Shell 整合，不滲入列表 model、sort 或 cache。
- 只做必要接線，不新增基本 menu、loading 策略、filter、timeout、helper process、封鎖名單或 extension 政策。

## Acceptance criteria

- [ ] 滑鼠選取、keyboard menu key／Shift+F10、空白處／footer 都對正確 items 或 background 開啟原生 menu。
- [ ] menu modal pump 中切換／關閉後不使用 stale view／selection，command target 不被新列表行位置改寫。
- [ ] 列表可在不建立 menu COM 的情況測試與載入；原生 verbs 與背景選單行為有實機對照，不承諾改善延遲。

## Testing decisions

Pane seam 只測 target snapshot／request routing，真實 Shell 比較目前功能；verbs 如開啟外部 app 須遵守既有桌面安全流程。

## Non-goals

不實作相鄰切片、不改右鍵選單產品策略、不加入程序隔離／plugin framework、不作破壞性 session migration、不改寫任何 completed ticket。開發路徑在正式 gate 前不替換既有正常預設；切預設與 contract 僅由對應票執行。

## Agent checks 與交付

沿用 LLVM-MinGW Clang／LLD，不尋找 MSVC。若工具未在 PATH，於當次 shell 暫加 E:\Dev\LLVM-MinGW\bin 與 E:\Dev\Ninja。

~~~powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
~~~

- 新行為測試須進入 CTest；可量測 harness 可作獨立 target，但必須寫明 build／執行命令與輸入，不能只交一份人工敘述。
- 從 Pane 公開操作／可觀察列表狀態測試，沿用 PaneHost；純 core 測試作補充。不要 fake IExplorerBrowser、斷言 private call sequence 或用 source scan 冒充新行為測試。
- Shell／網路／OneDrive／真正焦點與 accessibility 需要實機驗證；無環境時明記「未驗證,需真實桌面」，不得由 unit test 推論 PASS。launch smoke 使用隔離 session，不讀寫使用者真實 session。
- 僅在確實存在未解風險或不可重推結論時新增交接區；測量數字放 performance baseline，已否決方向放 tracker。不要附空白交接模板或把 diff 摘要當交接。
- 這是半天至兩天目標的單一切片；若實作追查證明超出，先在 tracker 拆分，不擴張到相鄰票。實作前重讀依賴交付的公開入口與 caller，不假設尚未建立的類型已存在。


## Checkpoint 驗證（2026-10-20）

本次保留 Windows Shell 原生 `IContextMenu`；選單內容由 Shell 建立，不新增自訂 UI、過濾、隱藏、重排或樣式。Pane 負責選取／背景目標快照與 mouse、Menu key、Shift+F10、footer routing；Shell 整合負責 acquisition、HMENU／COM lifetime、`IContextMenu2/3` forwarding 及 verb invocation。legacy background entry 共用選單執行邊界，仍透過 live view 的 `GetItemObject(SVGIO_BACKGROUND)` 取得選單。

目標契約：目前 folder 解析本機 leaf key 或 Shell parsing identity，不使用顯示名稱。`ParseDisplayName` 的結果必須是相對於該 folder、只有一個 `SHITEMID` 的 child PIDL，才可交給 `GetUIObjectOf`；解析失敗或得到多節 PIDL 時拒絕整份 selection，不縮減成部分選取。依據：[ParseDisplayName](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-parsedisplayname)、[GetUIObjectOf](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellfolder-getuiobjectof)。

測試透過 Pane 公開操作與 PaneHost、受控值型列表來源及選單 presenter seam，使用隱藏 common controls，不建立真實選單 COM。此邏輯涉及 Win32 ListView selection／hit testing 與 Shell integration，不能放入純 core。測項涵蓋 mouse 多選／未選取列、Menu key／Shift+F10、空白處／footer background、顯示名稱與 identity 分離，以及 modal 期間改選取／排序後保持原目標，切 tab／導覽／shutdown 後目標失效。

最終 focused Release 命令與結果（依使用者要求，不執行完整 CTest 或 GUI）：

~~~powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake -S . -B build -G Ninja -D'CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake' -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2 --target PaneDock panedock_managed_list_context_menu_test panedock_managed_list_selection_test panedock_managed_list_file_commands_test panedock_pane_test panedock_deferred_messages_test panedock_shell_reentry_guard_test
ctest --test-dir build -R '^panedock_(managed_list_context_menu|managed_list_selection|managed_list_file_commands|pane|deferred_messages|shell_reentry_guard|shell_reentry_gate|single_threaded)$' --output-on-failure
git diff --check
~~~

Release configure／build 成功；指定的 8 個 CTests 全部通過（2.77 秒）。其中 `shell_reentry_gate` 與 `single_threaded` 是政策檢查，不作為原生選單行為證據。`git diff --check` 通過。

## Post-PD-242 focused checkpoint（2026-10-21）

在 `5ad7d14`、`4ba2e0a`、`a81cbe4` 與 `b3cbc46` 後重新追查 mouse／Menu key／Shift+F10／footer routing、owned identity snapshot、原生 menu acquisition／message forwarding／verb invocation，以及 deferred replay／STA re-entry／shutdown 邊界；未發現剩餘的 PD-239 自動化接線缺口。Windows 原生 `IContextMenu` 路徑保留，不新增 custom menu。前一 checkpoint 的 legacy `GetItemObject(SVGIO_BACKGROUND)` 描述僅屬移除前紀錄；目前背景入口共用 managed-list snapshot，經目前 folder 的 `IShellFolder::CreateViewObject` 取得原生選單。

重跑上節精確指定的 Release configure、7 個 build targets 與 8 項 focused CTests，命令皆自然完成：configure／build exit 0（build：`ninja: no work to do`），CTest **8/8 PASS、exit 0**；`git diff --check` exit 0。政策檢查與 presenter seam 不作為真實原生選單／provider 相容性證據。未執行完整 CTest、GUI、launch smoke、真實 Shell-menu interaction、OneDrive、network 或 provider tests；既有使用者 WIP 保留。

剩餘缺口為下列真實選單／桌面／provider evidence，acceptance criteria 保持未勾選，追蹤表維持原值。依 sequencing-only waiver，下一張可繼續核對剩餘工作的票為 PD-242（PD-240 已完成、PD-241 為排序豁免）；不代表本票或 rollout gate 通過實機驗收。

## 交接區

- 只以一個 disposable 本機 `.txt` 驗證原生 `Properties` verb 指向的檔名與 folder；其他原生 verbs、folder background menu（含需要 view site 的 verbs）、installed shell extensions、legacy／managed 桌面對照、mixed-DPI keyboard anchor 均未驗證，需真實桌面。Pane seam PASS 不代表上述 Shell 相容性 PASS，也不代表選單延遲改善。
- namespace provider 是否接受目前的 parsing identity 並回傳單節 child PIDL，尤其 Desktop／This PC／虛擬 location，以及 OneDrive／網路環境的選單行為，仍需實機確認；不能由受控 namespace row 推論支援已通過。

## 實機自動化嘗試（2026-10-21，未驗證）

Windows 11 Pro 10.0.26300；`build\PaneDock.exe`（2026-10-21 08:47:31 建置）；隔離 `LOCALAPPDATA=build\manual-test\la-pd239`（絕對路徑）；單一實例，正常關閉且無殘留行程；拋棄式資料夾 `build\manual-test\menu`（`m1.txt`～`m3.txt`，操作後內容不變）。

前置條件僅部分成立：`LVM_HITTEST` 命中 item 0（`flags=0x4`），空白處座標命中無 item（`iItem=-1`）。但以 `SendInput`（絕對座標）對 item 右鍵後選取數仍為 0，焦點留在地址列（截圖中地址文字為選取狀態）；對該狀態送 Shift+F10、Menu key 與空白處右鍵，PaneDock 行程下均未出現選單視窗，也未在截圖中看到選單。因為選取與焦點沒有進入 list，這些結果不能判斷原生選單是否正確，也不是產品失敗。

在該輪嘗試中，滑鼠選取／Shift+F10／Menu key／空白處／footer 開啟的 target 正確性、modal pump 中切換或關閉後的 stale 防護、與 File Explorer 原生 verbs 的對照未驗證，需真實桌面；未執行任何會改檔案的 verb。截圖在 `build\manual-test\pd239-*.png`（未納入版控）。後續局部驗證見下方 checkpoint。

## Claude managed-list native-menu checks（2026-10-21；限定相容性樣本）

Claude 使用修正版隔離 Release binary `build\manual-test\pd242trace\final2\PaneDock-fixed.exe`，每次以新的絕對 `LOCALAPPDATA`、新建測試檔及單一 PaneDock 實例驗證；全部正常關閉，結束後 PaneDock 程序數為 0，未叫用會改檔案的 verb：

- 選取列滑鼠右鍵：先以真實左鍵證明 list focus、選取數 1、focused item 0；右鍵後觀察到同 PID 的可見 `#32768` popup。檔案 SHA256 前後相同。
- Shift+F10：同樣證明 list focus／單列選取；按鍵前沒有 `#32768`，按後出現同 PID 可見選單，Escape 後重新列舉為無可見選單。測試檔 hash 不變。
- Windows Menu key（`VK_APPS`）：與 Shift+F10 相同，按後同 PID 可見 `#32768`，Escape 後消失；測試檔 hash 不變。
- 資料夾背景右鍵：`WindowFromPoint` 確認點位在 pane list，`LVM_HITTEST` 得 `iItem=-1`／`LVHT_NOWHERE`；右鍵後同 PID 可見 `#32768`，Escape 後消失。新建測試檔 hash 不變。
- Footer 入口：狀態列的 “1 items” 是被動 Static，未對它右鍵；實際互動目標是可見／啟用的 “Folder context menu” Button。`WindowFromPoint` 確認點中該按鈕後，同 PID 出現可見 `#32768`，popup 底邊對齊按鈕上緣；Escape 後重新列舉為無可見 menu。測試檔 hash 不變。

截圖／原始輸出分別位於 ignored `build\manual-test\pd239f`、`pd239g`、`pd239h`、`pd239i`、`pd239j`。此為選單取得與取消的局部桌面證據，不代表選單 verbs／內容對照或 ticket acceptance 通過；截圖中的 menu item 未逐項檢視。selection／Group／tab 在 modal menu pump 中變更後的 stale-target 防護、Shell provider 與 File Explorer 對照仍未驗證；總覽狀態及原始 acceptance criteria 維持不變。

## Claude native Properties target check（2026-10-21；限定本機單檔）

Claude 使用目前 `build\PaneDock.exe`、fresh absolute `LOCALAPPDATA`、一個新建 disposable `.txt` 及單一 PaneDock instance。修正一次腳本字串比對（未執行 verb）後，選取列的原生 Shell menu 顯示「內容(&R)」；只叫用此 Properties verb，新增的 `#32770` 對話框標題、檔名欄位與位置欄位均指向同一個測試檔。關閉新開的 Properties 視窗後檔案仍存在且 SHA-256 未變；PaneDock 正常關閉，無殘留程序。未驗證 canonical verb 名稱、多選、namespace／network／OneDrive 或 modal 期間切換／關閉。

Binary SHA-256：`9C585990E8DB931A84122C810F6D24FD35A02914EF88454A70F1933EDEC039AD`。run log／screenshots 位於 `build\manual-test\pd239k\run-20261021-120610`；第一個腳本比對失敗 run 在叫用任何項目前按 Escape 停止，檔案未變，第二次修正後執行為上述有效結果。

## 隔離原生選單互動複驗（2026-10-21；部分驗證）

Claude 使用 Release `build\PaneDock.exe`（SHA-256 `9C585990E8DB931A84122C810F6D24FD35A02914EF88454A70F1933EDEC039AD`）、單一 PaneDock process 與新 isolated `LOCALAPPDATA`，對唯一本機 `.txt` 測試檔執行。重跑前確認 row hit-test 命中列表，真實左鍵後 selected count 1、focused row 0、焦點在 `SysListView32`。`Shift+F10` 出現同 PID `#32768` 原生 Shell 選單（19 項）；只點選文字「內容(&R)」，新 `#32770` Properties 視窗的 filename／location 欄位都吻合該測試檔與資料夾。Hash 前後相同。這只驗證本機單檔 Properties；沒有驗證 canonical verb 名稱。

同一列表可見空白處的 `LVM_HITTEST` 為 `iItem=-1`，右鍵開啟同 PID 背景 `#32768` 選單（11 項），Escape 收起且沒有執行 verb。背景選單打開時第一次點另一 pane 的 tab pill 只關閉選單，PaneDock ping 0ms／`hung=False`，active pane 不變；第二次點擊才切換到 pane 1。沒有 command 被呼叫，測試檔仍存在且 hash 未變。PaneDock 正常關閉約 135ms 後 process count 為 0；只刪除本次新建的 `data` 資料夾，logs／screenshots 保留在 ignored `build\manual-test\pd239m\run-261021143933\`。此前兩輪腳本在選單顯示後、執行任何項目前因 HMENU 參數解析錯誤停止；修正後第三輪為上述結果。

此複驗覆蓋單選檔案 Shift+F10、背景右鍵選單與一次 tab-pills dismiss 行為；canonical verb、footer menu、modal pump 中切 Group／tab 後 command target、selection 改變、namespace／OneDrive／network、多選及其他 verbs 仍未驗證。三項 acceptance 不全，PD-239 與 tracker 維持 `in_progress`。

## Claude modal selection snapshot check（2026-10-21；單一本機情境）

Claude 使用 Release `build\PaneDock.exe`（SHA-256 `E630EAC2325DFBE6896BC05B49D41B72887D2E95E3F8C4605398320694E23586`），確認啟動前 PaneDock process count 為 0；使用單一 process、隔離 `LOCALAPPDATA=build\manual-test\pd239-modal-target\run-261021154506\la`，以及兩個新建的 33-byte marker files。列表只載入該測試資料夾，row 0 是 A、row 1 是 B。對 A 做真實左鍵選取並確認 selected count 1、focused row 0、focus 在 list，接著以 Shift+F10 開啟同 PID 原生 `#32768` 選單。

選單仍顯示期間，Claude 由外部 harness 對底層 ListView 發送 `LVM_SETITEMSTATE`：先選取／focus B，再清除 A 的 selected state。回讀確認底層唯一選取項已是 B；原生 menu 仍開啟，`WM_NULL` UI ping 為 0 ms、`hung=False`。只叫用 Properties；新增 `#32770` 視窗的標題與 filename 欄位均為 A，location 是測試資料夾，非目前選取的 B。兩個檔案均未改動：測試開始 SHA-256 分別為 A `3E67A14B87C1C83EF051FE12F5030B71A422D15FBBB6D401031CEB676E7163A6`、B `602F73E053CE1150AB288B13EA82B732FF9726F5FF8EE547771A0D54FA9104E8`；結束後獨立重算仍相同。記錄腳本的 `hash unchanged: False` 是 PowerShell 變數名稱覆蓋錯誤，不能採作檔案變更證據。

只關閉本次 Properties 視窗與隔離 PaneDock；PaneDock 以 `WM_CLOSE` 約 145 ms 正常結束，process count 歸零。stderr 為空，正式 session metadata 未變；未讀寫 clipboard、未執行其他 verb、未刪除測試檔。原始輸出、腳本與截圖保留於 ignored `build\manual-test\pd239-modal-target\`，有效樣本為 `run-261021154506`。

這證明一次受控底層 selection 變更沒有改寫已開啟 menu 的 Properties target。selection 變更由 `LVM_SETITEMSTATE` 注入，尚未驗證使用者滑鼠／鍵盤在 modal menu 中的行為；切 tab／Group／關閉 pane 後的 stale target、其他 verbs、background／virtual／network／OneDrive provider 仍未驗證。Acceptance criteria 不全，總覽狀態維持 `in_progress`。
