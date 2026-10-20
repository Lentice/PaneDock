# PD-240 — 新列表的 live resize 與 mixed DPI

Phase 8 · managed list migration

## 來源與交付

2026-10-16 使用者回報偶發 tab 列表刷新延遲，明確決定自行管理列表，並確認 21 個切片的粒度與依賴。
功能來源：[自建列表規格](../research/2026-10-16-managed-list-spec.md)。本票的狀態與阻擋關係以 Ticket 總覽為唯一來源。

**What to build：**自建列表在八種 layout template、splitter 拖動與跨 DPI 螢幕時，內容、hit target 及 chrome 同步可用。

## Overrides

延續 FR-004 與 NFR-004，僅把幾何應用目標由 Shell view 換為自建內容。

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

Pane stage_layout／finish_layout／set_shell_rect、window position batching、core layout、WM_DPICHANGED、pane chrome geometry 與 paint helpers。

追查上列 shared functions 的所有 caller，涵蓋滑鼠、鍵盤、restore、deferred replay 與 shutdown；改動前核對兩份 ADR 及來源規格。本系列內容模組的新增入口可能由前票建立，應沿其實際實作閱讀，不為滿足票面名稱另建一套模組。

## 範圍

- 新內容 child 接入既有 parent-scoped deferred-position 批次，不以舊 Shell rect 之外的第二套 layout 造成中間 frame 不一致。
- 處理 DPI／font／row metrics／viewport 改變，基本 item identity 與 scroll anchor 保留；image 尺寸需求透過內容入口傳遞。
- retarget repaint 只覆蓋需要區域，無 resize polling／同步每步完整列舉，退化尺寸不產生負值。

## Acceptance criteria

程式實作與相關自動檢查已完成。需要真實桌面操作的 splitter／live resize、mixed-DPI、accessibility 與 GDI／handle 驗收依使用者指示延期，詳見 [PD-240 測試證據](../testing.md#pd-240-managed-list-resize-and-dpi-evidence)；此票完成不代表 PD-241 gate 通過。

- [ ] 八種版型與小視窗矩形正確，拖 splitter 時 chrome／內容不中斷或錯位。
- [ ] 96／144／192 DPI 或真實 mixed-DPI 轉移後文字／選取／hit target 一致，必要 accessibility bounds 更新。
- [ ] resize 不發出多餘導覽／列舉，沒有 GDI／handles 單調增加；無慢縮放節流掩蓋不一致。

## Testing decisions

延伸 core geometry／Pane real-control 結果檢查，加實際 live resize／mixed-DPI 操作；pixel test 不能代替互動驗收。

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
