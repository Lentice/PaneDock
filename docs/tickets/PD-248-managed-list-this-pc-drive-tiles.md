# PD-248 — 「本機」磁碟並排顯示與容量用量條

Phase 8 · 目標 1–2 天

## 來源與交付

來源：使用者 2026-10-22 回報。Windows 檔案總管在「本機」把磁碟機並排為卡片，每張卡有磁碟圖示、名稱、容量用量條及 `X GB free of Y GB`；PaneDock 現在以一般明細列表顯示，沒有用量。

交付一個結果：在 managed list 開啟「本機」（My Computer，parsing name `::{20D04FE0-3AEA-1069-A2D8-08002B30309D}`）時，磁碟機以並排卡片顯示，並含用量條與剩餘／總容量文字。其他 location 的顯示不變。

## Overrides

design-spec 沒有列出此功能，也沒有列入「不在範圍」。本票新增它；若實作需要改 design-spec，只補「本機」的顯示描述，不改其他 FR。沿用 PD-247：列表不提供 accessibility provider，本票不新增。

## 綁定限制（引用）

- AGENTS：「Event-driven idle path only. No busy loops, no polling timers.」用量只在進入／Refresh 時取得，不輪詢。
- AGENTS：「The UI/Shell host hazard is reentrancy, never concurrency.」容量查詢只能在 `src/managed_list/list_worker.cpp`（有界 worker）或既有可見項目 enrichment 路徑；UI 執行緒不得呼叫 Shell／磁碟 API。worker 只回傳 owned value，不回傳 COM／PIDL。
- design-spec NFR-003：「Group／版型／tab 切換與 pane input 不得因為某個 Shell location、metadata provider 緩慢或無法連線而凍結 UI。」斷線的網路磁碟機不得拖慢整頁；單一磁碟查詢失敗顯示名稱與圖示即可。
- AGENTS：「Never persist a PIDL or a COM pointer.」不新增持久化欄位。
- AGENTS：App UI 文字必須是英文；用量文字如 `28.8 GB free of 304 GB`。
- AGENTS：每個 pane 的 DPI 為 Per-Monitor-V2；卡片尺寸、用量條高度與字型用 pane 的 DPI 縮放。
- AGENTS：`src/core` 不得含 HWND、COM、`windows.h`。

## 必讀檔案與 caller

- `src/managed_list/list_worker.h`（`Row`、`PropertyValue`、容量上限 `kMaxRowBytes`）與 `list_worker.cpp` 的 `enumerate_shell_namespace`、可見項目屬性 enrichment。
- `src/app_shell/pane_list_icons.cpp` 的 `draw_list_card`（現有 `FVM_TILE`／`FVM_CONTENT` 卡片繪製）、`set_list_view_mode`、`list_item_image`。
- `src/app_shell/pane_managed_list.cpp` 的 `LVN_GETDISPINFOW` 處理與 `update_managed_list`（狀態列）。
- `src/app_shell/pane_list_properties.cpp` 的 `accept_property_result`、`finish_property_batch`（容量核帳與 `kMaxRowBytes`）。
- `tests/unit/managed_list_namespace_worker_test.cpp`（真實 Shell 的 worker 測試寫法）與 `managed_list_icons_test.cpp`。

## 具體範圍

1. 以 Shell 屬性 `System.FreeSpace` 與 `System.Capacity` 取得每個磁碟機的剩餘與總容量（比 `GetDiskFreeSpaceExW` 好：走 Shell identity，網路／光碟機語意與檔案總管一致）。只對 Shell namespace 列舉出的「本機」子項目、且 `SFGAO_FOLDER`＋屬於磁碟機的列要求；其他 location 不查。結果存入 owned 數值欄位（新增，計入 `kMaxRowBytes` 核帳），不新增 Shell 物件保留。
2. 「本機」使用並排卡片版面（沿用 `draw_list_card` 的卡片機制，新增用量條）：圖示、名稱、用量條、`X free of Y` 文字。用量條在已用比例 ≥ 90% 時用紅色，其餘用藍色（與檔案總管一致）。屬性尚未回來時只畫圖示與名稱，不畫假的 0%。
3. 版面只在「本機」自動啟用；使用者離開後恢復該 tab 原有的 view mode，不修改持久化的 view mode 欄位。
4. 排序、選取、鍵盤導覽、右鍵選單、拖放、雙擊進入磁碟機維持不變。

## Non-goals

不做「Devices and drives」群組標題、不做網路位置分組、不新增 view mode 設定 UI、不監看容量變化（見 PD-249，用量只在進入或手動 Refresh 時更新）、不做 accessibility。

## 驗收標準

- [ ] 開啟「本機」顯示並排磁碟卡片與用量條；C:、D:、E: 的剩餘／總容量與檔案總管一致（四捨五入同一單位）。
- [ ] 已用 ≥ 90% 的磁碟機為紅色，其餘為藍色；屬性未回來前不顯示 0%。
- [ ] 一個無回應的網路磁碟機不使 UI 卡頓；其他磁碟機仍正常顯示（NFR-003）。
- [ ] 離開「本機」後，tab 恢復原本的 view mode；session 檔不變。
- [ ] 100% DPI 與 150% DPI 下卡片不截字、用量條不變形。
- [ ] 容量核帳測試通過，idle 時 CPU 與磁碟 I/O 仍為 0。

## Agent checks

在 `tests/unit/managed_list_namespace_worker_test.cpp` 新增真實 Shell 測試：列舉「本機」，至少一個子項目取得 `Capacity > 0` 且 `FreeSpace <= Capacity`；並以受控 gate 讓一個 provider 永不返回，驗證其他列仍在限定時間內出現。

```powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

視覺驗證（卡片版面、紅藍條、DPI）需要真實桌面，交給使用者；不要用 computer-use 長時間操作使用者的滑鼠。

## 交接區

（完成時只填未解風險與不可重推的結論。）
