# PD-199 — 導覽請求身分與分頁身分的正確性修正

Phase: 7
Depends on: PD-170（導覽 generation）、PD-183（`Pane` 持有 `ExplorerHost`）、PD-196（`PaneTabStrip`）

本票來自 2026-09-06 的全 repo 稽核（`audit-project`）。稽核讀了 `core/shutdown.cpp`、`explorer_host/explorer_host.cpp`、`app_shell/pane.cpp`、`app_shell/main.cpp` 的導覽與拖曳流程、`core/model.cpp` 與 `core/session.cpp`，追蹤五條流程：導覽請求生命週期、shutdown 序列、分頁增刪、跨 pane 分頁拖曳、session 存檔。產出兩個帶完整證據鏈的缺陷與一個介面語意問題，本票一併修正。

## 背景：為什麼三件事在同一張票

三者都在同一條「導覽請求身分」鏈上，且 Scope 3 的行為變更會直接改變 Scope 1 的可觀測結果（`navigate()` 從永遠回 `S_OK` 改為回真正的 HRESULT，才讓 Scope 1 的失敗回報能被測試斷言）。拆票會讓中間狀態出現一個「HRESULT 已改、失敗歸屬未改」的半套版本。Scope 2 與前兩者無關，但同屬本次稽核的高信心發現且改動極小（一個參數、一個呼叫點），分票的管理成本高於收益。

## 綁定約束

`AGENTS.md`：

> Read the relevant spec section and trace every caller before touching shared code. A guard in the shared function is a smaller diff than a guard in every caller.

> **Never persist a PIDL or a COM pointer.** Persisted identity is parsing name plus known-folder identity plus a fallback path. Display names are never identifiers.

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

> New non-trivial logic needs one focused runnable test or self-check.

`explorer_host.h` 既有註解（本票的直接前提）：

> `IExplorerBrowserEvents` has no request token; preserve start order so a completion can carry the generation assigned when its navigation began.

## Scope 1 — 同步導覽失敗不得消費別人的 generation

**缺陷**：`ExplorerHost::navigate()` 以 `enqueue_navigation()` 把新 generation **push_back** 到 `navigation_requests_` 尾端，但同步失敗時呼叫 `navigation_failed()`，而後者用 `take_navigation_generation()` 從**佇列前端 pop**。前端屬於仍在飛行中的導覽，不是剛失敗的這一筆。上面引的註解說明整個 deque 靠 FIFO 對應 Shell 事件順序——同步失敗路徑本來就不是 Shell 事件，卻走了 Shell 事件的取值方式。

**失敗情境**：pane 正在導覽 A（慢速 UNC／離線磁碟，尚未回 `OnNavigationComplete`）→ 使用者在位址列輸入解析不到的 B → `SHCreateItemFromParsingName` 失敗 → `navigation_failed()` pop 出 gen(A)，與 `latest_navigation_generation_`（= gen(B)）不符而提早 return：**不顯示 error overlay、不觸發 `navigation_failed_callback_`**。之後 A 真的完成時，`navigation_complete` pop 出 gen(B)、與 latest 相符而被接受，於是 `Pane::navigation_complete` 把 **A 的位置**當成 B 的導覽結果寫進 `tab->location` 與 history。使用者輸入 B、沒有任何錯誤提示、pane 顯示並記錄 A。

**無防護**：`navigate()` 三條失敗路徑都回傳 `S_OK`（見 Scope 3），所以 `Pane::navigate_history` 的 `if (FAILED(hr))` 永遠不觸發，唯一補救就是這個被吞掉的非同步 callback。呼叫端沒有任何佇列深度檢查。

**做法**：
- `enqueue_navigation()` 改為回傳實際入列的 generation（`0` = 配置失敗），呼叫端保留它。
- 新增 `fail_enqueued_navigation(generation)`：反向搜尋 `navigation_requests_` 移除**自己那一筆**，再呼叫 `report_navigation_failed(generation)`。
- `navigation_failed()` 拆為兩段：Shell 事件入口維持 `report_navigation_failed(take_navigation_generation())` 的 FIFO 取值；`report_navigation_failed(generation)` 承載原本的判斷與回報主體。
- `navigate()` 與 `navigate_up()` 的所有同步失敗路徑改走 `fail_enqueued_navigation()`。

## Scope 2 — `move_tab` 不得讓同一個 tab id 存在於兩個 pane

**缺陷**：`core::move_tab` 先 `TabState moved = *tab;` 複製，`source.tabs.size() == 1` 的分支**不刪除來源分頁**，只把它重設為 `default_location` 並清空 history，接著仍把持有相同 `id` 的複本插入 target。函式開頭只檢查 target 是否已有這個 id，沒有檢查來源會不會保留它。

**失敗情境**：pane A 只剩 `tab-3`，拖到 pane B → A 有 `tab-3`（Desktop）、B 也有 `tab-3`。要把它拖回 A 時，`move_tab` 開頭的 `find_id(target.tabs, tab_id) != end()` 命中 → `return false`，`finish_tab_drag` 靜默 return，拖曳沒有任何反應也沒有提示。重複 id 會隨 `schedule_session_save` 寫進 `session.json` 並在重啟後保留（`is_valid` 只檢查 pane 內唯一，跨 pane 重複通得過）。

**無防護**：`finish_tab_drag`（`main.cpp`）在跨 pane 分支只檢查 pane index 範圍，沒有針對「來源只剩一個分頁」的處理，`move_tab` 回傳 false 時也不做任何回饋。

**做法**：
- `move_tab` 新增最後一個參數 `const std::string& retained_tab_id`：單分頁來源留下的佔位分頁改用這個 id，並同步更新 `source.active_tab_id`；移走的複本保有原 `tab_id`。
- `retained_tab_id` 為空、等於 `tab_id`、或已存在於 target 時**整個操作拒絕**（回傳 `false`），不得做部分修改——守衛必須在任何 mutation 之前。
- `finish_tab_drag` 在呼叫前先取 `state.make_unique_tab_id()`（group 全域掃描）作為 `retained_tab_id`。

## Scope 3 — `navigate()` 回傳真正的 HRESULT

**問題**：`ExplorerHost::navigate()` 在 bind context 取得失敗、`SHCreateItemFromParsingName` 失敗、`BrowseToObject` 失敗三種情況都回傳 `S_OK`，讓所有呼叫端的 `FAILED(hr)` 檢查形同虛設。

**做法**：三條路徑回傳真正的 HRESULT（bind context 失敗用 `E_FAIL`，另兩條用 Shell 的 HRESULT）。

**連帶必須處理的一處**：`initialize()` 原本 `return navigate(location);`。若直接讓失敗傳出去，`Pane::realize()` 的 `realized_ = SUCCEEDED(hr)` 會在一個**已經 Initialize 且必須 Destroy** 的 browser 上留下 `realized_ == false`，下一次 `apply_layout` 會嘗試二度 `initialize()`（回 `E_INVALIDARG`）。因此 `initialize()` 明確與首次導覽結果脫鉤：browser 建立成功就回 `S_OK`，導覽失敗只記錄，由 error overlay 呈現。這符合語意——無法解析的資料夾是 overlay 的情況，不是 realization 失敗。

## Non-goals

- 不改 `navigation_requests_` 的 FIFO 設計本身。Shell 事件仍然沒有 request token，FIFO 對應 Shell 事件的前提不變，本票只是不讓非 Shell 事件的路徑去消費它。
- 不改 `Pane::navigation_request_is_current` 的 group／tab 比對邏輯（PD-170）。
- 不動 `is_valid` 的不變式定義（不新增「tab id 在 group 內唯一」的檢查）——那會讓既有含重複 id 的 session 檔在載入時被判為無效並回退，屬於破壞性重新詮釋。修正產生源頭即可。
- 不做 session 遷移。既有已寫入的重複 id 不追溯修復。
- 不碰 `main.cpp` 的行數或 PD-197 記錄的協調層界線。

## 驗收

1. `ExplorerHost` 的同步失敗只影響自己的 generation：凡 `navigate()` 以 HRESULT 回報失敗，必定恰好觸發一次 `navigation_failed_callback_`，且帶的是該次呼叫自己的 generation。
2. `move_tab` 在任何情況下都不讓同一個 tab id 出現在兩個 pane；不合法的 `retained_tab_id` 導致整個操作拒絕且來源／目標皆未被修改。
3. 把 pane 唯一的分頁拖到另一個 pane 後，可以再把它拖回原 pane。
4. `navigate()` 失敗時回傳失敗 HRESULT；`initialize()` 在 browser 建立成功時回 `S_OK`，不因首次導覽失敗而讓 `Pane::realized()` 為 false。
5. 既有 build 與完整 CTest 通過，未關閉任何測試。

## Agent 檢查

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- `tests/unit/core_model_test.cpp` 的 `test_move_tab`：佔位分頁換 id、`active_tab_id` 同步、可拖回原 pane、三種不合法 `retained_tab_id` 皆拒絕且無部分修改。
- `tests/unit/explorer_host_lifetime_check.cpp`：連續多次導覽至無法繫結的位置，斷言「HRESULT 報失敗 ⇔ 恰好一次 callback 且 generation 相符」，並要求至少發生一次同步失敗以免空過。

## 交接區

**實作結果**（2026-09-06）：三個 Scope 全部完成，LLVM-MinGW Release build 無新增警告，CTest **24/24 通過**，`explorer_host_lifetime`／`core_model` 連續重跑三次穩定。

**測試設計上的一個實測事實，後續實作者需要知道**：`explorer_host_lifetime_check.cpp` 原本有一行 `EXPECT(SUCCEEDED(host.navigate({missing, {}, {}})));`，它是**空斷言**——舊 `navigate()` 在所有路徑都回 `S_OK`，這個 EXPECT 不可能失敗。Scope 3 之後它才有意義，而實測顯示 `L"?:\\PaneDock-PD-022-definitely-not-there"` 的失敗時機**不穩定**：同一個 host 連續導覽四次，第一次被 Shell 接受（失敗改由 `OnNavigationFailed` 非同步送達，而這個 check 不跑訊息迴圈所以觀察不到），後三次同步失敗。實測輸出 `synchronous navigation failures=3 of 4`。

因此測試改為斷言**契約**而非 Shell 的判定：迴圈四次，每次記錄呼叫前的 callback 次數，斷言 `FAILED(hr)` 時次數恰好 +1 且 `failed_generation` 等於本次的 generation、`SUCCEEDED(hr)` 時次數不變，最後再斷言同步失敗次數 > 0 以免整個迴圈空過。**不要**把它改回「斷言某個特定路徑必然同步失敗」——那會隨 Windows 版本與 Shell 狀態飄動。

**Scope 1 的驗證限制**：真正的觸發條件是「一筆導覽在飛行中時，另一筆同步失敗」。上述 check 在單一 host 上以連續呼叫逼出同步失敗路徑並驗證歸屬正確，但**沒有**構造出「A 在飛行中」的並行狀態——那需要一個可控的慢速 shell namespace，本票未做。人工驗證建議：在一個離線的網路磁碟機路徑導覽尚未回應時，於同一個 pane 的位址列輸入不存在的路徑，確認 error overlay 出現，且該 pane 的位置不會被前一筆導覽的結果覆寫。

**未做的取捨**：`move_tab` 的簽章多了一個必填參數而不是給預設值。給預設值會讓漏傳的呼叫端靜默回到舊行為（產生重複 id），編譯期報錯比較安全。目前唯一呼叫端是 `finish_tab_drag`。

**同一次稽核產出但未修的觀察**（見 `docs/tickets.md` 候選）：`activate_group` 持有指向 `state.application.groups` 的 `GroupState&`，橫跨會重入訊息迴圈的 `navigate_realized_panes()` 與 `apply_layout()`；目前靠 `defer_shell_reentry_mouse_message` 延後滑鼠訊息才沒被觸發，但那是間接防護。稽核未能證明可達路徑，故未修。
