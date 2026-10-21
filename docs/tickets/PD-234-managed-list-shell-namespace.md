# PD-234 — 虛擬 Shell location 導覽

Phase 8 · managed list migration

## 來源與交付

2026-10-16 使用者回報偶發 tab 列表刷新延遲，明確決定自行管理列表，並確認 21 個切片的粒度與依賴。
功能來源：[自建列表規格](../research/2026-10-16-managed-list-spec.md)。本票的狀態與阻擋關係以 Ticket 總覽為唯一來源。

**What to build：**My Computer、Desktop 等既有虛擬位置可在新列表導覽，保存及還原正確的 Shell identity。

## Overrides

舊只經 IExplorerBrowser 可列虛擬位置的實現可替換，Shell namespace 覆蓋產品要求不縮減。

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

ShellLocation identity／resolve_location、ExplorerHost navigate／navigate_up 的舊行為、shell_core、Pane navigation／history、預設 My Computer 規則。

追查上列 shared functions 的所有 caller，涵蓋滑鼠、鍵盤、restore、deferred replay 與 shutdown；改動前核對兩份 ADR 及來源規格。本系列內容模組的新增入口可能由前票建立，應沿其實際實作閱讀，不為滿足票面名稱另建一套模組。

## 範圍

- 實作必要 namespace enumeration／child identity／parent navigation，不用 path 字串代替虛擬項目；worker 所用 COM 在該 apartment 建立與銷毀。
- 支援預設位置與實際 session 已支援命名空間，綁定失敗可重試；parsing name／known-folder／fallback 不能被 display name 替代。
- 虛擬通知或 refresh 能依現有 change contract 接入；未提供 reliable notification 的位置明定手動刷新策略，不加入 polling。

## Acceptance criteria

- [x] 新 tab／新 Group／最後 tab replacement 的預設 My Computer 可用，Desktop→child→parent 與歷史正確。
- [x] 保存後重啟保留虛擬 identity，不能因無 filesystem path 丟棄項目或 session 欄位。
- [ ] 慢 provider 回覆仍受 request identity／capacity／shutdown 契約約束，無 UI-owned COM 跨 thread 使用。

## Testing decisions

Pane seam 測虛擬值 identity 與導覽結果，真實 Shell 驗證 My Computer／Desktop／known-folder restore；不 fake COM host。

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

## 自動化驗證 checkpoint（2026-10-21）

PD-243～PD-246 已提交；本次只驗證 PD-242 移除 legacy host 後的現行 managed-list 路徑，不新增實作。已核對 worker 的 owned child／canonical／parent 值與共用容量、Pane routing／Up／history／stale-result／re-entry／shutdown，以及 session identity／unknown-field 保存契約。

使用 LLVM-MinGW Clang／LLD Release，執行子票 Agent checks 的 focused targets／CTest 聯集：

```powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target PaneDock panedock_managed_list_selection_test panedock_managed_list_sort_test panedock_managed_list_changes_test panedock_managed_list_cache_test panedock_managed_list_restore_test panedock_managed_list_icons_test panedock_managed_list_namespace_worker_test panedock_managed_list_capacity_test panedock_managed_list_namespace_test panedock_core_session_test
ctest --test-dir build --output-on-failure -R '^panedock_(managed_list_(selection|sort|changes|cache|restore|icons|namespace_worker|capacity|namespace)|core_session)$'
git diff --check
```

Configure 與指定 targets 建置皆 exit 0（Ninja: no work to do）；CTest 10/10 PASS、exit 0，總時間 9.99 秒；diff check 通過。Configure／build／CTest 使用 elevated execution context，並非產品需要 elevation。證據涵蓋 identity／selection／sort／enrichment／cache／Refresh、Pane navigation／history／session restore，以及 controlled namespace worker capacity／late delivery／re-entry／non-joining shutdown；CTest 時間不是產品效能量測。使用者既有 WIP 保留，未納入本次文件提交。

依本次授權未執行完整 CTest、GUI、launch smoke、真實 Shell／Desktop／known-folder／OneDrive／network／provider 測試。This PC／Desktop 的實際 children／parent、known-folder 重啟還原、provider STA COM lifetime 與卡住 provider 的安全 shutdown 仍「未驗證，需真實桌面」；以上自動化 PASS 不勾選本票產品 acceptance，亦不代表 PD-226 或 PD-241 gate 通過。下一個本 umbrella 剩餘 gate 是上述真實 Shell 驗收；相鄰 PD-235 的真實 OneDrive 驗收仍待完成，後續整體相容性／效能證據依 PD-241 保留，sequencing waiver 不等於 gate PASS。

## 實機自動化 checkpoint（2026-10-21，部分驗證）

Windows 11 Pro 10.0.26300；`build\PaneDock.exe`（Release，2026-10-21 08:47:31 建置）；隔離 `LOCALAPPDATA=build\manual-test\la-pd234a`，每次僅一個 PaneDock 實例、正常關閉後才再啟動。操作為 PowerShell 5.1／.NET harness（`build\manual-test`，非產品碼、未納入版控）：地址列以 `WM_SETTEXT` 加 `PostMessage(WM_KEYDOWN VK_RETURN)`、pane 按鈕以 `BM_CLICK`、鍵盤以 `SendInput`；是腳本訊息，不是實體滑鼠操作。證據：stdout、`build\manual-test\pd234*.png`、隔離 `session.json`。

已觀察到：
- 啟動時 4 個 pane 皆為 This PC（`::{20D04FE0-3AEA-1069-A2D8-08002B30309D}`，3 items）；New Group 後 pane 0 位置仍為 This PC。
- 地址列輸入 `shell:Desktop` 導覽成功（75 items）；`Back` 回 This PC（3 items）；`Forward` 回到 Desktop，此時地址列顯示 `D:\Documents\Desktop`、狀態 `57 items`（無 refresh 後綴的載入中狀態，未等待收斂，不作為差異結論）。`shell:Downloads` 導覽成功（1442 items）。
- 正常關閉（結束碼未由 `Start-Process` 回報，視窗關閉後行程消失）後，`session.json` 的 `clean_shutdown=true`，Downloads 分頁保存 `parsing_name='D:\Downloads'`、`known_folder_identity={374DE290-123F-4565-9164-39C4925E467B}`、`fallback_path='D:\Downloads'`；Desktop 分頁為 `D:\Documents\Desktop`／`{B4BFCC3A-DB2C-424C-B029-7FE99A87C641}`／同路徑；其餘分頁保存 This PC 的 parsing name 與 known-folder id。
- 同一隔離 session 重啟後 pane 0 還原為 `D:\Downloads`（1443 items）、pane 1 還原為 `D:\Documents\Desktop`。

未驗證（不勾選任何 acceptance）：
- Desktop→child→parent：鍵盤 Down＋Enter 與 `SendInput` 滑鼠雙擊皆未能進入子資料夾（原始碼只在 `NM_DBLCLK` 開啟列，Enter 無對應處理；滑鼠按鍵事件未被控制項處理，診斷限時後放棄），`Up` 結果因此無法判讀。
- 新 tab 與「最後一個 tab replacement」的分頁數量變化未以畫面確認；This PC 子項目、Desktop 子項目名稱未讀出（UIA 對該 ListView 無 item 節點）。
- 慢 provider 的 request identity／capacity／shutdown、provider STA COM lifetime、卡住 provider 的安全關閉：無環境。

## 實機 Shell namespace 回歸（2026-10-21）

追查舊 checkpoint 的 Desktop→child→parent 缺口後，確認從 merged Desktop 列舉出的 child，其 parsing name 再解析會得到不同 PIDL parent；單靠 path 與 known-folder identity 不能還原原本 namespace parent。`ShellLocation` 現在額外保存 owned navigation-parent 值及 namespace-route flag（不保存 PIDL／COM pointer），session v1 以可保留未知欄位的 additive optional 欄位保存；Up 優先使用 parent hint，namespace root 仍送往 Shell namespace worker。一般 filesystem location 仍走原本路徑。

Pane seam 回歸涵蓋新預設 tab、最後 tab replacement、新 Group、Desktop child parent／Back history 及 stale result；session 測試涵蓋 namespace route 與 parent unknown-field round trip。實機用 Windows 11 Pro 10.0.26300、Release `PaneDock.exe`、隔離 `LOCALAPPDATA=build\\manual-test\\la-pd234p`，每次只啟動一個實例並正常關閉。Claude 的 Windows UI harness 在 `shell:Desktop` 顯示 75 項，進入 `CpuDieId` 子資料夾為 12 項；Up 回 75 項 merged Desktop，Back 回 12 項 child，Forward 回 75 項 Desktop。正常關閉重啟後從 child Up 亦回 75 項；session 分別保存 child 的 Desktop parent hint 與 root 的 `namespace_location=true`。未修改測試資料，兩次均正常關閉且行程歸零。測試檔及輸出保留在 ignored `build\\manual-test\\pd234p`。

移除臨時診斷輸出後執行 Release 增量建置，以及以下 focused CTest，8/8 PASS（6.09 秒）；`git diff --check` 通過：

```powershell
cmake --build build --target PaneDock panedock_managed_list_namespace_test panedock_core_session_test panedock_managed_list_namespace_worker_test panedock_managed_list_icons_test panedock_managed_list_cache_test panedock_managed_list_restore_test panedock_core_model_test
ctest --test-dir build --output-on-failure -R '^panedock_(managed_list_namespace|core_session|managed_list_namespace_worker|managed_list_icons|managed_list_cache|managed_list_restore|core_model|single_threaded)$'
```

因此已勾選前兩項 acceptance。PD-234 維持 `in_progress`：本次實機驗證只覆蓋本機 merged Desktop／單一 child 與重開；慢 provider 的 request identity／capacity、provider STA COM lifetime、卡住 provider 的 bounded shutdown 仍未驗證，不由本次結果推論通過，也不替代 PD-226／PD-241 gate。
