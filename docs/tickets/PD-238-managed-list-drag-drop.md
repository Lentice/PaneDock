# PD-238 — 跨 pane 與外部 drag/drop

Phase 8 · managed list migration

## 來源與交付

2026-10-16 使用者回報偶發 tab 列表刷新延遲，明確決定自行管理列表，並確認 21 個切片的粒度與依賴。
功能來源：[自建列表規格](../research/2026-10-16-managed-list-spec.md)。本票的狀態與阻擋關係以 Ticket 總覽為唯一來源。

**What to build：**自建列表可在 pane 間和外部應用程式拖放，維持 modifier、hover 導覽、取消與目標語意。

## Overrides

保留 FR-008 Shell／OLE 語意，無自製 transfer engine 或新 polling。

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

IDataObject／file_operations、OLE drop targets、coordinator cross-pane hover／tab drag、Pane selection／current folder、ShellReentryGuard／shutdown。

追查上列 shared functions 的所有 caller，涵蓋滑鼠、鍵盤、restore、deferred replay 與 shutdown；改動前核對兩份 ADR 及來源規格。本系列內容模組的新增入口可能由前票建立，應沿其實際實作閱讀，不為滿足票面名稱另建一套模組。

## 範圍

- 來源以 selection snapshot 提供 IDataObject，目的以 drop 當下 pane／folder identity 接 Shell；shift／ctrl 的 copy/move effect 不自行猜 path。
- 保留跨 tab／Group hover 及取消 threshold 語意；hover 後新內容 pending 時不把 drop 落到舊 folder。
- 重入／shutdown／資料來源壽命與 effect 回報一併處理，不改 tab 自身拖曳產品互動。

## Acceptance criteria

- [ ] 跨 pane、跨 volume、外部拖入／拖出符合原生 effect，取消不產生操作。
- [ ] hover 切 Group／tab 後 drop 作用於目前目的地；尚未可接受 drop 時有明確拒絕行為。
- [ ] 快速重入／切換／關閉後無 stuck drag、錯目標或已釋放 IDataObject 存取。

## Testing decisions

純 effect／target state 行為測試加真實 OLE／Explorer 和另一 app；不能把滑鼠事件模擬當檔案 transfer PASS。

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

## Checkpoint：實作邊界與限定驗證

來源經 Pane 的 selection snapshot 固定 owned parsing identities，重用既有 Shell item array → IDataObject 建立流程，再交給 OLE DoDragDrop。資料建立期間逐次檢查 binding；進入 OLE 後允許既有 hover 切 Group／tab，selection IDataObject 保持存活。Escape、來源視窗失效或 shutdown 取消來源拖曳；取消不回報完成 effect，也不依回傳 effect 自行刪來源。

Pane 子模組持有列表 HWND 的 OLE target，將目前 pane folder 的 Shell IDropTarget 接到 DragEnter／DragOver／Drop／DragLeave。modifier、screen point 與來源允許的 effect 原樣交 Shell，結果限制於來源允許值；不推測 volume 或自製 transfer。pending／error folder 拒絕 drop；binding 失效時先離開舊 target，再以目前已接受的 folder 建立新 target。Shell 解析／建立／callback 後重驗 identity；nested callback、外層 Shell-call 重入與 shutdown 拒絕操作。Drop 或取消會結束 session，撤銷先 detach Pane，再 RevokeDragDrop，保護 OLE 保留 target 的晚到回呼。

來源與目的的 drag lifetime 經 PaneHost 接既有 ShutdownCoordinator 計數；coordinator 原有 sidebar Group hover、tab hover 延遲／取消、deferred replay 與跨 pane tab drag 路由未改。OLE source loop 不持有整段 ShellCallScope，讓既有 hover 能運作；資料建立與 target provider 呼叫各自持有 gate。沒有新增 timer／polling，沒有右鍵或 tab 自身拖曳 UX 變更。

測試放在 Pane 公開 OLE 入口與 Shell resolver 邊界，因為 HWND、COM IDataObject／IDropTarget 壽命不能放入純 core。resolver double 只回報 effect／重入，不讀寫檔案，不是 Shell transfer 證據。共用 TestPaneHost 平衡 OleInitialize／OleUninitialize，讓既有列表測試也符合 RegisterDragDrop 的 STA 初始化需求。

2026-10-20，LLVM-MinGW Release：

~~~powershell
$env:PATH = "E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;" + $env:PATH
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2 --target PaneDock panedock_managed_list_drag_drop_test panedock_managed_list_file_commands_test panedock_file_operation_effect_test panedock_shell_reentry_guard_test panedock_core_shutdown_test panedock_core_shutdown_coordinator_test
ctest --test-dir build --output-on-failure -R "^panedock_(managed_list_drag_drop|managed_list_file_commands|file_operation_effect|shell_reentry_guard|core_shutdown|core_shutdown_coordinator|single_threaded|shell_core_boundary|shell_reentry_gate)$"
git diff --check
~~~

Configure、限定 targets build、上述 9 項 CTests 與 diff check 通過。新測試涵蓋 effect／modifier／point 轉送與允許值遮罩、pending 轉 ready、tab hover 使用的公開切換入口、Drop 當下重驗、resolver／provider 重入、外層 Shell-call、shutdown、error、取消與 target 比 PaneHost 更長壽時的安全回呼及 IDataObject 釋放。依使用者指示未跑完整 suite、GUI、launch smoke 或任何真實檔案 transfer。

原生介面契約參考：[IDropTarget::DragEnter](https://learn.microsoft.com/en-us/windows/win32/api/oleidl/nf-oleidl-idroptarget-dragenter)、[IShellItem::BindToHandler](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellitem-bindtohandler)。folder 背景 target 使用 folder 的 IShellFolder::CreateViewObject，沒有替換原生 menu／file operation 策略。

## Post-PD-242 focused checkpoint（2026-10-21）

在 `5ad7d14`、`4ba2e0a` 與 `a81cbe4` 後重新追查 selection snapshot → Shell `IDataObject` → OLE source、目前 folder 的 `IDropTarget`、Group／tab hover，以及 STA re-entry／shutdown／revoke lifetime。既有實作已提交，未發現需補作的 PD-238 自動化接線缺口；維持 Shell transfer／`IFileOperation` 邊界，不新增自製 transfer 或 context menu。

重跑上節精確指定的 Release configure、7 個 build targets 與 9 項 focused CTests，命令皆自然完成：configure／build exit 0（build：`ninja: no work to do`），CTest **9/9 PASS、exit 0**，total 2.45 s（測試時間，非效能證據）；`git diff --check` exit 0。未執行完整 CTest、GUI、launch smoke、真實外部拖放或 Shell／provider／network／OneDrive 整合測試；既有使用者 WIP 保留。

剩餘缺口僅為下列真實桌面／provider integration evidence；三項 acceptance criteria 保持未勾選，追蹤表維持原值。依既有 sequencing-only waiver，下一張可繼續核對剩餘工作的票為 PD-239（原生選單目標相容性接線），不代表本票或 PD-241 通過實機驗收。

## 交接區

- 依使用者明確指示，本環境刻意跳過真實檔案傳輸及桌面驗證。跨 pane／volume、Explorer 與另一 app 的外部拖入拖出、Ctrl／Shift／link、Escape／threshold、Group／tab hover 後立即 drop、provider progress 中關閉／取消、virtual namespace 與 OneDrive 語意皆**未驗證，需真實桌面**；本次測試不能宣告本票 acceptance 或 PD-241 drag/drop gate 通過。
- 真實 Shell target 建立與拖放 provider 呼叫仍在 UI STA；慢 provider 的延遲、progress／衝突提示與實際傳輸壽命尚未量測。fake resolver 的 effect／reentry 測試不保證原生 provider 相容性或 NFR-003 通過。

## 實機自動化診斷（2026-10-21，未驗證；是否為產品缺陷待判斷）

Windows 11 Pro 10.0.26300；`build\PaneDock.exe`（2026-10-21 08:47:31 建置）；隔離 `LOCALAPPDATA=build\manual-test\la-pd238d`（絕對路徑，正式 session 未被修改）；單一實例，正常關閉且無殘留行程。拋棄式資料：`build\manual-test\dnd\A`（`d1.txt`～`d3.txt`）與空的 `dnd\B`，pane 0／pane 1 各顯示其一。

前置條件皆成立：拖曳來源座標以 `LVM_HITTEST` 命中 pane 0 的 item 0（`d1.txt`，`flags=0x4`），選取數為 1；來源與目的座標的 `WindowFromPoint` 分別等於 pane 0 與 pane 1 的 `SysListView32`；PaneDock 主視窗為前景。以 `SendInput`（絕對座標、virtual-desktop）送 `move`／`down`／20 個 `move`／`up`，23 個事件每次回傳 1，游標落點與目標相差約 1 px。結果：拖曳後 A 仍為 `d1.txt,d2.txt,d3.txt`、B 仍為空，狀態列未變，未出現任何原生 UI 或額外視窗。

只能說明在此 harness 條件下沒有觀察到 drop；不能由此判定產品缺陷，也未涵蓋 `LVN_BEGINDRAG` 是否被觸發，需由另行 triage 判斷。同 pane、跨 volume、Explorer 拖入／拖出、取消、hover 切 Group／tab、快速重入全部維持未驗證，需真實桌面。早先一次使用相對路徑 `LOCALAPPDATA` 的 PD-238 嘗試誤用了正式 session，其結果作廢。
