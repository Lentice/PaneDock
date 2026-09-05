# PD-194 — pane 的 tab 生命週期命令收進 `Pane`

Phase 7 · architecture · Depends on: PD-191, PD-192, PD-193

- Source: 2026-09-05 使用者要求「main 中不要有屬於 pane 的獨立功能」。
- Priority: MEDIUM。

## 本票覆寫 PD-186 的一項 non-goal

PD-186 的 non-goals 寫著：

> 把 tab 操作包成 `Pane::add_tab()` 這類轉呼叫方法也不做（附觸發條件）：那些函式的本體是協調（`capture_pane_location` → `ShellCallScope` → `navigate` → `refresh_tab_strip` → `schedule_session_save`），只有中間一行是資料變更，包一層只多一層轉呼叫而耦合不變。

**這個理由在 PD-191～PD-193 之後失效。** 當時「協調」指的是那條鏈上的每一環都只有 `AppState` 拿得到；現在 `capture_location()` 是 `Pane` 的成員（PD-193）、`navigate_to()` 是 `Pane` 的成員（PD-192）、`ShellCall` 與 `schedule_session_save()` 由 `PaneHost` 提供（PD-191）。那條鏈整條都在 `Pane` 裡，搬進去不是「多包一層轉呼叫」，而是**把最後一個外部參考點消掉**。新證據為使用者 2026-09-05 的界線宣告。

## Outcome

四支 tab 命令成為 `Pane` 成員，`main.cpp` 不再有 `pane_index` 參數的 tab 命令函式。行為與視覺零變更。

## 要搬的函式（2026-09-05 工作樹行號）

| 目前 | 行數 | 搬成 |
|---|---|---|
| `switch_active_tab(HWND, AppState&, std::size_t, const std::string& tab_id)` `:3026-3056` | 31 | `void Pane::switch_active_tab(const std::string &tab_id)` |
| `cycle_active_tab(HWND, AppState&, std::size_t, bool forward)` `:3101-3115` | 15 | `void Pane::cycle_active_tab(bool forward)` |
| `add_tab_to_pane(HWND, AppState&, std::size_t, ShellLocation = default_shell_location())` `:3117-3145` | 29 | `void Pane::add_tab(core::ShellLocation initial_location)` |
| `close_tab_in_pane(HWND, AppState&, std::size_t, const std::string& tab_id)` `:3147-3170` | 24 | `void Pane::close_tab(const std::string &tab_id)` |

四支的 `HWND` 參數全部是未命名／未使用的，直接刪掉。

## Scope

1. 機械替換同 PD-192／PD-193，額外兩項：
   - `unique_tab_id(active_group(state), candidate)` → `pane_host()->make_unique_tab_id()`（PD-191 已提供）。`unique_tab_id` 自由函式**留在 `main.cpp`**，因為 `AppState::make_unique_tab_id` 是它唯一的呼叫者。
   - `default_shell_location()`（`main.cpp:660`）是 `Pane::add_tab` 的預設引數。**不要**把預設引數搬進 `pane.h`（那需要把 `default_shell_location` 也搬進 `app_shell`，而它是「桌面／使用者資料夾」這種 app 層級預設）。改為：`Pane::add_tab` 不給預設值，呼叫端明寫 `pane.add_tab(default_shell_location())`。
2. 每支開頭 `if (pane_host() == nullptr) return;`。
3. `switch_active_tab` 與 `add_tab_to_pane` 都呼叫 `refresh_tab_strip(pane, state)`，該支要到 PD-196 才搬。本票的過渡做法：`Pane` 新增一個 **空的 virtual-free 掛勾**不可行；改為在 `PaneHost` 新增第九支 `virtual void tab_strip_needs_refresh(Pane &) = 0;`，由 `AppState` 實作成呼叫既有的 `refresh_tab_strip(pane, *this)`。PD-196 完成後這支要被刪除，並把刪除寫進 PD-196 的 scope。此新增的理由與 PD-193 相同格式記在此處，PD-191 票面不編輯。
4. 更新全部呼叫點：`grep -n 'switch_active_tab\|cycle_active_tab\|add_tab_to_pane\|close_tab_in_pane' src/app_shell/main.cpp`。已知呼叫點包含 `handle_pane_command`（`:4642`）、`tab_strip_proc`（`:3924`）、`handle_context_menu`（`:4865`）、`close_tab_at_point`（`:3512`）、全域快捷鍵（`handle_global_command` `:4703`）。
5. `close_tab_at_point(HWND, AppState&, POINT)` `:3512-3525` 依賴跨 pane 的 `tab_item_at_point(panes, strip, point)`。**本票不動它**，留給 PD-196。

## Non-goals

- **不吸收 `core::add_tab`／`close_tab`／`set_active_tab`／`reorder_tab`／`move_tab`。** 那是 `docs/testing.md` 的唯一自動化測試接縫，`Pane` 只呼叫不吸收。這條是 PD-190 已確立的界線，本票不覆寫。
- 不改 tab id 的產生規則或唯一性範圍（仍是 Group 範圍）。
- 不動 `tab_context_menu_pane`／`tab_context_menu_tab_id` 這兩個 singleton（PD-190 已判定留在協調層）。
- 不動跨 pane 的 tab 拖曳（契約 (3)）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> **A `pane_index` parameter is a signal that the function belongs to that pane.**

> New non-trivial logic needs one focused runnable test or self-check.

`docs/tickets.md` 2026-09-04 界線：

> `PaneState` 的位址由 `core` 保證穩定（PD-184）並以單元測試把關；`TabState` 的位址**不保證**（`PaneState::tabs` 有 `push_back`／`erase`／`insert`），因此任何持有 `TabState*` 的設計都是錯的。

（`add_tab` 之後絕不可沿用先前取得的 `TabState*`／`TabState&`；每支搬移後都要重新確認這一點，`switch_active_tab` 現有程式碼就有兩次刻意的 `pane_state` 重取，不要在搬移時把它們簡化掉。）

> (b) `core` 的 pane 域純函式（`add_tab`／`close_tab`／`set_active_tab`／`reorder_tab`／`move_tab`／導覽歷史）——那是唯一的自動化測試接縫，`Pane` 只呼叫不吸收。

## Acceptance criteria

1. 四支皆為 `Pane` 成員，`main.cpp` 內無 `add_tab_to_pane`／`close_tab_in_pane`／`switch_active_tab`／`cycle_active_tab` 自由函式。
2. `src/core/model.cpp` 零改動。
3. 現有程式碼中每一處 Shell 呼叫後的「重取 `pane_state`」防護都保留（`switch_active_tab` 2 處、`add_tab_to_pane` 2 處）。
4. `Pane::add_tab` 無預設引數。
5. 零行為、零視覺變更。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
```

`tests/unit/pane_test.cpp` 新增：

- `test_add_tab_does_not_reuse_stale_tab_pointer` — 綁定一個有 3 個 tab 的 `PaneState`，`add_tab()` 後驗證 `active_tab()` 指向新元素且 `tabs.size() == 4`；刻意讓 `PaneState::tabs` 的 capacity 為 3 以強制重新配置。
- `test_close_last_tab_leaves_pane_consistent` — 關掉唯一的 tab 後 `active_tab()` 的行為與 `core::close_tab` 的既有語意一致。

`ctest` 既有的 `core` model 測試必須全數不變地通過（證明沒有吸收純函式）。

## 使用者實機檢查

1. 每個 pane 各自新增 tab（`+` 按鈕）→ 只影響該 pane。
2. Ctrl+Tab／Ctrl+Shift+Tab 在有焦點的 pane 內循環。
3. 中鍵點 tab、右鍵選單「關閉」、tab 上的 `x` 三種關法行為一致。
4. 關掉 pane 的最後一個 tab → 與改動前行為相同。
5. 切換 tab 時的資料夾導覽與存檔排程照舊（切 tab 後 5 秒內 session 檔更新）。
6. 跨 pane 拖曳 tab 仍正常（PD-110）。

## 交接區

（實作者填寫：`tab_strip_needs_refresh` 的實際簽章與 PD-196 刪除它的位置；`default_shell_location()` 呼叫端清單。）
