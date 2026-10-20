# PD-236 — 自建選取接回剪貼簿與 Shell 檔案操作

Phase 8 · managed list migration

## 來源與交付

2026-10-16 使用者回報偶發 tab 列表刷新延遲，明確決定自行管理列表，並確認 21 個切片的粒度與依賴。
功能來源：[自建列表規格](../research/2026-10-16-managed-list-spec.md)。本票的狀態與阻擋關係以 Ticket 總覽為唯一來源。

**What to build：**新列表的 Copy／Cut／Paste／Delete 對正確選取作用，保留 Windows 進度、衝突處理與跨 Group 剪貼簿。

## Overrides

只替換 selection source，不覆寫 FR-007 的 Shell 操作引擎。

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

file_operations 全部 public callers、IDataObject／IFileOperation effect 回報、Pane selection／location、coordinator paste／跨 Group routing、Shell reentry guards。

追查上列 shared functions 的所有 caller，涵蓋滑鼠、鍵盤、restore、deferred replay 與 shutdown；改動前核對兩份 ADR 及來源規格。本系列內容模組的新增入口可能由前票建立，應沿其實際實作閱讀，不為滿足票面名稱另建一套模組。

## 範圍

- 以當次 selection／folder identity snapshot 取得 IDataObject／Shell operation targets，不能靠 row position 或 live view 的舊選取。
- 接通 keyboard／toolbar 現有命令路由與 clipboard preferred/performed effect，背景點擊／active pane 語意維持。
- 跨泵點與執行前重新驗證目標，處理虛擬／已移除檔案／不同 parent selection，不能偷偷改直接 filesystem 操作。

## Acceptance criteria

- [ ] Copy/Cut/Paste/Delete 在本機與支援的 namespace 正確作用；跨 Group 的 clipboard data 不因來源切換而失效。
- [ ] 同／跨 volume、衝突／取消／部分失敗保留 Windows UI 與正確 move/copy effect。
- [ ] 操作中切 tab／Group 不刪錯位置，沒有 COM lifetime／shutdown 回歸。

## Testing decisions

沿用 file_operation_effect／Pane command 測試，在 disposable files、兩個 volume 與真實 clipboard 做整合驗證。

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

## Automated delivery checks（2026-10-20）

本次依使用者指示只交付 selection → clipboard／Shell file-operation command wiring；不修改右鍵／native menu、inline rename 或 drag/drop。正常啟動仍使用 legacy view。沒有現成 Copy／Cut／Delete toolbar command ID，因此不新增 toolbar 或選單；managed-list 鍵盤 Ctrl+C／Ctrl+X／Ctrl+V／Delete 接入同一 coordinator operation gate，既有 Ctrl+V 路由也共用此入口。

- [x] 新增並註冊 `panedock_managed_list_file_commands`，透過公開 Pane／PaneHost 與 controlled Source、hidden native list 驗證 owned absolute target snapshots。本機使用穩定 leaf key，虛擬列使用 absolute key，display text、fallback、row ordinal 不替代 identity；同名、不同 parent、無 navigable child 的列均保留各自目標。
- [x] Copy／Cut 使用相同選取 targets；Paste 固定目前 pane folder，包含右 pane 的完整 virtual ShellLocation，不使用選取子資料夾或左 pane 目標。地址列／其他 HWND／modifier chords 不被新命令路由攔截。
- [x] Group／tab／generation／unbind／shutdown 使舊 snapshot 失效；失效請求傳入真實 file_operations 公開入口時，setup callback 回 `E_ABORT`，在 Shell parsing／clipboard／DeleteItems 前拒收。測試未執行真實檔案刪除或 clipboard 修改。
- [x] `file_operation_effect` 改用 Release 下仍生效的 `EXPECT`（原 `assert` 在 NDEBUG 下不執行）。Copy／Cut preferred effect 仍決定 copy／move；成功才產生 performed effect，cancel／partial-abort／failure／未 handled／unsupported 不回報完整成功。Move 仍經原 `CFSTR_PASTESUCCEEDED` 回報路徑。
- [x] coordinator 以 owned binding snapshot 在每個 setup pump 後重驗 active pane、foreground window、Group／tab／generation／folder／shutdown。實際 transfer 開始後使用既有 shutdown cancel／finish lifetime gate，固定已提交 targets，不重新讀取切換後的選取。`ShellCallScope` 持續涵蓋 COM object 釋放；沒有新增 ExplorerBrowser 或修改其 Destroy 路徑。

`IShellItemArray` 從每個 absolute PIDL 建立，再取得 Shell `IDataObject`，不假定共同 parent；任何 identity 解析失敗即拒收完整選取，不能靜默執行其餘項目。Copy／Cut 設定 preferred effect，`OleSetClipboard` 後 `OleFlushClipboard` materialize Shell formats，避免 clipboard 依賴活著的來源 Pane。Delete 共用既有 `IFileOperation` progress sink／shutdown callbacks，保留原生 UI 並要求 recycle／undo。上述是真實 API 接線，**並非真實 clipboard／Shell provider 整合 PASS**。

公開 snapshot／command 邏輯依賴 HWND selection、ShellLocation 與 PaneHost／reentry gate，無法放進禁止 Win32／COM 的 core；新增 test 使用既有公開 seam，不 fake IExplorerBrowser。既有 `shell_reentry_gate` 的 callback-name 行為掃描由上述可執行失效請求測試取代，其餘政策掃描保留。

## Agent checks 執行紀錄（2026-10-20）

以下為實際命令；只建置指定 Release targets，只執行以下 11 個相關 CTests，未執行 full suite、launch smoke、GUI 或真實破壞性操作：

```powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4 --target PaneDock panedock_managed_list_file_commands_test panedock_file_operation_effect_test panedock_managed_list_selection_test panedock_managed_list_namespace_test panedock_managed_list_restore_test panedock_pane_test panedock_core_shutdown_coordinator_test panedock_shell_reentry_guard_test
ctest --test-dir build --output-on-failure -R '^panedock_(file_operation_effect|managed_list_(file_commands|selection|namespace|restore)|pane|core_shutdown_coordinator|shell_reentry_guard|single_threaded|shutdown_state|shell_reentry_gate)$'
git diff --check
```

Configure exit 0。restricted build 無輸出，使用 Ctrl+C 中止（exit 1），改用 elevated context 執行相同 targets。前兩次 elevated build 分別發現 `FOF_ALLOWUNDO` 缺少 shellapi.h、effect test 啟用 Release assertions 後缺少 ole2.h；補上對應 headers 後最終 build exit 0。首次 focused CTest 10/11：只有舊 callback 名稱掃描失敗；移除被公開行為測試取代的該項掃描後，最終 CTest **11/11 PASS、exit 0**，total 3.25 s（測試執行時間，不是效能量測）。`git diff --check` exit 0。

## 交接區

- 未解風險：三項原始 integration acceptance criteria 仍未完整驗證，tracker 不因此標示 done。真實本機／Shell namespace／network／OneDrive Copy／Cut／Paste／Delete、clipboard formats materialization 與跨 Group／來源關閉後 data lifetime、同／跨 volume、conflict／cancel／partial failure 的原生 UI 與 preferred／performed／paste-succeeded effect，均「未驗證，需真實桌面」。不同 parent 的真實 Shell data object provider 支援亦待驗證；seam 只能證明傳入的 identity 沒有被改成同 parent 或顯示名稱。
- 未解風險：真實 foreground／active pane／背景點擊與操作泵點切 tab／Group／shutdown 的 native focus、COM lifetime／progress cancellation，以及卡住 provider 的反應時間尚未驗證。單元測試與政策掃描不構成這些項目的 PASS，也不構成 PD-241 compatibility／performance／idle gate 證據。使用者明確延後上述實機／GUI 驗證；本票不啟動應用程式或改寫使用者 session。
