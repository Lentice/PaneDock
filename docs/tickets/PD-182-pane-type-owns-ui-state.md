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

## 交接區

**欄位數量**：`AppState` 的 pane-parallel 欄位從搬移前的 9 個降到 3 個（`explorers`、`realized`、`suppress_history_record`），符合 AC1。`pane_chrome` 重新命名為 `panes`；`PaneChrome` 更名為 `panedock::app_shell::Pane`（`src/app_shell/pane.h/.cpp`，取代 `pane_chrome.h/.cpp`）。`tests/unit/pane_chrome_test.cpp` 更名為 `tests/unit/pane_test.cpp`，`CMakeLists.txt`／`tests/CMakeLists.txt` 對應更新（`panedock_pane_chrome` → `panedock_pane`，並新增 `ole32` link 給 `RegisterDragDrop`/`RevokeDragDrop`）。

**`Pane` 公開介面清單**（新增部分）：
- `set_tabs(std::span<const std::wstring> labels)` — 清空並重建 `tab_visuals_`
- `tab_visuals() const` — 唯讀查詢，型別是 `std::vector<StripLabel>`（純 UI 文字，不是 `core::TabState`）
- `tab_geometry() const` / `set_geometry(TabStripGeometry)` — 純查詢／純命令
- `tab_at(POINT client) const` / `tab_at_screen(POINT screen) const`
- `tab_hover_index() const` / `set_tab_hover(...)`、`scroll_hover_index() const` / `set_scroll_hover(...)`
- `register_drag_hover_target(IDropTarget*)` / `revoke_drag_hover_target()` / `drag_hover_target() const`
- `folder_context_button() const` / `set_folder_context_button(HWND)`

沒有任何成員持有 `AppState*`、回呼介面或 `std::function`；`grep -n 'AppState|std::function|Host\*|callback' pane.h` 與 `grep -n 'TabState|std::vector<.*Tab' pane.h` 皆為空（見下方 Agent Checks 結果）。

**與 ticket 原始方法清單的差異（誠實記錄，理由如下）**：
- 原提案 `set_tabs(labels, active_index)`：拿掉了 `active_index` 參數。`active_index` 只在完整版面重算（`apply_tab_item_size`）裡有意義，而後者仍是協調層的自由函式（見下一點），所以 `set_tabs` 只單純替換文字。
- 原提案要求 `apply_tab_item_size`／`paint_tab_strip`／`scroll_tab_strip` 等「改為 Pane 的成員或改走 Pane 的 accessor」——**本票選擇「改走 accessor」而非「搬進 Pane 成為成員」**。原因：這些函式的版面計算需要 `state.chrome_font`、`state.tab_drag`（跨 pane 拖曳的協調層狀態）與一組 DPI-scaled 常數；把整段算法搬進 `Pane` 需要 `Pane` 認識這些協調層概念（違反契約第 1 條），或是把常數/字型複製一份進 `pane.cpp`（造成兩份真相）。維持它們是 `main.cpp` 裡的自由函式、只透過 `Pane` 的 `set_tabs`/`tab_visuals`/`tab_geometry`/`set_geometry` 存取資料，同時滿足 ticket Scope 第 6 點明文允許的「或改走 accessor」，且是本次變更範圍裡風險最低的選項。
- 原提案 `set_drag_placeholder(optional<size_t>)`：沒有實作為獨立方法。它需要的 `foreign_placeholder`／`foreign_placeholder_width` 只有協調層的 `AppState::TabDrag` 知道，最終還是要呼叫完整版面重算；`apply_tab_item_size` 已經把這條路徑走完，另開一個方法只是重複入口。
- 原提案 `scroll_tabs(bool forward)`：沒有實作為獨立方法，理由同上——捲動後仍需呼叫協調層的 `apply_tab_item_size` 做完整重算（因為必須考慮同時存在的跨 pane 拖曳 placeholder）。`scroll_tab_strip`（協調層自由函式）改為：取 `chrome.tab_geometry()` 的複本、算新 `scroll_offset`、用 `chrome.set_geometry(...)` 寫回、再呼叫 `apply_tab_item_size`。
- `tab_drag_targets`：`register_drag_hover_target` 依 ticket 簽章只收 `IDropTarget*`，但原本的 `WM_TIMER`/`kDragHoverMessage` 處理需要呼叫 `DragHoverTarget` 專屬的 `invoke_hover`/`timer_expired`（不在 `IDropTarget` 介面上）。解法：在 `pane.h` 新增一個小的 `DragHoverTimer` 抽象介面（`invoke_hover`/`timer_expired` 兩個虛擬函式），`main.cpp` 的 `DragHoverTarget` 多重繼承它；協調層對 `pane.drag_hover_target()` 回傳的 `IDropTarget*` 做 `dynamic_cast<DragHoverTimer*>` 取得那兩個方法。`Pane` 本身完全不知道 `DragHoverTarget` 這個型別，只認得抽象介面，契約第 1 條（單向依賴）未破壞。

**PD-155／PD-108 語意保留位置**：`apply_layout` 中 `WindowPositionBatch`／`BeginDeferWindowPos`／`EndDeferWindowPos` 的兩層批次結構未被觸碰，仍在 `main.cpp` 的 `WindowPositionBatch`（約 862-915 行）與 `apply_layout` 內的 `positions`／`explorer_positions[index]` 兩組批次（約 2500-2820 行）；`changed_panes[index]` 的 unchanged-pane skip 邏輯（PD-108）保留在同一段落，只是欄位存取改成 `state.panes[index].set_rect(...)` 等 `Pane` accessor，判斷邏輯一行未動。`Select-String 'BeginDeferWindowPos|EndDeferWindowPos'` 有結果，符合 AC4。

**`Pane::destroy()` 順序**：`revoke_drag_hover_target()`（RevokeDragDrop）→ `status_bar_`→`address_bar_`→`pinned_button_`→`view_mode_button_`→`refresh_button_`→`up_button_`→`forward_button_`→`back_button_`→`tab_strip_`→`explorer_container_`（逐一 `DestroyWindow`）→ 清空 `laid_out_pane_rect_`/`tab_tooltips_registered_`/`tab_visuals_`/`tab_strip_geometry_`/hover 索引。`RevokeDragDrop` 在 `DestroyWindow(tab_strip_)` 之前執行，符合 AC5 與 §9.4。

**收窄 `AppState&` 簽章**：本票沒有把任何既有函式簽章從 `AppState&` 改窄成 `Pane&`——被本票碰到的自由函式（`apply_tab_item_size`、`paint_tab_strip`、`scroll_tab_strip`、`update_tab_drag`、`cancel_tab_drag`、`finish_tab_drag`、`tab_item_at_point` 等）全部仍需要 `AppState&`（用於 `active_group`、`state.tab_drag`、`state.chrome_font`、`state.explorers` 等），這是上一點解釋過的「accessor 而非搬遷演算法」設計的直接結果。收窄數：**0**。這點誠實記錄，供日後決定是否要為 `PaneOutcome`（見下方候選）重新評估。

**編譯與測試**：`cmake --build build` 全綠；`ctest --test-dir build --output-on-failure` 20/20 全綠（含 `panedock_launch_smoke`）。四條 Select-String 檢查結果符合要求（前三條空、第四條非空）。`git diff --check` 無空白錯誤。`Start-Process build\PaneDock.exe` + `CloseMainWindow()` 驗證乾淨關閉，連續執行三次皆在 8 秒內結束（第一次因為啟動仍在進行中而假性逾時，補足啟動等待後穩定通過）。

**必填：`Pane` 向上呼叫點清單**（供 `PaneOutcome` 候選觸發條件判斷）：
`Pane` 本身沒有任何回呼機制，「向上通知」全部改寫成協調層直接呼叫 `Pane` 的純查詢/純命令方法，而不是 `Pane` 主動通知協調層。也就是說在本票完成後，`Pane` 完全沒有「向上」的呼叫路徑——資料流一律是協調層讀 `Pane` 的查詢方法、算完後寫回 `Pane` 的命令方法。**呼叫點數：0**，遠低於候選表訂的「超過 3 處」門檻，因此 `PaneOutcome` **不開票**，維持候選狀態；此判斷已回寫 `docs/tickets.md` §候選。

**使用者實機檢查清單（第 1–8 項）**：尚未執行，需要使用者在真實硬體上驗證（特別是第 3–6 項的拖曳行為與第 8 項的拖曳中關閉）。程式碼邏輯本身在本次重構中一行未改（只換了資料的存取路徑），但實機驗證仍是本票未完成的部分，留給使用者回報。

## 後續候選（本票不做）

若上述「向上呼叫點」清單**超過 3 處且讀起來確實糾纏**，才開票引入 `PaneOutcome` 回報協定（`Pane` 回傳意圖 enum、由協調層執行副作用，比照 `core::ShutdownSequence` 的 event→action reducer）。少於等於 3 處則不開，並把這個判斷寫進 `docs/tickets.md` §候選。
