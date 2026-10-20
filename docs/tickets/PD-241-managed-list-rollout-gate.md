# PD-241 — 自建列表相容性及效能 gate，通過後切預設

Phase 8 · managed list migration

## 來源與交付

2026-10-16 使用者回報偶發 tab 列表刷新延遲，明確決定自行管理列表，並確認 21 個切片的粒度與依賴。
功能來源：[自建列表規格](../research/2026-10-16-managed-list-spec.md)。本票的狀態與阻擋關係以 Ticket 總覽為唯一來源。

**What to build：**確認功能與資源符合門檻後才讓一般啟動使用自建列表，保留可審查的量測證據。

## Overrides

滿足明確 gate 後才覆寫 IExplorerBrowser 預設路徑；不放寬 idle gate 與 menu 故障邊界。

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

產品功能／效能矩陣、全部自建列表行為、performance baseline／release evidence、testing manual protocol、session roundtrip／launch smoke。

追查上列 shared functions 的所有 caller，涵蓋滑鼠、鍵盤、restore、deferred replay 與 shutdown；改動前核對兩份 ADR 及來源規格。本系列內容模組的新增入口可能由前票建立，應沿其實際實作閱讀，不為滿足票面名稱另建一套模組。

## Commander 參考提示

- **研究支援的比較維度**：[Double Commander 設定](https://doublecmd.github.io/doc/en/configuration.html)區分背景列表與後載圖示；[Total Commander 作者](https://www.ghisler.ch/board/viewtopic.php?p=311475)提到 EXE／LNK 圖示與防毒成本；[OneCommander 官方支援](https://groups.google.com/g/onecommander/c/9k-TMTz9CWk)區分基本資料與昂貴 metadata 排序。
- **本票提示**：分別量基本列表／圖示與縮圖補齊／昂貴排序，不混成單一「載入完成」。測 local、EXE／LNK 密集、媒體、雲端與慢網路，保留 cold／warm 差異。
- **證據護欄**：沒有相同環境的跨產品 benchmark，不能因採用相似做法就宣告更快／更省；原生 menu 延遲另列既有限制，不納入本次改善聲明。

上述為第一手文件／作者回覆所記錄的做法與本票的工程建議，未經 PaneDock 效能實測。完整來源及未確認事項見[Commander 研究](../research/2026-10-16-commander-performance.md)。本節不增加功能、阻擋關係或額外驗收門檻。

## 範圍

- 完整執行功能矩陣，含未直接列為 blocker 但已由依賴鏈交付的 keyboard／accessibility／history／notifications；未驗證不可算 PASS。
- 以固定資料集與機器記錄 cold/warm、10 萬項目、四 pane、slow UNC、rapid navigation、idle CPU／I/O／memory；分別記首次可操作、full enumeration 與總 CPU，不只報平均。
- 與第 1 票的預算逐項比較，功能／效能未達則開具體修復票，保持舊預設；全部通過才切正常啟動路徑，不同票順手刪除舊實作。

## Acceptance criteria

- [ ] 矩陣每列有機器／Windows／資料集／命令或互動程序及 PASS／FAIL／未驗證證據。
- [ ] 10 分鐘 idle 平均 CPU <0.1%、disk I/O 零，memory／queues／images 各 budget 合格，慢來源下其他 pane 可操作。
- [ ] 符合門檻才切自建列表為預設；隔離 session launch／restore／shutdown 與未知欄位保存通過，native menu 延遲未被宣稱已修復。

## Testing decisions

執行完整 CTest、Release 實機測量與既有功能協定；保留原始樣本與摘要，量測數字寫 performance baseline，不建立虛構 PASS。

## 自動化 checkpoint（2026-10-20）

本節記錄本回合由 Codex 執行 Release 建置與選定 CTest 命令的自動化證據，不代表相容性及效能 gate 通過。狀態與依賴仍只以 Ticket 總覽為準。

- **建置成功**：本回合 Codex 執行 Release 的 PaneDock 與 16 個 managed-list unit targets 建置，全部成功。
- **選定測試通過**：`managed_list_capacity_test.cpp` 的 fixture-only 修復（commit `a624e96`）後，本回合 Codex 執行選定 20-test 集合的 CTest 命令，全部通過，涵蓋 cache、restore、rename、drag/drop、file commands、native context menu seam、selection、resize、icons、cloud、sort、changes、namespace、namespace_worker、capacity、Shell reentry、shutdown-state 與 policy checks。這不是完整 CTest 通過的證據；seam／cloud 等自動測試亦不代表真實 Shell／OneDrive 驗證。
- **失敗及排除**：另行執行的 `panedock_managed_list` 失敗／segfault；其 fixture 沒有 parent HWND，`navigate_to` 失敗。此項不在上述通過集合內，不能算 PASS。`tests/unit/managed_list_test.cpp` 有使用者既有 WIP，本次不修改，該失敗仍未修復，不計為通過。
- **未驗證**：依使用者指示跳過完整 CTest、GUI、launch smoke 與真實 Shell／OneDrive／network 測試。因此真實功能矩陣、10 分鐘 idle CPU／disk／memory、UNC／OneDrive、真正 Shell extensions／verbs，以及 launch／session 行為均未驗證；不得從選定自動測試推論通過。
- **後續 gate 邊界**：維持既有 `IExplorerBrowser` 預設與實作；PD-242 必須等 PD-241 gate 通過後才可進行。右鍵優先使用原生 Windows Shell menu，僅必要時才考慮 custom menu；本次沒有此必要，也不實作選單變更。

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
