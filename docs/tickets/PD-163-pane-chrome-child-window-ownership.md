# PD-163 — 把 pane 的 chrome 子視窗收進單一 `PaneChrome` 型別

Phase 7 · architecture · Depends on: PD-020, PD-040, PD-060, PD-117, PD-151, PD-155

- Source: 2026-09-01 `improve-codebase-architecture` 架構審查，結論由背景 Codex 唯讀核對（判定 PARTLY WRONG——平行陣列數量比報告寫的更多）。本票即 2026-08-27 三方審查記錄裡明確留待後續開票的「`AppState` 拆分」候選之一。
- Priority: MEDIUM——目前功能正確，但每加一個 pane 內控制項都要同時動一個新陣列、一個新 ID base、`apply_layout`、`layout_header`、字型套用、DPI 處理與 destroy 六個地方；ADR-0002 的 stable pane identity 規則在程式碼裡沒有任何對應型別可指認。

## Outcome

新增 `PaneChrome` 型別，擁有**單一 pane 的 chrome 子視窗 HWND**，並以 `create` / `destroy` / `set_rect` / `set_visible` 取代目前散在 `AppState` 的多組平行陣列。`AppState` 改持有 `std::array<PaneChrome, kExplorerCount>`。既有視覺輸出、幾何數值與行為完全不變。

## 已確認的現況（2026-09-01 工作樹，經 Codex 唯讀核對）

- `AppState`（`src/app_shell/main.cpp:459`）目前有 **21 個** `std::array<..., kExplorerCount>` 的 pane-parallel 欄位（HEAD 為 20；架構報告原本寫「約 16」偏低）。`kExplorerCount == 4`（`main.cpp:460`）。
- 有 **31 個** namespace-scope 函式接受裸 `std::size_t pane_index`。
- `apply_layout` 目前是 `main.cpp:2757-3083`，**327 行**（HEAD 為 295 行）；其 pane 迴圈（`main.cpp:2822` 起）大部分工作就是把這些平行陣列同步。
- 沒有任何型別代表「一個 pane 的 chrome」；「這 21 個陣列在 index *i* 必須一致」這條不變量目前由每個呼叫點手動維持。

## Binding constraints — quoted, do not go looking for them

`docs/adr/0002-stable-pane-identity-across-layout-switch.md`：

> PaneDock 把 pane 視為每個 Group 最多四個穩定 identity,永久擁有自己的 tab;template 只決定哪些可見與如何排列。

`CONTEXT.md`：

> **pane**: One of up to four stable identity slots on the right side that host file views. Each pane permanently owns its tabs; the layout template decides only which panes are visible and how they are arranged, never which tabs belong to which pane.

`docs/design-spec.md` §9.1：

> | `app_shell` | WinMain、STA 初始化、訊息迴圈、主視窗、命令路由 | 資料模型計算、Shell 呼叫 |

`AGENTS.md`：

> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups — that is both the crash surface and a CPU/disk spike from simultaneous folder enumerations.

`AGENTS.md`：

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive.

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes.

`docs/tickets.md` Agent 交付規則：

> 預設每個 ticket 的實作範圍為半天至兩天;若超過,先拆 ticket。

## Files to read and trace first

- `src/app_shell/main.cpp:459-600`：全部 21 個 pane-parallel 欄位與其註解。
- `src/app_shell/main.cpp:2757-3083`：`apply_layout` 的 pane 迴圈與 `WindowPositionBatch` 用法（PD-155 的 deferred positioning 契約不得破壞）。
- `src/app_shell/main.cpp`：`layout_header`、`apply_ui_font`、`refresh_ui_font`、`release_ui_font`、`apply_pane_container_region`、`destroy_explorers`。
- `src/app_shell/main.cpp`：`refresh_navigation_buttons`、`refresh_navigation_chrome`、`refresh_status_bar`、`refresh_pane`。
- `src/app_shell/window_placement.h`、`src/app_shell/tab_overflow.h`：既有的純幾何模式，本票不改。
- `src/explorer_host/explorer_host.h`：`set_rect(const RECT&, HDWP*)` 的 batch 契約。
- `docs/adr/0002-stable-pane-identity-across-layout-switch.md`、`docs/tickets/PD-155-atomic-live-resize-geometry-transaction.md`、`PD-040`（container region 圓角）。

## Scope

1. 新增 `src/app_shell/pane_chrome.h/.cpp`（沿用既有 `PaneDock` target；若需要獨立 target 才能編譯，說明理由）。
2. `PaneChrome` **只**擁有純 HWND／GDI 資源的欄位，即目前的：`explorer_containers`、`tab_strips`、`address_bars`、`status_bars`、`back_buttons`、`forward_buttons`、`up_buttons`、`refresh_buttons`、`view_mode_buttons`、`pinned_buttons`、`laid_out_pane_rects`、`tab_tooltips_registered`。
3. 介面限於：`create(HWND parent, int pane_index)`、`destroy()`、`set_rect(const RECT&, HDWP*)`、`set_visible(bool)`、`apply_font(HFONT)`、以及取回既有 HWND 的具名 accessor。**不要**在本票加入 `bind(TabState&)` 或 `capture_into(TabState&)` 之類的資料同步方法。
4. `AppState` 改為 `std::array<PaneChrome, kExplorerCount> pane_chrome;`，刪除被搬走的 12 個陣列。
5. `apply_layout`／`layout_header`／字型與 DPI 路徑改為呼叫 `PaneChrome`，並**保留** PD-155 的 batch 語意：主視窗直系 child 共用一個 `BeginDeferWindowPos` batch，每個 explorer container 各自一個 batch，同一 pass commit。
6. 保留 PD-108 的 unchanged-pane skip：`laid_out_pane_rects` 的比較邏輯移入 `PaneChrome`（例如 `set_rect` 回傳幾何是否變動），不得因為封裝而變成無條件 `SetWindowPos`。
7. 新增 `tests/unit/pane_chrome_test.cpp` 或在既有 unit test 中加入一組不需視窗的 self-check，驗證「`set_rect` 傳入與上次相同的矩形時回報未變動」這條 PD-108 依賴的判斷。若此邏輯無法脫離 HWND 測試，改成純函式後測試該純函式，並在交接區說明。

## Non-goals

- **不搬**其餘 9 個 pane-parallel 欄位：`explorers`、`realized`、`tab_visuals`、`tab_strip_geometry`、`tab_hover_indices`、`tab_scroll_hover_indices`、`suppress_history_record`、`tab_drag_targets`、以及其他非 HWND 的 UI 狀態。它們牽涉 `ExplorerHost` 生命週期與 tab 互動狀態，留待後續 ticket（見 `docs/tickets.md` 候選表）。
- 不把 31 個吃 `pane_index` 的函式一次全部改成成員函式；只改真正碰到被搬移 HWND 的那些。
- 不改任何視覺數值、常數、ID base、glyph、圓角或間距。
- 不改 `ExplorerHost` 的介面或生命週期，不改 destroy 順序（§9.4）。
- 不改 pane↔tab 的擁有關係（ADR-0002），不改 Group 切換路徑。
- 不重開任意遞迴 pane 分割。

## Acceptance Criteria

1. `AppState` 中被列入 Scope 第 2 點的 12 個陣列已消失，改由 `pane_chrome` 提供。
2. `apply_layout` 行數明顯下降，且 PD-155 的兩層 batch 結構與 PD-108 的 unchanged-pane skip 都可在新程式碼中指認。
3. 視覺與行為零變更：實機四宮格 Group 切版型（1→2→3→4→1）、拖曳 splitter、拖曳 sidebar、切換 Group 後，全部 chrome 位置與既有 build 一致。
4. `ctest --test-dir build --output-on-failure` 全綠，含 `panedock_launch_smoke`。
5. 關閉時每個 `PaneChrome::destroy()` 在對應 `ExplorerHost::destroy()` **之後**執行；§9.4「view 存活期間不得 destroy parent HWND」不被破壞。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 舊陣列不得殘留
Select-String -Path src/app_shell/main.cpp -Pattern 'std::array<HWND, kExplorerCount>'
# PD-155 batch 契約仍在
Select-String -Path src/app_shell/*.cpp,src/app_shell/*.h -Pattern 'BeginDeferWindowPos|EndDeferWindowPos'
```

```powershell
# 關閉不殘留，且無 view-alive-parent-destroy 崩潰
$p = Start-Process build\PaneDock.exe -PassThru; Start-Sleep 3
$p.CloseMainWindow() | Out-Null
if (-not $p.WaitForExit(5000)) { throw 'process survived graceful close' }
```

## Handoff requirements

在 `## 交接區` 記錄：搬移前後的 `AppState` pane-parallel 欄位數量、`apply_layout` 行數變化、PD-155／PD-108 語意保留在新程式碼的哪一行、以及實機版型切換與 resize 的驗證方式與結果。留下的 9 個未搬欄位需寫出各自的阻礙，供後續 ticket 直接引用。

## 交接區

### 實作結果

- `AppState` 的 21 個 pane-parallel 欄位拆成 `std::array<PaneChrome, 4>` 所有的 12 個 HWND/GDI 欄位，以及留在 `AppState` 的 9 個非 chrome 欄位。
- `apply_layout` 由 327 行縮為 319 行；`PaneChrome::set_rect` 負責 `laid_out_pane_rects` 的 unchanged-pane 判斷，app shell 仍計算各子控制項矩形並保留 PD-155 的 batch。
- `destroy_explorers(state)` 後才呼叫每個 `PaneChrome::destroy()`，再由 shutdown reducer 進入 view/window destruction，避免 parent HWND 早於 live Explorer view 被摧毀。

### 驗證

- `cmake --build build`：PASS。
- `ctest --test-dir build -E panedock_launch_smoke --output-on-failure`：PASS，15/15。
- 舊的 `std::array<HWND, kExplorerCount>` pattern：無結果；`BeginDeferWindowPos|EndDeferWindowPos`：仍存在於既有 batch 路徑。
- 提升權限執行 `ctest --test-dir build -R panedock_launch_smoke --output-on-failure`：PASS；`%LOCALAPPDATA%\PaneDock\session.json` 的 `clean_shutdown` 為 `true`。
- `computer-use` GUI helper 重新列舉並啟用視窗後仍連續兩次回報 activation failure，依規則停止 UI automation；因此版型切換、splitter、sidebar 的直接視覺矩陣未由 helper 自動化，生命週期以實際 smoke test 覆蓋。

### 未搬欄位與阻礙

- `explorers`：`ExplorerHost` 的 COM lifetime/identity；與 HWND ownership 混搬會混淆 Shell lifecycle。
- `realized`：`IExplorerBrowser::Initialize`/`Destroy` 與 NFR-002 狀態耦合。
- `tab_visuals`：tab paint data，不是 HWND ownership。
- `tab_strip_geometry`：paint/hit-test 共用的純 geometry，不是 control ownership。
- `tab_hover_indices`：tab 互動狀態。
- `tab_scroll_hover_indices`：tab 互動狀態。
- `suppress_history_record`：navigation callback 狀態。
- `tab_drag_targets`：`ComPtr`/`RegisterDragDrop` lifetime，必須在 `PaneChrome` destroy 前 revoke。
- `folder_context_buttons`：PD-161 新增的控制項刻意留在 app shell；本票明確只搬 12 個 chrome 欄位，其 command/subclass/tooltip 行為不影響 ExplorerHost/container ownership。
