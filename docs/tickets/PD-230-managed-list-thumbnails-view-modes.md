# PD-230 — 縮圖與既有 view mode 對應

Phase 8 · managed list migration

## 來源與交付

2026-10-16 使用者回報偶發 tab 列表刷新延遲，明確決定自行管理列表，並確認 21 個切片的粒度與依賴。
功能來源：[自建列表規格](../research/2026-10-16-managed-list-spec.md)。本票的狀態與阻擋關係以 Ticket 總覽為唯一來源。

**What to build：**使用者可切換既有 view mode 與大小，可見項目按需取得縮圖，回到 tab 後模式正確還原。

## Overrides

保留 native thumbnail 來源整合能力，但繪製／工作排程由新列表掌管。

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

新圖示排程／cache、Pane view mode menu／apply_view_mode／capture_view_mode、shell_core view_mode_options 與 persisted mode 名稱。

追查上列 shared functions 的所有 caller，涵蓋滑鼠、鍵盤、restore、deferred replay 與 shutdown；改動前核對兩份 ADR 及來源規格。本系列內容模組的新增入口可能由前票建立，應沿其實際實作閱讀，不為滿足票面名稱另建一套模組。

## Commander 參考提示

- **可類比的做法**：[Double Commander 的列表後載入圖示選項](https://doublecmd.github.io/doc/en/configuration.html)支持先可操作、後補裝飾的分階段思路；[Explorer++ 的圖示快取紀錄](https://github.com/derceg/explorerplusplus/releases)可作重用結果的參考。
- **本票提示**：把此思路應用於 PaneDock 的縮圖，按可見範圍與目前尺寸需求排程，先保留文字和 placeholder，再補影像。
- **證據界線**：本研究沒有確認這些產品目前的縮圖取消、尺寸 key、LRU 或預取實作。縮圖 pipeline 的具體選型是本票工程設計，必須量測，不能當成已抄得的成熟方案。

上述為第一手文件／作者回覆所記錄的做法與本票的工程建議，未經 PaneDock 效能實測。完整來源及未確認事項見[Commander 研究](../research/2026-10-16-commander-performance.md)。本節不增加功能、阻擋關係或額外驗收門檻。

## 範圍

- 依功能矩陣對應既有 mode／size，保留 persisted meaning；必要 control 版面與縮圖取得同票接通。
- 縮圖優先可見範圍，尺寸／DPI 納入 cache key，容量有界；失敗不重試自旋，也不阻擋文字／選取。
- 切模式後釋放或淘汰過大影像，不預熱全部資料夾或 inactive tabs；正常縮圖行為與 OneDrive 專屬驗證分開。

## Acceptance criteria

- [ ] 既有 view mode 切換與保存還原有逐項對照，不以靜默改成 Details 取代未實作模式。
- [ ] 可見項目先更新，巨大／壞檔縮圖錯誤不困住列表；切換後不貼錯尺寸／identity。
- [ ] 影像上限與回收可量測，切換 20 次後資源不持續成長。

## Testing decisions

受控影像結果測 state／cache key，再實機檢查多模式／大小／失敗圖片；不能僅檢查持久化 enum。

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

- 使用者明確延後 GUI／實機驗證。真實 Shell 縮圖 provider、巨大／損壞檔案、透明影像、OneDrive 是否觸發下載、網路／虛擬 namespace、mixed-DPI 畫質與 Tiles／Content 實際排版、鍵盤／accessibility 仍「未驗證，需真實桌面」；受控 raster 與隱藏控制項測試不是這些相容性的證據。切換 20 次後的真實 process handles／private bytes 與十分鐘 idle CPU／I/O／wakeups 尚未量測，仍由 PD-241 gate 驗證，legacy 預設不變。
- 縮圖與繪製涉及 worker-owned Shell COM、HBITMAP／HICON 與虛擬 ListView，不能移入純 core；自動化替代檢查沿用 PaneHost seam，實機替代檢查是八種既有模式／尺寸切換、tab 還原、快速捲動、失敗縮圖與 mixed-DPI。Shell provider 及 COM teardown 仍可能永久阻塞；沿用 PD-229 四個影像 slot 的不 join 關閉策略，無法以受控測試推論真實 provider 的取消／資源結果。
