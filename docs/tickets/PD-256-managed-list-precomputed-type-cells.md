# PD-256 — 列舉時預先填入 Type／Duration，拖動捲軸不再出現 Loading

Phase 8 · 目標 0.5 天

## 來源與交付

來源：使用者 2026-10-22 回報按住捲軸拖動、尚未放開時，Type 欄仍可能顯示 `Loading...`。

交付一個結果：本機檔案的 Type 與 Duration 儲存格在列舉資料夾時就已填好，捲動中不需要任何 worker 請求。

## 原因與證據

拖動時每個新的可見範圍都會以 `replace` 取代 worker 佇列；Type 文字請求排在約 20 個冷快取類型圖示之後（每個約 5 ms），持續拖動時永遠輪不到。Type 與「Duration 是否不適用」都只取決於副檔名（`SHGetFileInfoW` 0.06 ms、`AssocGetPerceivedType`），不需要逐列請求。

## 綁定限制（引用）

- AGENTS：Shell／檔案 API 只在 `list_worker.cpp` 的 worker 上；回傳 owned 值。
- AGENTS：列的記憶體受 `kMaxRowBytes` 約束；`owned_row_bytes` 已計入屬性文字。

## 具體範圍

`list_worker.cpp` 的 `TypeCells`：每次列舉（本機列舉與變更 delta）一份副檔名快取；`properties[0]` 填 Type 文字，非音訊／視訊類型的 `properties[1]` 設為 `missing`。虛擬／Shell namespace 列維持原本逐列請求。

## Non-goals

不改 Shell namespace 列；不預先載入圖示；不改媒體檔的 Duration 讀取。

## 驗收標準

- [x] ctest 全部通過。
- [ ] 實機：按住捲軸拖動時 Type 欄不出現 `Loading...`（我只驗證程式化捲動，需使用者確認）。

## 交接區

- 未驗證：拖動的實機手感。媒體檔（mp3、mp4）的 Duration 仍要逐列讀 property store，拖動時可能暫時顯示 `Loading...`。
