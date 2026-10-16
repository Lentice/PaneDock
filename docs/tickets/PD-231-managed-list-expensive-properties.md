# PD-231 — 昂貴屬性欄位與排序

Phase 8 · managed list migration

## 來源與交付

2026-10-16 使用者回報偶發 tab 列表刷新延遲，明確決定自行管理列表，並確認 21 個切片的粒度與依賴。
功能來源：[自建列表規格](../research/2026-10-16-managed-list-spec.md)。本票的狀態與阻擋關係以 Ticket 總覽為唯一來源。

**What to build：**使用 duration 等昂貴欄位仍可操作和離開，排序期間有明確狀態，完成後列表不持續洗牌。

## Overrides

不承諾慢 provider 可強制中斷，不用縮短 menu 行為換取屬性排序通過。

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

基本排序與 anchors、Shell property identity／sort persisted keys、新列表 background job／generation／capacity policy。

追查上列 shared functions 的所有 caller，涵蓋滑鼠、鍵盤、restore、deferred replay 與 shutdown；改動前核對兩份 ADR 及來源規格。本系列內容模組的新增入口可能由前票建立，應沿其實際實作閱讀，不為滿足票面名稱另建一套模組。

## Commander 參考提示

- **來源做法與取捨**：[OneCommander 官方支援](https://groups.google.com/g/onecommander/c/9k-TMTz9CWk)說明 duration 等 metadata 經較慢的 Windows COM，完整排序需要所有 key；隨到隨重排又會持續改變項目位置。[Explorer++ 1.4 beta](https://github.com/derceg/explorerplusplus/releases)則延後取得非可見項目的欄位內容。
- **本票提示**：分開「顯示欄位」與「排序 key」兩種需求；可見欄位優先取得，整體排序需等待的資料則顯示 loading，按既定完成政策穩定提交。
- **建議對照**：同一資料夾以名稱和媒體屬性排序，記錄首次可操作／排序完成／CPU；不得以非可見項目略過 key 取得來產生錯序。

上述為第一手文件／作者回覆所記錄的做法與本票的工程建議，未經 PaneDock 效能實測。完整來源及未確認事項見[Commander 研究](../research/2026-10-16-commander-performance.md)。本節不增加功能、阻擋關係或額外驗收門檻。

## 範圍

- 先列出現有必須支援的 property keys，對可見欄位延後取得；排序需要全 key 時明確區分，不偽裝只讀 viewport 就能完成。
- 量取／排序與 UI 分開，成功／未知／失敗／取消各有狀態；完成以一致 snapshot 套用，保留 selection／scroll。
- 超時或不可取消遵守現有容量政策，不自建第二套無界 executor。

## Acceptance criteria

- [ ] 控制單一慢 key 可重現 loading，仍可切換目的地，舊 key 回覆不改新 tab。
- [ ] 所有 key 到齊或按已定義失敗政策結束後，排序穩定且 selection／anchor 正確。
- [ ] 缺 key／相同 key／取得失敗有確定次序與重試行為，無每筆 completion 全表洗牌。

## Testing decisions

以 Pane seam 批次／亂序／失敗 property 結果測可見行為，實機媒體目錄比較名稱與 property sort。

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
