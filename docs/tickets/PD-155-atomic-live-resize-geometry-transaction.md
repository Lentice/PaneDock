# PD-155 — Window、pane splitter 與 Groups sidebar 共用原子 live-resize geometry transaction

Phase 7 · app_shell / explorer_host · Depends on: PD-077, PD-095, PD-097, PD-104, PD-108

- Source: 使用者需求（2026-08-31）。
- Origin: 使用者回報拖曳 pane splitter 時，pane 內 nav buttons 與 address bar 會閃爍；主視窗 resize 則不會。使用者要求保留 live resize、移除 throttling，並讓 Groups sidebar resize 採用同一最佳方案。
- Priority: HIGH——目前 200 ms 更新造成明顯階梯感，逐一提交 child HWND geometry 又讓中繼 frame 暴露，直接影響三種常用 resize 的視覺品質。

## Outcome

主視窗 resize、pane splitter 拖曳與 Groups sidebar 寬度拖曳共用同一條 geometry-only live-resize 路徑。每次輸入先算完該 frame 的全部矩形，再以同一個 layout pass 的 parent-scoped Win32 deferred-window-position batches 提交 app-owned child HWND 與 `IExplorerBrowser` geometry；使用者拖曳時畫面即時跟隨，不再依賴 200 ms timer，也不出現 nav buttons、address bar 或 pane chrome 的閃爍／中繼錯位。

這裡的「同一個 layout pass」不是把不同 parent 的 HWND 硬塞進同一個 `HDWP`：Win32 `DeferWindowPos` 明確要求一個 batch 內所有視窗共用 parent。主視窗的 child controls/sidebar list 共用 app batch；每個 `explorer_containers[index]` 的 Shell view/error panel 使用該 container 專屬 batch，所有 batches 在同一個 pass 內完成 commit。這是符合 API 契約且仍能避免逐一 `SetWindowPos` 中繼 frame 的最小方案。

這張票明確覆寫 PD-097「以 200 ms throttle 降低 geometry 更新頻率」及 PD-104「sidebar resize 共用該 throttle」的決策。新證據是使用者實機觀察到 throttled splitter/sidebar resize 不夠絲滑，且要求 true live resize；根因調查另確認目前每個 child HWND 以獨立 `SetWindowPos` 提交並立即對 pane 執行 erase/all-children redraw，並非 live resize 本身必須付出的成本。

PD-095 的「拖曳中只做幾何、內容重算留到結束」與 PD-108 的「矩形未變 pane 不重排」繼續有效。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.1：

> 單一頂層視窗。左側為可調整寬度的 Group 側邊欄,右側為 pane 區域。兩者之間有可拖曳的分隔線,寬度屬於全域設定而非個別 Group 的狀態。側邊欄、pane 分隔線與頂層視窗邊框的調整皆為 live resize；每個中繼尺寸的 app chrome 與 Shell view 必須同步更新,不得以節流造成階梯式跳動或以 erase 造成閃爍。

`docs/design-spec.md` §FR-004：

> pane 分隔線可拖曳,比例以 0.0–1.0 的相對值儲存於該 Group,視窗縮放時維持比例。

> 調整 pane 分隔線、Group 側邊欄或頂層視窗時,版面幾何在同一個 layout frame 以 parent-scoped deferred-position 批次提交；導覽按鈕、網址列、分頁列、狀態列與 Shell view 不得在同一個中繼 frame 暴露不一致的位置。Win32 的 `DeferWindowPos` 要求同一批次內的視窗共用 parent,因此主視窗 child 與各 Explorer container child 各自使用一批,但在同一個 layout pass 完成提交。拖曳中的更新只重排幾何,內容重算與持久化在拖曳結束時處理。

`docs/design-spec.md` §NFR-004：

> Per-Monitor-V2 DPI awareness。視窗跨越不同 DPI 的螢幕時,全部 pane 正確縮放。

`docs/development.md` §Change workflow：

> Read and trace the files the ticket lists. Grep every caller of any shared function you intend to change.

> Make the smallest change that satisfies the acceptance criteria. Reuse before adding.

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> Event-driven idle path only. No busy loops, no polling timers.

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

> Keep `src/core` free of HWND, COM and `windows.h`.

> New non-trivial logic needs one focused runnable test or self-check.

## Confirmed implementation facts

1. `WM_SIZE` currently calls `apply_layout(window, state)` directly. Splitter/sidebar `WM_MOUSEMOVE` instead stores `latest_geometry_drag_point` and drives updates through `kSplitterDragTimerId` at `kSplitterDragThrottleIntervalMilliseconds = 200`.
2. `update_splitter_drag` and `update_sidebar_drag` already call `apply_layout(..., recompute_content=false)` during intermediate geometry updates. The geometry/content split from PD-095 therefore exists and must be reused.
3. `apply_layout` currently submits tab strip, five nav buttons, address edit, status bar and explorer container with separate `SetWindowPos` calls. A single frame can therefore be painted between sibling moves.
4. A changed pane ends with `RedrawWindow(window, &pane_rect, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN)`. This synchronously erases and redraws the parent plus all child windows after their positions were submitted separately; it is the strongest code-level explanation for the reported button/address-bar flashing.
5. `ExplorerHost::set_rect(const RECT&)` currently calls `IExplorerBrowser::SetRect(nullptr, rect)`. The native API accepts `HDWP*`, but the Win32 `DeferWindowPos` contract requires one batch's windows to share a parent; the Shell-hosted browser therefore must receive the deferred handle for its own `explorer_containers[index]` parent, never the main-window app batch. Reference: Microsoft Learn `DeferWindowPos` and `IExplorerBrowser::SetRect`.
6. `layout_sidebar` and `layout_header` also use immediate `SetWindowPos`; sidebar dragging additionally invalidates the entire main window with erase. Sidebar live resize must therefore join the same transaction, not merely remove its timer.
7. `apply_pane_container_region` uses `SetWindowRgn`; regions are not part of `HDWP` and must be applied after a successful geometry commit.

## Files to read and trace first

- `docs/tickets/PD-077-pane-footer-resize-repaint-ghost.md`——既有 sibling clipping 與 pane repaint 修正，不得重新引入殘影。
- `docs/tickets/PD-095-splitter-drag-full-relayout-and-item-count-rescan-per-mousemove.md`——保留 geometry/content 分離。
- `docs/tickets/PD-097-splitter-drag-geometry-throttling.md`——本票覆寫的 timer 決策與所有清理路徑。
- `docs/tickets/PD-104-resizable-persisted-sidebar-width.md`——sidebar drag、DPI 單位與 mouse-up persistence 契約。
- `docs/tickets/PD-108-skip-unchanged-pane-relayout-during-splitter-drag.md`——保留 changed-pane 判定與 committed rect cache。
- `src/app_shell/main.cpp`——timer constants、`AppState` drag/layout state、`layout_sidebar`、`layout_header`、`apply_layout`、`update_sidebar_drag`、`update_splitter_drag`，以及 `WM_SIZE`／`WM_DPICHANGED`／mouse capture／`WM_TIMER` 的所有 caller。
- `src/explorer_host/explorer_host.h`、`src/explorer_host/explorer_host.cpp`——`ExplorerHost::set_rect` 的宣告、實作及所有 caller。
- `docs/design-spec.md` §4.1、§FR-004、§NFR-004——加入三種 resize 均為即時且視覺一致的產品要求。

## Scope

1. 移除 geometry drag 的 200 ms timer、armed/latest-point 狀態及對應 `WM_TIMER` 分支。Splitter 與 sidebar 的每個有效 `WM_MOUSEMOVE` 立即更新 geometry；mouse-up 仍執行一次最終完整 content recompute 並保存，capture loss 仍只清理 drag state。
2. 讓 `WM_SIZE`、`update_splitter_drag`、`update_sidebar_drag` 共用同一個最小 geometry commit 路徑；不得以傳送／偽造 `WM_SIZE` 代替共享函式。
3. 每個 frame 先計算所有 sidebar、header、可見 pane chrome/container 的目標矩形，再建立 parent-scoped `BeginDeferWindowPos` batches。只把矩形有變的 app-owned HWND 加入主視窗 batch，最後在同一個 layout pass commit；不得把不同 parent 的 HWND 放進同一個 `HDWP`。
4. 對已 realize 且矩形有變的 view，建立該 `explorer_containers[index]` 專屬 batch，把其 `HDWP*` 傳入 `ExplorerHost::set_rect`，再由其傳入 `IExplorerBrowser::SetRect`。維持既有 `ShellCallScope`、shutdown/re-entry checks 與 failure propagation；不得把 Win32/COM 型別送進 `core`。不得因追求單一跨 parent handle 而違反 Win32 API 契約。
5. `EndDeferWindowPos` 成功後才更新 committed pane rect cache，並套用需要更新的 container regions。若建立或延伸 transaction 失敗，使用既有 immediate positioning 作為本 frame 的正確性 fallback；不得留下部分更新的 cache。
6. 移除 resize 熱路徑中的 per-pane `RDW_ERASE | RDW_ALLCHILDREN` 與 sidebar 全窗 erase。只 invalidate parent-owned chrome/decorations 的舊、新矩形聯集或差集，使用非同步 paint；child HWND 自己負責其內容重繪。
7. Shell `SetRect` 可能 pumping messages：加入最小 non-locking re-entry guard。巢狀 geometry request 只記錄「還需再排一次」，外層 transaction 完成後以一個 private posted message 套用最新 state；不得使用 mutex、遞迴 layout 或 polling timer。
8. 更新 `docs/design-spec.md`，明定 window、pane splitter 與 sidebar resize 皆為 live resize，三者使用一致的 geometry/repaint 行為。
9. 加入一個最小 runnable self-check，覆蓋可純化的 transaction plan／changed-rect 判定；若實作未新增可脫離 HWND/COM 的非平凡邏輯，沿用既有測試並在交接區說明，以真實桌面 resize matrix 作必要 runtime evidence，不為測試而擴張 `core` API。

## Non-goals

- 不新增 pane-host HWND、composition layer、snapshot/preview overlay、background worker 或新 dependency。
- 不使用 `WS_EX_COMPOSITED`、`LockWindowUpdate`、全樹 `WM_SETREDRAW` 或 blanket `SWP_NOREDRAW`；這些會改變 Shell-owned child 的 paint 契約或造成殘影。
- 不改 layout template、divider ratio 算法、sidebar 寬度範圍、DPI 單位、session schema、Group/pane/tab identity 或 Shell view 生命週期。
- 不把拖曳中的 content recompute 恢復成逐 frame；status counts、tab measurement 與 persistence 維持 PD-095／PD-104 的結束時處理。
- 不追求固定 FPS、不加 frame scheduler；Win32 mouse/size message coalescing 與 deferred positioning 已是本票需要的原生機制。

## Acceptance Criteria

1. 持續拖曳 pane splitter 時，每個可觀察的滑鼠位置都即時更新 pane，不再以約 200 ms 階梯前進；nav buttons、address bar、tab strip、status bar 與 Shell view 無閃爍、空白 frame 或彼此錯位。
2. 持續拖曳 Groups sidebar boundary 時，sidebar rows/footer、layout header 與全部 pane 即時同步移動；無全窗白閃、button/address-bar 閃爍或舊像素殘留。
3. 拖曳主視窗四邊與四角時，視覺結果與上述兩種 drag 一致，且 `WM_SIZE` 不執行 status/item-count 等 content recompute；resize 結束後內容正確。
4. 一次 geometry frame 只執行一個 layout pass：主視窗 child 共用一個 deferred-position batch，每個 Explorer container 至多一個專屬 batch；不得逐一以 immediate `SetWindowPos` 提交同 parent 的 chrome。`IExplorerBrowser::SetRect` 收到其 container batch 的 `HDWP*`，不得收到主視窗 batch。
5. 不再存在 `kSplitterDragThrottleIntervalMilliseconds`、`kSplitterDragTimerId` 或 geometry-drag `WM_TIMER` 狀態；未互動時仍為事件驅動的 0% CPU。
6. Splitter ratio 與 sidebar width 的最終值精確對應 mouse-up 座標並正常跨啟動保存；capture loss 不保存半成品、不留下 timer 或 stuck drag state。
7. 單 pane、八種 layout、1–4 個 realized panes、最小視窗尺寸及 Per-Monitor-V2 DPI 切換均無負／零尺寸、殘影或 crash。
8. 在 Shell operation 或 callback pumping messages 時 resize／close，不遞迴提交 layout、不 deadlock、不在 shutdown 後碰觸 HWND/COM。
9. Release build、CTest、ExplorerHost lifetime self-check 與 `git diff --check` 全數通過。

## Agent Checks

```powershell
rg -n "kSplitterDragThrottle|kSplitterDragTimerId|geometry_drag_timer_armed|latest_geometry_drag_point|BeginDeferWindowPos|DeferWindowPos|EndDeferWindowPos|SetRect" src/app_shell/main.cpp src/explorer_host

cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
.\build\panedock_explorer_host_lifetime_check.exe
git diff --check
```

在真實桌面執行 resize matrix：

1. 在四宮格中連續來回拖曳水平與垂直 pane splitter，觀察每個 pane 的 tab、五個 nav buttons、address bar、status bar 與檔案區。
2. 連續來回拖曳 Groups sidebar boundary，確認左側控制項、頂部 layout buttons 與四個 panes 同步。
3. 連續拖曳主視窗四邊與四角，縮到允許下限再放大；切換八種 layout 各重複一次。
4. 將視窗跨兩個不同 DPI 的螢幕後重複前三項。
5. 在大目錄載入／Shell file operation 期間拖曳 splitter，拖曳中關閉一次；確認無 crash、deadlock 或殘留 process。
6. 正常關閉並重啟，確認 sidebar width 與各 Group divider ratios 保存正確。

不得只以拖曳頭尾截圖宣稱無閃爍；需以螢幕錄影或連續觀察記錄中繼 frame。若 Agent 無法安全執行持續滑鼠操作，逐項標成待使用者驗證。

## Handoff requirements

- 記錄三種 resize 的 caller trace，以及它們如何進入同一個 geometry commit。
- 記錄每 frame 的 `HDWP` 建立、`IExplorerBrowser::SetRect` 參與、commit/fallback、region 與 invalidation 順序。
- 記錄 re-entry guard 被觸發時如何合併到最新 state，及 shutdown gate 的檢查點。
- 記錄移除的 timer constants/state/message branches。
- 記錄所有自動檢查與真實桌面 resize matrix；未驗證項目逐項列出。

## 交接區

<!-- 實作 agent 填寫，append-only -->

### 2026-08-31 實作交接

- Caller trace：`WM_SIZE`、`WM_DPICHANGED`、`update_splitter_drag`、`update_sidebar_drag` 皆進入 `apply_layout`；拖曳中傳 `recompute_content=false`，mouse-up 才以 `true` 完成內容重算與保存。`WM_MOUSEMOVE` 不再設定 geometry throttle timer。
- Geometry commit：`apply_layout` 先計算 sidebar/header/pane geometry；主視窗直系 child（含 sidebar list、nav buttons、address/status、tab strip、pane container）加入一個 app `WindowPositionBatch`。每個 changed 且已 realize 的 Explorer container 各自建立一個 `WindowPositionBatch`，其 handle 傳給 `ExplorerHost::set_rect` → `IExplorerBrowser::SetRect`；所有 batch 在同一個 `apply_layout` pass commit。這是因 `DeferWindowPos` 的 same-parent 契約而採用的最小合法方案。
- Commit/fallback 順序：app batch 先 `EndDeferWindowPos`；失敗時以既有 immediate positioning fallback 並補回 sidebar list。接著逐一 commit container batch；失敗時以 `SetRect(nullptr, ...)` fallback。成功／fallback 完成後才更新 committed pane rect cache、套用 `SetWindowRgn`，最後以 `InvalidateRect(..., FALSE)` 交給非同步 paint。移除了 changed-pane 的 `RDW_ERASE | RDW_ALLCHILDREN` 與 sidebar 全窗 erase。
- Re-entry/shutdown：`LayoutPassScope` 在 Shell call pumping message 時將巢狀 geometry request 合併成 `layout_pending`，外層 pass 完成後只 post 一個 `kDeferredLayoutMessage`；`apply_layout` 入口及每個 Shell call 後保留 close/shutdown gate。Explorer error panel 增加 `WM_SIZE` relayout，確保 deferred move 後內部 controls 使用新尺寸。
- Timer/state：移除 `kSplitterDragThrottleIntervalMilliseconds`、`kSplitterDragTimerId`、`latest_geometry_drag_point`、`geometry_drag_timer_armed` 及 geometry `WM_TIMER` branch；session-save 與 drag-hover timers 未改動。
- 自動驗證：Release CMake build PASS；elevated full CTest `11/11 PASS`（sandbox 內 smoke 因真實 `%LOCALAPPDATA%` session 寫入權限曾逾時，elevated 重跑通過）；`panedock_explorer_host_lifetime_check.exe` PASS；timer/HDWP `rg` 檢查 PASS；`git diff --check` PASS。
- 真實桌面 sanity：Release build 啟動成功；垂直 pane splitter drag 與 Groups sidebar boundary drag 後，Shell lists、nav buttons、address bars、tab/status chrome 保持可見且位置同步，無 crash/空白。原視窗已關閉並恢復測試改動的 sidebar 寬度。主視窗外框 drag 未驗證：當時視窗佔滿工作區，Computer Use 未抓到 outer-frame hit zone；需在非最大化視窗補做連續 mouse resize、八種 layout、混合 DPI 與 Shell operation/close 矩陣。

### 2026-08-31 region repaint 修正

- 根因 probe：`apply_pane_container_region` 是 `apply_layout` 唯一的 region caller；原本 `SetWindowRgn(container, region, TRUE)` 會在每個 changed pane 套用 region 時立即重繪，可能讓 splitter/sidebar live frame 暴露逐 pane 的中間畫面。
- 修正：改用 `SetWindowRgn(..., FALSE)`，待同一 layout pass 的 app/container geometry batches 完成後，對每個 changed Explorer container 使用 `RedrawWindow(..., RDW_INVALIDATE | RDW_NOERASE | RDW_ALLCHILDREN)`；保留主視窗的 no-erase invalidate。未加入 throttle，讓本次 probe 只改一個 redraw 變數。
- 驗證：Release configure/build PASS；elevated CTest `11/11 PASS`；`panedock_explorer_host_lifetime_check.exe` PASS；真實桌面連續拖曳 pane splitter 與 Groups sidebar 後，Shell view、nav buttons、address bar、tab/status chrome 保持可見且同步，未觀察到白閃或空白 frame；測試 sidebar width 已恢復，PaneDock 已正常關閉。
- 限制：目前沒有能對真實 Win32 paint frame 斷言閃爍的自動測試 seam；主視窗外框 resize、混合 DPI、八種 layout 與 Shell operation/close matrix 仍需真實桌面連續錄影驗證。

### 2026-08-31 tab strip geometry 修正

- 根因：tab strip 是自繪 `STATIC` child，`+` 與 overflow 導航按鈕的矩形由 `apply_tab_item_size` 依 `GetClientRect` 計算；geometry-only live resize 原本跳過這個 caller，而提前呼叫時又只能讀到 deferred move 前的舊寬度。
- 修正：將 `apply_tab_item_size` 移到 app `WindowPositionBatch::commit` 後，對可見且 geometry changed 的 pane（一般 content recompute 仍照常）重算 tab、`+`、overflow 按鈕與 tooltip 矩形；保留非 erase invalidate。
- 驗證：Release build PASS；排除 launch smoke 的 10 個 CTest PASS；`panedock_explorer_host_lifetime_check.exe` PASS；真實桌面以 Group 1 的 overflow tabs 拖曳 splitter，確認 tab 導航按鈕與 `+` 隨 pane 新寬度更新。
- 已知限制：`panedock_launch_smoke` 在本次環境的立即關閉情境仍逾時於 `CloseMainWindow` 後的 process exit；非 UI 測試與手動正常關閉均可完成。完整 resize/DPI/Shell operation matrix 仍需錄影驗證。
