# PD-261 — Group 切換重用有界列表快取

Phase 8 · managed-list responsiveness

## 來源與交付

來源：managed-list 效能檢視追查到，Group 切換時 `Pane::bind()` 取消目前列表；`cancel_managed_list()` 以 `prune_list_cache(true)` 清除該 Pane 的快取。現有 `save_list_cache()` 只收納目前 Group 中已完成的 inactive tab，故離開 Group 時的 active tab 沒有可供返回重用的 snapshot。A→B→A 因而重新等待列舉才能看到可用資料。這是程式路徑追查，切換延遲尚未實測。

**What to build：**切回最近離開的 Group 時，立即呈現其精確綁定的有界列表 snapshot 作為 Updating 畫面，再以目前 generation 的列舉結果驗證及更新；離開期間停止該列表的工作與通知訂閱。

## Overrides

本票覆寫 PD-233 對 Group binding 變更清除 Pane 所有 cache entry 的行為與 `binding_and_location_invalidation()` 中相應的測試預期。新證據是目前 Group transition 將已完成的 active-tab rows 一併丟棄，A→B→A 無法使用 PD-233 的 inactive-tab cache；使用者要求切換順暢並允許適度 cache。僅允許在既有 32 MiB 全域 LRU 預算內保留 completed owned-value snapshot，且必須維持 exact Group／tab／location identity、回返後重新驗證及既有淘汰規則。不修改 completed PD-233 文件。

## 綁定限制（引用與適用方式）

- design-spec §2.4：「使用者體驗優先」；CPU／記憶體／耗電其次，架構與可維護性第三。
- design-spec §4.2：「切換必須感覺即時；慢 location 的列舉不得阻塞 UI」；managed-list path 保留穩定 pane 與 UI，先呈現可用狀態，再接收符合目前 location generation 的結果。
- design-spec FR-006：「漸進更新可操作的列」；慢 location 或 metadata enrichment 不得阻塞 list input。
- design-spec NFR-001：「使用者未互動時：CPU 佔用不可量測(取樣期間平均 < 0.1%),磁碟 I/O 為零。」未量測仍視為 gate 未通過。
- design-spec NFR-002：「inactive tabs 不持續 enumerate，保留資料也計入總額。」snapshot 必須計入既有全域快取預算。
- design-spec NFR-003：本機第一批可操作列的 p95 預算為 cold 250 ms、warm 100 ms；未實測均為未驗證。
- development：「A pane-only feature is implemented in `Pane` or one of its sub-modules, not in the coordinator.」跨 pane Group transition 仍由 coordinator 掌管。
- development：「The UI, `app_shell`, and `explorer_host` remain single-threaded STA code. The only planned concurrency boundary is the exact future source file `src/managed_list/list_worker.cpp`, for list work only.」
- AGENTS：「Event-driven idle path only. No busy loops, no polling timers.」「Never persist a PIDL or a COM pointer.」「New non-trivial logic needs one focused runnable test or self-check.」

## 閱讀與追查入口

- `docs/design-spec.md` §2.4、§4.2、FR-006、NFR-001–003；`docs/development.md` Pane／coordinator 所有權與 STA／worker 邊界；`docs/performance-baseline.md` cache budget；`docs/testing.md` restore、Group/tab transition、stale generation 與 bounded cache 測試。
- `src/app_shell/pane.h`：`Pane::bind()`、`unbind()`、`cancel_managed_list()`、cache entry 與 `CachedList` 型別。
- `src/app_shell/pane_managed_list.cpp`：`prune_list_cache()`、`save_list_cache()`、`take_list_cache()`、`cancel_managed_list()`、`start_managed_list()`；追查 `content_request()`、identity 與 interaction snapshot 的生命週期。
- `src/app_shell/main.cpp`：`activate_group()`、`delete_group()`、`run_group_transition()`、`navigate_realized_panes()`、`apply_layout()`；確認 Group 切換、刪除、pane 隱藏及 shutdown 各自如何 bind/unbind Pane。
- `src/app_shell/pane.cpp`：tab 切換、關閉及導覽如何保存 inactive-tab cache；追查所有 `cancel_managed_list()` 的 caller，區分 Group rebind、location 變更與 Pane teardown。
- `tests/unit/managed_list_cache_test.cpp`：`binding_and_location_invalidation()`、warm refresh、exact identity、global budget／LRU 及 cache accounting；`CMakeLists.txt`：對應 CTest target。

## 範圍

- Group 切換離開時，只將符合現有 admission 條件的 completed／empty active-tab snapshot（含 interaction anchors）放入既有全域 cache；不得保存不完整列舉或以 cache 作為檔案存在證明。
- Group binding 變更時取消舊 source／notification／icon 與 metadata interest，再清空目前可見列表；可保留符合既有 budget 的 owned-value snapshot，不能讓舊 Group 的 row 留在新 Group 畫面。
- 命中時必須同時比對 Group ID、tab ID 及完整 location identity。先顯示 snapshot 並標示 Updating，再啟動目前 generation 的 refresh／reconcile；過期 generation 結果忽略，selection／focus／anchor 依 item identity 還原。
- 只重用現有全域 32 MiB cache、LRU 與 bytes accounting，不新增另一份快取預算，不寫磁碟，不新增 timer。Group inactive 時不得持續 enumerate、watch 或做 metadata／icon 工作。
- 區分一般 Group rebind 與 Pane 真正 teardown／shutdown；teardown 必須移除該 Pane 擁有的 entry，避免留下失效 owner。追查 Group/tab 刪除及 ID 生命週期，確保舊 entry 不會命中另一個 binding。

## 驗收標準

- [x] 受控 A→B→A 測試在回到 A 時、尚未收到新列舉 batch 前即可觀察到 A 的 snapshot，且狀態為 Updating；目前 generation 完成後呈現新資料並移除已不存在的項目。
- [x] A 的 rows 不會出現在 B；相同 tab ID／location 但不同 Group ID 也不會命中；location 改變、tab 關閉或 Group 刪除不會把舊資料當成目前內容。
- [x] selection、focus 與 viewport anchor 只依穩定 item identity 還原；測試 stale generation 結果仍被忽略。
- [x] 已離開 Group 的 source／watch 工作取消，無 polling；返回後最多只為目前可見 tab 啟動更新。idle cache 不觸發磁碟 I/O。
- [x] cache bytes 持續受既有 32 MiB global LRU 限制，超額仍淘汰；Pane teardown／shutdown 後不留 owner entry。
- [x] 更新 `binding_and_location_invalidation()` 的 Group-rebind 預期並保留 location invalidation、cross-Group isolation、LRU 與 budget 測試；不宣稱未執行的真實桌面 cold/warm 或 idle gate 已通過。

## Non-goals

不新增磁碟持久化快取、額外 cache budget、背景預熱、多執行緒或 polling timer；不更改列舉結果的 freshness／identity 契約、不放寬 `IFileOperation`／Shell 驗證、不改 PD-241 gate 或 legacy `IExplorerBrowser` 預設，也不修改已完成的 PD-233 ticket 文件。

## Agent checks 與交付

沿用 LLVM-MinGW Clang／LLD，不尋找 MSVC。若工具未在 PATH，於當次 shell 暫加 `E:\Dev\LLVM-MinGW\bin` 與 `E:\Dev\Ninja`。

~~~powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure -R '^panedock_managed_list_cache$'
ctest --test-dir build --output-on-failure
git diff --check
~~~

- A→B→A、跨 Group 同 tab identity、location change、stale generation、source cancellation、budget／LRU 與 teardown 測試納入對應 CTest target。
- 行為測試使用受控 Source 與 Pane 可觀察狀態，不 fake `IExplorerBrowser`、不斷言 private call order。
- 本票目標為半天至兩天；若 Group deletion invalidation 需要獨立 coordinator API，先拆分並在 tracker 記錄依賴，不擴大本票範圍。
