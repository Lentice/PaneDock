# PD-237 — 自建列表 inline rename

Phase 8 · managed list migration

## 來源與交付

2026-10-16 使用者回報偶發 tab 列表刷新延遲，明確決定自行管理列表，並確認 21 個切片的粒度與依賴。
功能來源：[自建列表規格](../research/2026-10-16-managed-list-spec.md)。本票的狀態與阻擋關係以 Ticket 總覽為唯一來源。

**What to build：**在自建列表按 F2 可就地重新命名，取消與錯誤可恢復，成功後選取和焦點跟隨正確項目。

## Overrides

維持 native file-operation 語意，自建 edit control 不等於自製 rename engine。

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

列表選取／keyboard／edit lifecycle、file_operations rename 能力、notification rename 合併、sidebar F2 precedence。

追查上列 shared functions 的所有 caller，涵蓋滑鼠、鍵盤、restore、deferred replay 與 shutdown；改動前核對兩份 ADR 及來源規格。本系列內容模組的新增入口可能由前票建立，應沿其實際實作閱讀，不為滿足票面名稱另建一套模組。

## 範圍

- 接通 begin-edit／commit／Esc／focus loss，F2 在 sidebar 仍 rename Group，在地址列不誤觸檔案。
- 提交使用 Shell rename／identity，失敗保留原項目可重試；成功更新與 notification 回報去重，不 duplicate rows。
- edit 期間收到 batch／sort／外部 delete／切 tab，定義安全終止政策，不沿舊 row index 修改另一項。

## Acceptance criteria

- [ ] Enter／Esc／focus loss、衝突／拒絕存取／外部移除行為可預期，無 stale selection 誤改。
- [ ] rename 成功後名稱／identity／selection／focus 與真實目錄收斂，不出現兩筆。
- [ ] F2 與 sidebar／地址列／global shortcuts 優先順序維持。

## Testing decisions

Pane seam 測 edit 狀態與結果，再以 disposable files 驗證實際 Shell rename 與通知收斂。

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


## Implementation checkpoint（2026-10-20）

Pane 自有 edit control 與公開 F2 routing 僅作用於 managed-list HWND、無 modifier 且恰好一筆 selection。sidebar F2 仍由既有 core routing rename Group；地址列不觸發檔案 rename。編輯中的文字 Ctrl+C／X／V 與 Backspace 留給 edit，Ctrl+Tab／Ctrl+W 等既有全域命令維持。F2 路由在主迴圈 legacy accelerator 的外層 ShellCallScope 之前執行；已處於真正巢狀 Shell call 時拒絕開始編輯。

- Enter 才提交；Escape、失焦、目前 request 的任何 accepted batch（含 external delete／recovery）、排序（含 metadata sort 最終排列）、捲動／geometry／view mode、navigation／tab／Group／unbind／destroy 均取消。不以 row index 保存提交目標。初始 loading／cache reconciliation／notification recovery 不允許開始 rename，避免尚未完成的列舉回報舊名稱。
- conflict／access denied／missing item／Shell cancel 保留原列與 identity；狀態列提示 F2 重試或 Refresh。空字串拒收。Shell 實際成功但無法取得完整結果 identity 時亦不猜測新 path，保留原列供通知或 Refresh 收斂。
- Pane 透過 PaneHost 取得既有 operation／shutdown gate；file_operations 使用 IFileOperation::RenameItem／PerformOperations 與 progress sink 回傳的實際 Shell item，取得 absolute parsing identity、parent-relative parsing name 與 display name。不同名衝突可能改變實際結果，因此不從使用者輸入自行組合新 identity。參照 Microsoft [RenameItem](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifileoperation-renameitem) 與 [PostRenameItem](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifileoperationprogresssink-postrenameitem)。
- Shell setup 每個 pump 後沿用 PD-236 owned snapshot 驗證；提交後固定原 target，回來再驗 Group／tab／generation／folder／shutdown。成功結果透過既有 bounded notification merge 更新、去重，selection／focus／anchor 隨新 identity 移動。無右鍵／menu 變更，正常預設仍是 legacy view。

新邏輯依賴 edit／list HWND、PaneHost 與 Shell operation gate，不能放進禁止 Win32／COM 的 core。新增 `panedock_managed_list_rename` 使用 hidden native controls、controlled Source 與 PaneHost owned-result adapter，從公開鍵盤／begin／cancel 操作及可觀察 rows／interaction／result 驗證；沒有 fake IExplorerBrowser，也沒有進行真實檔案 rename。涵蓋 Enter／Escape／失焦、空名稱、衝突／拒絕存取／missing／cancel、通知去重、本機 leaf 與 virtual absolute identity、batch／external deletion／sort／scroll／tab／Group／unbind／shutdown 失效、modifier／sidebar／address／edit shortcut precedence、巢狀 Shell gate，以及真實 rename API 在 stale setup 時無操作地拒收。

## Focused Agent checks（2026-10-20）

依本次使用者指示覆寫上方全套 Agent checks，只執行以下 Release targets 與相關 CTests；沒有 full CTest、GUI、launch smoke 或實機／disposable-file 操作。

```powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4 --target PaneDock panedock_managed_list_rename_test panedock_managed_list_file_commands_test panedock_managed_list_selection_test panedock_managed_list_sort_test panedock_managed_list_changes_test panedock_pane_test panedock_core_key_routing_test panedock_shell_reentry_guard_test
ctest --test-dir build --output-on-failure -R '^panedock_(managed_list_(rename|file_commands|selection|sort|changes)|pane|core_key_routing|shell_reentry_guard|single_threaded|shutdown_state|shell_reentry_gate)$'
git diff --check
```

Configure exit 0。restricted build 無輸出後以 Ctrl+C 中止，指定 targets 改在 elevated context 建置；最終 Release build exit 0、無 compiler warnings。最終相關 CTests 11/11 PASS、exit 0；`git diff --check` exit 0。自動測試結果僅證明上述 controlled behavior，不勾選三項原始 integration acceptance criteria。

## 交接區

- 未解風險：真實 Shell／disposable files rename 與通知收斂、衝突時實際新名稱／identity、access denied／external deletion／cancel 的原生 UI、本機／namespace／network／OneDrive provider 行為，均「未驗證，需真實桌面」。成功但 provider 不提供完整結果 identity 時，目前保留原列並要求通知／Refresh 收斂；該復原路徑仍需實機驗證。
- 未解風險：真正 keyboard focus、mixed DPI／各 view mode 的 edit 位置、accessibility、操作 pump 中切 tab／Group／shutdown 的 COM lifetime 及慢 provider 回應時間均未驗證。hidden-control seam 與政策掃描不構成以上項目或 PD-241 compatibility／performance／idle gate 的 PASS；原始 acceptance criteria 仍需 real integration evidence。
