# PD-154 — 雙擊 pane tab 條空白區新增 tab

Phase 7 · app_shell · Depends on: PD-019, PD-049, PD-055

- Source: 使用者需求（2026-08-31）。
- Origin: 使用者原文：「for pane tab bar, double clicks on empty space of tab bar will crate a new tab」。
- Priority: LOW——既有 `+` 與鍵盤新增 tab 均可完成同一工作；本票增加的是滑鼠效率捷徑。

## Outcome

使用者在任一可見 pane 的 tab 條空白區快速雙擊時，該 pane 新增並切換到一個 tab。這個入口必須與既有 `+` 按鈕使用完全相同的新增流程。

「空白區」只指 tab 條 client area 中未命中任何 tab、`+` 按鈕或左右 overflow 捲動按鈕的區域。雙擊上述既有目標不得新增 tab。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.6：

> 每個 pane 有一個以上的 tab,可新增、關閉、切換。tab 條在 pane 上緣；在 tab 條未被 tab、`+` 或捲動按鈕占用的空白區雙擊會新增 tab。

`docs/design-spec.md` §FR-005：

> 每個 pane 至少一個 tab。可新增、關閉、切換 tab；在 tab 條未被 tab、`+` 或捲動按鈕占用的空白區雙擊會新增 tab。關閉 pane 的最後一個 tab 時,該 tab 導覽至預設 location 而非留下空 pane。

`docs/design-spec.md` §NFR-002：

> 只有可見 pane 的 active tab 持有 live `IExplorerBrowser`。其餘 tab 僅以資料存在。

`docs/development.md` §Change workflow：

> Read and trace the files the ticket lists. Grep every caller of any shared function you intend to change.

> Make the smallest change that satisfies the acceptance criteria. Reuse before adding.

`AGENTS.md`：

> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups.

> Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs persist as data and are realized on activation.

> Event-driven idle path only. No busy loops, no polling timers.

> New non-trivial logic needs one focused runnable test or self-check.

## Confirmed implementation facts

1. `src/app_shell/main.cpp:4899-4908` creates each tab strip as a custom-painted `STATIC` child with `SS_NOTIFY`, then subclasses it with `tab_strip_proc`. PD-049 established this control shape; PD-055 restored mouse delivery with `SS_NOTIFY`.
2. `tab_strip_proc` (`main.cpp:4143-4247`) already handles mouse input. Its `WM_LBUTTONDOWN` path checks, in order, the overflow scroll buttons, a tab item, and the `add_rect`; it forwards `-1` through `kTabStripSelectionMessage` only for the existing `+` button.
3. The geometry needed to define non-empty targets already exists: `tab_item_at_point`, `tab_scroll_button_at_point`, and `state.tab_strip_geometry[pane_index].add_rect`. Reuse these directly; do not introduce another geometry representation.
4. The main-window handler for `kTabStripSelectionMessage` (`main.cpp:5026-5034`) routes `lparam == -1` to `add_tab_to_pane`.
5. `add_tab_to_pane` (`main.cpp:3348-3371`) already owns the complete operation: shutdown guards, unique identity, default location, active-tab selection, navigation of an existing realized pane, tab-strip refresh, and persistence. The double-click path must reuse it through the existing message route rather than duplicate any of those steps.

## Files to read and trace first

- `docs/tickets/PD-019-tab-strip-and-realize-on-activation.md`——原始新增／切換與 realize-on-activation 契約。
- `docs/tickets/PD-049-custom-tab-strip-control.md`——目前自繪 tab strip 的控制項形狀。
- `docs/tickets/PD-055-tab-strip-static-ss-notify-missing.md`——`STATIC` 滑鼠訊息與 `SS_NOTIFY` 的既有修正。
- `src/app_shell/main.cpp:3348-3371`——`add_tab_to_pane`；修改前追蹤其所有 caller。
- `src/app_shell/main.cpp:3717-3773`——tab strip index 與既有 hit-test wrappers。
- `src/app_shell/main.cpp:4143-4247`——`tab_strip_proc` 的完整滑鼠訊息流程。
- `src/app_shell/main.cpp:4899-4908`——tab strip 建立樣式與 subclass wiring。
- `src/app_shell/main.cpp:5026-5034`——`kTabStripSelectionMessage` 的既有新增入口。
- `src/app_shell/tab_overflow.h:176-347` 與 `tests/unit/tab_overflow_test.cpp`——既有 tab／add／scroll 幾何與命中契約；僅確認，不擴張它來承載 Win32 雙擊路由。

## Scope

1. 在 `tab_strip_proc` 處理 `WM_LBUTTONDBLCLK`，沿用系統雙擊時間與距離判定，不自行計時。
2. 在 active Group 與 `pane_index` 有效時，用既有三組命中資料排除 tab、`+` 與 overflow 捲動按鈕；只有三者皆未命中才視為空白區。
3. 命中空白區時，沿用 `kTabStripSelectionMessage` 的 `lparam == -1` 路徑，讓 parent 呼叫 `add_tab_to_pane`。不直接複製新增、切換、navigate、refresh 或 save 程式碼。
4. 已處理的空白區雙擊回傳 `0`；非空白目標不觸發新增，交回既有/default 處理，保留各目標目前的行為。
5. 加入一項最小人工 runtime check。這是 Win32 訊息合成與 hit-test routing，沒有新增可合理下沉到 `core` 的非平凡產品邏輯；既有 `tab_overflow_test` 已覆蓋幾何，勿為單一訊息分支新增介面或測試框架。

## Non-goals

- 不改變 `+` 按鈕或 `Ctrl+T` 等既有新增 tab 入口。
- 不替 tab 本身、關閉區、`+` 或 overflow 捲動按鈕新增雙擊語意。
- 不新增 middle-click、single-click 空白區行為，亦不新增「複製目前 tab」或選擇初始 location 的變體。
- 不改 tab 寬度、overflow、繪製、hover、tooltip、同 pane／跨 pane 拖曳排序。
- 不改 `core` model、session schema、持久化格式、Shell view 生命週期或 pane HWND。
- 不自行呼叫 `GetDoubleClickTime`、不加 timer、不輪詢，也不建立新 helper／抽象層。

## Acceptance Criteria

1. 在有可見空白區的 pane tab 條快速雙擊一次，恰好新增一個 tab；新 tab 立即成為該 pane 的 active tab。
2. 新 tab 的預設 location、預設 view mode、realize/navigation 與保存結果和按既有 `+` 按鈕新增完全相同；關閉並重啟後該 tab 仍存在。
3. 分別雙擊 tab 本體、tab 關閉區與 `+` 按鈕，不得因本票新增額外 tab；`+` 的既有單擊仍只新增一個 tab。
4. 製造 tab overflow 後，雙擊左／右捲動按鈕不得新增 tab，按鈕的既有捲動仍可用。
5. 每個可見 pane 都只新增到被雙擊的 pane；不得改變其他 pane 的 tab 集合或 active tab。
6. tab 單擊切換、關閉、同 pane 與跨 pane 拖曳仍可用；空白區雙擊不留下 mouse capture 或 drag state。
7. 空白區不足或不存在時不需要創造額外可點區域；本票不改 tab strip layout。
8. Release build、CTest 與 `git diff --check` 全數通過。

## Agent Checks

```powershell
rg -n "add_tab_to_pane|kTabStripSelectionMessage|WM_LBUTTONDBLCLK|tab_item_at_point|tab_scroll_button_at_point|add_rect" src/app_shell/main.cpp

cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

在真實桌面執行最小人工檢查：

1. 保留一個 tab、放大 pane 使 tab 條有明顯空白，記錄 tab 數；在空白區雙擊一次，確認只增加一個且成為 active。
2. 依序雙擊既有 tab、其關閉區、`+`；確認沒有本票造成的額外新增。
3. 建立足夠 tabs 觸發 overflow，雙擊左右捲動按鈕；確認不新增。
4. 在另一個可見 pane 的空白區雙擊；確認只改該 pane。
5. 正常關閉（不可 `/F`）並重啟，確認新增 tab 已保存。

## Handoff requirements

- 記錄 `WM_LBUTTONDBLCLK` 的實際插入位置與使用的既有 hit-test/message route。
- 記錄 `add_tab_to_pane` 的 caller trace，確認沒有複製新增流程。
- 記錄 Release build、CTest、`git diff --check` 結果。
- 記錄上述人工檢查結果；無法執行者逐項寫明原因，不得以靜態推測冒充 runtime 證據。

## 交接區

<!-- 實作 agent 填寫，append-only -->

- 實作位置：`src/app_shell/main.cpp` 的 `tab_strip_proc`，緊接既有 `WM_LBUTTONDOWN` 分支後新增 `WM_LBUTTONDBLCLK`。分支直接複用 `tab_scroll_button_at_point`、`tab_item_at_point` 與既有 `add_rect`；三者皆未命中時，送出既有 `kTabStripSelectionMessage(pane_index, -1)`，由 parent 集中呼叫 `add_tab_to_pane`。未新增 helper、core 邏輯、timer 或自訂雙擊判定。
- Caller trace：`add_tab_to_pane` 的既有 caller 為 `kTabStripSelectionMessage`（`+` 與本票共用）及 message loop 的 `Ctrl+T`；本票沒有新增 direct caller，也沒有複製 unique id、default location、navigate、refresh 或 save 流程。
- 驗證：Release configure/build 通過；CTest 首輪 10/11 通過，`panedock_launch_smoke` 在 sandbox 內因無法寫真實 `%LOCALAPPDATA%\PaneDock`，關閉時停在既有 session-save failure 對話框而於 30 秒 timeout。改在 elevated context 單獨重跑 `ctest --test-dir build -R "^panedock_launch_smoke$" --output-on-failure`，1/1 通過（1.09 秒），確認不是本票回歸。`git diff --check` 通過。
- 尚未驗證：Acceptance Criteria 1–7 的真實滑鼠雙擊矩陣（空白區、tab、close、`+`、overflow、另一 pane、正常重啟持久化）。完成這組 runtime check 前 tracker 維持 `in_progress`。
