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
