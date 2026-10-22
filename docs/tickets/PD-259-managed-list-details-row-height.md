# PD-259 — Details 列高比照檔案總管，不再過密

Phase 8 · 目標 0.5 天

## 來源與交付

來源：使用者 2026-10-22 附上 PaneDock 與檔案總管的截圖，指出 PaneDock 的檔案列表太密，檔案總管的列距較寬、體驗較好。

交付一個結果：Details（及 List／Small icon）列高改為 26 DIP，隨 DPI 縮放，圖示垂直置中。

## 原因

ListView 的列高取決於圖示清單的儲存格高度。原本儲存格是 16×16，列高約 19 px。

## 綁定限制（引用）

- AGENTS：Per-Monitor-V2；尺寸以 pane 的 DPI 縮放。
- AGENTS：影像快取有數量與位元組上限。儲存格變高時 `image_bytes` 略為低估（256 項約 0.4 MiB，遠小於 4 MiB 上限）。

## 具體範圍

`pane_list_icons.cpp`：`raster_icon` 接受 `cell_height`，把圖示垂直置中放入較高的位圖（遮罩補 1 = 透明）；`ensure_list_icon_images` 在非縮圖模式以 `kDetailsRowDip = 26` 建立圖示清單，並記錄 `icon_cell_height_`。磚格與大圖示模式維持正方形。

## Non-goals

不改字型、不做可切換的緊湊模式、不改磚格／內容檢視。

## 驗收標準

- [x] ctest 全部通過（58/58）；`managed_list_icons`／`managed_list_sort` 的幾何假設改為不綁定列高。
- [x] 截圖：100% DPI 下列距約 27 px，圖示置中。
- [ ] 實機：150% DPI 與檔案總管並排比較（需要使用者確認，數值 26 DIP 是依截圖比例估算）。

## 交接區

- 未驗證：150% DPI、與檔案總管逐像素的列高。若不夠接近，只需調整 `kDetailsRowDip`。
