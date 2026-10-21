# PD-242 — contract：移除舊 Shell view 路徑

Phase 8 · managed list migration

## 來源與交付

2026-10-16 使用者回報偶發 tab 列表刷新延遲，明確決定自行管理列表，並確認 21 個切片的粒度與依賴。
功能來源：[自建列表規格](../research/2026-10-16-managed-list-spec.md)。本票的狀態與阻擋關係以 Ticket 總覽為唯一來源。

**What to build：**依下列使用者授權的排序豁免，推進 managed-list-only code 並移除舊 view 路徑，留下清楚可維護的單一列表實作；不宣稱新列表已通過真實驗收。

## Overrides

2026-10-21 使用者明確授權覆寫：

> user authorized skipping real validation and proceeding through later tickets; the rollout measurement gate is waived for sequencing only, not passed.

此授權覆寫原先「contract 只在 rollout gate 完成後移除舊實作」及本票 Non-goals 中「正式 gate 前不替換既有正常預設」的排序限制，允許繼續 managed-list-only 實作。產品權威同步見 [design-spec Phase 8 override](../design-spec.md#phase-8-override2026-10-21)；狀態與阻擋關係只見 Ticket 總覽。

PD-241 所有未勾選驗收條件、未驗證風險、失敗與既有自動化證據均保留；這不是相容性／效能 PASS，不放寬功能、資源、有界安全退出或原生 menu／operations 契約，不改寫 completed tickets 的歷史內容。本次 waiver checkpoint 只改文件，不改實作，不執行真實測試、GUI、launch smoke 或完整測試套件；下列驗收條件及 checks 保留為後續工作要求，不能因排序豁免宣告通過。

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

所有 IExplorerBrowser／ExplorerHost／IShellView callers、realization plan、live view diagnostics、old accelerator routing、build links／tests／docs。

追查上列 shared functions 的所有 caller，涵蓋滑鼠、鍵盤、restore、deferred replay 與 shutdown；改動前核對兩份 ADR 及來源規格。本系列內容模組的新增入口可能由前票建立，應沿其實際實作閱讀，不為滿足票面名稱另建一套模組。

## 範圍

- 先證明無必要 caller／fallback，移除舊 view 初始化／事件／Destroy／retry 路徑與僅供它的 build targets；每步 compile 維持綠。
- 保留仍服務原生 menu、namespace、file operations、OLE 的 Shell 能力，不能按 explorer_host 名稱整包誤刪。
- 替換僅驗證舊 view 數量的 diagnostics／tests 為新列表 lifetime／capacity 契約；有價值行為測試不能因名稱舊就刪除，文件與 glossary 同步。

## Acceptance criteria

- [ ] repo／binary 不再需要 IExplorerBrowser view lifetime 舊路徑，無 dangling direct host access。
- [ ] 正常啟動、恢復、四 pane 切換、native menu／operations 與關閉通過；core 仍無 COM／HWND。
- [ ] build／完整 CTest 與資源 lifetime 檢查通過，無未使用 target／runtime dependency；未作 destructive cleanup 或 session migration。

## Testing decisions

caller inventory、完整 build／CTest、實機啟動／關閉與 Shell 功能 smoke，檢查實際 binary link 相依。

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

## 交接區

- `panedock_managed_list` now uses a hidden parent HWND and Pane window, matching the managed-list realization contract. Its focused CTest passes; the test skips only the real ACL-denial branch when setting the disposable directory DACL returns `ERROR_ACCESS_DENIED`. The controlled-source permission-denied seam remains covered, but real filesystem ACL denial is unverified in this environment.
- `IExplorerBrowser`／`IShellView` view hosting has been removed while the native Shell context-menu target remains in `panedock_shell_menu`. Native menu／operation behavior and managed-list compatibility still require real desktop/provider validation; automated seam tests do not establish that evidence.
- Full CTest, GUI／launch smoke, real Shell namespace／network／OneDrive checks, resource lifetime measurements, idle power checks, and bounded shutdown against a stuck provider remain unverified. Keep this ticket `in_progress` until its required evidence is supplied; the PD-241 sequencing waiver is not a pass.

## 隔離 launch smoke checkpoint（2026-10-21）

僅為隔離 launch smoke／啟動關閉結果，不是相容性或 lifetime 驗收。Windows 11 Pro 10.0.26300；`build\PaneDock.exe`（Release，2026-10-21 08:47:31 建置）；執行前確認沒有 PaneDock 行程。

```powershell
ctest --test-dir build --output-on-failure -R '^panedock_launch_smoke$'
```

`launch_smoke.ps1` 設定 `LOCALAPPDATA=build\smoke-localappdata`（未碰使用者 session），以 `--diagnostic` 單次啟動，經主視窗正常關閉結束，未強制終止。兩次執行皆 PASS（1.35 秒、1.23 秒），輸出 `launch smoke test passed (live_list_count reached 0)`，exit code 0；隔離 `session.json` 寫入 `clean_shutdown: true`，預設 4 pane 皆為 This PC（`::{20D04FE0-3AEA-1069-A2D8-08002B30309D}`）；結束後無 PaneDock 行程殘留。

只支持「正常啟動／關閉、關閉時 managed-list 計數歸 0」。仍未驗證：session 恢復、四 pane 切換、native menu／operations、真實 Shell／network／OneDrive、idle／resource lifetime 量測、stuck provider 的 bounded shutdown、完整 CTest；全部 acceptance 維持未勾選，狀態維持 `in_progress`。

## 實機 idle 資源量測（2026-10-21，發現異常，待判斷）

Windows 11 Pro 10.0.26300；`build\PaneDock.exe`（2026-10-21 08:47:31 建置）；隔離 `LOCALAPPDATA`（`build\manual-test\la-pd242`、`la-pd242b`，絕對路徑）；各單一實例，無輸入，harness 僅取樣行程計數器；視窗位於第二螢幕，非前景。原始輸出：`build\manual-test\pd242.log`、`pd242b.log`（未納入版控）。

- 首次量測（4-pane，啟動後 5 秒起算、120.1 秒 idle）：`TotalProcessorTime` 增加 104.36 秒（約單核 86.9%）；私有記憶體 11.41→10.99 MB、handles 809→792、threads 19→13；`ReadOperationCount`／`WriteOperationCount`／傳輸 bytes 增量皆為 0（沒有磁碟 I/O）。結束時 `live_list_count` 歸 0、`clean_shutdown=true`、正常關閉無殘留行程。
- 獨立重量測（8 個 5 秒視窗）：CPU 80.8%～88.3% 單核；10 秒的 per-thread 增量顯示單一 thread（狀態 Running）占 8.078 秒，其餘 thread 皆 ≤0.031 秒；以 `SC_MINIMIZE` 最小化後同樣 10 秒量得 0%。

AGENTS.md 要求 idle 時 0% CPU；本次量測的視窗可見時 idle CPU 遠高於此，且隨視窗最小化消失。這支持「可見狀態下有持續繁忙的 UI thread」的觀察，但尚未排除與此 harness 環境（第二螢幕、非前景、`SetWindowPos` 搬移視窗）相關的因素，也未定位原因，不判定為產品缺陷，需由後續 triage 以不經 harness 搬移的視窗重測。10 分鐘 idle、記憶體長期趨勢、binary／runtime 依賴、完整 CTest 與 stuck provider bounded shutdown 仍未驗證；PD-242 維持 `in_progress`，acceptance 未勾選。
