# PD-252 — 捲動後的列要載入 Type／圖示；Size 用千分位 KB

Phase 8 · 目標 0.5 天

## 來源與交付

來源：使用者 2026-10-22 回報 `D:\Downloads`（1443 項）以 Size 排序並往下捲動後，Type 欄的後段檔案一直顯示 `Loading...`；Size 顯示 `2473 bytes`，Windows 檔案總管顯示 `2,473 KB` 且靠右。

交付兩個結果：(1) 不論捲到哪裡，可見列都會取得 Type／Duration／圖示；(2) Size 以進位到整數 KB、千分位分隔、靠右對齊顯示。

## 原因與證據

`refresh_list_icon_viewport` 用 `ListView_GetNextItem(LVNI_VISIBLEONLY)` 找可見列。`LVS_OWNERDATA` 清單上它不隨捲動位置改變，永遠回傳前 `kIconInterest`（128）列。診斷記錄（`request_list_images rows=128 changed=0`，捲動前後 interest 完全相同）及實機截圖：第 128 列之後全是 `Loading...`，worker 閒置。改為 `ListView_GetTopIndex` 起算，逐列以 `LVM_GETITEMRECT` 與 client 區比對，超出即停止後，捲到列表最深處的 Type、圖示都正常載入。

## 綁定限制（引用）

- AGENTS：事件驅動，不新增 timer／輪詢。
- AGENTS：UI 文字英文；`KB` 為單位縮寫。
- 工作單位仍受 `kIconInterest`／`kIconValueBytes` 限制，可見列不會超過視窗大小。

## 具體範圍

1. `pane_list_icons.cpp`：`refresh_list_icon_viewport` 以 top index 加列矩形取得可見列（report/icon 向下流動，list 向右流動）。
2. 新增 `managed_list/size_text.h` 的 `file_size_text`，套用於 Details 與卡片；Size 欄 `LVCFMT_RIGHT`。
3. 新增 `tests/unit/managed_list_size_text_test.cpp`。

## Non-goals

不改 Type／Duration 取得方式、不改 worker、不改資料夾的 `Folder` 顯示。

## 驗收標準

- [ ] 實機：D:\Downloads 以 Size 排序後捲到任何位置，可見列的 Type 與圖示在數秒內出現（已用程式捲動截圖驗證前段與最深處）。
- [ ] Size 顯示如 `29,091 KB`，靠右（已截圖驗證）。
- [ ] ctest 全部通過（54/54）。

## 交接區

- 未驗證：icon／tile／list 檢視模式下的可見列範圍；目前只驗證 Details。`ListView_GetTopIndex` 在圖示檢視的行為未實測。
