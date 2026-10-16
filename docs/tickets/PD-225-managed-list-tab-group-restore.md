# PD-225 — 自建列表接通 tab、Group 與 session 復原

Phase 8 · managed list migration

## 來源與交付

2026-10-16 使用者回報偶發 tab 列表刷新延遲，明確決定自行管理列表，並確認 21 個切片的粒度與依賴。
功能來源：[自建列表規格](../research/2026-10-16-managed-list-spec.md)。本票的狀態與阻擋關係以 Ticket 總覽為唯一來源。

**What to build：**使用開發入口時，四 pane 的 tab／Group 切換與重新啟動會顯示各自正確位置及必要狀態。

## Overrides

舊 only-active-tab-live 的資源意圖保留，實現改以 active list 的工作／資料生命週期而非強制 IExplorerBrowser。

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

Pane::switch_active_tab／finish_tab_change／navigate_history、Group transition、core TabState／realization plan、SessionWriter 與 session preservation 測試。

追查上列 shared functions 的所有 caller，涵蓋滑鼠、鍵盤、restore、deferred replay 與 shutdown；改動前核對兩份 ADR 及來源規格。本系列內容模組的新增入口可能由前票建立，應沿其實際實作閱讀，不為滿足票面名稱另建一套模組。

## Commander 參考提示

- **來源做法**：[Double Commander 的 Folder tabs 設定](https://doublecmd.github.io/doc/en/configuration.html)可在啟動時不讀 inactive tab；[Explorer++ 1.4 beta 修正](https://github.com/derceg/explorerplusplus/releases)避免導航／關閉 tab 後舊欄位結果寫入新列表。
- **本票提示**：restore 多個 tab 時只啟動可見 active tab 所需工作，快速切換用 binding／generation 拒絕舊資料；驗證 navigation 與 enrichment 兩種晚到結果。
- **限制**：此處採用延後工作與結果隔離的概念，不推定其他產品使用和 PaneDock 相同的 request token。

上述為第一手文件／作者回覆所記錄的做法與本票的工程建議，未經 PaneDock 效能實測。完整來源及未確認事項見[Commander 研究](../research/2026-10-16-commander-performance.md)。本節不增加功能、阻擋關係或額外驗收門檻。

## 範圍

- 接通所有導覽入口（地址／歷史／Up／tab／Group／restore），不要只修 tab click；以同一請求 identity 判斷結果是否仍屬目前 pane binding。
- 在必要模型變更時 capture 自建列表的 view mode／sort；保留 history failure rollback 與 session 未知欄位，不以 live Shell view 取回自建內容位置。
- inactive tab 只保留必要狀態，不建立完整 live list；目前未支援虛擬位置維持舊路徑且明確區分，不能丟失原 identity。

## Acceptance criteria

- [ ] 四 pane、快速 A→B→A、切 Group 後晚到完成、關閉／搬移 tab 都不串錯內容或歷史。
- [ ] 地址／Back／Forward／Up 與 restore 經相同新內容入口；失敗不保存一個錯誤的已成功位置。
- [ ] 隔離儲存下重啟還原 layout／location／active tab／既有 mode／sort 及未知欄位；不新增每 tab 完整常駐列表。

## Testing decisions

沿用 PaneHost、core_group_transition、core_navigation、core_session 的行為測試；以隔離 session 進行 restore 實機檢查。

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
