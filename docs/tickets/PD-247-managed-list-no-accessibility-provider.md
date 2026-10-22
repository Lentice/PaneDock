# PD-247 — 列表不提供 accessibility provider

Phase 8 · 修正 `D:\Downloads` 切換排序與複製網址列卡頓

## Overrides

覆寫 [PD-227](PD-227-managed-list-selection-keyboard.md) 的「accessibility 可讀出名稱、焦點與選取」，以及 design-spec §4.8、roadmap Phase 8 中「PaneDock 負責 accessibility exposure」。2026-10-22 使用者決定：目前沒有 accessibility 使用需求，也尚無確定的最佳設計，先不提供。

## 新證據

`D:\Downloads`（1443 列，managed list，Release）重現：切換排序後 UI 執行緒被占滿約 3.8–4 秒，網址列複製也被同一段卡頓拖慢（探針：`SendMessageTimeout(WM_NULL)` 最長 3.8–10 s，CPU 時間約等於卡住時間）。

- `apply_managed_list_sort` 各階段（capture／sort／redraw／restore／finish）合計僅 0–76 ms；主視窗與網址列 window proc 沒有任何訊息超過 300 ms／100 ms。
- UI 執行緒堆疊取樣顯示時間落在 `COMCTL32` ListView 與 `UIAutomationCore`／`OLEACC` 的巢狀 `SendMessage`。機器上有多個 UI Automation 用戶端。
- A/B：列表視窗對 `WM_GETOBJECT` 回 0 之後，60 秒重現期間探針零卡頓。

原因：列表是 `LVS_OWNERDATA`，UIA／MSAA 用戶端走訪列時，每列文字都要在 UI 執行緒經 `LVN_GETDISPINFO` 取得。

## 變更

`Pane::list_icon_subclass` 對 `WM_GETOBJECT` 回 0。只作用於列表視窗；標頭、網址列與其他控制項不變。

## Non-goals

不設計替代的 accessibility provider。要重新提供，須另開票並證明 1000+ 列時不阻塞 UI 執行緒。

## 交接區

- 未驗證：Narrator 等螢幕閱讀器、`computer-use` 之類以 UI Automation 讀取列表項目的工具現在看不到列內容（AGENTS.md 的 Windows GUI 驗證流程只能靠截圖與視窗層級）。
- 未驗證：`PD-241` gate 的 accessibility 項目已隨 §4.8 一併不再適用；gate 已 waived，未重新評估。
