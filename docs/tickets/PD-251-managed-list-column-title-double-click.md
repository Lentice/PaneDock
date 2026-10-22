# PD-251 — 快速連點欄標題時排序要連續切換

Phase 8 · 目標 0.5 天

## 來源與交付

來源：使用者 2026-10-22 回報在 `D:\Downloads` 切換排序時，同一排序「第一次有效、緊接著第二次可能無效、要點第三次」。

交付一個結果：在欄標題上快速連點，每一次點擊都切換一次升冪／降冪。

## 原因與證據

快速的第二次點擊會以 `WM_LBUTTONDBLCLK` 送進 Header 控制項，而 ListView 只在一般點擊時送 `LVN_COLUMNCLICK`，雙擊不送。獨立小程式（純 `SysListView32`）驗證：click → `LVN_COLUMNCLICK` 1 次；`WM_LBUTTONDBLCLK`+up → 仍為 1 次；再 click → 2 次。與「第二次無效、第三次有效」吻合。

## 綁定限制（引用）

- AGENTS：不新增 timer、輪詢、鎖或執行緒。
- 欄位分隔線的雙擊（自動調整欄寬）必須保留。

## 具體範圍

1. `pane_managed_list.cpp`：對 `ListView_GetHeader` 加 subclass；`WM_LBUTTONDBLCLK` 經 `HDM_HITTEST` 為 `HHT_ONHEADER`（欄標題）時，改以 `WM_LBUTTONDOWN` 交給預設程序。
2. `tests/unit/managed_list_sort_test.cpp`：以真實 Header 滑鼠訊息驗證 click、double-click、click 各切換一次。

## Non-goals

不改排序規則、不改欄位寬度行為。

## 驗收標準

- [ ] 測試在移除 subclass 時失敗、加入後通過（已驗證）。
- [ ] ctest 全部通過。
- [ ] 實機：`D:\Downloads` 同一欄連點，每次都切換。

## 交接區

- 未驗證：實機連點手感，以及分隔線雙擊自動調整欄寬仍可用。
