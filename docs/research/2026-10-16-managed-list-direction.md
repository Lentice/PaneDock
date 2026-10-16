# 自行管理檔案列表：產品方向與設計草案

## 使用者決策

2026-10-16 使用者提出自行管理列表，參考 Commander 的優點，並明定取捨順序：

1. UX。
2. CPU／memory usage、power saving。
3. 程式架構與可維護性。

同日使用者進一步限定：**先處理列表，coding 時保留右鍵選單未來可變動性，這次不處理右鍵選單。**
因此以下實作順序以列表為主；不把基本選單、選單降級或 extension 隔離綁進列表遷移。

此為新的產品方向，不是一次 tab lag 的局部修補。它取代「不重刻檔案清單」作為未來設計的前提。
目前程式仍是 IExplorerBrowser host；本文件只記錄決策與設計草案，沒有宣稱遷移已完成。
`docs/design-spec.md`、`docs/development.md`、`AGENTS.md`、`CONTEXT.md` 與測試規範
仍描述舊架構；正式實作票之前需一起整理衝突條款。歷史 completed tickets 不重寫。

參考研究：[其他 Commander](2026-10-16-commander-performance.md)、
[低負擔 tracing](2026-10-16-low-overhead-tracing.md)。以下均為設計建議，尚未作 API 相容性與效能驗證。

## UX 契約建議

- 慢資料夾只讓該 tab 顯示 loading／error；切換 tab、改目的地、操作其他 pane 與關閉視窗仍有回應。
- 列舉分批回傳，列表先可操作；圖示、overlay、縮圖、非必要欄位按可見範圍延後取得。
- 使用者切換目的地時，立即取消訂閱舊工作，忽略舊 generation 的結果。
  取消等待不等於已中斷底層 I/O，必須限制殘留工作與資源數量。
- 不因每個補齊的屬性不斷重排列表；昂貴排序有明確 loading 狀態，完成後保持選取與捲動錨點。
- 快取可提供即時畫面，但標示更新中；操作前重新解析 identity，不能把快取存在當成檔案仍存在的證明。
- 鍵盤、多選、範圍選取、type-to-select、inline rename、拖放、DPI 與 accessibility 都是自建列表的功能成本。
  不能只以「檔名出現更快」當成 UX 完成。

## 模組責任草案

| 模組 | 責任 |
|---|---|
| core 的列表狀態 | 值型 identity token、generation、列資料、選取／錨點、排序狀態；不含 COM／HWND。 |
| 列表 UI | 可見範圍繪製／虛擬化、鍵盤與滑鼠互動、accessibility；不執行慢列舉與 Shell 查詢。 |
| 列舉與補資料 | 有界背景工作、分批結果、取消／過期回覆、共用容量限制；不逐檔案建立 thread。 |
| Shell 整合 | namespace identity、IDataObject、IFileOperation、第三方 verbs；COM 物件由建立它的 apartment 擁有。 |

保留 Win32/C++ 與既有 Group、pane、tab、導覽歷史和持久化模型。
自建列表不等於自建複製／刪除引擎，檔案操作繼續交給 Shell。
一般 filesystem 列舉與虛擬 Shell namespace 列舉是否採不同路徑，需要技術驗證；
不能先刪除現有 parsing-name／known-folder／fallback identity 契約。

## 右鍵選單：本次排除，保留整合位置

使用者已將右鍵選單排除於本次工作。前一版的「基本選單＋完整 Windows menu」建議
不列入本次範圍，也不視為已採用的產品決策。

- 維持原生 Shell 選單的功能與呈現；不新增自有基本選單、過濾、延遲策略、timeout、helper process 或封鎖名單。
- 列表模型只輸出目前資料夾與選取項目的值型 identity snapshot，不儲存 IContextMenu、HMENU、verb index 或 callback。
- 滑鼠右鍵與鍵盤選單請求匯到同一整合位置；列表只提供目標 snapshot 與顯示位置，
  Shell 整合持有 COM／選單生命週期與命令執行。空白處代表背景，不能誤用舊的選取目標。
- 先使用現有 Shell 策略的具體實作與小型入口，不為未知的未來選單新增 plugin framework 或多層抽象。
- 列表可操作性的驗收與原生選單延遲分開；本次不得宣稱已解決 Shell menu hang。

已追查的耦合：現有 item menu 由 live Shell view 提供，host 的 folder menu 又透過
`IShellView::GetItemObject(SVGIO_BACKGROUND)` 取得；移除該 view 後不能假設現有取得方式原封可用。
正式列表接入時允許補上維持相同行為所必要的 identity／selection 到 Shell 的接線，
但不藉此改選單產品行為。這是替換 view 的相容性工作，不是選單優化。

## 資源與省電原則

- UI 顯示虛擬化不代表資料記憶體有界；列資料、字串、排序 key、快取、工作佇列都需容量／淘汰政策。
- inactive tab 優先留位置、選取與捲動錨點；列表 snapshot 使用全域有界快取，不永久保留每個 tab 的完整資料。
- 工作分成互動必需與可延後，先服務可見 pane；縮圖等昂貴工作只排可見範圍與有限預取。
- 背景 worker 數量與排隊數全域有界；慢 I/O 不可讓所有 pane 共用的可用容量永久耗盡。
- filesystem change notifications 驅動增量更新；通知溢出才合併重新掃描，不使用輪詢 timer。
  虛擬位置與不可靠通知另訂行為，不承諾通知本身永遠沒有成本。
- 不設計預熱全部 tab、逐檔案重排、busy wait、持續寫 log；電池情境降低可選的預取工作。
- 若要隔離不可取消工作，須量 helper process 的固定記憶體與回收成本，不能以 worker 數有界推論 UX 已有界。

## 分階段驗證建議

1. **需求與規格對齊**：保留現有 navigation／virtual folders／cloud／拖放功能矩陣，
   明定哪些必須同版保留、哪些允許分階段；同步更新目前互相衝突的架構條款。
2. **單 pane 技術驗證**：本機、慢 UNC、取消後切換、10 萬項目；比較虛擬列表與分批列舉，
   驗證基本操作與狀態模型。此階段不是可發佈的完整替代品。
3. **Shell 相容性驗證**：虛擬位置、OneDrive placeholders、原生檔案操作、外部與跨 pane 拖放；
   原生 menu 只驗證替換列表後仍能對正確選取／背景使用既有功能，不調整選單策略。
   列表列舉與補資料另驗證永久不返回的工作及容量耗盡；相關設計不能擴張成 menu 隔離工作。
4. **四 pane 與 UX 完整度**：鍵盤、多選、rename、accessibility、mixed DPI、排序時穩定性、
   session 復原與快取淘汰；測項必須涵蓋既有工作流程。
5. **量測後切換正式路徑**：不同 cache 狀態下記錄首次可操作、完整列舉、切換回應、
   CPU time、peak／steady memory、idle I/O 與背景喚醒；同時量測故意慢／hang 的工作。

UX 優先不等於取消資源驗收；互動延遲、最大記憶體、背景活動與等待隔離要各有測量。
目前沒有數字支持「重寫後一定更快／更省」，也沒有承諾能消除 OS 或第三方內部所有等待。
