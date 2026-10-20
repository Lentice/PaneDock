# PD-235：placeholder 中繼資料與免 hydration 邊界

## 第一手依據

- [FindFirstFileExW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw) 搜尋 directory entries 並回傳 metadata；沿用 `FindExInfoBasic`，不開啟 child data stream。
- [Reparse Point Tags](https://learn.microsoft.com/en-us/windows/win32/fileio/reparse-point-tags) 說明 `FILE_ATTRIBUTE_REPARSE_POINT` 存在時，find data 的 `dwReserved0` 是 tag；列出 CLOUD、CLOUD_1…F、CLOUD_MASK 及舊 ONEDRIVE／FILE_PLACEHOLDER tag。
- [File Attribute Constants](https://learn.microsoft.com/en-us/windows/win32/fileio/file-attribute-constants) 定義 recall、offline 與 pinned/unpinned。PINNED 是保留本機資料的意圖；RECALL_ON_DATA_ACCESS 表示資料未完整在本機，不能據此區分完全 online-only 與部分下載。
- [CfGetPlaceholderStateFromAttributeTag](https://learn.microsoft.com/en-us/windows/win32/api/cfapi/nf-cfapi-cfgetplaceholderstatefromattributetag) 僅接受 attributes/tag，文件允許從列舉取得輸入；不需要為取得狀態開啟檔案。此次工具鏈沒有 cfapi.h／CldApi import library，因此沒有自行宣告 ABI 或新增該呼叫。直接保留 attributes/tag，僅解讀上述已記載的旗標，不模擬完整 CF_PLACEHOLDER_STATE。
- [SHGetFileInfoW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shgetfileinfow) 的 USEFILEATTRIBUTES 模式明訂不存取目標；現有 PIDL／overlay 路徑並非該模式。沒有建立現有項目圖示／overlay、thumbnail factory 或任意 property handler 的免 hydration 契約，因此已辨識 cloud placeholder、recall/offline/pin 項目不呼叫這些 API。

## 實作邊界與追查結論

`enumerate`、notification `decode_changes` 及 recovery enumeration 共用 `basic_row`，複製 attributes/tag、size、time，不讀檔案內容。Shell namespace enumeration 仍用原有 parsing identity、child location 與 SFGAO_FOLDER；不新增 property handler 取得 cloud 狀態。未知屬性顯示 Unknown，不將 display text／fallback path 當 identifier。

Availability 欄位是列舉屬性的顯示：Not fully local、Locally available、Always available (pinned)、Pinned - not fully local；矛盾的 pinned/unpinned 顯示 Unknown。它不是 provider sync health 或精確 online-only／partial hydration 的診斷。普通檔案不附加雲端狀態；未知 metadata 本身不當成 cloud 證據。

`Pane::list_icon_key` 攜帶相同 metadata，`IconKey::matches` 同時檢查 metadata 與既有 identity；cloud 變化不能沿用舊 key。Refresh／通知沿用現有 generation、selection/anchor 與圖示失效規則。rescan 的 unchanged 比較納入 cloud metadata，避免相同 size/time 留下舊狀態。沒有改變圖示快取的跨 navigation 政策。

`run_icons` 是影像與屬性工作的共同呼叫邊界，已辨識 cloud 或 recall/offline/pin 時回傳空影像／missing optional property，保留本機產生的 file/folder 圖示。該路徑不取得 thumbnail，也不為 cloud 狀態開啟資料。原生 Open、選單與 IFileOperation 不受此限制；它們可能因使用者操作由 OS hydration。

普通本機項目、非雲端 reparse point、未知 metadata 的虛擬項目沿用原有 enrichment，沒有全域停用 provider；這不等於其 API 或第三方 provider 已通過免 hydration 證明。虛擬 provider 未提供 cloud metadata、排程後 metadata 變化，以及 OS／extension 自行觸發的行為仍是實機邊界。列舉虛擬化資料夾也可能由 OS 取得目錄內容，不能宣稱完全沒有 provider／網路活動。

## 尚未取得的實機證據

依使用者指示跳過真實帳戶、GUI 與完整 CTest。以下全部 **未驗證，需真實桌面**：

- online-only 檔案基本列舉前後的 residency／hydration、基本圖示顯示前後觀測。
- locally available 與 always available 檔案的真實 attributes/tag、顯示結果與瀏覽前後 residency。
- online-only／locally available／always available 相互轉換後的通知、Refresh、selection／navigation 目標。
- 離線／未同步／provider 錯誤的恢復，以及其他 pane 的實際回應性。
- Windows build、OneDrive client 版本、帳戶／sync-root、各測試檔案與其前後狀態：此次均無觀測資料，不填造數字或 PASS。

controlled attributes／隱藏控制項／有界 worker 測試只能驗證 state-to-row、identity 與 fallback 行為，不能當作 hydration 證據；PD-241 gate 仍未通過。
