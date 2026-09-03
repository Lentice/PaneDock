# PD-182 — `PaneChrome` 升級為 `Pane`，接手六個純 UI 的 pane-parallel 欄位

Phase 7 · architecture · Depends on: PD-163, PD-178, PD-179

- Source: 同 PD-178（2026-09-03 使用者重構需求）。本票即 `docs/tickets.md` §候選 早已登記的「把剩餘 9 個 pane-parallel 陣列搬進 `PaneChrome`」的**前半**；其觸發條件（PD-163 完成且交接區已寫出每個未搬欄位的具體阻礙）已於 PD-163 成立。opencode 獨立審查指出原本「9 個一起搬」會讓 `Pane` 握有 Shell view 生命週期、與本系列自訂的所有權界線矛盾，因此拆成本票（純 UI）與 PD-183（Shell 生命週期）。
- Priority: HIGH——這是整個系列裡真正降低耦合的第一張。

## Outcome

`PaneChrome` 更名為 `Pane`，接手 `AppState` 中六個**純 UI**的 pane-parallel 欄位。`Pane` 擁有自己的 HWND、幾何、hover 與 drag 視覺狀態，對外只提供純查詢與純命令；它不認識 `AppState`、不認識 session、不認識 `core::GroupState`。

行為與視覺零變更，**特別是 PD-110 的跨 pane tab 拖曳**。

## 已確認的現況（2026-09-03 工作樹）

- `AppState`（`src/app_shell/main.cpp:501-629`）仍有 9 個 pane-parallel 欄位，PD-163 交接區已逐個寫出阻礙：

  | 欄位 | 行 | PD-163 記錄的阻礙 | 本票 |
  |---|---|---|---|
  | `tab_visuals` | 599 | tab paint data，不是 HWND ownership | **搬** |
  | `tab_strip_geometry` | 603 | paint/hit-test 共用的純 geometry | **搬** |
  | `tab_hover_indices` | 607 | tab 互動狀態 | **搬** |
  | `tab_scroll_hover_indices` | 609 | tab 互動狀態 | **搬** |
  | `tab_drag_targets` | 627 | `ComPtr`/`RegisterDragDrop` lifetime，必須在 destroy 前 revoke | **搬**（見下方理由） |
  | `folder_context_buttons` | 621 | PD-161 新增，刻意留在 app shell | **搬** |
  | `explorers` | 529 | `ExplorerHost` 的 COM lifetime | 留待 PD-183 |
  | `realized` | 530 | `Initialize`/`Destroy` 與 NFR-002 耦合 | 留待 PD-183 |
  | `suppress_history_record` | 619 | navigation callback 狀態 | 留待 PD-183 |

  `tab_drag_targets` 搬進本票的理由：它是註冊在**該 pane 自己的 tab strip HWND** 上的 `IDropTarget`，必須在那個 HWND 被摧毀之前 `RevokeDragDrop`。這是 **HWND 生命週期**問題，與 HWND 的擁有者同住才正確；它不是跨 pane 的拖曳**狀態**（那條界線見下方 Scope 第 5 點）。目前的 revoke 在 `main.cpp:700-710` 的 `revoke_drag_hover_targets`。

- 跨 pane tab 拖曳（PD-110）目前：狀態在 `AppState::TabDrag`（`main.cpp:565-575`）；`update_tab_drag`（`main.cpp:4042-4101`）把座標轉成螢幕座標後，在 `4078-4089` 逐 pane 對 `tab_strip()` 做 hit-test；`finish_tab_drag`（`3983-4040`）呼叫 `core::reorder_tab` 或 `core::move_tab`。**這段是 parent-agnostic 的，本票不改變它的判定邏輯**，但它讀取的 `tab_strip_geometry` 會搬進 `Pane`，所以存取路徑必須改走 `Pane` 的 accessor。
- `tab_overflow.h`（368 行）已提供 tab strip 的純幾何與命中測試函式，且有 `tests/unit/tab_overflow_test.cpp`。**本票不改它**，`Pane` 只是這些純函式的呼叫者與結果的持有者。
- `tab_strip_proc`（`main.cpp:4327-4449`，122 行）是扁平 if-chain，處理 `WM_PAINT`／`WM_ERASEBKGND`／`WM_MOUSEWHEEL`／`WM_LBUTTONDOWN`／`WM_LBUTTONDBLCLK`／`WM_MOUSEMOVE`／`WM_MOUSELEAVE`／`WM_LBUTTONUP`／`WM_CAPTURECHANGED`／`WM_NCDESTROY`，subclass id 為 `pane_index`。
- `paint_tab_strip`（`main.cpp:4148-4327`，179 行）與 `apply_tab_item_size`（`1816-1920`，104 行）。

## Binding constraints — quoted, do not go looking for them

`docs/adr/0002-stable-pane-identity-across-layout-switch.md`：

> PaneDock 把 pane 視為每個 Group 最多四個穩定 identity,永久擁有自己的 tab;template 只決定哪些可見與如何排列。

`CONTEXT.md`：

> **pane**: One of up to four stable identity slots on the right side that host file views. Each pane permanently owns its tabs; the layout template decides only which panes are visible and how they are arranged, never which tabs belong to which pane.

`AGENTS.md`：

> **Keep `src/core` free of HWND, COM and `windows.h`.** It is the only automated test seam in this project (`docs/testing.md`); every type that leaks into it costs that seam.

`AGENTS.md`：

> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups — that is both the crash surface and a CPU/disk spike from simultaneous folder enumerations.

`AGENTS.md`：

> Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`docs/design-spec.md` FR-004：

> Win32 的 `DeferWindowPos` 要求同一批次內的視窗共用 parent,因此主視窗 child 與各 Explorer container child 各自使用一批,但在同一個 layout pass 完成提交。

`docs/design-spec.md` FR-005：

> 關閉 pane 的最後一個 tab 或**跨 pane 搬走來源 pane 的最後一個 tab**時,留下的 tab 也導覽至同一個預設 location,而非留下空 pane。

`docs/tickets.md` PD-110 決策：被丟到目標 pane 的 tab 在目標裡自動變成 active tab；有效丟放區只有 tab strip 本身。

`docs/tickets.md` PD-066 決策 2／PD-074 決策 1：

> 不做「跟隨游標的浮動縮圖」。那需要 layered window 或即時 blit,複雜度遠高於收益,而且與本專案的純 GDI 繪製路線不符。

`docs/development.md`：

> Do not add a dependency, background loop, framework, or abstraction without a measured need.

`docs/tickets.md` Agent 交付規則：

> 預設每個 ticket 的實作範圍為半天至兩天;若超過,先拆 ticket。

## 本系列的模組契約（本票必須遵守）

1. **單向依賴**：協調層（`main.cpp`）→ `Pane`。`Pane` **不得**持有 `AppState*`、不得持有回呼介面、不得持有 `std::function` 成員。
2. **真相來源硬切**：
   - `core::PaneState` 擁有 tabs、location、view mode、sort、history、active tab id（會被存檔、有 unit test）。
   - `Pane` 只擁有 HWND、幾何、hover 索引、捲動位移、drag placeholder、tooltip 註冊旗標（永不存檔）。
   - **`Pane` 不得持有自己的 tab 清單複本。** 它對 tab 的認知只有「畫幾個格子、每格什麼字」，由協調層以 `set_tabs(std::span<const std::wstring>)` 餵入。
3. **跨 pane 的拖曳狀態留在協調層**（因為它天生跨越來源與目標兩個 pane）。`Pane` 只提供純查詢與純命令。

## Files to read and trace first

- `src/app_shell/pane_chrome.h/.cpp`：現況全貌。
- `src/app_shell/main.cpp:501-629`：全部 9 個 pane-parallel 欄位。
- `src/app_shell/main.cpp:1779-1940`：`update_tab_strip_tooltips`、`apply_tab_item_size`、`refresh_tab_strip`、`refresh_tab_strips`。
- `src/app_shell/main.cpp:3913-4101`：`close_tab_at_point`、`tab_scroll_step`、`scroll_tab_strip`、`cancel_tab_drag`、`finish_tab_drag`、`update_tab_drag`。
- `src/app_shell/main.cpp:4103-4449`：`draw_tab_scroll_button`、`paint_tab_strip`、`tab_strip_proc`。
- `src/app_shell/main.cpp:277-455`：`DragHoverTarget`；`:700-710` `revoke_drag_hover_targets`；`:3474-3514` `register_tab_drag_hover_targets`。
- `src/app_shell/main.cpp:2859-3188`：`apply_layout`，PD-155 的兩層 batch 與 PD-108 的 unchanged-pane skip。
- `src/app_shell/tab_overflow.h`：全部純函式與 `TabStripGeometry`、`TabStripDragLayout`。
- `docs/tickets/PD-110-cross-pane-tab-drag.md`、`PD-074`、`PD-066`、`PD-155`、`PD-163`。

## Scope

1. `src/app_shell/pane_chrome.h/.cpp` 更名為 `src/app_shell/pane.h/.cpp`，型別 `PaneChrome` → `panedock::app_shell::Pane`。`AppState::pane_chrome` → `AppState::panes`。更新 `tests/unit/pane_chrome_test.cpp` 的檔名與 include。
2. 把六個欄位搬進 `Pane`：`tab_visuals`、`tab_strip_geometry`、`tab_hover_indices`、`tab_scroll_hover_indices`、`tab_drag_targets`、`folder_context_buttons`。從 `AppState` 刪除。
3. 新增 `Pane` 的成員函式，全部是純查詢或純命令：
   - `void set_tabs(std::span<const std::wstring> labels, std::size_t active_index)` — 更新 `tab_visuals` 並重算幾何。
   - `const TabStripGeometry& tab_geometry() const noexcept`
   - `std::optional<std::size_t> tab_at(POINT client) const noexcept` — 委派 `tab_overflow.h` 的既有純函式。
   - `std::optional<std::size_t> tab_at_screen(POINT screen) const noexcept` — 內含 `ScreenToClient` + `GetClientRect` 範圍檢查，供跨 pane 拖曳使用。
   - `void set_tab_hover(std::optional<std::size_t>)`、`void set_scroll_hover(std::optional<std::size_t>)`
   - `void set_drag_placeholder(std::optional<std::size_t> insert_index)` — 只改視覺，不改任何資料。
   - `bool scroll_tabs(bool forward)`
   - `bool register_drag_hover_target(IDropTarget*)` / `void revoke_drag_hover_target()`
4. `Pane::destroy()` 必須在摧毀 `tab_strip_` 之前先 `RevokeDragDrop`。`main.cpp:700-710` 的 `revoke_drag_hover_targets` 改為委派或刪除。
5. **跨 pane 拖曳的界線**：`AppState::TabDrag` **留在協調層**。`update_tab_drag` 的逐 pane 掃描改為呼叫 `panes[i].tab_at_screen(screen)`；placeholder 的顯示改為呼叫 `panes[i].set_drag_placeholder(...)`。`finish_tab_drag` 的 `core::reorder_tab` / `core::move_tab` / 重新導覽全部留在協調層，一行不改語意。
6. `paint_tab_strip`、`apply_tab_item_size`、`update_tab_strip_tooltips`、`close_tab_at_point`、`scroll_tab_strip` 改為 `Pane` 的成員或改走 `Pane` 的 accessor。`tab_strip_proc` 的 subclass 可留在 `main.cpp`，但其內部狀態存取一律經由 `Pane`。
7. **保留 PD-155 的兩層 batch 與 PD-108 的 unchanged-pane skip**，在交接區指出它們在新程式碼中的行號。
8. 順手把本票碰到的函式簽章從 `AppState&` 收窄成實際需要的型別（多數應變成 `Pane&`）。在交接區記錄收窄了幾個。

## Non-goals

- 不搬 `explorers`、`realized`、`suppress_history_record`（PD-183）。
- **不引入 `PaneOutcome` / `PaneRequest` 之類的回報協定。** `tab_overflow.h` 已提供測過的純命中測試接縫，向上呼叫點可能只剩少數幾處，撐不起一個新協定；`docs/development.md` 明文禁止無實測需要的抽象。條件觸發見下方「後續候選」。
- 不註冊新的 window class、不改變任何 HWND 的 parent。
- 不改 tab 的視覺數值、圓角、間距、placeholder 顏色（`RGB(148, 163, 184)`）或捲動步進。
- 不做跟隨游標的浮動縮圖（PD-066 決策 2／PD-074 決策 1 已否決）。
- 不支援跨 Group 拖曳 tab（PD-110 non-goal）。
- 不改 `core` 的任何型別或函式。
- 不重開任意遞迴 pane 分割。

## Acceptance Criteria

1. `AppState` 中只剩 3 個 pane-parallel 欄位（`explorers`、`realized`、`suppress_history_record`）。
2. `Pane` 的公開介面中沒有 `AppState`、沒有回呼、沒有 `std::function` 成員。
3. `Pane` 不持有任何 tab 清單複本；`grep` 不到 `std::vector<TabState>` 或等價物在 `pane.h`。
4. PD-155 的 batch 結構與 PD-108 的 unchanged-pane skip 可在新程式碼中指認（交接區給行號）。
5. `Pane::destroy()` 中 `RevokeDragDrop` 早於 `DestroyWindow(tab_strip_)`。
6. `ctest --test-dir build --output-on-failure` 全綠，含 `panedock_launch_smoke`。
7. **PD-110 的跨 pane 拖曳零行為變更**（見實機清單第 3–6 項）。
8. 視覺零變更。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 舊欄位不得殘留在 AppState
Select-String -Path src/app_shell/main.cpp -Pattern 'tab_visuals|tab_strip_geometry|tab_hover_indices|tab_scroll_hover_indices|tab_drag_targets|folder_context_buttons' | Where-Object { $_.Line -match 'std::array' }
# Pane 不得反向依賴協調層
Select-String -Path src/app_shell/pane.h -Pattern 'AppState|std::function|Host\*|callback'
# Pane 不得持有 tab 資料複本
Select-String -Path src/app_shell/pane.h -Pattern 'TabState|std::vector<.*Tab'
# PD-155 batch 契約仍在
Select-String -Path src/app_shell/*.cpp,src/app_shell/*.h -Pattern 'BeginDeferWindowPos|EndDeferWindowPos'
```

前三條必須無結果，第四條必須有結果。

```powershell
# 關閉不殘留
$p = Start-Process build\PaneDock.exe -PassThru; Start-Sleep 3
$p.CloseMainWindow() | Out-Null
if (-not $p.WaitForExit(5000)) { throw 'process survived graceful close' }
```

```powershell
git diff --check
```

## 交給使用者的實機檢查清單

1. 四宮格 Group：切版型 1→2→3→4→1，確認所有 chrome 位置與既有 build 一致。
2. Tab 基本操作：新增、關閉、切換、雙擊空白處新增、tab 數量超出時的左右捲動與捲動按鈕 hover。
3. **同 pane 拖曳排序**（PD-035）：拖動一個 tab 到同 pane 的其他位置，確認 placeholder 空位撐開的位置、淡化繪製與落點都不變。
4. **跨 pane 拖曳**（PD-110）：把 pane A 的 tab 拖到 pane B 的 tab strip，確認 (a) placeholder 出現在**目標** pane、(b) 落下後該 tab 在目標 pane 成為 active、(c) 兩個 pane 都正確重新導覽。
5. **跨 pane 搬走最後一個 tab**（FR-005）：來源 pane 只剩 1 個 tab 時把它拖走，確認來源 pane 留下 1 個重置為預設位置的 tab，而不是變空。
6. 拖曳中途按 Esc 或放開在無效區域（explorer 內容區、sidebar、視窗外），確認拖曳取消且無殘留 placeholder。
7. 檔案拖放懸停自動切換（PD-034）：從檔案總管拖一個檔案懸停在某個 tab 上，確認會自動切換到該 tab。
8. 拖曳進行中直接關閉主視窗（PD-173），確認乾淨結束。

## Handoff requirements

在 `## 交接區` 記錄：搬移前後 `AppState` 的 pane-parallel 欄位數量、`Pane` 的公開介面清單、PD-155／PD-108 語意保留在哪一行、`destroy` 的順序、收窄了幾個 `AppState&` 簽章、以及使用者實機檢查（特別是第 3–6 項拖曳）的回報結果。

**必填**：逐一列出實作後 `Pane` 需要「向上」通知協調層的呼叫點及其形狀。這份清單是下一條候選的觸發依據。

## 後續候選（本票不做）

若上述「向上呼叫點」清單**超過 3 處且讀起來確實糾纏**，才開票引入 `PaneOutcome` 回報協定（`Pane` 回傳意圖 enum、由協調層執行副作用，比照 `core::ShutdownSequence` 的 event→action reducer）。少於等於 3 處則不開，並把這個判斷寫進 `docs/tickets.md` §候選。
