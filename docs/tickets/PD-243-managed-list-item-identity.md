# PD-243 — 列表 item identity 與顯示名稱分離

Phase 8 · PD-234 拆分 · 目標 1–2 天

## 來源與交付

來源：[PD-234](PD-234-managed-list-shell-namespace.md)、[自建列表規格](../research/2026-10-16-managed-list-spec.md) Implementation Decisions／Testing Decisions。依賴與狀態只見 `docs/tickets.md` Ticket 總覽。

交付一個結果：列項的穩定 identity 不再由 display name 決定，Pane 的選取、anchor、快取及本機通知仍保持既有行為。這是接入虛擬 namespace 前的值契約遷移，不啟用虛擬 provider。

## Overrides

覆寫 PD-234 原本單票完成 namespace 支援的工作切分，不縮減其產品驗收。2026-10-19 追查發現 `Row::name` 同時用於顯示、selection／anchor、PD-232 reconciliation、PD-233 cache；只增加列舉器會將 display name 當 identity。既有本機 leaf identity 合法，必須保留其通知 rename 語意。未重開 tracker 已否決的 fake IExplorerBrowser、Shell UI 自動化或 plugin framework。

## 綁定限制（引用）

- design-spec FR-006：「Shell identity 不使用顯示名稱或持久化 PIDL。」FR-010：「每個 tab 提供網址輸入(可鍵入或貼上)、上一頁、下一頁、上層。」本票只建立 item 值契約。
- design-spec NFR-005：「必要狀態(版型、location、tab、view mode、排序)必須精確還原」。不新增跨重啟 selection 保證。
- development：「A pane-only feature is implemented in Pane or one of its sub-modules, not in the coordinator.」「Use Microsoft::WRL::ComPtr for every interface pointer.」「Never destroy a parent HWND while a hosted view is alive.」
- AGENTS：「Keep src/core free of HWND, COM and windows.h.」「Event-driven idle path only. No busy loops, no polling timers.」「Never persist a PIDL or a COM pointer.」「New non-trivial logic needs one focused runnable test or self-check.」
- AGENTS：「A read that encounters a field it does not recognize preserves that field rather than silently dropping it on the next write-back.」沿用 schema version、atomic replace／backup，不改 session 格式。
- UI／Pane 仍為單一 STA；worker 只在 `src/managed_list/list_worker.cpp`，不交接 UI-owned COM／PIDL／HWND。pump 後重新核對 request／active／shutdown；不加 UI locks。既有 initialized ExplorerBrowser 必須先 Destroy。
- 上限維持：4 enumeration slots（含 abandoned）、每 slot 1 pending／2 batches、128 rows/batch、100,000 rows／24 MiB active rows、4 MiB recovery index、32 MiB global inactive snapshots。新增字串 capacity 也須核帳。正式預設仍待 PD-241；不改右鍵或檔案操作，不加依賴、telemetry、網路功能、服務或 elevation。

## 必讀檔案與 caller

- `AGENTS.md`、`docs/development.md`、`docs/design-spec.md` §4.7–4.10、FR-005/006/009/010/012、NFR-002/003/005、§9–11；兩份 `docs/adr/0001-cpp-wrl-win32-over-rust-and-csharp.md`、`0002-stable-pane-identity-across-layout-switch.md`；來源規格及 `docs/testing.md`、`docs/performance-baseline.md`。
- `src/managed_list/list_worker.h` 的 Row／Batch／Request；`list_worker.cpp` 的 enumerate、basic_row、decode_changes 及其 caller。
- `src/app_shell/pane.h` 的 ListItemIdentity、ManagedListInteraction、RowNameLess／recovery_index_；`pane_managed_list.cpp` 的 process_content_results、apply_list_changes、capture/restore_managed_list_interaction、list_interaction_snapshot、save/take_list_cache、open_list_row；追查排序與 cached rescan 的所有 caller。
- `src/app_shell/pane_list_icons.cpp`、`pane_list_properties.cpp` 的 row.name 消費者；本票保持 enrichment 舊入口相容，後續票接入絕對 child identity。
- `tests/unit/test_pane_host.h`、`managed_list_selection_test.cpp`、`managed_list_sort_test.cpp`、`managed_list_changes_test.cpp`、`managed_list_cache_test.cpp`、`managed_list_restore_test.cpp`、`tests/CMakeLists.txt`。

## 具體範圍

1. 擴充 owned Row 值以區分 display text 與穩定 item key／可選 absolute child ShellLocation；本機維持 parent＋leaf 的合法 identity。虛擬項目必須能攜帶 parsing-name／known-folder／fallback，不能以顯示文字拼路徑；不強迫修改 `core::ShellLocation`。
2. 將 interaction snapshots、selection／focus／anchor restore、recovery index 的比對改用 identity。排序、type-to-select、UI 顯示仍使用 display text。相同 display text 的兩個不同 item 必須可區分。
3. 既有 PD-232 本機 removed／renamed leaf 契約須明確轉換，不將 display text 當 rename key。cached reconciliation 也用同一 identity；不新增虛擬 delta provider。
4. 更新 active row、interaction 及 inactive cache 的 capacity 核帳與清理。保留目前注入 Source 的本機測試資料相容性，避免單純機械更名造成廣泛無關變動。

## Non-goals

不實作 Shell enumeration、parent lookup、virtual routing 或新通知；不修改 PD-226；不碰右鍵、OneDrive、file operations、session migration 或 completed ticket 文件。

## Acceptance criteria

- [x] 注入相同 display name、不同 parsing identity 的兩列，經排序／Refresh reconciliation／cache round-trip 後 selection 與 anchor 仍指向原項目；刪除不選到同名替代項。
- [x] 本機 rename 能轉移既有 selection identity，removed／overflow recovery 行為保持；不將 display name 寫入 ShellLocation。
- [x] 所有新增 owned identity 字串計入 24 MiB／32 MiB 上限；超限停止且可手動重試；過期 request 不改新 pane。
- [x] 正常預設與 virtual routing 保持原狀，沒有新增 UI Shell call。

## Agent checks

沿用 PaneHost／Source 注入邊界，不 fake COM；新案例擴充現有 CTest targets。僅執行聚焦檢查：

```powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target panedock_managed_list_selection_test panedock_managed_list_sort_test panedock_managed_list_changes_test panedock_managed_list_cache_test panedock_managed_list_restore_test
ctest --test-dir build --output-on-failure -R '^panedock_managed_list_(selection|sort|changes|cache|restore)$'
git diff --check
```

本票測試是 Pane 值與列表行為證據；真實 Shell、focus／accessibility 未驗證，需真實桌面。使用者已延後實機／UI 驗證，不自動執行、不以 seam 測試宣稱通過。僅有未解風險或不可重推結論時新增交接區。
