# PD-222 — 對齊自建列表的規格與工程限制

Phase 8 · managed list migration

## 來源與交付

2026-10-16 使用者回報偶發 tab 列表刷新延遲，明確決定自行管理列表，並確認 21 個切片的粒度與依賴。
功能來源：[自建列表規格](../research/2026-10-16-managed-list-spec.md)。本票的狀態與阻擋關係以 Ticket 總覽為唯一來源。

**What to build：**讓後續 agent 能依一致規格實作自建列表，明定 UX → CPU／memory／power → 架構與可維護性，保留原生選單與既有檔案操作。

## Overrides

直接覆寫 AGENTS 的「The file list is never reimplemented」、design-spec §2.4／FR-006 的 view-only 前提、development 的 idle-first 排序，以及對所有新工作一律 single-STA 的舊決定。新證據為 2026-10-16 使用者明確決定自建列表及優先順序；無需重作 PD-001 No-Go 判定，亦不再受 fallback 候選僅限 No-Go 的舊觸發條件限制。

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

design-spec 的 §2.4、§4.8、FR-006、NFR-001～007、§9.1～9.2；development 的 Product boundary／Architecture rules；AGENTS 的 Project intent／STA 規則；CONTEXT；兩份既有 ADR；single_threaded_check；testing／roadmap。

追查上列 shared functions 的所有 caller，涵蓋滑鼠、鍵盤、restore、deferred replay 與 shutdown；改動前核對兩份 ADR 及來源規格。本系列內容模組的新增入口可能由前票建立，應沿其實際實作閱讀，不為滿足票面名稱另建一套模組。

## Commander 參考提示

- **先區分來源事實與產品決策**：[Double Commander 官方設定](https://doublecmd.github.io/doc/en/configuration.html)提供背景列舉、列表後載圖示與 inactive tab 延後讀取；[Explorer++ 1.4 beta 版本紀錄](https://github.com/derceg/explorerplusplus/releases)記錄可見項目欄位取得、圖示快取與拒收舊導覽結果。
- **本票提示**：把分階段載入、過期結果與資源上限寫入新契約；各產品的選項／歷史修補不等於 PaneDock 的預設政策或效能數字。後續票依實際效果驗收，不要求逐項照抄設定 UI。

上述為第一手文件／作者回覆所記錄的做法與本票的工程建議，未經 PaneDock 效能實測。完整來源及未確認事項見[Commander 研究](../research/2026-10-16-commander-performance.md)。本節不增加功能、阻擋關係或額外驗收門檻。

## 範圍

- 同步更新產品規格、工程規則、術語及測試政策，區分現有 Shell view 與未來自建列表；保留 C++／WRL 與 stable pane identity 的 ADR 決定。
- 限定列表背景工作可離開 UI STA；訂定值型結果、COM apartment 所有權、有界容量、取消／pending shutdown 契約。不可用全域解禁 thread 或刪除測試代替窄化規則。
- 寫出功能保留矩陣、各 view mode／sort 欄位對應範圍、開發入口與正式切換門檻。明定 interaction latency／memory 預算與測量方式；新預算是驗收目標，不能填成已量到的數字。

## Acceptance criteria

- [ ] 規格不再同時要求不重刻列表與自建列表；原生 menu 不改造、核心無 COM、檔案操作經 Shell 等約束清楚保留。
- [ ] single-STA 靜態檢查保留對 UI／Shell host 的有效限制，允許未來指定列表 worker；本票不導入 worker。
- [ ] 功能／效能矩陣列出測量命令或可執行量測流程、樣本與 PASS／FAIL／未驗證口徑，舊 completed ticket 不修改。

## Testing decisions

文件間條款／術語交叉審查；執行既有 single-threaded policy check 及全部既有檢查，證明規則調整未以直接關閉測試完成。

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
