# PD-151 — pane 為穩定 identity，切換版型不搬移 tab

Phase 7 · core + app_shell · Depends on: PD-004, PD-005, PD-006, PD-009, PD-087

- Source: 使用者 2026-08-31 grilling session（目標原文：「每個 pane 有記錄自己的 tabs，不會因為 change pane layout (visible panes) 而導致 tabs 被移動到其他的 pane 中」）。
- Origin: 使用者在「CPU MPT」Group 的實測回報——四宮格時 pane-4 有 tab X/Y/Z，切到 1-pane 後 tabs 被搬進 pane-1；切回 4-pane 時又無法在 pane-4 看到原 tabs。
- Priority: HIGH——核心資料模型中 pane 語意的根本變更；影響所有版型切換、session 載入與 `is_valid` 不變式。

## Outcome

**pane 是穩定 identity（每個 Group 至多 4 個），永久擁有自己的 tabs。** 版型（layout template）只決定「**哪些** pane 可見、如何排列」，不決定「哪一個 tab 屬於哪一個 pane」。切換版型時：

- **縮小**：只隱藏多餘的 pane，**不搬移、不刪除**其 tabs；被隱藏的 pane 之 active tab 釋放 live `IExplorerBrowser`（de-realize）。
- **放大**：若該 pane identity 已存在（被隱藏過），則直接顯示、保留其原 tabs 與 active tab；只有**尚未存在**的 identity 才新增一個「以預設 location 開啟」的 tab。
- **active pane**：縮小後若原本 active 的 pane 落入「隱藏區」時，回退到可見列的第一個（identity-1）；不記憶跨縮放的 active pane。
- **divider ratio**：維持現狀，切換版型即重設為 0.5。

## 覆寫（override）聲明

本票**覆寫** `docs/design-spec.md` §FR-003 現行的「切換版型時，若新版型的 pane 數較少，超出的 pane 之 tab 依序併入保留的 pane；若較多，新增的 pane 以預設 location 開啟一個 tab」。自本票起改為上述「pane 穩定 identity、縮小只隱藏、放大才新增尚未存在的 identity」語意。覆寫依據（新證據）：

1. 併入式搬移違反使用者「tabs 留在原地」的心智模型——四宮格下 pane-4 的 tabs 切到 1-pane 後莫名出現在 pane-1，造成資料「錯置」感。
2. 併入式搬移使「縮小→放大」round trip 無法還原：被併入的 pane 在放大時被重建為全新的空白 pane，原內容已從原本的 identity 流失（本質是資料遺失感，儘管 tab 總量未少）。
3. 既有 `apply_layout` 的隱藏分支（`src/app_shell/main.cpp` 的 else 分支，PD-087 修法）本來就對隱藏 pane 呼叫 `destroy()`；穩定 identity 模型正是把「pane 終身」與「pane 是否可見」分開，與該已有行為一致。

本票是規格覆寫，非純 bug 修正；`docs/design-spec.md` §4.3／§FR-003 與 `CONTEXT.md`「pane」須一併更新，並新增 ADR 記錄此決策。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` FR-003（本票覆寫，見上）：
> 支援且僅支援八種版型:單一、左右、上下、左一右二、左二右一、上一下二、上二下一、四宮格。

`docs/design-spec.md` FR-005：
> 每個 pane 至少一個 tab。可新增、關閉、切換 tab。關閉 pane 的最後一個 tab 時，該 tab 導覽至預設 location 而非留下空 pane。

`docs/design-spec.md` §NFR-002：
> 只有可見 pane 的 active tab 持有 live `IExplorerBrowser`。其餘 tab 僅以資料存在。

`AGENTS.md`：
> 每個 `IExplorerBrowser` 必須 `Destroy`；所有 user data 採 atomic replace；**只有可見 pane 的 active tab 持有 live `IExplorerBrowser`**，Inactive tabs persist as data and are realized on activation。

`AGENTS.md`：
> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups。（——注意這指的是**切換 Group**；版型縮小仍要釋放隱藏 pane 的 view，這是 NFR-002 與 PD-087 的既有語意，不是本票要違反的對象。）

`docs/design-spec.md` §3.2：
> 任意遞迴 pane 分割 —— 已否決；pane 至多 4 個、不支援任意分割。

`docs/tickets.md` Agent 交付規則：
> 必須保持既有 build／CTest 可用；不得用關閉測試來取得綠燈。每個非平凡邏輯至少新增一個 focused runnable test。

## Files to read and trace first

- `docs/design-spec.md` §4.3、§FR-003（本票覆寫）、§FR-004、§FR-005、§NFR-002。
- `docs/tickets/PD-009-active-pane-and-layout-toggle.md`——保活式版型切換與 active pane 語意的既有紀錄。
- `docs/tickets/PD-087-layout-shrink-leaks-hidden-explorerhost.md`——縮小須釋放隱藏 pane view 的修法與理由。
- `src/core/model.h`——`GroupState`、`kMaxPaneCount`、`pane_count`、`is_valid`、`switch_layout` 宣告。
- `src/core/model.cpp`——`is_valid(GroupState)`、`switch_layout`、`pane_count`、`default_divider_ratios`。
- `src/app_shell/main.cpp`——`apply_layout` 的 `visible` 判定（一帶 `index < group.panes.size()`）；`set_layout`；`new_group_state`；`capture_locations`／`capture_pane_location`／`navigate_realized_panes`（都靠 `state.realized` 守護，隱藏 pane 因 de-realized 而安全）；Tab／F6 循環 active pane 的計數（一帶 `active_group(...).panes.size()`）。
- `tests/unit/core_model_test.cpp`——`group()` 測試 helper 與版型遷移測試。

## Scope

1. `src/core/model.h`：新增 `inline constexpr std::size_t kMaxPaneCount = 4;`。
2. `src/core/model.cpp` `is_valid(GroupState)`：把 `group.panes.size() != pane_count(group.layout_template)` 放寬為 `group.panes.size() < pane_count(group.layout_template) || group.panes.size() > kMaxPaneCount`。其餘（`divider_ratios.size() == divider_ratio_count(...)`、每 pane 至少一 tab、tab id 唯一、active 存在）不變。
3. `src/core/model.cpp` `switch_layout`：**移除**「縮小 → 依序併入殘留 pane + `resize`」路徑；改為 grow-only（`panes.size() < new_count` 才以 `new_pane_ids`/`new_tab_ids` append）；縮小不更動 panes（累積）。active_pane 若落入 `index >= new_count` 的隱藏區，回退到 `panes.front().id`。`divider_ratios` 以 `default_divider_ratios` 重設。
   - 移除上一版（併入式模型）為合併去重所加的 `fresh_unique_tab_id`／`used_ids` merge 區塊——該路徑已不存在。
4. `src/app_shell/main.cpp` `apply_layout`：決定「可見 pane」的判定由 `index < group.panes.size()` 改為 `index < panedock::core::pane_count(group.layout_template)`；隱藏分支（de-realize + hide）沿用。
5. `src/app_shell/main.cpp` Tab／F6 循環 active pane：計數由 `active_group(...).panes.size()` 改為 `panedock::core::pane_count(active_group(...).layout_template)`，避免把 active 切到隱藏 pane。
6. Rewrite `tests/unit/core_model_test.cpp`：`test_layout_migration_both_directions` → 改名為穩定 identity 版本（縮小保留 4 panes、tabs 不移動、active 回退到 identity-1；放大還原同一 identity 的 tabs）；既有 `test_layout_migration_dedups_cross_pane_tab_ids` → 改為驗證「跨 pane 相同 tab id 的 Group（CPU MPT 形狀）縮放後仍合法、tabs 不被移動」。

## Decisions（本票確認並採納）

- **pane 數上限 = 4**（`kMaxPaneCount`），對齊 `kExplorerCount` 與最大版型；不支援任意分割。
- **active pane 不記憶跨縮放的選擇**：縮小後回退到 identity-1；放大回來不會彈回原先的 pane。
- **divider ratio 切版型即重設**：除非使用者在實機驗收上反映比例也被搬移造成混淆，否則維持現狀。
- **session schema 不升版**：`panes.size()` 由「等於版型數」鬆綁為「≥版型數且 ≤4」；既有 session.json 的 Group 其 `panes.size() == pane_count(template)` 必成立，一律可載入；新舊檔格式相同，故 schema_version 維持 1。

## Non-goals

- 不支援任意遞迴 pane 分割；不新增「刪除 pane」操作。
- 不新增跨縮放記憶的 active pane；不把 divider ratio 綁到 pane identity 儲存。
- 不改 sidebar 摘要的 pane 數（維持欄位語意：Group 擁有的 pane identity 數與總 tab 數）。
- 不改變 Group 切換的保活行為（Group 切換仍 re-navigate 既有 pane HWND，不 destroy/recreate）。
- 不新增網路、第三方 runtime、polling timer 或新的 UI framework。
- 不把 `IExplorerBrowser` 的 realize-on-activation 原則推翻；隱藏 pane de-realize 仍遵守 NFR-002。

## Acceptance criteria

1. 用「CPU MPT」形狀（三宮格、跨 pane 有相同 tab id）的 Group：切到 1-pane → tabs 不移動；切回三宮格 → 原 identity 的 tabs 原封不動；`is_valid` 全程成立。
2. 任一 Group 由大版型縮到小版型再放大：隱藏的 identity 還原其原 tabs；新增的 identity（此前不存在者）僅含一個預設 location 的 tab。
3. active pane 在縮小後若其 identity 被隱藏，回退為 identity-1；Tab／F6 只會在「可見」pane 間循環。
4. 縮小後隱藏 pane 的 live view 已釋放（de-realized），放大後重新 realize 且導覽到當下資料夾（非過期資料）。
5. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。
6. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kMaxPaneCount|pane_count\(|switch_layout|panes.size\(\)" src\core src\app_shell tests\unit\core_model_test.cpp
git diff --check
```

`panedock_core_model_test` 收錄本票的核心語意（穩定 identity、跨 pane 相同 tab id、active 回退、縮放還原）。

## Handoff requirements

- 記錄 `is_valid` 放寬後的精確條件與 `switch_layout` 的 grow-only 邏輯。
- 記錄 `apply_layout` 可見列判定、Tab／F6 計數變更的 call sites。
- 記錄移除的 merge/dedup helper 與其對應的上一版 tests 的處理方式。
- 記錄 build、full CTest、`rg` 與 `git diff --check` 結果，以及 4-pane↔1-pane 來回切換的實機行為（若有真實桌面）。

## 交接區

<!-- 實作 agent 填寫，append-only -->

### 2026-08-31 實作交接

- `src/core/model.h` 增 `kMaxPaneCount = 4`；`src/core/model.cpp` 的 `is_valid` 鬆綁與 `switch_layout` 改為 grow-only（移除 merge 路徑與 `fresh_unique_tab_id`／`used_ids`）；`src/app_shell/main.cpp` 的 `apply_layout` 可見列判定、Tab／F6 計數改為 `pane_count(group.layout_template)`。
- `tests/unit/core_model_test.cpp` 改名為 `test_layout_migration_stable_pane_identity` 與 `test_layout_keeps_cross_pane_duplicate_tab_ids`，驗證縮小保留 identity、tabs 不移動、active 回退、縮放還原、跨 pane 相同 tab id 合法。
- `docs/design-spec.md` §FR-003 已改寫為穩定 identity 語意；`CONTEXT.md`「pane」已改定義；`docs/adr/0002` 記錄本決策。
- 自動檢查：`cmake --build build` PASS；`ctest --test-dir build --output-on-failure` 11/11 PASS（含 `panedock_launch_smoke`）。
- 尚未在真實桌面做 4-pane↔1-pane UI 手動驗收；需有環境後補做 acceptance criterion 3/4 的視覺確認。
