# PD-130 — startup 絕不能因「可復原的 pane/view 失敗」而整窗開不起來或靜默空白;每個失敗都要有提示

Phase 7 · app_shell · Depends on: (none)

- Source: 使用者要求「audit AP startup,考量系統太慢／disk slow／anti-virus block／race condition ... 避免 AP 無法順利開啟或開啟後沒有 UI 或部分功能無法使用,並且出現合適的提示」。稽核迴圈(startup 補強)確認。
- Priority: HIGH——「部分功能無法使用(空白 pane)而無提示」與「單一 bad pane 讓整窗開不起來」都是使用者明確列出的情境,屬正確性／可用性問題。

## 已確認的根因(有程式碼證據)

### A. 單一不可達的 pane 會讓整個視窗開不起來(關鍵)

`WM_CREATE`(`src/app_shell/main.cpp:4295`,稽核前):

```cpp
if (FAILED(apply_layout(window, *state))) {
    state->startup_error_message = L"PaneDock could not open the Shell view.";
    revoke_drag_hover_targets(*state);
    destroy_explorers(*state);
    return -1;   // ← CreateWindowExW 回傳 null,整個 app 不開
}
```

`apply_layout` 在 `WM_CREATE` 以 `startup_realize_pending=true`(`:4120`)時只同步 realize **active pane**(`apply_layout` `index == active`, `:2692`)。若該 active tab 的 stored location 現在不可達(拔除的隨身碟、離線的網路分享、權限拒絕、AV 阻擋、無效解析名稱),`state.explorers[active].initialize(...)` 回 FAILED。此時**一個 pane 壞掉 → `return -1` → 整窗失敗 → app 完全開不起來**。即使把錯誤提示改成訊息,使用者依然「點開就沒有 UI」,因為視窗根本沒建立。

### B. `WM_CREATE` 的多個 `return -1` 沒有錯誤訊息(稽核前)

`case WM_CREATE` 中這些路徑都 `return -1` 但**不設 `startup_error_message`**:
- `InitCommonControlsEx` 失敗(`:4123`)
- `sidebar.create` 失敗(`:4127`)
- `SetWindowSubclass(group_list_proc)` 失敗(`:4132`)
- 各子視窗 `CreateWindowExW` ／`SetWindowSubclass` 失敗(area `:4141`-`:4171`、address/status bar `:4280`-`:4290`)
- `register_tab_drag_hover_targets` 失敗(`:4307`)

當其中任一失敗 → `CreateWindowExW` 回傳 null → `wWinMain` 的 `window==nullptr` 分支只顯示 `startup_error_message`,而它此時為**空字串**(只有 A 的 `apply_layout` 路徑有設定) → **完全靜默退出**,使用者點開圖示卻毫無訊息。

### C. deferred Shell realization 失敗→ 空白 pane 無提示

`kDeferredRealizeMessage`(稽核前 `:4335`):

```cpp
const HRESULT hr = apply_layout(window, *state, true);   // realize_deferred_panes=true
state->startup_realize_pending = false;
if (FAILED(hr)) OutputDebugStringW(...);                  // 只有 debug
```

因 `WM_CREATE` 一律 `startup_realize_pending=true`(`:4120`),`startup_realize_pending` 的 deferred realize 對**每次啟動都會走**;`apply_layout(..., true)` 會 realize 全部 pane,任一 pane 失敗(尤其**非 active** 的 3 個 pane,它們在 `WM_CREATE` 不會被同步 realize)→ `FAILED(hr)` → 只 debug log。容器已 `ShowWindow`(`:2683`)、`state.realized[index]` 為 false → 使用者看到**一個空白的 pane,沒有任何提示**。這正是「部分功能(Pane)無法使用且無提示」。

## Binding constraints — quoted, do not weaken

`AGENTS.md`:

> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups.

`AGENTS.md`:

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`:

> App UI text must be English. No Chinese strings ship in the binary.

`AGENTS.md`:

> Do not bundle schema migrations or destructive cleanup into an unrelated change.

## Files to read and trace first

- `src/app_shell/main.cpp:4120`(`WM_CREATE` 開頭)、`:4295`-`:4301`(`apply_layout` 失敗,本次改寫)、`:4307`-`:4313`(`register_tab_drag_hover_targets`,本次改寫)、`:4330`-`:4341`(`kDeferredRealizeMessage`,本次改寫)。
- `src/app_shell/main.cpp:2590`-`2749`(`apply_layout`:pane realize 分支 `:2690`-`2692`、失敗 `continue` `:2696`-`2699`、回傳首個 failure `:2748`)。
- `src/app_shell/main.cpp:5100`-`5110` 前(`WM_CREATE` 內所有 `return -1` 的清點)。

## Fix 方向 / Scope(已實作)

把「可復原的 startup 失敗」與「致命(UI 建不起來)」分開;可復原者**保留視窗**並提示,致命者**確保有訊息**。

1. **WM_CREATE 開頭**(`main.cpp:4123`):先設 `state->startup_error_message = L"PaneDock could not create its user interface.";`——任何後續致命 `return -1`(sidebar／subclass／控制 create)都保證 `CreateWindowExW==null → wWinMain` 會顯示,消除 B。
2. **`apply_layout` 失敗＝可復原**(`main.cpp:4295`-`4304`):不再 `destroy_explorers`＋`return -1`,改為:
   ```cpp
   const HRESULT layout_result = apply_layout(window, *state);
   if (FAILED(layout_result)) {
       state->startup_warning_message =
           L"PaneDock could not open the Shell view for one or more panes. Some panes may be empty.";
   }
   ```
   不可達的 pane 保持未 realize(空白),但視窗照常開啟;提示在視窗建立後顯示。消除 A。
3. **`register_tab_drag_hover_targets` 失敗＝可復原**(`main.cpp:4317`-`4326`):不再 `destroy_explorers`＋`return -1`;改為 `revoke_drag_hover_targets(*state)` 並在 `startup_warning_message` 空時設「PaneDock could not enable tab drag-and-drop.」。拖曳懸停屬加分功能,不應擋 app 開啟。
4. **「可在視窗建立後顯示的提示」欄位**:於 `AppState` 新增 `std::wstring startup_warning_message;`,`wWinMain` 在視窗建立後(`:5355` 附近,unclean-shutdown 訊息之後)以 `MessageBoxW(window, ...)` 顯示;此為 create 返回後的 modal loop,無 PD-126 的「Half-created HWND」重入問題(`startup_warning_message` 不影響 `window==nullptr` 分支)。
5. **deferred realize 失敗＝可復原**(`main.cpp:4335`-`4346`):`FAILED(hr)` 且 `startup_warning_message.empty()` 時,`MessageBoxW(window, L"PaneDock could not open the Shell view for one or more panes. Some panes may be empty.", ...)`。`empty()` 判斷避免 active pane 在 WM_CREATE 已警告時(其 deferred 再失敗)雙重彈窗;若 WM_CREATE 已成功而 deferred 失敗(通常是非 active pane)才彈一次。主動告知而非靜默 debug。消除 C。

為何有效:任一 pane(尤其 active)壞掉不再讓 app 無法開啟(A);所有 UI 建構致命路徑、所有可復原的面板失敗都有對應提示(B/C)。「用戶不被靜默」與「順序、destroy、保存」皆符合 AGENTS.md。

## Non-goals

- 不為「damage pane」自動 fallback 到別的 folder 或改寫 stored location(那是資料更動,非本 ticket 目的)。
- 不做真正的 intra-window 錯誤 banner 系統(既有 UI 只有 modal warning,沿用;不做無 UI 的自訂訊息 region)。
- 不改 `kDeferredRealizeMessage` 的既有延遲標記語意(`startup_realize_generation`／`pending`)、不新增 thread。
- 不做非 UI 的 toast／tray 通知;僅用與產品一致的 modal `MessageBoxW`,這是 Startup 已有模式。
- 不改 `src/core`。

## Acceptance Criteria

1. `cmake --build build`、`ctest --test-dir build --output-on-failure`、`git diff --check` 全數通過。
2. `rg -n "startup_warning_message|could not create its user interface|could not open the Shell view|could not enable tab drag-and-drop" src/app_shell/main.cpp`:
   - `WM_CREATE` 開頭有 generic `could not create its user interface` 設定;`apply_layout` 失敗改寫 `startup_warning_message` 且**不再** `destroy_explorers`／`return -1`;`register_tab_drag_hover_targets` 失敗改寫 warning 且**不再** `return -1`;`kDeferredRealizeMessage` 失敗彈 `MessageBoxW`。
3. 實機(Release)用「session 中 active tab 指向不存在／拒絕存取的 folder」的 session:
   - app 應**會開啟**(有視窗、側邊欄、其他 pane),active pane 空白;
   - 出現 `could not open the Shell view ...` 提示;
   - 不應「點開無視窗」。
4. 用不可 drag-drop 環境(可考慮暫時以 `--diagnostic` 或模擬 `RegisterDragDrop` 失敗)驗證:「tab drag-and-drop 失敗」提示出現而視窗仍在。
5. 一般正常 session:啟動後不應彈出上述任一提示(無誤報)。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
rg -n "startup_warning_message|could not create its user interface|could not open the Shell view|could not enable tab drag-and-drop|destroy_explorers\(state\)|return -1" src/app_shell/main.cpp
```

> 注意 `rg ... destroy_explorers(state)`,應確認 `WM_CREATE` 內不再於 `apply_layout`／`register_tab_drag_hover_targets` 失敗時呼叫(它們以上 wWinMain 的 `window==nullptr` / 關閉路徑仍合法)。實機驗證應使用真實 `session.json`、不帶 `/F` 的關閉、以及真實 unreachable folder;不要以猜測路徑或強制終止代替。

## Handoff requirements

- 記錄 fatal(UI 建構)vs recoverable(pane／drag)的二分法與各自訊息。
- 記錄 `startup_warning_message` 的引入、它與 `startup_error_message`(window==nullptr)的分工,以及 deferred realize 為何要在 `window` 建立後才 `MessageBoxW`(避免 PD-126 的重入).
- 記錄 build／CTest／diff 結果與是否有「unreachable folder session 仍能開啟且給出提示」的驗證。
- 任何未驗證項目均須標明,不得以編譯通過代替根因。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-30 已實作 recoverable/fatal 二分與提示

- 根因:A) `WM_CREATE` 中 `apply_layout` 失敗→`destroy_explorers`＋`return -1`→整窗失敗(一個不可達 pane 讓 app 開不起來);B) `WM_CREATE` 其餘 `return -1`(sidebar/子控制/視窗子類/drag-drop)不設 `startup_error_message`→`CreateWindowExW==null` 分支訊息為空→靜默退出;C) `kDeferredRealizeMessage` 失敗僅 debug log→非 active pane 變空白無提示。
- 已修改 `src/app_shell/main.cpp`(僅此一檔):
  - `AppState` 新增 `std::wstring startup_warning_message;`(說明:可復原、視窗建立後顯示)。
  - `case WM_CREATE` 開頭設 `startup_error_message = L"PaneDock could not create its user interface.";`(涵蓋所有致命 `return -1`,消除 B)。
  - `apply_layout` 失敗(`:4295`-`4304`)改為寫 `startup_warning_message`,不再 `destroy_explorers`／`return -1`,視窗照開(消除 A)。
  - `register_tab_drag_hover_targets` 失敗(`:4317`-`4326`)改為 `revoke_drag_hover_targets`＋警告(空時才設),不再 `return -1`。
  - `kDeferredRealizeMessage`(`:4335`-`4346`)失敗時 `MessageBoxW(window, ...)`,且只在 `startup_warning_message.empty()`(避免 active pane 已警告時雙彈),消除 C。
  - `wWinMain` 於視窗建立後、unclean-shutdown 訊息後顯示 `startup_warning_message`(`MessageBoxW(window, ...)`).
- §9.4 順序、`destroy_explorers` 於關閉／window==null 路徑仍是合法呼叫、單一執行個體、無常駐 thread/timer 不變。
- 驗證:`cmake --build build` PASS;`ctest --test-dir build --output-on-failure` 6/6 PASS(完整與獨立 `panedock_launch_smoke` 重跑均通過;該測試有已知間歇 0xC0000409,與本次無關);`git diff --check` 通過。
- 待補實機驗證:unreachable folder 的 session 應開啟＋提示、正常 session 應無提示、`statidrag-drop` 失敗提示。
