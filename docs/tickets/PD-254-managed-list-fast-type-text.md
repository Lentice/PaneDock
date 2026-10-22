# PD-254 — Type 欄改用副檔名查詢，捲動時不再停在 Loading

Phase 8 · 目標 0.5 天

## 來源與交付

來源：使用者 2026-10-22 回報拖動捲軸時，Type 欄在放開滑鼠前只看得到 `Loading...`。

交付一個結果：本機檔案的 Type 文字改為極低成本的查詢，捲動或大幅跳轉後立即出現，不必等待逐檔屬性請求。

## 原因與證據

量測（`D:\Downloads` 1348 個檔案）：worker 原本對每個檔案開啟 property store 讀 `System.ItemTypeText`，約 5.12 ms／檔（全部約 7 秒）；以 `SHGetFileInfoW(SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES)` 依副檔名查詢約 0.06 ms／檔（全部 82 ms）。每次捲動都會以 `replace` 取代待處理請求，慢請求常在完成前被丟棄，因此持續拖動時永遠停在 `Loading...`。修正後實機跳到清單深處 0.7 秒內 Type 與圖示都已載入。

## 綁定限制（引用）

- AGENTS：Shell 與檔案 API 只在 `list_worker.cpp` 的有界 worker 上執行，UI 執行緒不碰。
- AGENTS：事件驅動，不新增 timer／輪詢。
- 虛擬項目（`child_location`、Shell item identity）維持原本的 property store 路徑。

## 具體範圍

1. `list_worker.cpp` `read_property`：`System.ItemTypeText` 且為本機葉節點時，以副檔名查詢；失敗時回到原本路徑。
2. 新增 `tests/unit/managed_list_type_text_test.cpp`：檔案、資料夾、以及不存在的檔名（證明不開檔）。

## Non-goals

不預先載入視窗以外的列、不改圖示載入、不改其他屬性（Duration 等）。

## 驗收標準

- [x] 測試在停用快速路徑時失敗、啟用後通過。
- [x] ctest 全部通過（57/57）。
- [ ] 實機：按住捲軸拖動時 Type 即時更新（我只驗證程式化的大幅跳轉，滑鼠拖動需使用者確認）。

## 交接區

- 未驗證：按住捲軸拖動的實機手感。圖示載入（`load_item_icon`）仍是逐檔 Shell 呼叫，拖動時可能暫時顯示預設圖示；若使用者仍覺得慢，下一步量測圖示成本。
