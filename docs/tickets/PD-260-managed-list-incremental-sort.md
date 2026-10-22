# PD-260 — 增量列舉時避免重排整份清單

Phase 8 · managed-list performance

## 來源與交付

來源：managed-list 效能檢視追查到，`Pane::process_content_results()` 在一次 UI dispatch 收到一或多批列資料後，只要 rows 有變更，就會在 dispatch 結尾呼叫 `sort_managed_list_rows()`；該函式對目前累積的整份 `list_.rows` 執行完整排序。列舉每批最多 128 列、每次 UI dispatch 最多收兩批，資料量增加時便會對愈來愈大的前綴反覆排序。這是靜態追查結果，尚無桌面效能量測。

**What to build：**在列舉仍持續提供可用、符合目前排序設定的漸進清單，同時避免每批都重排整份已累積資料，降低 UI STA 的無謂 CPU 工作。

## 綁定限制（引用）

- design-spec §2.4：「使用者體驗優先。清單輸入回應快、可漸進操作、選取與焦點穩定；慢 location 不凍結其他 pane。」其次是 CPU／記憶體／耗電，第三是最小且可測的架構。
- design-spec FR-006：「PaneDock-managed list 顯示 Shell namespace item，漸進更新可操作的列，並提供目前可用的排序、selection 及鍵盤行為」；「慢 location 或 metadata enrichment 不得阻塞 list input。」
- design-spec NFR-001：「使用者未互動時：CPU 佔用不可量測(取樣期間平均 < 0.1%),磁碟 I/O 為零。」未量測仍視為 gate 未通過。
- design-spec NFR-003：「command acknowledgement 的 p95 預算為 100 ms」；慢來源不得凍結其他 pane。
- development：「A pane-only feature is implemented in `Pane` or one of its sub-modules, not in the coordinator.」
- development：「The UI, `app_shell`, and `explorer_host` remain single-threaded STA code. The only planned concurrency boundary is the exact future source file `src/managed_list/list_worker.cpp`, for list work only.」
- AGENTS：「Keep `src/core` free of HWND, COM and `windows.h`.」「Event-driven idle path only. No busy loops, no polling timers.」「New non-trivial logic needs one focused runnable test or self-check.」

## 閱讀與追查入口

- `docs/design-spec.md` §2.4、FR-006、NFR-001、NFR-003；`docs/development.md` Pane 所有權與 STA／worker 邊界；`docs/testing.md` managed-list 測試 seam 與排序驗證。
- `src/managed_list/list_worker.h`：`kBatchRows`、`kQueuedBatches`、`kMaxRows` 與列資料上限。
- `src/app_shell/main.cpp`：`content_event()` 就緒後呼叫 `Pane::process_content_results()` 的 UI event loop。
- `src/app_shell/pane_managed_list.cpp`：`Pane::process_content_results()`、`Pane::sort_managed_list_rows()`、`Pane::apply_managed_list_sort()` 與 `finish_property_batch()`；追查排序入口及列舉、delta、recovery、property sort 的所有呼叫路徑。
- `src/app_shell/pane_list_rename.cpp`：rename 完成後排序與 selection／viewport 還原。
- `src/app_shell/pane.h`：Pane 列狀態、排序與 interaction snapshot 的所有權。
- `tests/unit/managed_list_sort_test.cpp`、`CMakeLists.txt`：現有排序／選取錨點測試與 CTest target。

## 範圍

- 讓新增列在列舉進行中仍依目前 sort key、方向及既有 Explorer parity 規則呈現有序結果；完整列舉結果須與現有排序契約一致。
- 避免每次批次都對累積的完整 row vector 做完整排序。採用可在現有 Pane／managed-list 邊界測試的增量處理；不為此加入背景 thread、timer、idle polling 或新的全域快取。
- 保留 PD-253 的 folder grouping、自然名稱排序、方向語意及相等項目規則；保留昂貴 property sort 的載入／完成行為。
- 在插入、合併或重排後保留選取 identity、focus、selection mark 與 viewport anchor。
- 對 recovery／delta／rename 等非單純追加路徑逐一追查，維持其既有語意；只在證明安全的路徑使用追加批次最佳化。

## 驗收標準

- [x] 每個已提交的列舉批次後，Pane 可觀察的列順序符合目前排序設定；最終順序與 reference sort 結果相同。
- [x] 受控 100,000 列、每批 128 列、每次 UI dispatch 最多消耗 `kQueuedBatches`（兩批）的確定性 workload 覆蓋整段漸進列舉，並以比較次數或等價的演算法工作計數與「每次 UI dispatch 完整排序累積前綴」基準比較，證明總排序工作顯著降低；不以單次 wall-clock 門檻取代可重現計數。
- [x] 部分批次與最後一批到達時，已選項目、focus 與 viewport anchor 仍綁定原 item identity；等顯示名稱的不同 identity 不互換。
- [x] 每次 event dispatch 仍受既有 batch 上限約束；輸入訊息可在批次之間處理，閒置時沒有排序工作或新輪詢。
- [x] 現有排序、昂貴 property 排序、rename、delta／recovery 相關 managed-list 測試均通過；不得由此宣稱 NFR-001 桌面 idle gate 已通過。

## Non-goals

不改 sort key／顯示規則、不增加使用者設定、不更改 worker 數量與 batch 上限、不加入多執行緒排序、不改 `src/core` 邊界、不改 legacy `IExplorerBrowser` 預設或 PD-241 gate，也不把本票的合成 workload 當成真實 Shell／OneDrive 效能證據。

## Agent checks 與交付

沿用 LLVM-MinGW Clang／LLD，不尋找 MSVC。若工具未在 PATH，於當次 shell 暫加 `E:\Dev\LLVM-MinGW\bin` 與 `E:\Dev\Ninja`。

~~~powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure -R '^panedock_managed_list_sort$'
ctest --test-dir build --output-on-failure
git diff --check
~~~

- 新增的工作量回歸檢查必須可重跑並納入 CTest；測試以 Pane 可觀察結果或純 owned-value 排序 seam 驗證，不斷言 private call order、不 fake `IExplorerBrowser`。
- 若實作新增排序 helper，保持它在 Pane／managed-list 的最小邊界；不得把 HWND、COM 或 `windows.h` 帶進 `src/core`。
- 本票目標為半天至兩天。若追查發現 recovery／property 排序需要另一個獨立成果，先拆票並在 tracker 記錄依賴。
- 2026-10-22 自動化證據：`build/pd260` Release 全量建置成功；修正比較基準後 `panedock_managed_list_sort` 1/1 通過，100,000-row synthetic workload 計為 20,463,121 次增量比較，對照每次 dispatch 完整排序的 381,388,200 次（約 18.6 倍差距）。比較次數是演算法工作量，不是實際 Shell latency 或 idle CPU 量測。完整非 GUI CTest 57/57 通過；`panedock_launch_smoke` 依本次工作要求排除。`git diff --check` 通過。
- 尚未驗證真實桌面、Shell／OneDrive 的切換延遲、idle CPU 與磁碟 I/O；這些數據仍由 PD-241 gate 覆蓋。
