# PD-255 — 圖示兩階段載入：先依檔案類型、再逐檔精確圖示

Phase 8 · 目標 1 天

## 來源與交付

來源：使用者 2026-10-22 回報拖動捲軸時圖示一直是預設圖示，要求比照檔案總管的體驗（體驗優先、效能其次）。

交付一個結果：可見列在捲動後數十毫秒內就顯示正確的「檔案類型圖示」，隨後才由逐檔載入的精確圖示（含覆蓋圖示）取代。

## 原因與證據

量測（`D:\Downloads` 1348 檔）：逐檔圖示（解析 PIDL＋`SHGFI_ICON|ADDOVERLAYS`）約 6 ms／檔，且被捲動取消的請求要重做；整個資料夾只有約 169 種不同的類型圖示。實機：遠跳後 150 ms 時所有可見列已是正確類型圖示，2.5 秒內全部完成。Duration 欄原本對每個檔案開 property store（約 5 ms）才知道不是媒體。

## 綁定限制（引用）

- AGENTS：Shell／檔案 API 只在 `list_worker.cpp` 的有界 worker 上執行；結果為 owned 值。
- AGENTS：影像快取有位元組與數量上限；類型圖示與逐檔圖示共用同一個 `icon_cache_` 與 LRU，不新增快取。
- AGENTS：事件驅動，不新增 timer／輪詢；失敗的類型請求在同一個 viewport 內不重試。

## 具體範圍

1. `IconRequest::by_type`；類型 key 為 identity `~.ext`（無副檔名為 `~`，副檔名小寫），資料夾以 `directory` 區分。虛擬（Shell item）列不走第一階段。
2. `shell_core::load_extension_icon`（`SHGFI_USEFILEATTRIBUTES`，不開檔）；把點陣化抽成 `rasterize_icon` 供兩條路徑共用。
3. `Pane`：`list_type_icon_key`、`append_type_icon_requests`（排在屬性與逐檔請求之前，同類型只送一次）、`list_item_image` 在沒有逐檔圖示時回傳類型圖示。
4. Duration：以 `AssocGetPerceivedType` 判斷非音訊／視訊類型，直接回傳 `missing`，不開檔。
5. 測試：`managed_list_icons`（類型請求在前、同副檔名共用、逐檔圖示取代、失敗不重試）、`managed_list_type_text`（Duration 規則）。

## Non-goals

不使用 Windows 系統圖示清單（`SHGetImageList`）；不為縮圖模式做第一階段縮圖；不預取視窗外的列。

## 驗收標準

- [x] 遠跳後 150 ms 內可見列有正確類型圖示（截圖驗證）。
- [x] ctest 全部通過（57/57）。
- [ ] 實機：按住捲軸拖動時圖示即時更新（我只驗證程式化遠跳，滑鼠拖動需使用者確認）。

## 交接區

- 未驗證：滑鼠拖動手感；OneDrive／Dropbox 覆蓋圖示在第二階段才出現（第一階段不含覆蓋圖示，與檔案總管的逐步顯示一致，但未實測）。
- 結論：類型圖示快取與逐檔圖示共用 LRU；測試替身把 `by_type` 請求分到 `type_interest`，舊測試的 `interest` 仍只含逐檔請求。
