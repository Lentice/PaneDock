# Commander 類檔案管理器的效能處理

研究日期：2026-10-16。範圍：官方文件、官方 release notes、作者／官方支援回覆；沒有執行跨產品 benchmark。以下「確認」指來源明文支持，不代表已在目前 PaneDock 工作負載驗證。沒有以閉源產品的外觀推斷內部架構。

同日後續產品決策：使用者已決定自行管理列表，並核准 PD-222～PD-242 的遷移切片；
本研究不再作為是否允許自建列表的 gate。右鍵選單仍排除改造，只做必要原生相容性接線。
以下來源事實與證據限制保留，工程適用性須依新列表方向解讀。

## 已確認的做法

| 產品 | 第一手證據 | 可借鏡之處與限制 |
| --- | --- | --- |
| Explorer++ | 1.4 beta release notes 說明：欄位內容只取可見項目；導覽／關閉 tab 後不能把舊結果寫入新列表；圖示快取；infotip 改在背景取得。另曾針對背景右鍵選單忽略 OneDrive extension，以修復 crash。[官方版本紀錄，Explorer++ 1.4.0 beta 的 Bug fixes／Misc](https://github.com/derceg/explorerplusplus/releases) | 延後昂貴資料、拒收過期結果與快取是具體方法。這些紀錄沒有證明檔案列舉或所有排序都在背景完成；忽略特定 extension 是歷史相容性修補，不能直接當作 PaneDock 的封鎖名單。 |
| Double Commander | 提供「Load file list in separate thread」、「Load icons after file list」、啟動時不讀 inactive tab；圖示有標準／關聯／EXE-LNK 等等級，並可依目錄停用特殊圖示；auto-refresh 可對背景視窗或指定路徑停用。[官方設定說明，Files views／Icons／Auto refresh](https://doublecmd.github.io/doc/en/configuration.html) | 把首次列表與裝飾工作分階段，提供針對慢路徑的降級。文件描述的是選項能力，這裡沒有確認預設值、工作佇列、取消或執行緒隔離細節。 |
| Double Commander | Windows 版本使用系統右鍵選單並加入自己的 Actions submenu。[官方 Basic Help，Context menu](https://doublecmd.github.io/doc/en/help.html) | 自己控制列表不代表移除 Shell 選單依賴。來源沒有宣稱選單 extension 有獨立程序隔離或逾時。 |
| Total Commander | 作者明確回覆 overlay 在背景載入、前景繪製。[作者回覆，2013-10-14](https://ghisler.ch/board/viewtopic.php?t=38393)；另說 EXE/LNK 圖示在背景取得，讀取 EXE 可能引發防毒掃描。[作者回覆，2016-06-19](https://www.ghisler.ch/board/viewtopic.php?p=311475) | 即使已非同步，系統 I/O／防毒成本仍存在。這是歷史實作說明，不能據此宣稱目前版本所有圖示處理或 Shell 呼叫都不阻塞。 |
| OneCommander | 官方支援說明，Shell menu 各 extension 的初始化與判斷可能延遲整個選單；建議辨識慢 extension。[官方支援回覆，2025-08-15](https://groups.google.com/g/onecommander/c/ZSwf4-A9L-k) | 這是其他檔案管理器同樣面對 Shell menu 延遲的第一手案例，不是 PaneDock 此次卡頓的歸因證據，也沒有揭露它的列舉或隔離實作。 |
| OneCommander | 作者指出：在 OC 隱藏選單項目仍有載入成本；當時有不等待所有項目才顯示的選項；v2 曾用不含第三方項目的自有選單，使用者不喜歡。[作者回覆，2022-09-22](https://groups.google.com/g/onecommander/c/9YHPT1BxcAQ) | 「藏起來」和「不載入」不同；基本選單能換速度，但整合功能與使用習慣是實際取捨。這是歷史行為，未確認目前版本選項。 |
| OneCommander | 官方支援說明 name／size／date／type 在其列舉時取得；duration 等 metadata 透過較慢的 Windows COM，排序要等所有 key 到齊，漸進排序又會讓項目持續跳動。[官方支援回覆，2026-01-19](https://groups.google.com/g/onecommander/c/9k-TMTz9CWk) | 用名稱排序與媒體屬性排序做對照，是具體且低成本的診斷。這是 OC 自身資料路徑說明，不能泛化為所有 Windows filesystem 的所有 type 查詢免費，或宣稱 PaneDock 一樣。 |

## 對 PaneDock 的適用性

以下為本研究的工程判斷，沒有將其他產品的做法宣稱為已驗證的 PaneDock 修正。

1. **可立即參考：先分辨工作。** 把舊 tab 狀態擷取、location 解析、導覽呼叫、回呼／後續更新分開量測；用基本本機資料夾、雲端目錄、EXE/LNK 多的目錄，以及不同 view mode／排序欄位作對照。這能測試「延後圖示／減少屬性工作」是否值得追。
2. **需要追現有實作：避免重複讀取與隱藏工作。** Double Commander 的 lazy tab 是參考案例；PaneDock 已規定 inactive tab 只存資料，不應以背景預熱全部 tab 換取切換速度。應檢查一次啟用是否發出多次導覽或不必要的 Shell 狀態查詢。
3. **在新列表落實背景列舉／可見列屬性。** 現有程式仍使用 `IExplorerBrowser`，無法直接接管 Shell view 的內部 pipeline；新列表可自行安排這些工作，應依遷移票建立、驗證，不要只對舊 host 加背景包裝就宣稱完成。
4. **保留 apartment 與生命週期紀律。** 新方向允許列表所需背景工作，正式限制由產品契約票對齊；UI-owned COM 指標不能直接搬到 worker。其他產品使用 worker 不是任意跨 apartment 存取的依據。
5. **列表與選單分開決策。** 自建列表本身不能保證解決 menu extension 卡頓。使用者已排除本次自有基本選單與其他選單改造；僅保留 identity snapshot 到原生 menu 的必要接線及日後可替換性。

## 尚未確認

- 本次第一手證據沒有建立：上述產品普遍透過獨立 helper process 隔離／終止慢的傳統 `IContextMenu` handler。不可把「背景載入圖示」說成「Shell extensions 已有程序隔離」。
- Explorer++ 的 visible-only 欄位取得不能推論為 visible-only 排序；排序可能需要所有項目的 key。尚未追出其目前排序資料路徑。
- 尚未取得相同資料集、cold／warm cache、相同 extension 與 view 設定下的跨產品耗時。產品宣稱快速不是效能證據。
- Shell、儲存、雲端、防毒與 PaneDock host 各自占多少時間，仍需要這台機器卡頓當下的 trace。

落地依已核准遷移切片進行，將「減少、延後、快取昂貴工作」加入對應票的實作提示，並以可歸因的階段計時、圖示／縮圖／排序對照驗證。自建列表來自使用者的產品決策，不是本研究已證明能消除此次卡頓或優於其他產品的結論。
