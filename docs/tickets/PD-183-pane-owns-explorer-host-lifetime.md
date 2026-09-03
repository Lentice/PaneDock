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
