# PD-181 — 把 error window 從 `ExplorerHost` 抽成獨立的 `PaneErrorOverlay`

Phase 7 · architecture · Depends on: PD-180

- Source: 同 PD-178（2026-09-03 使用者重構需求）。本票由該次重構掃描發現：`explorer_host.cpp` 裡藏著一整個與 host `IExplorerBrowser` 無關的第二 UI。
- Priority: MEDIUM——**誠實標註**：嚴格說本票超出使用者原始需求所列的 panes／groups／tabs 範圍，是相鄰發現。它可以獨立捨棄而不影響系列其餘部分。價值在於 `ExplorerHost` 的四個生命週期方法目前同時在處理兩件無關的事。

## Outcome

`ExplorerHost` 中的 error window（自有 window class、window proc、兩個子控制項、DPI 版面、retry 按鈕）抽成獨立的 `PaneErrorOverlay` 型別。`ExplorerHost` 只保留 host `IExplorerBrowser` 的職責，其 `set_rect` / `set_visible` / `focus` / `destroy` 不再夾雜 overlay 分支。

行為與視覺零變更：不可解析位置仍顯示同樣的錯誤面板與 retry 按鈕。

## 已確認的現況（2026-09-03 工作樹）

- `src/explorer_host/explorer_host.cpp` 共 1216 行，其中 error window 相關：
  - `kErrorWindowClassName[] = L"PaneDock.ErrorPanel"`（`explorer_host.cpp:22`）、`kRetryButtonId = 1`（`:23`）
  - `ExplorerHost::register_error_window_class()`（`:533-543`）
  - `ExplorerHost::error_window_proc()`（`:545-564`）
  - `ExplorerHost::layout_error_controls()`（`:566-591`）
  - `ExplorerHost::retry_navigation()`（`:593-600`）
  - `navigation_failed()` 中建立 overlay 的區塊（`:1097-1136`）
  - `WM_NCCREATE` prologue（`:546-551`），與 `main.cpp` 的四份同形
- `error_window_` / `error_message_` / `retry_button_` / `error_visible_` 在整個檔案中被引用 **40 次**。
- `src/explorer_host/explorer_host.h` 的 17 個成員中，有 4 個（`:117-122`、`:134-137`）**只為 error window 存在**。
- overlay 邏輯滲進四個生命週期方法：`set_rect`（`:929`）、`set_visible`（`:957`）、`focus`（`:983`）、`destroy`（`:1144`）。
- 抽出後 `PaneErrorOverlay` 對外只需要三樣東西：`rect_`、`parent_`、`location_.parsing_name`（錯誤訊息中顯示的位置字串）。retry 動作需要回到 `ExplorerHost::navigate`。
- `explorer_host` 依 `docs/testing.md` **沒有自動化測試**，本票的驗證完全依賴編譯、`launch_smoke` 與使用者實機檢查。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`：

> **App UI text must be English.** No Chinese strings ship in the binary.

`docs/design-spec.md` §9.4：關機順序

> 順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。

`CONTEXT.md`：

> **unresolvable location**: A Shell location that cannot currently be resolved — a disconnected network drive, a removed USB volume, a deleted folder. It produces a recoverable error state in the tab and never causes the saved configuration to be discarded.

`docs/tickets.md` §已否決的方向：

> 為 `IExplorerBrowser` 加抽象層以便 fake…只有一個真實實作的介面,買到的覆蓋率不對應真實風險。

**本票不重開上述否決方向。** 抽出的是一個純 Win32 的錯誤面板 UI，不是 `IExplorerBrowser` 的抽象層；`ExplorerHost` 與 COM 的關係一行不動。

`docs/testing.md`：

> `explorer_host`, `shell_core` and `file_operations` have no automated tests.

`docs/development.md`：

> Do not add a dependency, background loop, framework, or abstraction without a measured need.

## Files to read and trace first

- `src/explorer_host/explorer_host.h`：全部 17 個成員，標出哪 4 個屬 overlay。
- `src/explorer_host/explorer_host.cpp:22-23`、`:533-600`、`:1097-1136`：overlay 的全部程式碼。
- `src/explorer_host/explorer_host.cpp:920-1000`：`set_rect`、`set_visible`、`focus` 中的 overlay 分支。
- `src/explorer_host/explorer_host.cpp:1140-1160`：`destroy` 的順序。
- `src/explorer_host/explorer_host.cpp:1014-1090`：`navigation_complete`，確認成功導覽時如何隱藏 overlay。
- `src/app_shell/main.cpp:2812-2820`：`handle_navigation_failed`，app shell 端的錯誤路徑。
- `tests/release/address_bar_failure_check.ps1`：它斷言 `handle_navigation_failed` 的行為（PD-175）。
- `docs/tickets/PD-175-address-bar-reverts-to-stale-path-on-failed-navigation.md`：導覽失敗時位址列的既有語意，不得改變。

## Scope

1. 新增 `src/explorer_host/pane_error_overlay.h/.cpp`（同一個 `PaneDock` target），型別 `panedock::explorer_host::PaneErrorOverlay`：
   ```cpp
   class PaneErrorOverlay final {
   public:
       bool show(HWND parent, const RECT& rect, std::wstring_view location_text) noexcept;
       void hide() noexcept;
       void set_rect(const RECT& rect) noexcept;
       void destroy() noexcept;
       bool visible() const noexcept;
       bool focus() noexcept;
       bool retry_requested() const noexcept;   // 由 window proc 設旗標
       void clear_retry_request() noexcept;
   };
   ```
   **不持有 `ExplorerHost*` 反向指標、不持有回呼。** retry 按鈕只在 overlay 內部設一個旗標並通知 parent（`WM_COMMAND` 送到 parent HWND，或由 `ExplorerHost` 在既有的訊息路徑上查詢 `retry_requested()`）；由 `ExplorerHost` 決定何時呼叫自己的 `navigate`。**擇一實作，選 diff 較小者並在交接區說明。**
2. `ExplorerHost` 改為持有一個 `PaneErrorOverlay` 成員，刪除 `error_window_` / `error_message_` / `retry_button_` / `error_visible_` 四個成員與對應的四個方法。
3. `set_rect` / `set_visible` / `focus` / `destroy` 中的 overlay 分支改為單行委派。
4. **`destroy` 的順序必須維持**：overlay 是 pane container 的子視窗，其 destroy 不得早於 live `IExplorerBrowser` 的 `Destroy`，也不得讓 parent HWND 在 overlay 存活時被摧毀。順序在交接區逐行寫出。
5. error window class 的註冊改用 PD-180 產出的 `register_simple_window_class` helper（若簽章不合，說明理由並保留原註冊）。
6. UI 字串原樣搬移，不得翻譯、不得改寫。

## Non-goals

- 不改錯誤面板的文案、版面、字級、顏色或 retry 按鈕位置。
- 不改導覽失敗的判定條件或 `handle_navigation_failed` 的既有語意（PD-175）。
- 不改 `ExplorerHost` 與 COM 的任何互動、不改 site 契約、不改 `IServiceProvider` 回呼。
- 不為 `IExplorerBrowser` 加抽象層。
- 不拆 `explorer_host.cpp` 的其他三塊職責（COM 回呼 shim、reentrancy／navigation generation queue、context menu hosting）。若日後要拆，另開票。
- 不為 `explorer_host` 新增自動化測試（`docs/testing.md` 明訂的邊界）。

## Acceptance Criteria

1. `explorer_host.h` 中不再有 `error_window_` / `error_message_` / `retry_button_` / `error_visible_`。
2. `explorer_host.cpp` 行數下降 ≥ 150。
3. `ExplorerHost::set_rect` / `set_visible` / `focus` / `destroy` 中的 overlay 處理各自不超過 2 行。
4. `destroy` 順序在交接區逐行寫出，且與 §9.4 一致。
5. `ctest --test-dir build --output-on-failure` 全綠，含 `panedock_launch_smoke`。
6. 視覺與行為零變更。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 舊成員不得殘留
Select-String -Path src/explorer_host/explorer_host.h,src/explorer_host/explorer_host.cpp -Pattern 'error_window_|retry_button_|error_visible_|error_message_'
# overlay 不得回指 ExplorerHost
Select-String -Path src/explorer_host/pane_error_overlay.h -Pattern 'ExplorerHost'
# 不得洩漏中文字串
Select-String -Path src/explorer_host/pane_error_overlay.cpp -Pattern '[一-鿿]'
```

三條都必須無結果。

```powershell
# 關閉不殘留，且無 view-alive-parent-destroy 崩潰
$p = Start-Process build\PaneDock.exe -PassThru; Start-Sleep 3
$p.CloseMainWindow() | Out-Null
if (-not $p.WaitForExit(5000)) { throw 'process survived graceful close' }
```

```powershell
git diff --check
```

## 交給使用者的實機檢查清單

1. 製造不可解析位置：把某個 tab 導向一個隨即拔除的隨身碟，或一個已中斷的網路磁碟機。確認錯誤面板出現，文字與既有 build 相同。
2. 在錯誤面板顯示時：切換版型（1→2→3→4）、拖曳 splitter、調整視窗大小，確認面板跟著正確縮放與定位。
3. 恢復該位置（重新插入隨身碟／重新連線）後按 Retry，確認導覽成功且面板消失。
4. 在錯誤面板顯示時切換到別的 Group 再切回來，確認狀態正確。
5. 在錯誤面板顯示時直接關閉主視窗，確認乾淨結束、無殘留 process、無崩潰。
6. 四個 pane 同時處於錯誤狀態，再一次關閉，確認同上。

## Handoff requirements

在 `## 交接區` 記錄：`explorer_host.cpp` 前後行數、retry 通知採用哪一種做法及理由、`destroy` 的逐行順序、`register_simple_window_class` 是否適用、以及使用者實機檢查（特別是第 5、6 項）的回報結果。

## 交接區

### 2026-09-04 實作交接

#### 完成內容與行數

- 新增 `PaneErrorOverlay`，完整接手 error window class、window proc、STATIC/BUTTON 子控制項、原有英文文案、DPI 版面、顯示／隱藏、focus、resize、retry request 與 HWND cleanup。`ExplorerHost` 只保留一個 `error_overlay_` 成員。
- `src/explorer_host/explorer_host.cpp`：本票記錄的基準 1216 行降為 1066 行（-150，符合 AC）；因 PD-180 完成後工作樹在本票實作開始時已是 1192 行，所以相對本次 Git HEAD 的淨下降是 126 行。兩個數字都保留，避免把前票已搬走的 24 行算成本票實際 diff。
- `ExplorerHost::set_rect`、`set_visible`、`focus`、`destroy` 的 overlay 處理各為一行委派。
- 沒有新增 dependency、background loop、`IExplorerBrowser` abstraction 或 explorer-host 自動化測試；沿用既有 `panedock_explorer_host_lifetime_check` 與 release smoke。本票明訂 `explorer_host` 無自動化測試 seam，實機錯誤面板檢查仍保留給使用者。

#### Retry 通知

- 採用 Scope 的「既有訊息路徑查詢 `retry_requested()`」方案。按鈕的 window proc 只設 `retry_requested_ = true`，並 `PostMessageW(..., WM_NULL, ...)` 喚醒既有 message loop；`DispatchMessageW` 返回後，app shell 讓每個 `ExplorerHost::process_retry_request()` 查詢並清除旗標，再由 `ExplorerHost` 複製保留中的 `location_` 並呼叫自己的 `navigate()`。
- 這比 `WM_COMMAND` 路由 diff 小：不需要新增全域 command ID、不需要把 overlay HWND／pane container 反查成 pane index，也不需要改既有 `window_proc` command dispatcher。`PaneErrorOverlay` 不持有 `ExplorerHost*`、callback、`std::function` 或 pane index。

#### Destroy 順序（實作逐行順序）

`ExplorerHost::destroy()` 先解除會回到 host 的 Shell 連結，再依下列順序執行；overlay 在 live browser 的 `Destroy` 完成後才銷毀：

```text
remove_context_menu_subclass()
reset active context-menu COM pointers
Site::detach()
ViewCallback::detach()
IExplorerBrowser::Unadvise()
IUnknown_SetSite(browser, nullptr)
restore the previous IShellFolderViewCB
reset view_callback_, previous_view_callback_, current_view_
browser_->Destroy()
live_view_.reset()
error_overlay_.destroy()
reset events_, site_, browser_
parent_ = nullptr
```

外層 shutdown 順序維持：

```text
destroy_explorers(state)
assert(live_view_count() == 0)
destroy every PaneChrome (including each explorer-container parent HWND)
DestroyWindow(main window)
PostQuitMessage(0)
exit message loop
OleUninitialize()
```

因此 overlay 不早於 live `IExplorerBrowser::Destroy()`，其 pane-container parent HWND 也不會在 overlay 或 view 存活時被銷毀，符合 `docs/design-spec.md` §9.4。

#### Window class helper 與介面歧義

- PD-180 的 `register_simple_window_class` 簽章可直接使用；`pane_error_overlay.cpp` 也沿用其 `window_state_from_create` prologue。因四個 pane 可能重複要求同一 class，helper 補回原 error registration 的 `ERROR_CLASS_ALREADY_EXISTS` 成功語意。helper implementation 改由 `panedock_explorer_host` static target 編譯，讓 `PaneDock` 與獨立 lifetime-check executable 都能解析同一份實作，沒有複製 registration code。
- Scope 列出的 `PaneErrorOverlay` API 缺少 `set_visible(bool)`，但 Scope 3 同時要求 `ExplorerHost::set_visible` 單行委派，且既有行為必須在「pane 暫時隱藏」時保留 error-active 狀態。自行採最小一致解法：補一個 `set_visible(bool)`；`hide()` 仍專用於成功導覽後清除 error-active 狀態。若只把 `set_visible(false)` 映射成 `hide()`，再次顯示 pane 時會遺失既有 error overlay，違反零行為變更。

#### Agent checks

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：PASS（LLVM-MinGW Clang/LLD + Ninja）。
- `cmake --build build`：PASS。
- `ctest --test-dir build --output-on-failure`：PASS，20/20，含 `panedock_launch_smoke`。
- `build\panedock_explorer_host_lifetime_check.exe`：PASS；建立真實 `IExplorerBrowser`、觸發既有 unresolvable-location 路徑、呼叫 `ExplorerHost::destroy()`，並確認 live-view count 歸零且 parent HWND 不再有 child。此 executable 未註冊為 CTest，因此另行直接執行並如實分列。
- 舊成員掃描：PASS，0 matches。
- `pane_error_overlay.h` 的 `ExplorerHost` 反向指標掃描：PASS，0 matches。
- `pane_error_overlay.cpp` 中文字串掃描：PASS，0 matches。
- ticket 原始 graceful-close 指令：PASS，process 在 5 秒內退出，exit code 0。
- `git diff --check`：PASS。
- 受限 sandbox 內第一次完整 CTest 為 19/20，唯一 `panedock_launch_smoke` 因 `%LOCALAPPDATA%\PaneDock` 無寫入權而停在既有 save-failure modal；沒有把它當成程式 PASS。改在可寫桌面環境以相同 smoke 單獨重跑 PASS，再以相同環境重跑完整 CTest 得 20/20。

#### 使用者實機檢查

- 清單 1～4：未驗證，需使用者以真實不可解析 USB／網路磁碟與互動操作確認。
- 清單 5（error overlay 顯示時直接關閉）：未驗證，需真實桌面；自動 graceful-close 只驗證無 overlay 的關閉路徑，不能替代本項。
- 清單 6（四 pane 同時 error 後關閉）：未驗證，需真實桌面。
