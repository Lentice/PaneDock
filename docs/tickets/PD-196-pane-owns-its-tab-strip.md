# PD-196 — tab strip 收進 `Pane`（`PaneTabStrip`）

Phase 7 · architecture · Depends on: PD-191, PD-194

- Source: 2026-09-05 使用者要求「main 中不要有屬於 pane 的獨立功能，所有 pane 的獨立功能都應該搬到 pane 之中，**或是另外的物件歸屬在 pane 的掌控之下**」。後半句直接授權本票的做法：tab strip 成為 `Pane` 擁有的獨立物件，而不是塞成 `Pane` 的 40 個成員。
- Priority: MEDIUM-HIGH。本系列最大的一張（約 700 行搬移），也是價值最高的一張。

## 本票結清一個長期候選

`docs/tickets.md` §候選「把 tab strip 的 hover／drag／paint／`WNDPROC` 收進 `app_shell::TabStrip` 模組」的觸發條件是「**PD-187～PD-189 完成後重新評估**」。三張票都已 `done`，重新評估結論為**開票**：pane 已有自己的 HWND 與 proc、繪製與命令都已就地處理，tab strip 的邊界現在是清楚的。本票完成後，請把該候選列標記為已開票（比照 §候選 既有的刪除線寫法），**不要刪除原始描述**。

該候選列同時記錄了兩項必須沿用的事實：

> `layout_tab_strip` 其實不在 `main.cpp`，它是 `tab_overflow.h:176-336` 的無狀態純函式（不依賴 `AppState`／HWND），`Pane` 現在就能直接呼叫。

> `cancel_tab_drag`／`finish_tab_drag`／`update_tab_drag`／`register_tab_drag_hover_targets` 依契約 (3) 天生跨 pane，不可能整群變成 `Pane` 成員——真正可搬的比 853 這個數字小。

## Outcome

新增 `src/app_shell/pane_tab_strip.{h,cpp}`，型別 `PaneTabStrip` 由 `Pane` 以值持有。tab strip 的 UI 狀態、幾何、繪製、hit-test、捲動與 `WNDPROC` 全在裡面。跨 pane 拖曳留在協調層。行為與視覺零變更。

## 搬進 `PaneTabStrip` 的（2026-09-05 工作樹行號）

| 目前 | 行數 |
|---|---|
| `apply_tab_item_size(Pane&, AppState&, bool)` `:1728-1830` | 103 |
| `refresh_tab_strip(Pane&, AppState&)` `:1834-1862` | 29 |
| `tab_viewport_rect(const Pane&)` `:3527-3529` | 3 |
| `tab_scroll_button_at_point(...)` `:3542-3546` | 5 |
| `tab_scroll_step(const Pane&, ...)` `:3548-3552` | 5 |
| `scroll_tab_strip(AppState&, std::size_t, bool forward)` `:3554-3567` | 14 |
| `draw_tab_scroll_button(HWND, HDC, const RECT&, ...)` `:3701-3744` | 44 |
| `paint_tab_strip(HWND, AppState&, std::size_t, ...)` `:3746-3922` | 177 |
| `tab_strip_proc(HWND, UINT, WPARAM, LPARAM)` `:3924-4046` | 123 |

外加 PD-182 已在 `Pane` 裡的六個 UI 欄位（`tab_visuals_`、`tab_strip_geometry_`、`tab_hover_index_`、`tab_scroll_hover_index_`、`tab_drag_target_`）與 `update_tab_strip_tooltips`／`tab_at`／`tab_at_screen`／`set_tabs`／`set_geometry`／`tab_visuals`／`tab_geometry`／`tab_hover_index`／`set_tab_hover`／`scroll_hover_index`／`set_scroll_hover`／`register_drag_hover_target`／`revoke_drag_hover_target`／`drag_hover_target`——這些從 `Pane` 移進 `PaneTabStrip`，`Pane` 對外改為 `PaneTabStrip &tab_strip_ui()` 一支 accessor，或轉呼叫（實作者自選，記在交接區）。

`tab_strip_` 這個 HWND 成員也搬進 `PaneTabStrip`（它是這個物件的視窗）。

## 留在協調層的（契約 (3)，不要搬）

| 函式 | 行數 | 理由 |
|---|---|---|
| `cancel_tab_drag(AppState&, HWND)` `:3569-3579` | 11 | 跨 pane |
| `finish_tab_drag(AppState&, HWND)` `:3581-3639` | 59 | 跨 pane |
| `update_tab_drag(AppState&, HWND, ...)` `:3641-3699` | 59 | 跨 pane |
| `register_tab_drag_hover_targets(HWND, AppState&)` `:3062-3099` | 38 | 四 pane 迴圈 |
| `tab_item_at_point(const std::array<Pane, N>&, HWND, POINT)` `:3531-3540` | 10 | 吃全部四個 pane |
| `tab_strip_index(...)` `:3496-3502` | 7 | strip HWND → pane index 反查 |
| `close_tab_at_point(HWND, AppState&, POINT)` `:3512-3525` | 14 | 用 `tab_item_at_point` |
| `refresh_tab_strips(AppState&)` `:1864-1866` | 3 | 四 pane 迴圈 |
| `tab_display_text(AppState&, ...)` `:1718-1724` | 7 | 見下 |
| `state.tab_drag`／`tab_context_menu_pane`／`tab_context_menu_tab_id` | — | singleton／跨 pane |

## 三個必須先想清楚的相依

### 1. `tab_display_text` 需要 `ShellCallScope`

它把 parsing name 轉成顯示名稱，需要 Shell 呼叫。`refresh_tab_strip` 在迴圈裡對每個 tab 呼叫它。

處理：`PaneHost` 新增 `virtual std::wstring tab_display_text(const core::ShellLocation &location) const = 0;`（或吃 parsing name，依現況簽章）。理由：它是 Shell 名稱解析，與 pane 無關，且需要協調層的重入計數。`tab_display_text` 自由函式留在 `main.cpp` 作為該 override 的實作。

### 2. `refresh_tab_strip` 的重入自我檢查

現有程式碼（`:1849-1857`）在每次 `tab_display_text` 之後重新比對 `pane.pane_state()` 是否仍是同一份、tab 數量與 id 是否未變，不符則直接 `return`。**這段防護必須原封保留**——它擋的是 Shell 呼叫重入導致 tab 清單在迴圈中途被改掉。搬移時不得「順手簡化」。

### 3. `tab_strip_proc` 的 `AppState*` 取得方式

沿用 PD-189 確立的模式：proc 是協調層的自由函式，從視窗資料取得指標，然後把工作分派給 `PaneTabStrip` 的成員。**不要**讓 `PaneTabStrip` 自己註冊 window class 並持有 `AppState*`。實務切法：`tab_strip_proc` 留在 `main.cpp`，但收斂成一個分派表——純 pane 內的 case（`WM_PAINT`、`WM_MOUSEMOVE` hover、`WM_MOUSELEAVE`、`WM_MOUSEWHEEL` 捲動、scroll button 點擊）轉呼叫 `PaneTabStrip` 成員；跨 pane 的 case（拖曳的 `WM_LBUTTONDOWN`／`WM_MOUSEMOVE` 拖曳分支／`WM_LBUTTONUP`／`WM_CAPTURECHANGED`）仍呼叫協調層的 `update_tab_drag` 等。搬完後 `tab_strip_proc` 應顯著短於現在的 123 行；實際行數記在交接區。

## Scope

1. 建 `pane_tab_strip.{h,cpp}`，加進 `CMakeLists.txt`。
2. 依上表搬移，機械替換同 PD-192；`state.chrome_font` → `pane_host()->chrome_font()`、`state.layout_tooltip` → `pane_host()->tooltip()`、`foreign_placeholder` 段落 → `pane_host()->tab_drag_layout(...)`（PD-191 已提供）。
3. `PaneTabStrip` 需要回頭問 `Pane` 兩件事：綁定的 `PaneState*`（畫作用中 tab、算 `reveal_active`）與 `PaneHost*`。做法是 `PaneTabStrip` 持有 `Pane *owner_`，在 `Pane` 的建構或 `create()` 時設定。這是**下行**的所有權關係（`Pane` 擁有 `PaneTabStrip`），不違反契約 (1)。
4. **刪除 PD-194 引入的 `PaneHost::tab_strip_needs_refresh(Pane&)`**——`Pane` 現在可以直接呼叫 `tab_strip_ui().refresh()`。同步把 `PaneHost` 的方法數更新記在 `docs/tickets.md`。
5. 更新全部呼叫點。

## Non-goals

- 不改任何 tab strip 的視覺：色票（`main.cpp:139-147` 的 `kTab*` 常數）、圓角、捲動按鈕外觀、hover 過渡、佔位動畫全部不動。色票常數搬進 `pane_tab_strip.cpp` 的匿名 namespace，值不變。
- 不改 `tab_overflow.h` 的純函式（`layout_tab_strip`、`tab_strip_hit_test`、`tab_scroll_button_hit_test`）。它們已是無狀態純函式且有測試。
- 不搬跨 pane 拖曳（上表已列）。
- 不為 `PaneTabStrip` 註冊新的 window class。
- 不改 tooltip 的註冊時機或內容。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`docs/tickets.md` 2026-09-03 模組契約：

> (3) **跨 pane 的拖曳狀態留在協調層**，因為它天生跨越來源與目標兩個 pane。

`docs/tickets.md` 2026-09-04 PD-189 條目：

> pane proc 是協調層的自由函式（比照既有的 `tab_strip_proc`，從視窗資料取得 `AppState*`），`Pane` 這個型別本身仍然沒有 `AppState*`／回呼／`std::function` 成員。

## Acceptance criteria

1. `pane_tab_strip.{h,cpp}` 存在；`pane_tab_strip.h` 不得出現 `AppState`、`GroupState`。
2. `main.cpp` 減少約 500 行以上（`paint_tab_strip` 177 + `tab_strip_proc` 大部分 + `apply_tab_item_size` 103 + 其餘）。
3. 上表「留在協調層」的九項一項都沒被搬走。
4. `refresh_tab_strip` 的重入自我檢查逐行保留。
5. `PaneHost::tab_strip_needs_refresh` 已刪除。
6. 每個 `kTab*` 色票常數的數值與改動前逐一相同（diff 比對）。
7. 零行為、零視覺變更。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

既有的 `tests/release/shell_reentry_gate_check.ps1` 以 `tab_display_text` 的函式體範圍當錨點（PD-190 交接區記載錨點為 `'void apply_tab_item_size('`）。`apply_tab_item_size` 搬走後該錨點失效，**必須更新錨點但不得放寬它守的不變式**（`tab_display_text` 的呼叫點都要帶 `state`），新舊錨點寫進交接區。

`tests/unit/` 新增 `pane_tab_strip_test.cpp`：

- `test_refresh_bails_when_tab_list_changes_mid_scan` — 假 `PaneHost::tab_display_text` 在第二次呼叫時從 `PaneState` erase 一個 tab，驗證 `refresh()` 提早 `return` 且沒有寫入不一致的標籤集合。這是重入防護第一次獲得單元測試涵蓋。
- `test_scroll_offset_clamps_at_both_ends` — 捲動到最左／最右後再捲不會越界。

## 使用者實機檢查

1. 開 8 個以上 tab 讓 strip 溢位 → 左右捲動按鈕出現、可捲、到底停住。
2. 滑鼠滾輪在 strip 上捲動。
3. hover 每個 tab、hover 捲動按鈕 → 高亮與改動前相同。
4. tab 的 `x` 關閉按鈕 hover 與點擊。
5. `+` 新增 tab 按鈕。
6. tooltip：hover 被截斷的 tab 標題 → 顯示完整名稱。
7. 同 pane 內拖曳重排 tab。
8. 跨 pane 拖曳 tab（PD-110）→ 目標 pane 佔位寬度、放開後的位置都正確。
9. 拖曳中按 Esc 取消。
10. 拖曳中把視窗拖到另一個不同 DPI 的螢幕 → strip 重新排版正確。
11. 把視窗拉到很窄 → tab 縮到最小寬度後出現捲動按鈕。
12. 切換 Group → 四個 strip 的標籤全部更新。
13. 空 Group／無 tab 的 pane → strip 只有 `+` 按鈕。

## 交接區

（實作者填寫：`Pane` 對外是 `tab_strip_ui()` 還是逐支轉呼叫；`tab_strip_proc` 搬移後的行數；`shell_reentry_gate_check.ps1` 新舊錨點；`main.cpp` 前後行數。）
