# PD-161 — pane footer 新增目前資料夾背景選單按鈕

Phase 7 · app_shell / explorer_host · Depends on: PD-060, PD-083, PD-117, PD-159

- Source: 使用者於 2026-09-01 提出：pane 太小時 Shell view 可能沒有可供右鍵點擊的空白處，需要一個按鈕等同在空白處按右鍵。
- Follow-up: 使用者明確要求每個按鈕都必須有 on-hover style 與 tooltip；本票新增的按鈕不得例外。
- Priority: MEDIUM——不影響既有 Shell 操作，但直接改善小 pane 下原生 folder background menu 難以觸發的可用性。

## Outcome

每個可見 pane 的 footer 最右側有一顆可鍵盤聚焦的按鈕。按下後，該 pane 成為 active pane、既有項目選取被清除，並在按鈕上方開啟目前 active tab 的原生 Windows Shell folder background context menu；行為等同在該 Shell view 空白處按右鍵。按鈕具有與既有 pane chrome 一致、肉眼可辨的 hover style，以及英文 tooltip `Folder context menu`。

## 已確認的產品與程式碼事實

1. `ExplorerHost::show_background_context_menu(HWND, LPARAM)` 已在 `src/explorer_host/explorer_host.cpp` 建立 PD-159 的共享原生 menu bridge：它使用 `IShellView::GetItemObject(SVGIO_BACKGROUND, IID_IContextMenu)`、轉發 `IContextMenu2/3` menu messages，並以該 host 的 `location_` invoke installed Shell verbs。不得另做第二份 context-menu 實作。
2. 現有入口只由 Shell view subclass 的 `WM_CONTEXTMENU` 呼叫，而且有選取項目時會返回 `false`。按鈕不會自然產生「點空白處先取消選取」的 Shell 行為，因此按鈕路徑必須先清除選取，才能符合本票的等同行為。
3. Microsoft `IShellView::SelectItem`／`SVSI_DESELECTOTHERS` 契約允許以 null item 搭配 `SVSI_DESELECTOTHERS` 清除全部選取。這項 Shell 呼叫應留在 `explorer_host`，不可移入 `app_shell` 或 `core`。
4. pane 導覽列已有 Back、Forward、Up、Refresh、View、Pinned locations 六顆按鈕；再加一顆會壓縮小 pane 的 address bar。pane footer 已存在且最右側沒有 action，因此本票選 footer 右端，不選 nav row。
5. `app_shell` 已有 owner-draw button hover tracking（PD-083）與共用 Common Controls tooltip window（PD-117）。本票直接重用兩者，不新增 hover timer、tooltip window、UI framework或自訂 tooltip。
6. 永久「空白專區」會持續占用 Shell view 面積，反而惡化小 pane 問題，因此不採用。

## Scope override

PD-064 移除的是沒有功能的全域 more-actions `...` 佔位按鈕。本票不恢復該 placeholder；新增的是每個 pane footer 內、具體呼叫目前 tab 原生 folder background menu 的功能按鈕。新需求與 PD-159 已完成的原生 background-menu bridge 是重新加入可見 menu affordance 的實證依據。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §2.4：
> **不重刻檔案清單。** 檔案檢視一律承載原生 Shell view,原生行為由 Windows 提供。

`docs/design-spec.md` §4.8：
> pane 內部的一切互動由 Shell view 處理:多選手勢、右鍵選單、拖放、就地重新命名、鍵盤操作。PaneDock 不介入。

`docs/design-spec.md` §FR-006：
> 每個已 realize 的 tab 透過 `IExplorerBrowser` 承載 Shell view,提供原生圖示與縮圖、原生右鍵選單與已安裝的 shell extension、多選、就地重新命名。

`docs/development.md` Architecture rules：
> `app_shell` | WinMain, STA init, message loop, main window, command routing to the active pane | Model computation, Shell calls, persistence format

`docs/development.md` Architecture rules：
> `explorer_host` | `IExplorerBrowser` instances, site objects, view lifetime, browser events | Product decisions, persistence, layout math

`docs/development.md` Change workflow：
> Make the smallest change that satisfies the acceptance criteria. Reuse before adding.

`AGENTS.md`：
> Keep `src/core` free of HWND, COM and `windows.h`.

`AGENTS.md`：
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`：
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

`AGENTS.md`：
> App UI text must be English. No Chinese strings ship in the binary.

## Files to read and trace first

- `docs/design-spec.md` §2.4、§4.8、§FR-006、§9.1–9.4、§12.3–12.5——原生 Shell 行為、module ownership 與真實桌面驗證邊界。
- `docs/development.md` Architecture rules、COM lifetime rules、Change workflow。
- `docs/tickets/PD-060-pane-status-bar-selection-size-and-separator.md`——footer 現有繪製、分隔與資訊 layout 契約。
- `docs/tickets/PD-069-chrome-spacing-scale.md`——footer padding 與 chrome 間距尺度。
- `docs/tickets/PD-083-remaining-buttons-missing-or-weak-hover.md`——owner-draw button hover 的既有機制與可辨識對比標準。
- `docs/tickets/PD-117-pane-button-tooltips.md`——共用 `layout_tooltip`、`TTF_IDISHWND | TTF_SUBCLASS` 的既有做法。
- `docs/tickets/PD-159-background-shell-verb-current-folder.md`——原生 background `IContextMenu` bridge、installed extensions、location identity、menu re-entry 與 close-while-menu 驗證。
- `src/explorer_host/explorer_host.h`——`show_background_context_menu` 的 visibility、ShellCallScope 與 lifetime state。
- `src/explorer_host/explorer_host.cpp`——完整讀取 `show_background_context_menu`、`handle_context_menu_message`、subclass install/remove、navigation complete 與 `destroy`，追蹤所有 caller 後再改 shared seam。
- `src/app_shell/main.cpp`——完整追蹤 `AppState` 的 pane HWND arrays、button IDs、`owner_draw_button_proc`、`draw_navigation_icon_button`、tooltip registration、`draw_status_bar`、`apply_layout`、`WM_DRAWITEM`、`WM_COMMAND`、active-pane 切換與 shutdown gate。
- `tests/release/shell_reentry_gate_check.ps1`、`tests/unit/explorer_host_lifetime_check.cpp`——現有 Shell re-entry 與 lifetime 靜態檢查。

## Scope

1. 在 `AppState` 增加每 pane 一個 footer context-menu button HWND，配置獨立 command-ID base；以現有 `BUTTON | BS_OWNERDRAW | WS_TABSTOP`、`owner_draw_button_proc` 與 pane icon style 建立，不新增自訂 control class。
2. 按鈕固定在各 pane footer 最右側；`apply_layout` 與 `draw_status_bar` 必須為按鈕保留 DPI-scaled rect，狹窄時 status text 可裁切，但按鈕不得與文字重疊、超出 pane 或變成零尺寸。hidden pane 的按鈕一起隱藏。
3. 按鈕採能表達 menu 的簡潔 glyph；其 normal、hover、pressed、keyboard-focus、disabled 狀態沿用既有 pane chrome 視覺。hover 必須肉眼可辨，disabled 時不得套用 hover；只在 hover target 改變時 invalidate，不新增 timer 或 polling。
4. 將 tooltip `Folder context menu` 以既有 `layout_tooltip` 及 `TTF_IDISHWND | TTF_SUBCLASS` 註冊到四顆 footer button，不新增 tooltip window。
5. `WM_COMMAND` 由 command ID 算出實際 pane index；確認該 pane 可見且 realized，將它設為 active pane，再以按鈕最新 screen rect 的左上方／上緣作 popup anchor。不得把命令一律送到先前的 active pane。
6. 在 `ExplorerHost` 提供一個窄的 public entry point（建議 `show_folder_context_menu(HWND owner, POINT screen_point) noexcept`），專供外部 chrome 觸發目前 host 的 folder background menu。它在既有 `ShellCallScope` 內呼叫 `current_view_->SelectItem(nullptr, SVSI_DESELECTOTHERS)` 清除選取，再收斂到 PD-159 現有 menu construction／message forwarding／invoke 路徑；既有 Shell view `WM_CONTEXTMENU` caller 行為不變，不公開 COM pointer 或 selection-policy flag。
7. 清除選取、menu 建立或 invoke 失敗時沿用現有 HRESULT logging 與安全返回；不得 fallback 到 `ShellExecute`、synthetic mouse message 或自製 verbs。menu 開啟期間關閉程式仍必須遵守既有 deferred shutdown／Destroy 順序。
8. 更新 `docs/design-spec.md`，明確記錄 pane footer 提供原生 folder background menu affordance，並說明這是對 §4.8 的有限 host-level 入口，不改由 PaneDock 重建 menu。

## Non-goals

- 不在 nav row 增加第七顆按鈕，不新增永久空白專區。
- 不重新設計或稽核既有全部按鈕；PD-083／PD-117 已處理既有 hover 與 tooltip，本票只要求新增按鈕完整遵守相同規則。
- 不自製、過濾、排序或包裝 Shell menu，不新增 PaneDock-specific verb，不讀 registry，不使用 `ShellExecute`。
- 不以 `SendMessage(WM_CONTEXTMENU)`、假座標或合成滑鼠右鍵繞過既有 `ExplorerHost` seam。
- 不改 item context menu、Shift+F10、Shell view 真實空白處右鍵或既有 installed shell extensions 行為。
- 不修改 `src/core`、session schema、Group/tab/location persistence、file operations 或 Shell view realize/destroy policy。
- 不新增 dependency、thread、timer、polling、動畫、設定選項或可自訂按鈕位置。

## Acceptance criteria

1. 每個可見 pane 的 footer 最右側都有一顆 folder context-menu button；一至四 pane 與所有 layout template 下不重疊、不越界，96/144/192 DPI 與窄 pane 下仍可點擊。
2. 新按鈕的 hover 背景肉眼可辨，移出後恢復；pressed、keyboard focus 與 disabled 狀態正確，滑鼠靜止時不持續 invalidate，idle CPU/I/O 不受影響。
3. 每顆新按鈕 hover 都顯示英文 tooltip `Folder context menu`；DPI、resize、Group/layout 切換後仍綁定正確 HWND。
4. 點擊或以 Tab 聚焦後按 Space/Enter，該按鈕所屬 pane 成為 active pane，menu 在該按鈕上方出現；即使先前另一個 pane active，也不得開錯 pane/location。
5. 有選取項目時按下按鈕會先清除選取，再顯示 folder background menu；選單內容不是 item context menu，footer 的 selection count 同步更新。
6. 無選取時按下按鈕，menu 與同一 Shell view 空白處右鍵的原生背景選單一致，保留 installed shell extensions。`Open with Code`、`Open Git Bash here` 等 current-folder verbs 使用該 pane/tab 的正確 location。
7. 真實空白處右鍵與檔案項目右鍵行為不變；另一 pane、tab 或 Group 的 location 不會被誤用。
8. menu 保持開啟時關閉 PaneDock，不 crash、不 deadlock、不殘留 process；所有 initialized `IExplorerBrowser` 仍先 `Destroy()`。
9. 沒有 synthetic input、`ShellExecute`、registry verb 特例、新 dependency、timer/polling，且 `src/core` 無 HWND/COM/`windows.h` leakage。
10. `cmake --build build`、`ctest --test-dir build --output-on-failure`、boundary checks 與 `git diff --check` 全數通過。

這是 app_shell HWND 與 live Shell COM integration，沒有可信的 `core` 自動測試 seam。focused runnable check 是真實 Release build 的按鈕 hover/tooltip、selection-clear、多 pane/location、installed verb 與 close-while-menu 矩陣；不得以 source grep 或 mock Shell view 取代。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "show_background_context_menu|show_folder_context_menu|SVGIO_BACKGROUND|IContextMenu|SelectItem|SVSI_DESELECTOTHERS|ShellCallScope|context_menu_active|destroy" src\explorer_host src\app_shell\main.cpp
rg -n "status_bars|owner_draw_button_proc|owner_draw_hovered_button|TTM_ADDTOOLW|TTF_IDISHWND|Folder context menu|WM_COMMAND|WM_DRAWITEM" src\app_shell\main.cpp
powershell -NoProfile -ExecutionPolicy Bypass -File tests\release\shell_reentry_gate_check.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests\release\shell_core_boundary_check.ps1
git diff --check
```

```powershell
.\build\PaneDock.exe
# 真實桌面：逐一驗證 1–4 pane、至少兩個不同 location、有／無 item selection、
# hover、tooltip、鍵盤 Space/Enter、installed current-folder verbs，以及 close-while-menu。
```

## Handoff requirements

- 記錄最終 button ID、HWND ownership、glyph、tooltip、normal/hover/pressed/focus/disabled 視覺值及 DPI-scaled rect。
- 記錄 `app_shell` command 如何定位 pane，以及 `ExplorerHost` public entry point 與既有 `WM_CONTEXTMENU` caller 如何收斂到同一份 PD-159 menu seam。
- 記錄 selection-clear 使用的 Shell API、HRESULT 與 status-bar selection count 更新結果。
- 分別記錄兩個 location、有／無 selection、真實空白處右鍵、item 右鍵、installed verbs、四 pane 與 close-while-menu 結果。
- 如實區分自動檢查、單次 GUI 截圖與多步驟人工驗證；未驗證項目不得宣稱 PASS。
- 確認沒有新增 timer/polling、synthetic input、`ShellExecute`、registry 特例、dependency 或 `src/core` Win32/COM leakage。

## 交接區

<!-- 實作 agent 填寫，append-only -->

### 2026-09-01 實作交接

- 每個 pane 的 footer button 使用 command ID `790 + pane_index`（`790–793`），HWND 存在 `AppState::folder_context_buttons`，由主視窗擁有；建立樣式為 `BUTTON | BS_PUSHBUTTON | BS_OWNERDRAW | WS_TABSTOP`，沿用 `owner_draw_button_proc` 的 hover tracking。glyph 使用 `Segoe MDL2 Assets` `U+E712`（More），字型失敗時以三個小方點 fallback 繪製。
- 視覺沿用 pane navigation chrome：normal `RGB(255,255,255)`、hover `RGB(242,245,248)`、pressed `RGB(226,232,240)`、glyph `RGB(90,102,122)`、disabled glyph `RGB(190,197,209)`；keyboard focus 使用既有 `DrawFocusRect`，disabled 不採用 hover/pressed fill。tooltip 為英文 `Folder context menu`，透過既有 `layout_tooltip` 與 `TTF_IDISHWND | TTF_SUBCLASS` 註冊。
- footer 96-DPI rect 使用 status bar 的 `24px` 高度作 button 寬度；一般情況右側保留 `kSpaceTight = 4px`、button 與 status text 之間保留 `4px`，所有值經 `scaled_value`（144/192 DPI 分別按比例縮放）。pane 過窄時 button 寬度 clamp 到 pane 寬度，status rect 退讓到不重疊；hidden pane 同步隱藏並停用 button。
- `WM_COMMAND` 從 `790 + pane_index` 還原實際 pane，確認 active Group、pane 可見、realized 且 button visible，呼叫既有 `set_active_pane` 並再次確認 active pane；以最新 `GetWindowRect` 的 `{left, top}` 作 anchor，交給 `ExplorerHost` 以 `TPM_BOTTOMALIGN` 開在 button 上緣。
- `ExplorerHost::show_folder_context_menu(HWND, POINT)` 是新的窄 public entry point。它與既有 Shell view `WM_CONTEXTMENU` caller 共用 `show_folder_context_menu_at` 的 PD-159 menu construction、`IContextMenu2/3` message forwarding、location-aware `InvokeCommand` 與 deferred Shell-call gate；button path 只額外先呼叫 `IShellView::SelectItem(nullptr, SVSI_DESELECTOTHERS)`，失敗時記錄 HRESULT 並安全返回。button path 的 popup owner 仍使用目前 Shell view subclass HWND，因此 installed menu extension 的 `WM_INITMENUPOPUP`／draw／measure forwarding 不會繞開既有 seam；`CMINVOKECOMMANDINFOEX::hwnd` 保留主視窗 owner。
- selection-clear 的成功 HRESULT 會繼續建立 background menu；既有 `SFVM_SELECTIONCHANGED` callback 會 reset item-count cache 並觸發 status-bar refresh。未執行真實 selection GUI，因此本次未取得實測 HRESULT、selection count 截圖或 installed verb 啟動結果。

#### 驗證結果

- PASS：指定 LLVM-MinGW/Ninja Release configure、`cmake --build build`、`ctest` 前 12 個 unit/release checks、`shell_reentry_gate_check.ps1`、`shell_core_boundary_check.ps1`、票據指定的 `rg` 檢查與 scoped/global `git diff --check`。
- 未完成：`panedock_launch_smoke` 在 30.75 秒後回報 `PaneDock did not exit after its main window was closed`。檢查當時 `%LOCALAPPDATA%\PaneDock\session.json` 為 `clean_shutdown:false`；啟動會出現既有 unclean-shutdown modal warning，而 smoke script 未處理該 dialog，故不能把這次結果當作 PD-161 的 close-while-menu 證據，也未宣稱整份 CTest PASS。
- 未驗證：96/144/192 DPI 與所有 layout 的實機幾何截圖、hover/tooltip、Space/Enter、兩個 location、有／無 selection、真實空白處右鍵、item 右鍵、installed current-folder verbs、四 pane close-while-menu 與 idle CPU/I/O。沒有使用 synthetic input 或以 mock Shell view 取代這些驗證。
- 確認：沒有新增 timer/polling、thread、dependency、`ShellExecute`、registry verb 特例；`src/core` 未修改且沒有 HWND/COM/`windows.h` leakage。

### 2026-09-01 視覺 follow-up

- footer button 改為與 status bar 融合的視覺透明樣式：status rect 延伸到 button 左緣，button normal/keyboard-pressed 狀態使用 `kStatusBarBackground`，只有 hover 使用既有淺灰背景；其他 navigation buttons 的 normal/pressed 視覺不變。這是使用相同 footer 色的 owner-draw 實作，不依賴 `WS_EX_TRANSPARENT`。
- 依使用者後續視覺決策，folder button 改比照 pane `+ New tab`：normal 不畫框線，hover 才以 `RoundRect` 顯示 `RGB(236,240,244)` 填色與 `RGB(226,232,240)` 細框；active glyph 使用相同的深色 `RGB(31,41,55)`，鍵盤 focus 仍保留既有 `DrawFocusRect`。共用的 tab-add 色票也套用到既有 `+` 繪製路徑，避免兩顆按鈕樣式漂移。

### 2026-09-01 focus follow-up

- folder button 的 `WM_COMMAND` 在建立 menu 前呼叫既有 `ExplorerHost::focus()`，把 focus 交回該 pane 的原生 Shell view；因此滑鼠點擊後不會留下 owner-draw `DrawFocusRect` 虛線，按 Tab 聚焦按鈕時仍保留 keyboard focus indicator。

### 2026-09-01 right-click follow-up

- `owner_draw_button_proc` 僅對 folder button（`790–793`）攔截 `WM_RBUTTONUP`，將右鍵啟用轉換成既有的 `WM_COMMAND`／`BN_CLICKED` 路徑；因此左鍵、右鍵與 Tab/Space/Enter 都共用同一個 active-pane、selection-clear 與原生 Shell menu flow。右鍵訊息由 subclass 消費，不會再落入按鈕的預設處理。

### 2026-09-01 footer-boundary follow-up

- folder button 改為 footer 內縮的方形矩形；96 DPI 約為 `18×18`（原本為整個 `24px` footer 高度），上／下各保留 `kTabAddButtonVerticalInset`、右側保留 `kSpaceTight`。status bar 恢復覆蓋完整 footer，按鈕位於其上方，讓上緣 separator 與 pane card 外框不再被 hover/normal fill 蓋住；status text 同步保留 button reserve。

### 2026-09-01 z-order follow-up

- status bar 覆蓋完整 footer 後，layout pass 以 `SetWindowPos(..., HWND_TOP, ..., SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)` 明確將 folder button 放回 sibling 上層；如此 separator 仍由 status bar 保留，button icon 與 hover 繪製不會被覆蓋。

### 2026-09-01 button-size follow-up

- 依使用者指定調整 footer button 邊界：96 DPI 約由 `18×18` 放大為 `22×22`；上緣上移 `1px`、下緣下移 `3px`（夾在 footer 內）、左緣向左展開 `4px`，右緣維持原位置。status text reserve 同步增加 button 寬度，`...` glyph 以最終 owner-draw `rcItem` 的水平與垂直中心繪製。
