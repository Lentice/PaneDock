# PD-127 — teardown 重入未受守衛:queued 訊息在 `IExplorerBrowser::Destroy` pump 內重入,重新建立 live view 後 parent 才被銷毀

Phase 7 · app_shell / explorer_host · Depends on: PD-124, PD-125, PD-126

- Source: 「關閉 AP 但 process 依然存在」稽核迴圈第四輪(2026-08-30),獨立 agent 複核(eadle session)確認。
- Origin: PD-124/125/126 修的都是「關閉訊息本身」(WM_CLOSE/WM_QUIT/WM_ENDSESSION)的旗標與守衛;但 teardown 的 `IExplorerBrowser::Destroy` 會 pumping,能重入派發**非關閉訊息**(queued `WM_COMMAND`、`WM_TIMER`、drag-hover message),這些訊息進入 `apply_layout`／`activate_group`／`switch_active_tab` 時,**沒有 `closing_` 守衛**。
- Priority: HIGH——正是 `AGENTS.md` 明指的「Host-side locking and shutdown sequencing must be reentrancy-safe」與「Never destroy a parent HWND while a view is alive」。屬崩潰／洩漏面,不是風格。

## 已確認的根因(有程式碼證據)

1. `WM_CLOSE`(`src/app_shell/main.cpp:5013`)的 teardown 中,`destroy_explorers`(`:2545`)逐一 `ExplorerHost::destroy()`;後者在 `explorer_host.cpp:759` 同步呼叫 `IExplorerBrowser::Destroy()`。Shell teardown 會在我們的 STA 上 pump 一個巢狀 loop。
2. 此時佇列中若已有**非關閉訊息**(例:使用者先點側邊欄某 Group(**queued `LBN_SELCHANGE`→`WM_COMMAND`**),隨即關閉;或 splitter 節流 `WM_TIMER`、拖曳懸停 `kDragHoverMessage`),該 pump 會派發它:
   - `WM_COMMAND`→`activate_group`(`main.cpp:2806`-`2813`):對 `state.realized[pane]` 的 host 呼叫 `navigate(...)`,再 `apply_layout`。若先前 destroy 迴圈已跑過某槽,此處會**重新觸發 navigate / initialize** 重新建立 live view。
   - `apply_layout`(`:2591`-`:2744`):對選取槽 `explorer.initialize(...)`／`set_rect`／`set_visible`——在一個正在 `Destroy` 的 host 上 re-enter,或於 parent 銷毀後才被 `destroy_explorers`(`main.cpp:5364`)第二次 `Destroy`。
   - `switch_active_tab`(`:2957`-`:2966`):同樣對 `state.realized[pane_index]` 的 host `navigate`。
3. 這些入口都只在 `WM_CLOSE`／`WM_ENDSESSION`／`WM_DESTROY` 之外運行,沒看 `state.closing_`;而 `closing_`(`main.cpp:464`)只在 `WM_CLOSE`(`:5005`)／`WM_ENDSESSION`(`:5054`)置位。因此 `closing_` 對這些**重入的 live-view mutation** 完全無效。
4. 後果:一個 live view 被重新建立,接著 parent HWND 被 `DestroyWindow(window)`($9.4 違反),並在 parent 死後被尾部 `destroy_explorers` 第二次 `Destroy`——use-after/re-enter into a mid-destroy COM view,可能崩潰或以洩漏的方式殘留。

## 已確認的次要根因(同批稽核,窄觸發)

### B. `WM_CREATE` 內建 modal `MessageBoxW` 於 half-created HWND 上
- `WM_CREATE` 中 `apply_layout` 失敗時(`main.cpp:4291`-`4296`)呼叫 `MessageBoxW(window,...)`,此 MessageBox 在 `CreateWindowExW` 尚未返回時跑 modal loop;若此時收到對該 HWND 的 `WM_CLOSE`,會於 window 建立中途 destroy 它,之後 `CreateWindowExW` 與 `wWinMain` 對死亡的 HWND 操作。
- 僅在 `CLSID_ExplorerBrowser` 建立失敗／資源耗盡等降級路徑可達,窄但屬崩潰面。

## Binding constraints — quoted, do not weaken

`docs/design-spec.md §9.4`:

> 1. 擷取現行狀態並原子寫入 session document
> 2. **destroy 全部 live `IExplorerBrowser`**(每個曾 `Initialize` 的都必須 `Destroy`)
> 3. destroy pane HWND
> 4. destroy 主視窗
> 5. 退出訊息迴圈
>
> 順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。

`AGENTS.md`:

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`:

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

## Files to read and trace first

- `src/app_shell/main.cpp:464`(`closing_`)、`:2545`(`destroy_explorers`)、`:5013`-`:5068`(`WM_CLOSE`／`WM_ENDSESSION`)。
- `src/app_shell/main.cpp:2591-2744`(`apply_layout` 內 `initialize`/`set_rect`/`set_visible`)、`:2806-2813`(`activate_group.navigate`)、`:2957-2966`(`switch_active_tab.navigate`)。
- `src/explorer_host/explorer_host.cpp:708-771`(`ExplorerHost::destroy`／`browser_->Destroy()`),其 `destroying_`/`initialized_` 守衛(僅保護 destroy,不保護重入的 initialize/navigate)。
- `src/explorer_host/explorer_host.h`——`initialize`/`navigate`/`set_rect`/`set_visible`/`focus` 簽章。
- `docs/tickets/PD-124/125/126` 交接區——先前旗標與守衛的範圍。

## Fix 方向 / Scope(已實作)

在共享的 live-view mutation 入口加 `closing_` 守衛,並把 teardown 前的資源回收補齊,使「關閉進行中絕不重新建立／導覽 live view」。

1. `apply_layout`(`main.cpp:2546`):函式第一行 `if (state.closing_) return S_OK;`——阻斷任何關閉期間重入的 layout 帶(它正是 `initialize`/`set_rect`/`set_visible`/splitter/WM_SIZE/`WM_TIMER`/drag-hover 的共同入口)。
2. `activate_group`(`main.cpp:2799`):第一行 `if (state.closing_) return;`——Group 切換重入(側邊欄 `LBN_SELCHANGE`→`WM_COMMAND`、drag-hover)在關閉時 no-op。
3. `switch_active_tab`(`main.cpp:2949`):第一行 `if (state.closing_) return;`——tab 切換重入(kTabStripSelectionMessage、drag-hover)在關閉時 no-op。
4. `WM_ENDSESSION`(`main.cpp:5051`):在 destroy 前補 `revoke_drag_hover_targets(*state)` 與 `cancel_session_save_timer(*state)`(與 `WM_CLOSE` 一致),避免 OLE drop-target 註冊殘留於即將銷毀的視窗、以及 timer 在關機路徑洩漏。
5. **(次要 B)** `WM_CREATE` 失敗(`main.cpp:4291`):不再於 half-created HWND 上彈 modal `MessageBoxW`,改為 `state->startup_error_message = L"PaneDock could not open the Shell view.";`;由 `wWinMain` 在 `window == nullptr` 分支以無 owner 的 `MessageBoxW` 顯示(`main.cpp:5257`)。消除該啟動 modal loop,避免 window 建立中途被 close 摧毀。

為何有效:守衛選在「重新建立／導覽 live view」的共享函式(`apply_layout`/`activate_group`/`switch_active_tab`),任何這三種重入在關閉中都會立即返回,不再以損害中的 host 建立 view;§9.4 順序、`save_now`、無 thread/hook/timer 皆不變。B 項把啟動錯誤回饋移到 `CreateWindowExW` 返回後,關閉中途不再有 half-created HWND。

## Non-goals

- 不改變 §9.4 順序、不跳過任何已 `Initialize` browser 的 `Destroy`、不把 teardown 搬離 UI 執行緒。
- 不為「重新建立 view」的每一個呼叫端加守衛(只在共享入口,遵循「a guard in the shared function is a smaller diff than a guard in every caller」)。
- 不修改 `src/core`,不為 `IExplorerBrowser` 新增 fake／抽象測試 seam。
- 不因窄觸發的 B 項而新增 async runtime、背景執行緒或 IPC。
- 不把啟動錯誤的 UX 改成靜默;保留「顯示錯誤並退出」,只是延後到 create 返回後。

## Acceptance Criteria

1. `cmake --build build`、`ctest --test-dir build --output-on-failure` 與 `git diff --check` 全數通過。
2. `rg -n "closing_|startup_error_message|case WM_ENDSESSION" src/app_shell/main.cpp` 確認:`apply_layout`／`activate_group`／`switch_active_tab` 開頭皆有 `closing_` 守衛;`WM_ENDSESSION` 有 `revoke_drag_hover_targets`＋`cancel_session_save_timer`;`WM_CREATE` 失敗路徑不再有 `MessageBoxW`,改為 `startup_error_message`;`wWinMain` 的 `window == nullptr` 分支顯示它。
3. 無任何 `src/app_shell/main.cpp` 內殘留 `MessageBoxW(window`(WM_CREATE 內的 modal bubble 已移除;其餘 `MessageBoxW` 在 create 返回後才是合法的)。
4. 實機(Release):正常啟動關閉、以及「點某 Group 後立即關閉」與「splitter 拖曳中關閉」均在 5 秒內退出且無 crash(沿用 PD-106 協定,`EnumWindows`＋不帶 /F 的 `taskkill /PID`)。
5. 關閉後 `session.json` 的 `clean_shutdown` 為 `true`,未知欄位保留與 backup/atomic replace 行為不變。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
rg -n "closing_|startup_error_message|MessageBoxW\(window|revoke_drag_hover_targets|cancel_session_save_timer" src/app_shell/main.cpp
```

實機 check 使用 Release `build\PaneDock.exe`、真實 `EnumWindows`/`EnumChildWindows` 幾何與不帶 `/F` 的 `taskkill /PID`;不得以猜測座標、`CopyFromScreen` 或強制終止代替。

## Handoff requirements

- 記錄三個共享入口的 `closing_` 守衛位置、為何它們涵蓋 queued `WM_COMMAND`/`WM_TIMER`/drag-hover 的重入,及 `WM_ENDSESSION` 補上的 revoke/timer。
- 記錄 B 項(WM_CREATE 錯誤延後顯示)的改動與啟始錯誤欄位。
- 記錄 build／CTest／diff 結果與是否有真實桌面的「Group 點按→立即關閉」／「splitter 拖曳→關閉」驗證。
- 列出未驗證項目與原因;不得把單次成功或工具退出延遲當作根因。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-30 已實作共享入口守衛與啟動錯誤延後

- 根因(主要)∶teardown pump 重入「非關閉訊息」,經 `apply_layout`/`activate_group`/`switch_active_tab` 重新建立／導覽 live view。`closing_` 原只在 WM_CLOSE/WM_ENDSESSION 檢查,對這些入口無效。
- 根因(次要 B)∶WM_CREATE 內 `MessageBoxW(window,...)` 在 half-created HWND 上跑 modal loop。
- 已修改 `src/app_shell/main.cpp`(僅此一檔):
  - `apply_layout`(`:2546`)、`activate_group`(`:2799`)、`switch_active_tab`(`:2949`)開頭加 `if (state.closing_) return;`(apply_layout 回 `S_OK`)。
  - `WM_ENDSESSION`(`:5051`)destroy 前補 `revoke_drag_hover_targets`＋`cancel_session_save_timer`。
  - `AppState` 新增 `std::wstring startup_error_message;`;`WM_CREATE` 失敗(`:4291`)改為設此欄位、移除 in-create `MessageBoxW`;`wWinMain` 的 `window == nullptr` 分支(`:5257`)以無 owner 的 `MessageBoxW` 顯示。
- §9.4 順序、`save_now`、`FlushFileBuffers` 原子寫入、未知欄位保留未變;未新增 thread/hook/timer。
- 驗證:`cmake --build build` PASS、`ctest --test-dir build --output-on-failure` 6/6 PASS(獨立重跑 `panedock_launch_smoke` 亦 PASS;全 suite 重跑通過)、`git diff --check` 通過。
- `panedock_launch_smoke` 有已知的間歇性 `STATUS_STACK_BUFFER_OVERRUN`(tracker §候選,PD-003 記錄,無法穩定重現),與本次改動無關——它偶發於全 suite 執行,單獨重跑及完整重跑皆通過。
- 待補實機驗證:Release 桌面下「Group 點按→立即關閉」與「splitter 拖曳中關閉」,確認 5 秒內 exit 且無 crash。
