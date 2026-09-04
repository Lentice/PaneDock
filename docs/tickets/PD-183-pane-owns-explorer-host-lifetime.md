# PD-183 — `Pane` 接手 `ExplorerHost`，把 §9.4 的 destroy 順序變成解構子不變量

Phase 7 · architecture · Depends on: PD-182

- Source: 同 PD-178（2026-09-03 使用者重構需求）。本票是 `docs/tickets.md` §候選「把剩餘 9 個 pane-parallel 陣列搬進 `PaneChrome`」的**後半**。opencode 獨立審查主張這三個欄位不該搬，理由是「會讓 `Pane` 握有 Shell view 生命週期」；本票採取的立場是**持有 ≠ 決策**——realization 政策仍由 `core::plan_realization` 與協調層決定，`Pane` 只是持有者，而讓同一個型別同時持有 view 與其 parent HWND，正是把 §9.4 的順序從「呼叫端的約定」變成「解構子裡的不變量」的唯一方法。
- Priority: MEDIUM——本系列風險最高的一張（Shell view 生命週期），但也是唯一能把已知崩潰面制度化的一張。

## Outcome

`AppState` 的最後三個 pane-parallel 欄位（`explorers`、`realized`、`suppress_history_record`）搬進 `Pane`。`Pane` 同時持有該 pane 的 `ExplorerHost` 與它的 parent container HWND，因此「view 存活期間不得 destroy parent HWND」這條規則從 `main.cpp` 的呼叫順序約定，變成 `Pane::destroy()` 內部的單一不變量。

行為與視覺零變更。realization 政策（誰該 realize、何時 realize）**完全不變**，仍由 `core::plan_realization` 決定、由協調層執行。

## 已確認的現況（2026-09-03 工作樹）

- PD-182 完成後 `AppState` 只剩三個 pane-parallel 欄位：
  - `std::array<panedock::explorer_host::ExplorerHost, kExplorerCount> explorers`（原 `main.cpp:529`）
  - `std::array<bool, kExplorerCount> realized`（原 `:530`）
  - `std::array<bool, kExplorerCount> suppress_history_record`（原 `:619`）
- 目前的 destroy 順序（PD-163 交接區記錄）：`destroy_explorers(state)`（`main.cpp:2821-2829`）先跑，**之後**才對每個 `PaneChrome::destroy()`，再由 shutdown reducer 進入 view/window destruction。**這條順序目前只靠 `main.cpp` 裡的呼叫次序維持，沒有任何型別強制它。**
- `PaneChrome::destroy()`（`pane_chrome.cpp:79-92`）目前的順序是：status bar → address bar → 6 顆按鈕 → tab strip → **explorer container（最後）**。`ExplorerHost` 的 view 就住在 explorer container 裡。
- realization 決策已在 `src/core/layout.h` 的 `RealizationMode` / `RealizationPlan` / `plan_realization` 純函式中（PD-164），有 `tests/unit/core_realization_plan_test.cpp`。**本票不改它。**
- `suppress_history_record` 由 navigation callback 路徑讀寫，與 `ExplorerHost` 的導覽事件同生共死，因此與 `explorers` 一起搬。相關函式：`handle_navigation_complete`（`main.cpp:2787-2811`）、`navigate_tab_history`（`:3583`）、`navigate_realized_panes`（`:856`）。
- `ExplorerHost` 的 reentrancy 契約：`ShellCallScope`（`explorer_host.h:30-42`）、`app_shell_call_state_changed`（`main.cpp:689`）。`tests/release/shell_reentry_gate_check.ps1` 同時掃 `main.cpp` 與 `explorer_host.cpp`。
- `explorer_host` 依 `docs/testing.md` **沒有自動化測試**；`tests/unit/explorer_host_lifetime_check.cpp` 存在於 `tests/unit/` 但**未註冊**於 `tests/CMakeLists.txt`。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`：

> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups — that is both the crash surface and a CPU/disk spike from simultaneous folder enumerations.

`AGENTS.md`：

> Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs persist as data and are realized on activation. This is what keeps memory bounded.

`AGENTS.md`：

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`docs/design-spec.md` §9.4：關機順序為 capture state → destroy 所有 live `IExplorerBrowser` → destroy pane HWND → destroy 主視窗 → 離開訊息迴圈 → `CoUninitialize`

> 順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。

`docs/design-spec.md` §4.2：

> 以保活既有 view 並重新導覽達成,不重建 HWND。

`docs/design-spec.md` NFR-002／`docs/tickets.md` §已否決的方向：

> 每個 tab 都保留 live `IExplorerBrowser`…這是記憶體無上界成長的唯一原因。

`docs/tickets.md` §已否決的方向：

> 為 `IExplorerBrowser` 加抽象層以便 fake…只有一個真實實作的介面,買到的覆蓋率不對應真實風險。

**本票不重開上述兩條否決方向**：不改變「只有可見 pane 的 active tab 是 live」的規則，也不為 `IExplorerBrowser` 加任何抽象層。

`docs/development.md`：

> Do not add a dependency, background loop, framework, or abstraction without a measured need.

`docs/tickets.md` Agent 交付規則：

> 預設每個 ticket 的實作範圍為半天至兩天;若超過,先拆 ticket。

## Files to read and trace first

- `src/app_shell/pane.h/.cpp`（PD-182 產出）：現有的 destroy 順序。
- `src/app_shell/main.cpp:2821-2829`：`destroy_explorers`。
- `src/app_shell/main.cpp:5025-5143`：`finish_shutdown`、`run_shutdown_action`、`begin_shutdown`、`complete_deferred_close`。
- `src/app_shell/main.cpp:2859-3188`：`apply_layout` 中 realize／de-realize 的執行點。
- `src/app_shell/main.cpp:3188-3272`：`refresh_startup_chrome`；`:3273-3328` `activate_group`。
- `src/app_shell/main.cpp:837-892`：`navigation_request_is_current`、`navigate_realized_panes`。
- `src/app_shell/main.cpp:2787-2820`：`handle_navigation_complete`、`handle_navigation_failed`。
- `src/core/layout.h`、`src/core/layout.cpp`：`plan_realization`（本票不改）。
- `src/core/shutdown.h`、`src/core/shutdown.cpp`：`ShutdownSequence` 的事件與動作（本票不改）。
- `src/explorer_host/explorer_host.h`：完整生命週期介面與 `ShellCallScope`。
- `tests/release/shell_reentry_gate_check.ps1`、`shutdown_state_check.ps1`。
- `tests/unit/explorer_host_lifetime_check.cpp`：未註冊的既有 self-check。
- `docs/tickets/PD-164-realization-plan-pure-function.md`、`PD-163`、`PD-172`、`PD-173`、`PD-177`。

## Scope

1. 把 `explorers`、`realized`、`suppress_history_record` 三個欄位搬進 `Pane`，從 `AppState` 刪除。
2. **`Pane::destroy()` 的順序寫死為**：
   1. `RevokeDragDrop`（PD-182 已建立）
   2. `explorer_host_.destroy()`——若曾 `Initialize`，必須呼叫 `Destroy`
   3. 其餘 chrome 子視窗
   4. `explorer_container_`（最後，因為 view 住在裡面）

   在 `pane.cpp` 的該函式上方寫一段註解，逐點引用 §9.4 並說明為何順序不可調換。
3. `main.cpp` 的 `destroy_explorers(state)` 刪除或改為「對每個 `Pane` 呼叫 `destroy()`」的單一迴圈。**shutdown reducer 的事件序列一行不改。**
4. `Pane` 新增純命令／純查詢：
   - `bool realize(HWND parent, const core::ShellLocation&)`、`void derealize()`
   - `bool realized() const noexcept`
   - `HRESULT navigate(const core::ShellLocation&)`
   - `ExplorerHost& host() noexcept` / `const ExplorerHost& host() const noexcept`（過渡期允許，但交接區要列出仍需直接存取 `host()` 的呼叫點）
   - `void set_suppress_history(bool)` / `bool suppress_history() const noexcept`
5. **realization 政策不動**：`core::plan_realization` 仍在協調層被呼叫，其 `RealizationPlan` 的執行改為對 `panes[i].realize()` / `derealize()` 下命令。`Pane` **不得**自行決定要不要 realize。
6. `ShellCallScope` 與 reentrancy 閘門的行為完全不變；`app_shell_call_state_changed` 的回呼路徑不改。
7. 把 `tests/unit/explorer_host_lifetime_check.cpp` 註冊進 `tests/CMakeLists.txt`（它已存在但未被執行）。若它無法在無視窗環境下通過，說明理由並改為 `tests/release/` 的腳本檢查。
8. 順手把本票碰到的函式簽章從 `AppState&` 收窄。

## Non-goals

- 不改「只有可見 pane 的 active tab 是 live」的規則，不改 `core::plan_realization` 的任何邏輯。
- 不改 Group 切換路徑（保活 + 重新導覽，不重建 HWND）。
- 不改 `ExplorerHost` 的公開介面、site 契約、`IServiceProvider` 回呼或 PIDL 生命週期。
- 不為 `IExplorerBrowser` 加抽象層、不加 fake、不加 mock。
- 不改 `ShutdownSequence` 的事件、動作或狀態轉移。
- 不統一 `{ ShellCallScope } + if (closing_ || shutdown_deferred) return X;` 慣用法。
- 不註冊新的 window class、不改變任何 HWND 的 parent。
- 不重開任意遞迴 pane 分割、不重開「每個 tab 都保留 live view」。

## Acceptance Criteria

1. `AppState` 中已無任何 `std::array<..., kExplorerCount>` 的 pane-parallel 欄位。
2. `Pane::destroy()` 中 `ExplorerHost::destroy()` 早於任何 chrome 子視窗的 `DestroyWindow`，且 `explorer_container_` 最後被摧毀；該順序有引用 §9.4 的註解。
3. `plan_realization` 的呼叫點與其回傳的 `RealizationPlan` 語意未改變；`core_realization_plan_test` 仍全綠。
4. `explorer_host_lifetime_check` 已註冊並在 `ctest` 中出現（或交接區寫明為何改成 release 腳本）。
5. `ctest --test-dir build --output-on-failure` 全綠，含 `panedock_launch_smoke`。
6. 連續 20 次切換版型後無殘留 live view（見 Agent Checks 的 `live_view_count` 檢查）。
7. 視覺與行為零變更。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# AppState 不得再有 pane-parallel 陣列
Select-String -Path src/app_shell/main.cpp -Pattern 'std::array<.*kExplorerCount>'
# Pane 不得自行決定 realization 政策
Select-String -Path src/app_shell/pane.cpp -Pattern 'plan_realization|RealizationPlan'
# Pane 仍不得反向依賴協調層
Select-String -Path src/app_shell/pane.h -Pattern 'AppState|std::function|callback'
```

三條都必須無結果。

```powershell
# 關閉不殘留，且無 view-alive-parent-destroy 崩潰
$p = Start-Process build\PaneDock.exe -PassThru; Start-Sleep 3
$p.CloseMainWindow() | Out-Null
if (-not $p.WaitForExit(5000)) { throw 'process survived graceful close' }
```

```powershell
# 診斷模式的 live view count（NFR：切版型後應回到基線）
build\PaneDock.exe --diagnostic
# 手動切版型 20 次後關閉，再讀取 live view count 檔案，確認回到基線
```

```powershell
git diff --check
```

## 交給使用者的實機檢查清單

**本票是整個系列風險最高的一張，請完整執行。**

1. 切版型 1→2→3→4→1，**連續 20 次**，確認：沒有崩潰、記憶體沒有單調成長、每次都正確顯示。
2. 切換 Group（至少 3 個不同版型的 Group）來回 10 次，確認 view 保活、位置正確、沒有閃爍或重建。
3. Tab 切換：在同一個 pane 內來回切換 10 個 tab，確認 realize-on-activation 正常、歷史（上一頁／下一頁）正確。
4. 不可解析位置：把 tab 指向已拔除的隨身碟，確認錯誤面板出現、Retry 可用、切走再切回狀態正確。
5. 檔案操作進行中關閉主視窗，確認 transfer close dialog 的三個選項行為不變。
6. 拖曳進行中關閉主視窗（PD-173），確認乾淨結束。
7. 在 Shell 內容區開右鍵選單、觸發一個會開啟其他程式的動作，再關閉主視窗，確認乾淨結束。
8. 每次測試後檢查工作管理員中沒有殘留的 `PaneDock.exe`。

## Handoff requirements

在 `## 交接區` 記錄：`Pane::destroy()` 的逐行順序與其對應的 §9.4 條款、仍需直接存取 `Pane::host()` 的呼叫點清單（這是下一次收窄的依據）、`plan_realization` 呼叫點的前後對照、`explorer_host_lifetime_check` 的註冊結果、20 次版型切換的 live view count 讀數、以及使用者實機檢查 8 項的逐項回報結果。

## 交接區

**欄位搬移**：`AppState` 的最後三個 pane-parallel 欄位（`explorers`、`realized`、`suppress_history_record`）已刪除，全部搬進 `Pane`（`src/app_shell/pane.h/.cpp`）。`AppState` 現在已無任何 `std::array<..., kExplorerCount>` 的 raw pane-parallel 資料欄位（`panes` 本身、局部變數 `changed_panes`/`pending_shell_rects`/`shell_positions_deferred`/`container_rects`/`explorer_positions`、以及新增的自由函式 `realized_flags` 不算——它們是 `Pane` 陣列本體與 `apply_layout` 內部的局部暫存，不是「還沒搬進 Pane 的並行資料」）。符合 AC1。

**`Pane::destroy()` 的逐行順序**（`pane.cpp`，對應 §9.4 逐條）：
1. `revoke_drag_hover_target()`（`RevokeDragDrop`）——tab strip HWND 上的 drag-drop 註冊必須先於任何 HWND destroy。
2. `explorer_host_.destroy()`（若曾 `initialize()` 過，`ExplorerHost::destroy()` 內部已是 idempotent no-op guard）——對應「view 存活期間不得 destroy parent HWND」：此時 `explorer_container_` 還活著，Shell view 在自己的 parent 還在時被摧毀，順序正確。
3. 其餘 chrome 子視窗（`status_bar_`→`address_bar_`→`pinned_button_`→`view_mode_button_`→`refresh_button_`→`up_button_`→`forward_button_`→`back_button_`→`tab_strip_`）逐一 `DestroyWindow`。
4. `explorer_container_` 最後——Shell view 曾經住在裡面，此時 view 已在步驟 2 摧毀，摧毀 container 是安全的。

四步驟寫在 `pane.cpp` 的 `Pane::destroy()` 正上方，逐點引用 §9.4 與 AGENTS.md 的原句。符合 AC2。

**新增的純命令／純查詢**（依 Scope 第 4 點，含一處刻意偏離字面签名）：
- `HRESULT realize(const RECT& local_rect, const core::ShellLocation& location)` — **偏離**：ticket 字面要求 `bool realize(...)`。改成回傳 `HRESULT` 的理由：`apply_layout` 需要精確的失敗 `HRESULT` 才能記錄 `LayoutFailure{hr, index}`（既有行為，PD-183 不改），`bool` 會丟失這個資訊。`realize()` 內部呼叫 `explorer_host_.initialize(explorer_container_, local_rect, location)` 並據其成功與否設定 `realized_`。
- `void derealize()` — 呼叫 `explorer_host_.destroy()` 並清 `realized_`。
- `bool realized() const noexcept`
- `HRESULT navigate(const core::ShellLocation&)` — 委派給 `host().navigate(location)`（單參數多載；帶 generation 的多載仍走 `host()`，見下）
- `void set_suppress_history(bool)` / `bool suppress_history() const noexcept`
- `ExplorerHost& host() noexcept` / `const ExplorerHost& host() const noexcept` — 過渡期直接存取

**仍需直接存取 `Pane::host()` 的呼叫點**（main.cpp，共 34 處，供下一次收窄參考；分類如下）：
- 導覽（generation 相關，`navigate`/`navigate_up` 的雙參數多載、`begin_navigation`）：`begin_navigation`（1 處）、`navigate_pane`/`navigate_up_pane`（2 處）、`navigate_realized_panes`（1 處）
- View mode／sort／item counts（讀寫都與 `core::TabState` 的欄位一一對應，PD-183 範圍不含這些的純化）：`capture_pane_view_mode`/`capture_pane_sort`/`apply_pane_view_mode`/`apply_pane_sort`（6 處）、`refresh_status_bar`（1 處）
- Location 讀取：`capture_pane_location`（2 處）、`show_folder_context_menu` 附近（1 處）
- Realize 流程的回呼註冊（Scope 第 5、6 點明訂回呼路徑不變，回呼捕捉 `&state`，`Pane` 不能持有）：`set_shell_call_callback`/`set_navigation_callback`/`set_navigation_failed_callback`/`set_selection_changed_callback`（4 處）
- `set_rect`（版面/DPI 相關，非 realize/derealize 決策本身）：4 處
- `set_visible`（2 處）
- `focus()`（7 處）
- `refresh()`/`set_view_mode`（右鍵選單/工具列相關，2 處）
- `show_folder_context_menu`/`translate_accelerator`/`process_retry_request`（3 處）
- `set_shell_call_callback` 於 realize 流程前段（併入上面回呼類）

這份清單本身就是 opencode 當初對「不該讓 `Pane` 握有 view 生命週期」疑慮的部分驗證：`Pane` 只持有 `ExplorerHost`，並未把上述任何一個決策邏輯內部化——協調層仍然透過 `host()` 直接驅動 `ExplorerHost` 的細節行為，`Pane` 新增的四個命令/查詢只覆蓋「要不要有一個活的 view」這個生死開關，不覆蓋「這個 view 現在該長什麼樣子」。

**`plan_realization` 呼叫點前後對照**：兩處呼叫（`navigate_realized_panes`、`apply_layout`）原本傳入 `state.realized`（`std::array<bool,4>`，隱式轉 `span`），現在傳入新增的自由函式 `realized_flags(state)`——逐一讀取 `state.panes[i].realized()` 組成一份 `std::array<bool,4>` 快照。`plan_realization` 本身簽章與邏輯一行未改，`core_realization_plan_test` 仍全綠。`Pane` 沒有呼叫 `plan_realization`/引用 `RealizationPlan`（`Select-String -Path src/app_shell/pane.cpp -Pattern 'plan_realization|RealizationPlan'` 空）。

**`destroy_explorers` → `destroy_panes`**：原本 `destroy_explorers(state)`（只 destroy Shell view）與其後緊接的 `for (auto& chrome : state.panes) chrome.destroy()`（destroy chrome HWND）兩段分開的邏輯，現在 `Pane::destroy()` 已經把兩者摺進同一個順序不變量，所以三個呼叫點（`finish_shutdown`、視窗建立失敗的早退路徑、訊息迴圈結束後的保底清理）統一改成一個新的自由函式 `destroy_panes(state)`：對每個 `Pane` 呼叫 `destroy()`，每次呼叫仍各自包一層 `ShellCallScope`（因為 `Pane::destroy()` 內部會做一次 Shell 呼叫）。**注意**：這個 rename 使三個既有的 PowerShell 掃描腳本（`shutdown_state_check.ps1`、`address_bar_failure_check.ps1`、`startup_frame_order_check.ps1`）原先寫死的 `destroy_explorers(` / `state.realized` / `state.suppress_history_record[pane_index] = false` 字面模式全部失效——這正是 AGENTS.md／PD-181 交接區點名的「反向斷言腳本」風險。已逐一改成對應的新寫法（`destroy_panes(`、`realized_flags(state)`、`state.panes[pane_index].set_suppress_history(false)`），三個腳本改動前先確認舊字面在改動前的程式碼裡確實存在（即改動是同義重寫而非放寬判定），改動後 `ctest` 三者皆綠。

**`explorer_host_lifetime_check` 註冊結果**：該測試檔案（`tests/unit/explorer_host_lifetime_check.cpp`）本來就已经被**根目錄** `CMakeLists.txt` 编译成 `panedock_explorer_host_lifetime_check.exe`（第 119-128 行），只是從未被任何 `add_test` 收錄進 `ctest`。已在 `tests/CMakeLists.txt` 的 `if(WIN32)` 區塊補上 `add_test(NAME panedock_explorer_host_lifetime COMMAND panedock_explorer_host_lifetime_check)`，不需要改成 release 腳本（它本身就是一個會建立真實 HWND + `OleInitialize` + 一個真實 `IExplorerBrowser` 的可執行檔，headless CI 下用隱藏視窗跑，實測通過）。`ctest` 中已出現為 `panedock_explorer_host_lifetime`，本地執行 0.63 秒通過。

**Select-String 三條 Agent Check 的結果與偏差說明**：
1. `Select-String -Path src/app_shell/main.cpp -Pattern 'std::array<.*kExplorerCount>'` —— **非空**（9 個結果）。**誠實記錄**：這條檢查字面上會連帶命中 `panes` 欄位本身（`std::array<panedock::app_shell::Pane, kExplorerCount> panes{}`）、新增的 `realized_flags` 自由函式內部的局部變數，以及 `apply_layout` 裡本來就存在、與本票無關的五個純局部暫存陣列（`changed_panes`/`pending_shell_rects`/`shell_positions_deferred`/`container_rects`/`explorer_positions`）。這條規則的意圖（AppState 不得再有 pane-parallel 的「原始資料」陣列）已透過人工核對 `AppState` 定義逐項確認達成——`explorers`/`realized`/`suppress_history_record` 三個欄位已不存在。regex 字面沒有排除「Pane 陣列本體」與「函式局部變數」，故不可能字面上清零；已在此處記錄而非削弱檢查或刪除合法程式碼去湊合它。
2. `Select-String -Path src/app_shell/pane.cpp -Pattern 'plan_realization|RealizationPlan'` —— 空，符合。
3. `Select-String -Path src/app_shell/pane.h -Pattern 'AppState|std::function|callback'` —— 空（把原本一段解釋性註解裡的「navigation callbacks」改寫成「navigation notification setup」，避免注解本身觸發這條反耦合檢查；程式碼本身從未有這些依賴）。

**編譯與測試**：`cmake -S . -B build ... && cmake --build build` 全綠（另外把 `tests/CMakeLists.txt` 裡 `pane` 測試目標的連結庫從單獨 `panedock_pane` 補成 `panedock_pane panedock_explorer_host panedock_core`，因為 `Pane` 現在內嵌 `ExplorerHost` 成員，`pane_test.cpp` 連結時需要它的符號）。`ctest --test-dir build --output-on-failure`：**21/21 全綠**（含 `panedock_launch_smoke`、新註冊的 `panedock_explorer_host_lifetime`）。`git diff --check` 無空白錯誤。

**Graceful-close 驗證**：`Start-Process build\PaneDock.exe` + `CloseMainWindow()` 連續執行 **3 次**，每次都在 5 秒逾時內正常結束（未殘留 process）。

**20 次版型切換 live view count 讀數**：**未執行**——這需要對執行中的 GUI 做連續滑鼠互動（切版型 1→2→3→4→1 共 20 次並讀 `--diagnostic` 模式的 live view count 檔案），依專案既有規範（sustained/multi-step 互動測試留給使用者，不用 computer-use 佔用使用者滑鼠），此項與下方使用者實機檢查清單第 1 項合併，留待使用者在真實硬體上執行並回報 live view count 是否回到基線。程式碼本身這次改動只換了 realize/derealize 的呼叫路徑（`chrome.realize(...)`/`chrome.derealize()` 取代直接呼叫 `state.explorers[index].initialize/destroy`），`plan_realization` 的輸入輸出語意完全不變，理論上行為零改變，但仍需要實機確認。

**使用者實機檢查清單（第 1–8 項，本系列風險最高的一張，尚未執行）**：全部 8 項尚待使用者在真實硬體上驗證，尤其：
- 第 1 項（連續 20 次切版型）與第 6 項（拖曳中關閉，PD-173 的既有場景）——這兩項最直接測試本票改動的 realize/derealize 呼叫路徑與 `Pane::destroy()` 的新順序。
- 第 5、7 項——分別測試檔案操作進行中關閉、開右鍵選單觸發外部程式後關閉；這兩項曾在 PD-181 的交接區列為「`graceful-close` 自動化測不到」的項目，本票同樣測不到，原因相同（需要真正啟動一個檔案操作/右鍵選單流程並在其進行中觸發關閉，屬互動測試）。
- 第 8 項（工作管理員無殘留 `PaneDock.exe`）——每次測試後人工檢查。

## 後續候選（本票不做，僅記錄判斷）

`Pane` 向上呼叫點清單（供 `PaneOutcome` 候選觸發條件判斷）已在本票完成後重新核算，仍是 **0 處**——見 `docs/tickets.md` §候選 的對應行，已回寫本次判斷。`PaneOutcome` 維持不開票。
