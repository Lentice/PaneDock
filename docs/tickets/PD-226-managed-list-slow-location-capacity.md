# PD-226 — 慢網路、容量耗盡與 pending shutdown

Phase 8 · managed list migration

## 來源與交付

2026-10-16 使用者回報偶發 tab 列表刷新延遲，明確決定自行管理列表，並確認 21 個切片的粒度與依賴。
功能來源：[自建列表規格](../research/2026-10-16-managed-list-spec.md)。本票的狀態與阻擋關係以 Ticket 總覽為唯一來源。

**What to build：**慢或不可達網路位置不困住使用者；可離開、重試、操作另一 pane，資源滿時有明確降級，關閉不無限等待列舉。

## Overrides

不沿用 PD-168 的 binding deadline 作為必定可中止的保證；仍不授權 extension/menu process isolation。

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

新列表列舉／結果交付／資源上限、Pane navigation identity、ShutdownCoordinator、ShellReentryGuard、ExplorerHost 舊 binding deadline、shutdown tests。

追查上列 shared functions 的所有 caller，涵蓋滑鼠、鍵盤、restore、deferred replay 與 shutdown；改動前核對兩份 ADR 及來源規格。本系列內容模組的新增入口可能由前票建立，應沿其實際實作閱讀，不為滿足票面名稱另建一套模組。

## Commander 參考提示

- **來源線索**：[Double Commander 官方設定](https://doublecmd.github.io/doc/en/configuration.html)提供背景讀列表；[Total Commander 作者說明](https://www.ghisler.ch/board/viewtopic.php?p=311475)指出 EXE／LNK 圖示背景取得仍可能牽涉防毒掃描。
- **本票提示**：背景執行只改變 UI 等待位置，必須另外測 OS／provider 長時間占住工作容量時的退化與關閉。對照慢列舉和慢圖示，避免只測一條快失敗路徑。
- **限制**：尚無第一手證據證明這些產品可強制取消任意 Shell handler，或普遍使用程序隔離；不得把它寫成既有可直接移植的解法。

上述為第一手文件／作者回覆所記錄的做法與本票的工程建議，未經 PaneDock 效能實測。完整來源及未確認事項見[Commander 研究](../research/2026-10-16-commander-performance.md)。本節不增加功能、阻擋關係或額外驗收門檻。

## 範圍

- 將 UNC／mapped drive 的慢解析、列舉與錯誤回傳接到列表；UI 不同步等待，保留原位置以便重試。
- 受控來源涵蓋所有工作額度被占用、重複切換、晚到失敗；限制 abandonment／queue／retry，明定何時拒絕新工作及顯示方式。
- 驗證可取消 OS 讀取與正常關閉的實際能力；不可取消的情形不得 TerminateThread、無界 detached worker 或增開 process 蒙混過關。若無法達成契約，保留 FAIL 證據並先拆出後續設計，不能標完成。

## Acceptance criteria

- [ ] 真正慢／不可達網路的等待期間，另一 pane 與改目的地仍回應；retry 不遺失 parsing／fallback identity。
- [ ] 耗盡情境下 memory／pending work 不持續增加、錯誤明確、晚到結果不覆蓋新位置。
- [ ] 正常 shutdown 無 use-after-free，且不因任意慢操作無限 join；記錄不可取消與容量降級的可觀察行為。

## Testing decisions

controlled blocked work 驅動 Pane 可操作性與 shutdown，再於真實 UNC／disconnected mapping 記錄時間；不把本機失敗快回當慢網路 PASS。

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

- 尚缺既有 disconnected mapped drive 的實際讀取、離開／重試及 pending shutdown 證據；本機唯讀盤點沒有目前或持久化映射，未獲授權建立或中斷映射。真實 UNC pending probe 不能替代 mapped drive 驗收。
- Claude 後續以提升權限觀察到單一 TEST-NET UNC pending 時，另一 pane 可載入本機資料，且離開 UNC 後的本機結果在約 10 秒內未被改寫；證據見下方 2026-10-21 checkpoint。這不是一般使用者權限下的結果，也沒有觀察到 UNC 的晚到／最終結果；多操作、容量壓力、Group／tab 互動及不可取消 provider 的 worker 清理仍未驗證。受控 worker 的有界 abandonment／無 join 只能證明自身生命週期；快成功或粗粒度計時不能推論完整驗收通過。測量與重跑方式見 performance baseline／testing。
- 2026-10-20 normal-integrity managed-list UI probe：4-pane session 可啟動，但 `\\192.0.2.1\PD226` 約 1.7 秒內顯示 `Error 53`，沒有 pending 期間可操作其他 pane／tab／Group；這不是慢源 UX 證據，也不能用提升權限下的 pending 結果代表一般使用者。下次需取得一般使用者權限下可維持 pending 的來源，或由使用者決定是否另行授權提升權限驗證。

### PD-226 授權 UNC 路徑單次實測（2026-10-21）

Claude 使用隔離 Release `build\PaneDock.exe`（SHA-256 `9C585990E8DB931A84122C810F6D24FD35A02914EF88454A70F1933EDEC039AD`），啟動前確認 PaneDock process count 為 0；單一 PID 31632 正常關閉約 137ms 後 process count 歸 0。唯一測試路徑為使用者授權的 `\\vianextfs06\Tmp\Lentice\test`，僅導覽讀取，沒有在 share 建立、修改、更名或刪除資料。

新隔離 `LOCALAPPDATA` 位於 `build\manual-test\pd226u\run-20261021-142914\la`。UNC 導覽在第一個 7ms 輪詢時已顯示 `This folder is empty.`，列表 0 items；整段 UI thread ping 為 0ms、`hung=False`。在 UNC 已完成後，pane 1 tab pill 點擊耗時 166ms 並成功切換，接著本機隔離資料夾導覽顯示 1 item。因沒有觀察到 UNC pending／loading 狀態，這只能證明該次路徑很快返回且完成後 UI 可操作，不能證明慢網路期間的回應能力；也無法判斷 share 為真空資料夾或其他原因顯示空列表。證據／截圖在 ignored `build\manual-test\pd226u\run.out.txt` 與 `run-20261021-142914\`。真實慢／不可達 UNC、耗盡與晚到結果、mapped drive、取消及 bounded shutdown acceptance 均未補足，PD-226 與 tracker 維持 `in_progress`。

### PD-226 pending UNC 與目的地替換觀察（2026-10-21；單次、有限樣本）

Claude 使用 Release `build\PaneDock.exe`（SHA-256 `E630EAC2325DFBE6896BC05B49D41B72887D2E95E3F8C4605398320694E23586`），隔離設定在 `build\manual-test\pd226-late-result\run-261021154917\la`，測試資料只在相鄰新建的 `local1`／`local2`。啟動前 PaneDock process count 為 0；截圖顯示一個 Group、四 pane。Pane 0 導覽到 `\\192.0.2.1\PD226` 後，在約 1.4 秒仍顯示 `0 items - Loading...`。Pane 1 同時導覽到 `local1` 並顯示唯一 marker1；約 t=1.75 秒，Pane 0 離開 UNC 到 `local2`，8 ms 後已顯示正確位址及唯一 marker2。接下來約 10 秒，Pane 0 的位址、狀態與 row 未被改寫；各觀察點的 UI ping 為 0 ms、`hung=False`。

此程式以提升權限執行；一般權限下相同 TEST-NET UNC 曾快速回 Error 5，因此本次不能代表一般使用者行為。觀察期間沒有看到舊 UNC request 的最終結果或晚到 batch；只能記錄新目的地在約 10 秒窗口內保持穩定，不能宣稱已驗證 late-result discard 或 worker 退出。測試結束 PaneDock 以 `WM_CLOSE` 約 125 ms 正常退出，process count 為 0；正式 session metadata 未變、stderr 空白，未用 clipboard、未執行檔案 verb、未刪除 marker。marker 前後 hash 相同，結束時 SHA-256 為 local1 `074EA36F4876404935748B81577DF9B17879CD4888D4CEE9C595DC99CA742CDE`、local2 `35B4AA07E579368065FA71FCF5D60653E5BA9E1F639B5CC30A92A6E816F57AC0`。輸出／截圖留在 ignored `build\manual-test\pd226-late-result\run-261021154917\`。

這是單一 provider、單次導覽與本機目的地替換樣本；沒有涵蓋 30 次其他 pane command、capacity exhaustion、retry identity、disconnected mapped drive，或真正觀察到晚到結果後的丟棄。三項 acceptance 仍未全數達成，總覽維持 `in_progress`。
