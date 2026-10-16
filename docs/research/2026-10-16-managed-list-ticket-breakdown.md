# 自建列表：使用者核准的切分紀錄

來源：[功能規格](2026-10-16-managed-list-spec.md)。使用者於 2026-10-16 確認本切分，
已建立 PD-222～PD-242，依序對應下列 1～21。以下為核准當時的討論紀錄；
現行 status 與 dependencies 只見 [Ticket 總覽](../tickets.md)，不以本紀錄追蹤後續狀態。

右鍵選單排除產品改造；第 18 項僅為移除 Shell view 所必要的既有原生行為相容性。
測試主介面已由使用者確認為 Pane 公開操作／可觀察列表狀態，沿用 PaneHost；
Shell／network／OneDrive 另外真實整合驗證，不再重問此決策。

## 遷移方式

先 expand：保留現有 Shell view，讓自建列表在明確的開發驗證入口中運作；
再逐個使用情境補齊，最後通過驗收才改預設並 contract 移除舊路徑。
開發入口不新增持久化格式，也不作為可以遺失既有功能的發佈藉口。
每票以半天至兩天為目標；若追查後無法達到，須在正式寫票前再拆，不能把架構遷移假裝成一張小票。

## 建議切片與 blocking edges

| 討論編號 | 標題 | Blocked by | 可獨立驗證的交付 |
|---|---|---|---|
| 1 | 對齊自建列表的規格與工程限制 | 無 | 更新原生列表／單一 STA 限制的覆寫、術語與 UX 優先順序；明定保留功能與測量口徑。 |
| 2 | Pane 內容入口 expand，保持既有行為 | 1 | 將導覽、焦點、選取目標與內容生命週期集中到 Pane 的必要入口；現有 Shell view 流程與測試維持正常，不加未使用的 plugin framework。 |
| 3 | 單 pane 本機資料夾分批列表 | 2 | 開發入口可瀏覽真實本機資料夾，分批出現名稱／基本資訊、開啟子資料夾及返回；loading/error、generation、取消訂閱、基本資源上限與安全清理隨切片交付。 |
| 4 | 自建列表接通 tab、Group 與 session 復原 | 3 | 四 pane 的切換、Back/Forward/Up、地址輸入與重啟恢復指向正確位置；晚到結果不污染新 binding，inactive tab 不新增完整列表常駐。 |
| 5 | 慢網路、容量耗盡與 pending shutdown | 3 | 慢／不可達 UNC 可離開與重試，其他 pane 可操作；模擬容量耗盡驗證明確降級、無無界排隊，正常關閉不等任意慢列舉。不可安全中止的情形需明列技術結論，不引入 menu 隔離。 |
| 6 | 列表選取、鍵盤與 accessibility | 3 | 滑鼠與鍵盤多選、範圍選取、type-to-select、焦點及可讀的 accessibility 狀態；分批加入項目不改選取 identity。 |
| 7 | 基本排序與穩定更新 | 6 | 依基本欄位排序，批次結果與重新排序維持選取與捲動錨點；排序方向可保存還原。 |
| 8 | 可見項目圖示與 overlay | 3 | 先出基本內容再補可見項目圖示／overlay，舊目的地與離開 viewport 的工作不搶前景，影像及排隊數有界。 |
| 9 | 縮圖與既有 view mode 對應 | 8 | 可見範圍按需縮圖，切換既有 view mode 與大小有正確呈現／還原；失敗保留可操作列表，無全目錄縮圖預熱。 |
| 10 | 昂貴屬性欄位與排序 | 7 | 媒體等慢 key 不阻塞切換；資料未齊有 loading，排序完成一次穩定更新並保留 identity／錨點，失敗有明確政策。 |
| 11 | 本機變更通知與恢復掃描 | 7 | 外部新增／刪除／重新命名反映到目前列表，保留選取；通知溢出會合併補掃，無 polling 與 idle 自旋。 |
| 12 | tab 列表 snapshot 的全域有界快取 | 4, 7, 11 | 返回近期 tab 可先顯示快取並更新，過期目標不拿來直接操作；跨 tab 淘汰符合上限，回復 anchor 不依賴 row number。 |
| 13 | 虛擬 Shell location 導覽 | 4 | My Computer、Desktop 等既有支援位置可列舉／導覽，保存 Shell identity；不能當成一般路徑，慢 provider 結果受 generation／資源限制約束。 |
| 14 | OneDrive placeholder 瀏覽 | 8, 13 | 自建列表顯示與辨識 placeholder，基本瀏覽不主動讀內容 hydration；狀態改變可刷新，以實機確認 icon／cloud 相容性。 |
| 15 | 自建選取接回剪貼簿與 Shell 檔案操作 | 6, 13 | Copy/Cut/Paste/Delete 使用目前 identity，保留原生進度／衝突及跨 Group 操作，不以自行拼 path 寫 filesystem。 |
| 16 | 自建列表 inline rename | 11, 15 | F2／編輯／取消／提交透過 Shell 重新命名，錯誤可恢復，成功後選取與焦點跟隨新 identity。 |
| 17 | 跨 pane 與外部 drag/drop | 4, 15 | 正確目標及 modifier 語意，保留跨 tab／Group hover、取消與 pending 切換；以實際 IDataObject／OLE 驗證。 |
| 18 | 原生選單目標相容性接線 | 6, 13 | 滑鼠／鍵盤與背景入口交給既有原生策略，對目前 identity 作用；列表不持有 menu COM／verb index。不改選單呈現、速度、過濾或隔離。 |
| 19 | 新列表的 live resize 與 mixed DPI | 3 | 八種 layout template、splitter／主窗 resize 與 DPI 轉移下，列表與 pane chrome 幾何及 hit targets 一致，選取／捲動不中斷。 |
| 20 | 自建列表相容性及效能 gate，通過後切預設 | 5, 9, 10, 12, 14, 16, 17, 18, 19 | 執行完整功能矩陣與 cold/warm／大目錄／四 pane／idle 測量，補齊未被依賴直接涵蓋的 keyboard/accessibility；符合第 1 項定義的門檻才切預設。不把未量測當成 PASS。 |
| 21 | contract：移除舊 Shell view 路徑 | 20 | 確認無 caller 後移除 IExplorerBrowser 與僅服務舊內容的路由／測試，保留新列表必要 Shell 整合；build、CTest 與替換後的啟動／關閉驗證通過。 |

第 1 項是規格前置、第 2 項是必要 prefactor、第 20/21 項是整合與 contract，
其餘為可展示的使用情境；不另外拆出只有 model、只有 queue、只有 UI 的水平實作票。
表列依賴以直接 gate 為主，沒有因檔案重疊就強制串成一條線。
第 20 項依賴其餘功能的終端節點，間接包含第 4、6、7、8、11、13、15 項。

## 正式票的共同約束

- 新增非 trivial 邏輯至少一個 runnable behavior test／self-check；Shell 整合不以 fake COM 代替實機。
- 在各切片內測安全失敗與清理，不將它們全留到第 20 項。
- 所有程序內 worker／UI 所有權與 shutdown 契約須一致；UI 物件不可被背景工作直接存取。
- 不預先批准第三方 extension process isolation；若第 5 項證明達不到既定 UX 契約，先記錄證據調整設計，不能無限加 worker。
- 保留未知持久化欄位，不加入無關 schema migration；檔案操作繼續經 Shell。
- 建立正式本地票時使用現有 repository 的 tickets 位置及 tracker 單一狀態規則，不另建 scratch 版狀態副本。
- GitHub 發布的 tracker／triage 設定尚未完成；目前缺 ready-for-agent 標籤。技能要求先執行 /setup-matt-pocock-skills。

## 核准與落地

使用者已回覆「OK」核准粒度與依賴，已依專案本地 tracker 慣例建立一票一檔。
後續實作若發現超過半天至兩天，依 tracker 規則再拆，不以本歷史清單作為禁止調整的理由。
GitHub issues 未發布；仍需先完成 tracker／triage 設定。
