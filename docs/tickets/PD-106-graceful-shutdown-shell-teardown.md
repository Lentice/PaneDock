# PD-106 — Graceful close 後 Shell teardown 殘留程序

Phase 7 · app_shell / explorer_host · Depends on: PD-007, PD-032, PD-068

- Source: PD-102 實機驗證期間的可重現退出現象(2026-08-28)。
- Origin: 測試程序用不帶 `/F` 的 `taskkill /PID` 關閉後,主視窗先消失,但 PaneDock PID 仍存活；重送一次同樣訊號後才退出。
- Priority: HIGH——退出時程序殘留會鎖住執行檔、阻礙下一次啟動與建置,也可能讓使用者誤以為 PaneDock 已經關閉但仍佔用 Shell 資源。

## 已確認的現況(有程式碼與實測證據)

1. `src/app_shell/main.cpp:3951-3963` 的 `WM_CLOSE` 在 `DestroyWindow` 之前同步執行 `revoke_drag_hover_targets`、`save_now` 與 `destroy_explorers`。
2. `destroy_explorers`(`main.cpp:1906-1912`) 逐一呼叫四個 `ExplorerHost::destroy()`。
3. `src/explorer_host/explorer_host.cpp:708-767` 的 `ExplorerHost::destroy()` 最後同步呼叫 `IExplorerBrowser::Destroy()`(`:759`),之後才釋放 browser 與其 view 相關 COM 物件。
4. 同一次 Release diagnostic 測試先由 `EnumWindows` 找到 `PaneDockMainWindow`(PID 37164,主視窗矩形 `408,192–1394,837`),再由 `EnumChildWindows` 找到 tab strip 候選；送出不帶 `/F` 的 `taskkill /PID 37164` 後,後續檢查看到 PID 37164 仍存活且 `MainWindowHandle=0`,已沒有頂層主視窗。第二次送出同樣的 `taskkill /PID 37164` 後程序才消失。
5. 這不是「漏掉 `Destroy`」的證據；相反地,目前所有已初始化的 host 都被要求在同一個 UI 關閉路徑同步 teardown。可觀察到的根因範圍是 Shell view teardown(`IExplorerBrowser::Destroy`/其回入的 Shell 工作)阻塞關閉路徑,或其與主視窗銷毀的重入順序讓程序在主視窗消失後仍未返回訊息迴圈。實作 agent 必須以階段計時或 debugger/stack evidence 把兩者釘死,不可只憑猜測改順序。

## Binding constraints — quoted, do not weaken

`docs/design-spec.md §9.4 關閉序列`:

> 1. 擷取現行狀態並原子寫入 session document
> 2. **destroy 全部 live `IExplorerBrowser`**(每個曾 `Initialize` 的都必須 `Destroy`)
> 3. destroy pane HWND
> 4. destroy 主視窗
> 5. 退出訊息迴圈
> 6. `CoUninitialize`
> 順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。

`AGENTS.md`:

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

> Keep changes scoped to the ticket, and update the affected documents when behavior changes.

`docs/development.md`:

> Shell APIs re-enter our message loop. Any host-side lock, and any "close the view then wait for its event" sequence, must be reentrancy-safe or it will deadlock.

> For Shell-extension troubleshooting, start the same `PaneDock.exe` process with `--diagnostic` (or `/diagnostic`).

## Files to read and trace first

- `src/app_shell/main.cpp:1847-1866`——`save_now` 與 atomic session write。
- `src/app_shell/main.cpp:1906-1912`——`destroy_explorers` 的所有 host teardown 呼叫。
- `src/app_shell/main.cpp:3951-4005`——`WM_CLOSE`、`WM_DESTROY`、`WM_NCDESTROY`、`WM_ENDSESSION` 的關閉與重入路徑。
- `src/app_shell/main.cpp:4211-4268`——訊息迴圈退出後的第二次 `destroy_explorers` 呼叫。
- `src/explorer_host/explorer_host.cpp:708-767`——`ExplorerHost::destroy()` 的完整 COM/ HWND 清理順序。
- `src/sidebar/sidebar.cpp:77-81`——sidebar `RevokeDragDrop` 是否與主視窗 teardown 重複或回入。
- `src/core/session.cpp:520-580`——session 原子替換與 durability hook 的實際成本。
- `docs/design-spec.md §9.2、§9.4、§11、§12.3、§14`、`docs/development.md`——STA、關閉順序、錯誤處理與禁止 fake COM seam 的約束。
- `docs/tickets/PD-007-single-explorer-host-and-shutdown.md`、`docs/tickets/PD-032-endsession-clean-shutdown-handling.md`、`docs/tickets/PD-068-shell-folder-view-setcallback-null-out-param.md`——既有生命週期、系統關機與 teardown crash precedent。

## Fix 方向 / Scope

1. 先在不改變產品關閉順序的前提下,為 `WM_CLOSE` → `ExplorerHost::destroy` → `IExplorerBrowser::Destroy` → `DestroyWindow` → message loop exit 建立最小、可移除的階段計時或等價 debugger evidence,確認實際阻塞點。
2. 修正已確認的阻塞／重入根因,使正常 local Shell view 與 `--diagnostic` 模式的 graceful close 在主視窗消失後不留下 PaneDock process；仍必須 destroy 每個已 Initialize 的 browser,不得用跳過 `Destroy`、`TerminateProcess` 或 `/F` 掩蓋問題。
3. 若根因是關閉路徑在 `WM_CLOSE`、`WM_DESTROY`、message loop 尾端重複做同一工作,集中成一次 reentrancy-safe teardown；若根因在 `IExplorerBrowser::Destroy` 的 Shell 回入,採符合單一 STA 與 §9.4 順序的最小修正,不建立 async runtime 或 background worker。
4. 保留 session 的 clean-shutdown 語意與 atomic replace；關閉時不得因 teardown 延遲而遺失最後一次必要狀態。
5. 完成後移除所有暫時 debug log／probe,並在交接區留下實測的各階段耗時與退出結果。

## Non-goals

- 不改變 §9.4 的「先保存、再 destroy live Shell view、再 destroy parent HWND、再退出 message loop」順序。
- 不在本票修改 `src/core`，不為 `IExplorerBrowser` 新增 fake／抽象測試 seam。
- 不把 live Shell view 移到第三方 process,不新增 IPC、服務、driver、runtime 或網路依賴。
- 不用 `TerminateProcess`、`taskkill /F`、強制關閉或跳過 COM cleanup 解決表面上的程序殘留。
- 不改變 Group、tab、layout、拖放或 PD-102 的「+」按鈕視覺行為。
- 不把所有 Shell teardown 搬到背景執行緒；單一 STA 與 COM apartment 規則仍有效。

## Acceptance Criteria

1. Release build 下以 `PaneDock.exe --diagnostic` 啟動,用現有 `EnumWindows`/`EnumChildWindows` 或等價真實 HWND 定位方式確認主視窗；以不帶 `/F` 的 `taskkill /PID <pid>` 關閉後,在 5 秒內同時觀察到主視窗與 PID 消失,連續 3 次通過。
2. 至少再以一般模式執行 1 次相同的 graceful-close check；若一般模式被既有 Shell location 阻塞,需記錄實際階段與明確原因,不得把未量測的結果宣稱通過。
3. 階段 evidence 證明每個曾 `Initialize` 的 `IExplorerBrowser` 都在 parent HWND 銷毀前完成 `Destroy`，且 no live view remains after message loop exit。
4. 關閉後 `session.json` 的 `clean_shutdown` 正確為 `true`，既有未知欄位與 backup/atomic replace 行為不變。
5. `cmake --build build`、`ctest --test-dir build --output-on-failure` 與 `git diff --check` 全數通過。
6. `rg` 確認暫時 debug prefix、`TerminateProcess`、`taskkill /F` 與新增 background shutdown loop 均未留在產品程式碼。

## Agent Checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
rg -n "WM_CLOSE|WM_DESTROY|destroy_explorers|ExplorerHost::destroy|IExplorerBrowser::Destroy|PostQuitMessage" src\app_shell\main.cpp src\explorer_host\explorer_host.cpp
```

實機 check 必須使用 Release `build\PaneDock.exe --diagnostic`、真實 `EnumWindows`/`EnumChildWindows` 幾何與不帶 `/F` 的 `taskkill /PID`；不得以猜測座標、`CopyFromScreen` 或強制終止代替。若加入暫時計時器,只能是一次性的診斷 instrumentation，完成後刪除。

## Handoff requirements

- 記錄實際確認的阻塞函式、每個 teardown 階段耗時與量測機制。
- 記錄 diagnostic 3 次與一般模式 1 次的 graceful-close 結果、PID 是否在 5 秒內消失。
- 記錄所有已 Initialize 的 host 數量、`Destroy` 呼叫數量與 parent HWND 銷毀先後證據。
- 記錄 session clean-shutdown、backup 與 atomic replace 的結果。
- 列出未驗證項目與原因；不得把單次成功或工具退出延遲當作產品根因。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-28 診斷交接

- Release diagnostic 實測 PID 37164：`EnumWindows` 找到 `PaneDockMainWindow`，主視窗矩形為 `408,192–1394,837`；`EnumChildWindows` 找到 `Static` tab strip 候選，Id 200 的矩形為 `658,283–1010,314`。送出第一次不帶 `/F` 的 `taskkill /PID 37164` 後，主視窗消失但後續 `Get-Process` 仍看到 PID 37164 且 `MainWindowHandle=0`；再次送出同樣訊號後程序才消失。
- 程式碼 evidence：`WM_CLOSE` 在 `DestroyWindow` 前同步呼叫 `save_now` 與 `destroy_explorers`；`destroy_explorers` 逐一呼叫 `ExplorerHost::destroy`；後者同步呼叫 `IExplorerBrowser::Destroy`。目前根因定位到 synchronous Shell teardown／其 re-entry 造成的關閉路徑殘留，精確阻塞點留待本票以階段計時或 debugger stack 釘死。
- 未修改退出路徑；PD-102 的暫時樣式 patch 仍由原實作 agent 接續處理。未開新增 automated test，因 `explorer_host` 不屬唯一 `core` seam；本票要求真實 Release diagnostic graceful-close evidence。

### 2026-08-28 階段計時與驗證方法根因

- 以一次性 `[PD106-PROBE]` stderr 探針量測 `WM_CLOSE`、每個 `ExplorerHost::destroy()`、`IExplorerBrowser::Destroy()`、`DestroyWindow`、message loop exit 與 `OleUninitialize`；探針完成後已由產品碼移除。另以 P/Invoke `EnumWindows`／`EnumChildWindows`、`GetWindowThreadProcessId`、`GetClassNameW`、`GetWindowRect`、`GetDlgCtrlID` 建立 5 秒判定迴圈；沒有使用推估座標、`CopyFromScreen`、`/F` 或強制終止。
- 實際阻塞點**不在** `WM_CLOSE -> destroy_explorers -> ExplorerHost::destroy -> IExplorerBrowser::Destroy -> DestroyWindow` chain。四次 diagnostic 的第一次 `taskkill /PID` 都在 5 秒逾時，且 probe log 完全沒有 `WM_CLOSE enter`；訊號送出前 `EnumWindows` 的同 PID 頂層 HWND 依序包含 `tooltips_class32`、（一輪另有 `WorkerW`）、`PaneDockMainWindow`、`IME`。因此第一次非強制 `taskkill` 命中同 PID 的 Shell-owned 頂層 tooltip／worker，而沒有進入 PaneDock 主視窗的 `WM_CLOSE`；先前「主視窗消失但 PID 存活」並非 Shell teardown 阻塞的充分證據。
- 對逾時 PID 重送同一個不帶 `/F` 的 `taskkill /PID` 後，才觀察到 `WM_CLOSE enter`，且 teardown 全部快速返回。四輪 diagnostic 的四個 `IExplorerBrowser::Destroy` 耗時分別為 `63/46/47/47 ms`、`47/62/31/47 ms`、`63/46/63/47 ms`、`63/47/31/62 ms`；`WM_CLOSE` 到 message loop exit 分別為 `266/266/312/281 ms`，`OleUninitialize` 再耗 `15/16/16/0 ms`。沒有任何 `Destroy` 超過 63 ms，也沒有 parent teardown hang。
- 每輪 diagnostic 在 `destroy_explorers enter` 都量到 `live=4`，四個 host 各恰好一次進入並離開 `IExplorerBrowser::Destroy`，之後 `live=0`；接著才出現 `DestroyWindow enter`、`WM_DESTROY`、`DestroyWindow leave` 與 message loop exit。訊息迴圈後的既有防禦性第二次 `destroy_explorers` 對四個已 uninitialized host 都立即返回，沒有第二次呼叫 `IExplorerBrowser::Destroy`。這證明所有 initialized host 均在 parent HWND 銷毀前完成 `Destroy`。
- 票面 3+1 結果：diagnostic 單一非強制訊號連續四次皆未在 5 秒內退出（`5010/5055/5044/5028 ms`，0/4 PASS）；相同訊號重送後四次皆在約 0.3 秒內完成產品 teardown。一般模式一次在 `740 ms` 內 PID 與全部頂層 HWND 同時消失（1/1 PASS）；該輪訊號送出時只有第一個延後 realize 的 host 已 initialized，probe 顯示 `live=1 -> 0` 且其 `Destroy` 為 `62 ms`。
- graceful close 寫出的 `session.json` 實測 `clean_shutdown:true`；`session.json.bak` 存在且在關閉時更新，primary 長度維持 63494 bytes，顯示既有 temp + durability hook + rename／backup 路徑仍執行。測試前 primary 已備份，測試後完整還原；還原檔與測試前備份 SHA-256 同為 `2148BCC06A86F760C4A85DA029AF596D77D0D76BF62EF2DE9760E28340EE938A`。
- 未修改產品關閉碼：讓主視窗攔截其他模組所建頂層 HWND 的 `WM_CLOSE`、改變 z-order，或銷毀 Shell tooltip 都只是在迎合 `taskkill` 的單 HWND 選擇行為，會侵入原生 Shell UI 且沒有產品故障證據。因 diagnostic 3 次單訊號 acceptance 未達成，`docs/tickets.md` 的 PD-106 維持 `ready`；後續需先把 graceful-close 驗證改成對 `EnumWindows` 找到的 `PaneDockMainWindow` 明確送 `WM_CLOSE`（再獨立觀察 PID），或提出產品情境可重現的真正 teardown hang 證據，才適合修改產品碼。
