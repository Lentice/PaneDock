# PD-235 — OneDrive placeholder 瀏覽

Phase 8 · managed list migration

## 來源與交付

2026-10-16 使用者回報偶發 tab 列表刷新延遲，明確決定自行管理列表，並確認 21 個切片的粒度與依賴。
功能來源：[自建列表規格](../research/2026-10-16-managed-list-spec.md)。本票的狀態與阻擋關係以 Ticket 總覽為唯一來源。

**What to build：**新列表可辨識 OneDrive 佔位項目並顯示對應圖示／狀態，單純列出基本內容不主動 hydration。

## Overrides

基本列表不為圖示／屬性讀檔內容；原生 Open／operations 所需 hydration 行為保留。

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

namespace identity、icon／overlay enrichment、cloud-related Shell attributes、列表 refresh、現有 OneDrive manual acceptance。

追查上列 shared functions 的所有 caller，涵蓋滑鼠、鍵盤、restore、deferred replay 與 shutdown；改動前核對兩份 ADR 及來源規格。本系列內容模組的新增入口可能由前票建立，應沿其實際實作閱讀，不為滿足票面名稱另建一套模組。

## Commander 參考提示

- **相容性教訓**：[Explorer++ 1.4 beta 版本紀錄](https://github.com/derceg/explorerplusplus/releases)曾記錄背景選單忽略 OneDrive extension 的 crash 修補，顯示雲端整合與特定 Shell 路徑須分別驗證。
- **本票提示**：實測 placeholder 基本列舉、圖示取得與狀態更新各自是否導致 hydration，不能只看列表出現就判定雲端相容。
- **限制**：該紀錄是歷史 menu crash 修補，不是 placeholder 效能／免 hydration 證據，更不授權 PaneDock 忽略 OneDrive extension；本次不改選單。

上述為第一手文件／作者回覆所記錄的做法與本票的工程建議，未經 PaneDock 效能實測。完整來源及未確認事項見[Commander 研究](../research/2026-10-16-commander-performance.md)。本節不增加功能、阻擋關係或額外驗收門檻。

## 範圍

- 取得基本 attributes／cloud state 不主動開檔讀內容；worker／icon enrichment 使用適當 identity。
- 測試 online-only／locally available／always available 及狀態變更刷新；選取、導航與 refresh 維持正確目標。
- 區分本票基本瀏覽與其他票縮圖／真正 Open／Copy 可能由 OS 觸發 hydration 的語意；不全域停用原生 provider。

## Acceptance criteria

- [ ] 實機 online-only 檔案在基本瀏覽前後狀態不因 PaneDock 主動讀內容而 hydration，有明確觀測紀錄。
- [ ] cloud 狀態／圖示更新指向同一 identity；錯誤不阻擋其他 pane，未同步／離線仍有可恢復行為。
- [ ] 不在產品中宣稱 OS／第三方 extension 永遠不會 hydration；無帳號環境記未驗證而非 PASS。

## Testing decisions

controlled attributes 結果測 UI state，加真實 OneDrive／offline／placeholder 觀測；記錄 Windows／client／檔案狀態。

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

## 實作與驗證紀錄（2026-10-20）

API 依據、caller 追查與免 hydration 邊界見[中繼資料研究](../research/2026-10-20-placeholder-metadata-boundary.md)。基本列舉共用 attributes/tag 值型轉換；Availability 欄位與 enrichment key 使用相同 metadata。已辨識 cloud／recall／offline／pin 項目保留本機 generic icon，不呼叫未建立免 hydration 契約的 item icon／overlay、thumbnail 或 property handler。未知 metadata 不全域停用一般／虛擬項目的原有 enrichment。

`panedock_managed_list_cloud` 使用受控 attributes、隱藏 Win32 控制項與 PaneHost，驗證列文字、delta／rescan／Refresh、selection identity、過期 generation 與 worker fallback。此邏輯位於 Windows 列舉／列表邊界，不能放入禁止 windows.h 的 core；不需要真實 Shell view。受控測試不是 hydration 證明。

Release configure 使用 LLVM-MinGW toolchain；sandbox build 停滯後改在 elevated context 建置。最終限定命令：

~~~powershell
cmake --build build --target panedock_managed_list_icons_test panedock_managed_list_cloud_test
ctest --test-dir build --output-on-failure -R '^panedock_managed_list_(icons|cloud)$'
git diff --check
~~~

結果：兩個 CTests 通過；diff check 通過。前次直接相關 cache／restore／sort／changes／namespace／namespace_worker CTests 已通過，但當時 icons／cloud 失敗，不能把該次執行當整批 PASS。首次失敗是 icons fixture 沒有 parent HWND 卻進入 child-list realization；補齊隱藏控制項，並將舊跨 Refresh cache 假設改為同一列表內未變 metadata 重用檢查，未改正式 cache 語意。unknown metadata 的過廣 worker 限制也已移除，雲端變化另由 IconKey 匹配測試保護。

依使用者要求未執行 GUI／完整 CTest／真實 OneDrive 帳戶驗證。總覽維持 in_progress，以上不勾選實機 acceptance。

### PD-242 後聚焦回歸檢查（2026-10-21）

PD-242 移除 legacy Shell view 後，Release 設定下 `panedock_managed_list_icons_test` 與 `panedock_managed_list_cloud_test` targets 已是最新；聚焦 CTests `panedock_managed_list_icons`、`panedock_managed_list_cloud` 通過（2/2）。此結果僅覆蓋受控 metadata、列表狀態與 icon identity 更新路徑，不代表真實 OneDrive hydration／provider 行為。未執行完整 CTest、GUI、launch smoke、OneDrive、網路、Shell/provider interaction 或 hydration 測試。tracker 維持 `in_progress`；剩餘實機驗收仍依下方交接區執行。

## 交接區

- online-only、locally available、always available 三種真實檔案的列舉／圖示前後 residency、狀態轉換通知及 Refresh、離線／未同步恢復、selection／導航與其他 pane 回應性全部未驗證，需真實桌面；Windows／OneDrive client 版本與檔案前後狀態亦未取得。具體待觀測項目見研究文件，不得用 controlled attributes 測試推論 hydration PASS。
- 未知 metadata 的虛擬 provider 沿用既有 enrichment；沒有足夠證據宣稱它或排程後變動的 provider 狀態永遠不 hydration。此次安全限制只依列舉已辨識的 cloud／recall／offline／pin metadata 生效。
