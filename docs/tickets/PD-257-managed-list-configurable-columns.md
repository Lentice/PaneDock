# PD-257 — Details 欄位可選、可拖曳排序、可調寬並記住

Phase 8 · 目標 1–2 天

## 來源與交付

來源：使用者 2026-10-22 指出檔案列表可檢視的欄位不能改動，與 Windows 檔案總管體驗不同。

交付一個結果：在 Details 的欄標題按右鍵可勾選要顯示的欄位；欄標題可拖曳排序、拖曳分隔線調寬；每個 tab 記住設定，重啟後還原。

## Overrides

design-spec 沒有列出欄位設定。本票新增它，不更動預設：預設仍是 Name、Size、Date Modified、Type、Duration、Availability，順序不變。

## 綁定限制（引用）

- AGENTS：每個持久化設定必須有向前相容設計，未知欄位寫回時保留。`columns` 是 tab 內的新增選用欄位（`"id:width,..."`），不改既有欄位意義；session 不含此欄位時載入預設；未知欄位 id 在讀寫往返中保留。
- AGENTS：不新增 timer／輪詢、不新增鎖；`src/core` 不含 HWND／COM。
- AGENTS：UI 文字英文。
- AGENTS：`TrackPopupMenu` 會泵訊息，之後必須重新確認 pane 與視窗仍有效。

## 具體範圍

1. `core/columns.h`：欄位資訊、`parse_columns`／`format_columns`／`toggle_column`；Name 永遠存在且在最前，欄位 id 不重複，寬度夾在 24–4000，未知 id 保留。
2. `TabState::columns` 與 session 讀寫（空字串不寫出）。
3. `Pane`：`apply_list_columns`（依 tab 設定重建標頭）、`capture_list_columns`（讀回顯示順序與寬度並存檔，Name 強制維持第一）、`toggle_list_column`、`show_list_column_menu`（標頭右鍵選單，Name 灰階勾選）。
4. comctl32 的資料回呼、`LVN_COLUMNCLICK` 與標頭索引一律回報「標頭位置」；以 `list_column_subitem` 轉成穩定欄位編號，排序指示、屬性需求（只對可見的 Duration 欄發請求）同步改用。
5. `LVS_EX_HEADERDRAGDROP`；`HDN_ENDTRACK`／`HDN_ENDDRAG`／`HDN_DIVIDERDBLCLICK` 後以訊息延後讀回版面。

## Non-goals

不做「More...」欄位對話框、不新增 Size 以外的欄位、不做依資料夾類型的設定（設定屬於 tab）。

## 驗收標準

- [x] 純邏輯測試：`columns`（解析、夾限、未知 id 保留、切換）與 `core_session`（往返、可省略）。
- [x] `managed_list_sort`：隱藏欄位後標頭位置的點擊仍排序正確欄位；拖曳排序後顯示順序存入；Name 不能離開第一位；再顯示欄位時附加在最後。
- [x] 實機截圖：自訂 `name,type,size,duration` 時，標頭與儲存格內容對齊、排序箭頭在 Size。
- [ ] 實機：右鍵選單勾選、拖曳排序、拖曳調寬後重啟仍保留（需要滑鼠操作，由使用者確認）。

## 交接區

- 未驗證：標頭右鍵選單與滑鼠拖曳的實機行為。
- 結論：comctl32 在資料回呼與 `LVN_COLUMNCLICK` 回報的是「標頭位置」，不是 `LVCOLUMN::iSubItem`（已用獨立程式與實機截圖確認）；不要再把它當作穩定欄位編號使用。
