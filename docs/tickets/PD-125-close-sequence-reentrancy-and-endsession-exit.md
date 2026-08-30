# PD-125 — 關閉序列重入與 `WM_ENDSESSION` 未退出訊息迴圈(違反 §9.4)

Phase 7 · app_shell / window_proc · Depends on: PD-007, PD-032, PD-106, PD-124

- Source: 「關閉 AP 但 process 依然存在」稽核迴圈的第二輪發現(2026-08-30)。PD-124 已修掉「巢狀 modal loop 吃掉 `WM_QUIT`」的第一種態樣;本票處理同一關閉路徑上另兩個同類問題。
- Origin: 承接 PD-124 的 audit loop,停在「直到沒有 important/critical issue」。本票是該工具發現的下一批。
- Priority: HIGH——兩者都直接落在 `AGENTS.md` 明文「Host-side locking and shutdown sequencing must be reentrancy-safe」與 `docs/design-spec.md §9.4` 的關閉順序上,前者是崩潰面(在 view 仍存活時 destroy parent HWND),後者把「destroy 主視窗／退出迴圈」留給 OS,若 OS 延後終止即殘留。

## 已確認的根因(有程式碼證據)

### A. `WM_CLOSE` 可被重入,造成二次 teardown 並提前 destroy parent HWND

1. `WM_CLOSE`(`src/app_shell/main.cpp:5008`)在同一次處理中依序:`destroy_pinned_locations_manager` → `revoke_drag_hover_targets` → `cancel_session_save_timer` → `capture_window_placement` → `save_now` → `destroy_explorers` → `DestroyWindow` → `PostQuitMessage`。
2. `destroy_explorers`(`:2539`)逐一呼叫 `ExplorerHost::destroy()`;後者同步呼叫 `IExplorerBrowser::Destroy()`。`AGENTS.md` 明言 Shell API 會在「internal view work」期間重入本程式 message loop——`IExplorerBrowser::Destroy` 正是這種會 pumping 的 teardown。
3. 若在該 pumping 期間有一個**已排入佇列的第二個 `WM_CLOSE`**(使用者快速連按 X／Alt+F4,或上一輪 `WM_CLOSE` 尚未結束又關一次),它會被內層 loop 重入派發,再次執行整段 `WM_CLOSE`:重新 `save_now`、重新 `destroy_explorers`(對 `destroying_` guard 以外的剩餘 host 直接二次 destroy),並**提前 `DestroyWindow(window)`**——在 `destroy_explorers` 仍對第一個 host 執行 `IExplorerBrowser::Destroy()` 時就銷毀 parent HWND。這違反 §9.4「先 destroy 全部 live view、再 destroy parent HWND」,是 `AGENTS.md` 明指的已知崩潰面。PD-124 的 `quit_requested` 旗標能讓此路徑最終退出 process,但消除不了這段提前 destroy。

### B. `WM_ENDSESSION(wparam=TRUE)` 不做視窗銷毀、不退出訊息迴圈

1. 現行 `WM_ENDSESSION`(`:5055`)只在 `wparam` 為真時 `destroy_pinned_locations_manager` + `destroy_explorers`,**不 `DestroyWindow`、不 `PostQuitMessage`、不設 `quit_requested`**——把「destroy 主視窗、退出迴圈」全部留給 OS 在關機流程中強制終止。
2. 若關機終止被 OS 延後(某些 session end／fast user switch 情境),process 會帶著「主視窗仍在、但 live view 已被 destroy」的狀態殘留,命中「看得到卻關不掉」的同類現象。
3. 對照 §9.4,保存已在 `WM_QUERYENDSESSION`(`:5048`)完成;每個 initialized browser 的 `Destroy` 在 `WM_ENDSESSION` 完成;因此「destroy parent HWND、PostQuitMessage 退出迴圈」也該由本 app 完成,不該仰賴 OS。

## Binding constraints — quoted, do not weaken

`docs/design-spec.md §9.4 關閉序列`:

> 1. 擷取現行狀態並原子寫入 session document
> 2. **destroy 全部 live `IExplorerBrowser`**(每個曾 `Initialize` 的都必須 `Destroy`)
> 3. destroy pane HWND
> 4. destroy 主視窗
> 5. 退出訊息迴圈
> 6. `CoUninitialize`
>
> 順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。

`AGENTS.md`:

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`:

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

## Files to read and trace first

- `src/app_shell/main.cpp:449-476`——`AppState` 的 `quit_requested` 與新增的 `closing_`。
- `src/app_shell/main.cpp:4996-5065`——`WM_CLOSE`／`WM_DESTROY`／`WM_ENDSESSION`(修正後版本)。
- `src/app_shell/main.cpp:2395-2410`(`destroy_explorers`)與 `src/explorer_host/explorer_host.cpp:708-771`(`ExplorerHost::destroy` / `destroying_` guard)。
- `docs/tickets/PD-124-*-nested-modal-loop-close-lost-quit-message.md`、`PD-106-*-graceful-shutdown-shell-teardown.md`——同一關閉路徑的先前兩輪修正與其交接區。

## Fix 方向 / Scope(已實作)

引入 `AppState::closing_`(首次關閉請求即置位)並讓關閉序列**冪等**——重入的關閉訊息不再重跑 teardown、也不提前 destroy parent HWND。

1. `WM_CLOSE`(`main.cpp:5008`):把整段 teardown 與 `DestroyWindow(window)` + `PostQuitMessage(0)` 收進 `if (state != nullptr && !state->closing_)` 內,進入時先 `state->closing_ = true; state->quit_requested = true;`。重入的 `WM_CLOSE` 因 `closing_` 已真而直接 `return 0`,既不重跑 teardown,也不在其仍位於 `destroy_explorers` 中時提前 destroy parent。
2. `WM_ENDSESSION`(`main.cpp:5055`):`wparam` 為真時同樣以 `closing_` 保護,做完 `destroy_pinned_locations_manager` + `destroy_explorers` 後 `assert(live_view_count()==0)`、`DestroyWindow(window)`、`PostQuitMessage(0)`——§9.4 的「destroy 主視窗、退出迴圈」結束由 app 自己完成,不再仰賴 OS 強制終止。
3. §9.4 順序(保存→destroy views→destroy parent→退出迴圈→`CoUninitialize`)嚴格不變;`save_now` 仍在 view teardown 之前(在 `WM_QUERYENDSESSION`);未新增 thread、hook、timer、polling 或 cleanup short-circuit。
4. PDA-106 已把 `PostQuitMessage` 放在 `WM_CLOSE` 的 `DestroyWindow` 之後;本票以 `closing_` 讓「第一次才做」冪等,與 PD-124 的外層 `quit_requested` 旗標互補(前者保證順序,後者保證退出,層層不互相依賴)。

## Non-goals

- 不改變 §9.4 順序,不跳過任何已 `Initialize` browser 的 `Destroy`,不把 teardown 移到背景執行緒。
- 不為「關機被取消、需保留 app」的情境加 veto 邏輯;`WM_QUERYENDSESSION` 一律回 TRUE 的既有行為不變;只有 `WM_QUERYENDSESSION` 被取消後送 `WM_ENDSESSION(wparam=FALSE)` 時才不退出(`if (wparam)` 為假)。
- 不修改 `src/core`,不為 `IExplorerBrowser` 新增 fake／抽象測試 seam。
- 不因 fast user switch/掛起這類 OS 專屬情境新建「關閉確認」提示或 barrier ——不在本票範圍,屬 PD-123 的 runtime validation 類。

## Acceptance Criteria

1. `cmake --build build`、`ctest --test-dir build --output-on-failure` 與 `git diff --check` 全數通過。
2. `rg -n "closing_|WM_QUERYENDSESSION|WM_ENDSESSION|PostQuitMessage" src/app_shell/main.cpp` 確認:`WM_CLOSE` 與 `WM_ENDSESSION(wparam=TRUE)` 都以 `closing_` 保護、都在其中執行 `DestroyWindow`＋`PostQuitMessage`;`WM_QUERYENDSESSION` 不含 `DestroyWindow`。
3. 普通、非重入的關閉仍正常:Release `build\PaneDock.exe` 下以 `EnumWindows` 找 visible `PaneDockMainWindow`、送一次不帶 `/F` 的 `taskkill /PID`,`PaneDock` 的 PID 與全部頂層 HWND 在 5 秒內同時消失(沿用 PD-106 協定,連續 3 次)。
4. `WM_ENDSESSION(wparam=TRUE)` 路徑(以 `PostMessage`／`taskkill` 模擬關機)下,`PaneDock` 在 5 秒內退出且 `clean_shutdown` 仍為 `true`;未出現「主視窗仍在、view 已死」的殘留態。
5. 關閉後 `session.json` 的 `clean_shutdown` 為 `true`,未知欄位保留與 backup/atomic replace 行為不變。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
rg -n "closing_|WM_QUERYENDSESSION|WM_ENDSESSION|PostQuitMessage|DestroyWindow\(window\)" src/app_shell/main.cpp
```

實機 check 使用 Release `build\PaneDock.exe`、真實 `EnumWindows`/`EnumChildWindows` 幾何與不帶 `/F` 的 `taskkill /PID`;不得以猜測座標、`CopyFromScreen` 或強制終止代替。

## Handoff requirements

- 記錄 `closing_` 守衛位置、為何讓 `WM_CLOSE`／`WM_ENDSESSION` 冪等、以及它如何消除「重入時提前 destroy parent HWND」。
- 記錄 build／CTest／diff 結果與是否有真實桌面的普通關閉／模擬關機驗證。
- 列出未驗證項目與原因;不得把單次成功或工具退出延遲當作根因。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-30 已實作 `closing_` 守衛與 `WM_ENDSESSION` 退出

- 根因 A∶見上方「已確認的根因」;`WM_CLOSE` 的 `destroy_explorers` 同步呼叫 `IExplorerBrowser::Destroy()`(Shell teardown 會 pumping),期間可重入派發第二個已排隊 `WM_CLOSE`。
- 根因 B∶`WM_ENDSESSION(wparam=TRUE)` 原本不 destroy 主視窗、不退出迴圈,全仰賴 OS。
- 已修改 `src/app_shell/main.cpp`(僅此一檔):
  - `AppState` 新增 `bool closing_{};`(`:468`,含註解)。
  - `WM_CLOSE`(`:5008`)與 `WM_ENDSESSION`(`:5055`)改為 `if (state != nullptr && !state->closing_) { closing_ = true; quit_requested = true; ...DestroyWindow(window); PostQuitMessage(0); }`。
- §9.4 順序、`save_now`、`FlushFileBuffers` 原子寫入、未知欄位保留皆未變;未新增 thread/hook/timer。
- 驗證:`cmake --build build` PASS、`ctest --test-dir build --output-on-failure` 6/6 PASS、`git diff --check` 通過。PANELDOCK 全程 6/6。
- 與 PD-124 關係:PD-124 修「外層 loop 沒收到 WM_QUIT」(靠 `quit_requested` 外層檢查);本票修「重入讓 close 序列重跑／提前 destroy parent」(靠 `closing_` 冪等守衛)。兩者在同一 `window_proc`,互為補充。
- 待補實機驗證:Release 桌面下普通關閉 3 次與模擬關機(`WM_ENDSESSION` 觸發)1 次,確認 5 秒內 exit 且 clean_shutdown。
