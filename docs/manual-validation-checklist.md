# 手動驗證清單（managed-list／Phase 8 遺留實機驗收）

本清單以各 ticket 目前的 Acceptance criteria 為來源，供人工在真實桌面執行。它不是測試結果，不宣告任何條件通過，也不改變任何 ticket 狀態；狀態只依 `docs/tickets.md` 的 Ticket 總覽為準。證據需回填到對應 ticket 的文件後才算數。

已完成、不得重複：PD-242 的隔離 `panedock_launch_smoke`（2026-10-21，通過，僅證明隔離環境的正常啟動與關閉，見 PD-242「隔離 launch smoke checkpoint」）。其餘 PD-242 條件仍未驗證。

## 0. 準備與安全

1. 先確認沒有 PaneDock 行程：`Get-Process PaneDock`。若有，只用視窗關閉鈕正常關閉並等待結束；任何時刻都只能有一個 PaneDock 行程，不得啟動第二份。不得使用 `Stop-Process`、`taskkill` 或任何強制終止。
2. 使用隔離 session，不碰正式資料：
   ```powershell
   $env:LOCALAPPDATA = 'E:\GitHub\PaneDock\build\manual-localappdata'
   New-Item -ItemType Directory -Force $env:LOCALAPPDATA | Out-Null
   ```
3. 以 `Start-Process build\PaneDock.exe -ArgumentList '--diagnostic' -RedirectStandardOutput <輸出檔>` 啟動一次並記下 PID。需要重啟的步驟，必須先正常關閉前一個行程、確認已結束，才可再啟動。
4. 只在你自行建立的拋棄式資料夾或已授權路徑上操作（本機兩個資料夾 A、B，最好分屬不同 volume；網路僅限 `\vianextfs06\Tmp\Lentice\test`）。不得對使用者資料執行刪除、改名或會改檔案的 verb。
5. 不要建立、變更或中斷任何網路磁碟機對應；不要變更使用者設定。
6. 右鍵選單一律保留 Windows 原生 Shell menu，不測也不要求自訂選單。
7. 每項證據都必須記錄：OS 版本（`[Environment]::OSVersion`／`winver`）、`build\PaneDock.exe` 建置時間與版本、相關 provider 版本（OneDrive、SMB 等，可得時）、日期，以及**原始**輸出與計時（貼原文或原始錄影，不要只寫摘要）。
8. 結論只有三種：**通過**（每個條件都有直接證據）、**失敗**（有觀察到的反例，保留證據）、**未驗證**（缺帳號、裝置或互動，或只有間接證據）。腳本發送訊息的結果、本機快速失敗回應、單元測試都不得當作實體操作的通過證據。
9. 結束一輪後正常關閉並記錄：結束碼、輸出中的 `live_list_count`、隔離 `session.json` 的 `clean_shutdown`、是否有殘留 PaneDock 行程。

## 1. PD-197 pane 命令、chrome 與導覽收尾

來源：PD-197 驗收（Pane 成員、`navigation_complete`、`pane_index` 使用、零行為／零視覺變更）。程式碼面的複驗已有紀錄；此處只補實機對照。

- 設定：4 pane、每 pane 多個 tab，tab 數多到出現 overflow。
- 操作：依序測 tab hover、拖曳 tab（同 pane／跨 pane）、overflow 捲動、導覽 Back／Forward／Up、地址列、pane 命令按鈕。
- 記錄：與改版前（以 git 舊 commit 建置的版本，若無則記為無基準）逐項對照的螢幕錄影或截圖；任何差異的描述。
- 判定：每項與基準一致且無異常 → 該項通過；有差異 → 失敗並附證據；沒有基準或未做對照 → 未驗證。不得以 source 檢查代替視覺結論。

## 2. PD-209 切換延遲儀器

來源：PD-209 驗收 1、3（`--diagnostic` 輸出 `panedock.*_ms=`、performance-baseline 填實測）。

- 設定：以 `--diagnostic` 啟動；建立至少 2 個 Group、4 pane、各 pane 2 個以上 tab。
- 操作：手動切換 Group 至少 5 次、切換 tab 至少 5 次。
- 記錄：輸出檔中所有 `panedock.*_ms=` 原始行（Group 切換、tab 啟用兩類）；機器規格、pane／tab 數、每次操作的大約時間點。
- 判定：兩類計時行都出現且數值可讀 → 驗收 1 有證據；未帶 `--diagnostic` 時無計時輸出需在前一行程關閉後另行啟動確認，未做 → 驗收 2 未驗證。performance-baseline 三列由使用者決定何時填入，本清單不修改。「單一不可達網路路徑」一列見第 3 節，不得以本機結果代替。

## 3. PD-226 慢網路、容量與 shutdown

來源：PD-226 三項驗收。已知限制：先前 `\192.0.2.1\PD226` 約 1.7 秒就回 Error 53，不是慢源證據；沒有既有的 mapped drive，也不得建立或中斷；真實 UNC pending 不能替代 mapped drive 的驗收。

- 設定：需要一般權限下能維持 pending 的真實來源（慢或不可達的 UNC）。若有**既有**、本來就存在的 mapped drive 且處於斷線狀態，才可用於 mapped-drive 項；沒有則該項記為未驗證，不要求任何人建立或中斷對應。
- 3.1 pending 期間的操作性：導覽某 pane 到該來源；pending 期間操作其他 pane、其他 tab、切 Group、改目的地，用計時或錄影記錄每個操作是否立即回應。離開後再重試，確認 parsing／fallback identity 保留。
- 3.2 容量耗盡：連續快速切換或重試 10 次以上；記錄私有位元組前後值、錯誤訊息是否明確、晚到結果是否覆蓋新位置。
- 3.3 shutdown：在 pending 中以正常視窗關閉，記錄關閉耗時、是否卡住、結束碼、是否有殘留行程；記錄不可取消情形與容量降級的可觀察行為。
- 判定：三項各自要有直接證據才通過。回應慢時仍可操作其他 pane 才算 3.1 通過；僅「快速失敗」→ 未驗證。有 UI 阻塞、結果覆蓋、關閉無限等待或殘留行程 → 失敗。

## 4. PD-234 虛擬 Shell location

來源：PD-234 三項驗收。已知限制：provider STA lifetime 與卡住 provider 的安全 shutdown 未驗證。

- 設定：隔離 session；先正常關閉隔離 session 的第一個行程才可重啟。
- 4.1 預設位置與導覽：新 tab、新 Group、關掉最後一個 tab，確認預設為 This PC；操作 This PC → Desktop → 子資料夾 → Up，並檢查 Back／Forward 歷史。記錄每一步的位置畫面。
- 4.2 還原：開啟 Desktop 與一個 known folder（如 Documents）後正常關閉；檢查隔離 `session.json` 的 `parsing_name`、`known_folder_identity`、`fallback_path`；再啟動一次確認還原到同一位置。
- 4.3 慢 provider：需要真實的慢 provider；沒有則記為未驗證，不能由自動化測試推論通過。
- 判定：4.1、4.2 各步與預期相符且有紀錄 → 該項通過；任何位置被丟棄或被 display name 取代 → 失敗。

## 5. PD-235 OneDrive placeholder

來源：PD-235 三項驗收。範圍：僅 `D:\OneDrive - via.com.tw`，只觀察 metadata 與狀態，不開啟或讀取檔案內容，不變更 hydration 或可用性狀態。

- 設定：需要一個已存在、明確為 online-only 的檔案；若找不到，整項記為未驗證，不要求任何人新增或改動同步狀態。目前盤點只在頂層看到目錄 `附件`，沒有檔案；列舉未知雲端目錄可能改變狀態，不要為了找檔案而展開。
- 5.1 不 hydration：瀏覽前以 `Get-Item` 記錄該檔 Attributes（含 RECALL_ON_DATA_ACCESS `0x400000`）與長度；在 PaneDock 瀏覽、捲動、排序但不雙擊、不預覽；之後再記錄一次。不得以任何方式讀取內容。
- 5.2 狀態更新與恢復：狀態或圖示更新是否指向同一 identity；其他 pane 不被阻擋；未同步／離線有可恢復行為（僅在不改變使用者 OneDrive 設定的前提下觀察）。
- 5.3 記錄 OneDrive 客戶端版本；不得宣稱 OS 或第三方 extension 永遠不會 hydration。
- 判定：前後 Attributes 與長度相同且有紀錄 → 5.1 通過；狀態改變 → 失敗並附證據；無合適檔案或無帳號 → 未驗證。

## 6. PD-236 剪貼簿與 Shell 檔案操作

來源：PD-236 三項驗收。僅用自建的拋棄式資料夾 A、B（不同 volume 各一）。

- 6.1 Copy／Cut／Paste／Delete：各做一次，記錄結果與實際檔案狀態；於 Group 1 複製、切到 Group 2 再貼上，確認剪貼簿仍有效。
- 6.2 effect 與 UI：同 volume 與跨 volume 的 move／copy 效果；製造檔名衝突、取消、部分失敗，確認出現 Windows 原生對話框且取消無殘留。
- 6.3 操作中切換：操作進行時切 tab／Group，確認沒有刪錯位置、當機或 COM lifetime 異常。
- 記錄：每步原始畫面或錄影、檔案前後清單、`live_list_count`、結束碼。
- 判定：每項與原生行為一致 → 通過；任何錯目標、遺失資料或無原生 UI → 失敗。

## 7. PD-237 inline rename

來源：PD-237 三項驗收。僅用自建拋棄式檔案。

- 7.1 Enter／Esc／點別處（focus loss）；名稱衝突、唯讀或拒絕存取、外部先刪除該檔後再 rename，各觀察一次。
- 7.2 成功 rename 後：列表只有一筆、名稱／selection／focus 與實際目錄一致。
- 7.3 F2 與側欄、地址列、全域快捷鍵的優先順序維持。
- 記錄：每步畫面或錄影、目錄前後清單。
- 判定：行為可預期且與實際目錄一致 → 通過；出現兩筆、改錯項目 → 失敗；provider 專屬行為沒有環境 → 未驗證。

## 8. PD-238 跨 pane 與外部 drag/drop

來源：PD-238 三項驗收。必須是實體滑鼠操作；腳本發訊息不算證據。僅用拋棄式檔案。

- 8.1 跨 pane（同 volume、跨 volume）拖曳，記錄游標顯示的 effect；從 File Explorer 拖入、拖出到 Explorer；拖曳中按 Esc 取消，確認未產生任何操作。
- 8.2 拖曳中懸停切 Group／tab 後 drop，確認作用於目前目的地；不可接受 drop 時有明確拒絕。
- 8.3 快速重入、切換、拖曳中關閉視窗，確認無 stuck drag、錯目標或已釋放 IDataObject 存取。
- 判定：effect 與原生一致、取消無操作 → 通過；有 stuck drag、錯目標或當機 → 失敗；沒有實體互動 → 未驗證。

## 9. PD-239 原生選單目標相容性

來源：PD-239 三項驗收。選單一律是 Windows 原生 Shell menu；本清單不要求也不評估自訂選單。

- 9.1 滑鼠右鍵項目、空白處、footer；鍵盤選單鍵與 Shift+F10：確認開啟的選單對應正確的 items 或 background。
- 9.2 選單開啟（modal pump）期間切 tab／Group 或關閉：確認未使用過期的 view 或 selection，command 目標沒有被新列表行位置改寫。
- 9.3 與 File Explorer 的原生 verbs 並排比對，列出差異；只選不改檔案的 verb，不得對使用者資料執行會改檔案的 verb。不承諾改善延遲。
- 判定：目標正確且與 Explorer 一致 → 通過；目標錯誤或當機 → 失敗；未做並排比對 → 未驗證。

## 10. PD-242 剩餘條件（isolated launch smoke 已完成，不重複）

已完成：隔離 `panedock_launch_smoke` 通過（正常啟動／關閉、`live_list_count` 歸 0、`clean_shutdown: true`）。以下仍未驗證：

- 10.1 session 恢復與四 pane 切換：由第 4 節的重啟與第 2 節的切換涵蓋，引用其證據。
- 10.2 native menu 與 operations：由第 6、8、9 節涵蓋，引用其證據。
- 10.3 完整 CTest：另行由使用者決定何時執行，本清單不要求。
- 10.4 閒置資源：閒置 10 分鐘的 CPU 應為 0%、無磁碟 I/O；記錄工作管理員或 `Get-Counter` 的原始取樣、記憶體與 handle 趨勢。
- 10.5 stuck provider 的 bounded shutdown：需要真實卡住的 provider，與第 3.3 節一併記錄；沒有則維持未驗證。
- 判定：PD-242 的狀態維持 `in_progress`，直到使用者依證據另行決定；本清單的結果不自動改變任何狀態。

## 回報格式

每項寫：步驟編號、結論（通過／失敗／未驗證）、OS／app／provider 版本、原始輸出與計時、剩餘缺口。缺帳號、裝置或互動一律寫「未驗證，需真實桌面」。
