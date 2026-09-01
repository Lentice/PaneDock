# PD-164 — 把 realize／de-realize 決策抽成 `core` 的純函式

Phase 7 · architecture · Depends on: PD-005, PD-009, PD-093, PD-151, PD-155

- Source: 2026-09-01 `improve-codebase-architecture` 架構審查，結論由背景 Codex 唯讀核對（判定 PARTLY WRONG——「只有 runtime 檢查」不成立，見下）。
- Priority: MEDIUM——NFR-002 的記憶體上界只靠一條規則保證，而那條規則目前是 `apply_layout` 裡的內嵌條件式，沒有任何可對八種版型全組合驗證的測試。

## Outcome

`src/core` 新增純函式，輸入 Group 狀態與版型、輸出「哪些 pane 要 realize、哪些要 de-realize、哪些只要 re-navigate、哪些不動」。`apply_layout` 只執行這份計畫，不再自行判斷。NFR-002 的不變量從此可在 unit test 中對八種版型全組合斷言。

## 已確認的現況（2026-09-01 工作樹，經 Codex 唯讀核對）

- realize／de-realize 條件確實內嵌在 `apply_layout`：`src/app_shell/main.cpp:2948` 與 `:3016-3022`。
- `realize_startup_panes`（`main.cpp:3098`）是啟動流程協調函式，**不是**獨立的 realization policy reducer；它不能當作既有 seam 重用。
- `src/core/layout.cpp` 只計算矩形，不含 realization 政策。
- **架構報告原本寫「只有透過 runtime `live_view_count` 檢查」，這點不正確**：`tests/release/startup_frame_order_check.ps1:25` 也以 source pattern 檢查 realization 條件。
- `live_view_count`（`main.cpp:428` 一帶）只在 diagnostic mode 輸出，另有 shutdown assertion；它不是完整的 policy test。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` NFR-002：

> 記憶體由架構決定,不由語言決定。恰有一個條件保證上界:**只有可見 pane 的 active tab 持有 live `IExplorerBrowser`**。其餘 tab 僅以資料存在。

`docs/design-spec.md` §9.3：

> 5. 套用 active Group 的版型,**先 realize active pane 的 active tab**
> 6. 其餘 pane 延後 realize,避免被網路或離線路徑阻塞

`docs/design-spec.md` §9.1：

> | `core` | Group／pane／tab 資料模型、版型矩形計算、session 序列化與遷移 | **任何 HWND、COM 或 `windows.h`** |

`docs/adr/0002-stable-pane-identity-across-layout-switch.md`：

> 縮小會隱藏一個 pane(並依 NFR-002 de-realize 其 live view)而不搬移其 tab;放大回來時同一個 identity 帶著完整 tab 回歸。

`AGENTS.md`：

> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups.

`AGENTS.md`：

> Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs persist as data and are realized on activation. This is what keeps memory bounded.

`AGENTS.md`：

> Keep `src/core` free of HWND, COM and `windows.h`.

`docs/tickets.md` 已否決的方向：

> | 每個 tab 都保留 live `IExplorerBrowser` | `docs/design-spec.md` §NFR-002 | 這是記憶體無上界成長的唯一原因 |

## Files to read and trace first

- `src/app_shell/main.cpp:2757-3083`（`apply_layout` 全文，特別是 `:2948` 與 `:3016-3022` 的 realize／de-realize 條件）。
- `src/app_shell/main.cpp:3098`（`realize_startup_panes`）與 `main.cpp:5100`、`:6209` 兩處呼叫點。
- `src/app_shell/main.cpp` 的 `activate_group`、`set_layout`、`switch_active_tab`、`set_active_pane`。
- `src/core/layout.h/.cpp`：既有的 `compute_layout_rects` 與 `LayoutTemplate`，本票的函式應放在同一模組並沿用同樣的純函式風格。
- `src/core/model.h`：`GroupState`、`PaneState`、`TabState`。
- `src/explorer_host/live_view_count.h`：既有的 live view 計數契約。
- `tests/unit/core_layout_test.cpp`：既有測試風格。
- `tests/release/startup_frame_order_check.ps1`：現有 source-level realization 斷言，pattern 若因搬移失效必須更新。
- `docs/tickets/PD-093`（啟動延後 realize）、`PD-151`（stable pane identity）。

## Scope

1. 在 `src/core/layout.h/.cpp` 新增純函式，例如：

   ```cpp
   struct RealizationPlan final {
       std::vector<std::size_t> realize;     // 需要建立 live view
       std::vector<std::size_t> derealize;   // 需要 destroy live view
       std::vector<std::size_t> navigate;    // 已 live，只需 re-navigate
       std::vector<std::size_t> keep;        // 不動
   };

   RealizationPlan plan_realization(const GroupState& group,
                                    LayoutTemplate templ,
                                    std::span<const bool> currently_realized,
                                    bool startup_deferred);
   ```

   確切簽章由既有邏輯決定；原則是輸入全為值、無 HWND／COM，輸出可完整斷言。
2. `apply_layout` 改為先取得 plan、再執行。`realize_startup_panes` 的延後 realize 決策也改為向同一個函式索取 plan（`startup_deferred` 分支），不得留下第二套判斷。
3. 保留現有全部行為：`docs/design-spec.md §9.3` 的「先 realize active pane 的 active tab、其餘延後」；ADR-0002 的「隱藏 pane de-realize 但不搬 tab」；Group 切換 re-navigate 不 destroy。
4. 新增 `tests/unit/core_realization_plan_test.cpp`（或加入 `core_layout_test.cpp`）並註冊到 `tests/CMakeLists.txt`，至少涵蓋：
   - 八種版型各自的 plan，斷言 `realize.size() + navigate.size() + keep.size()` 不超過該版型的可見 pane 數 —— 即 NFR-002 的上界。
   - 4-pane → 1-pane 縮小：被隱藏的三個 pane 落在 `derealize`，其 tab 不變。
   - 1-pane → 4-pane 放大：三個 pane 落在 `realize`。
   - Group 切換（全部 pane 已 live）：全部落在 `navigate`，`derealize` 為空。
   - 啟動延後：只有 active pane 落在 `realize`，其餘落在延後集合。
5. `tests/release/startup_frame_order_check.ps1` 的 pattern 若因搬移失效，更新指向新位置；不得刪除該檢查。

## Non-goals

- 不改任何 realize／de-realize 的**時機或條件**；本票是純重構。
- 不動 `ExplorerHost::initialize`／`destroy` 內部或 §9.4 順序。
- 不把 `apply_layout` 的 `SetWindowPos`／`DeferWindowPos`／region／字型部分搬進 core（那些必須留在 app_shell）。
- 不改 PD-155 的 batch 結構或 PD-108 的 unchanged-pane skip。
- 不新增產品內計時儀器（`docs/tickets.md` 候選表已明確保留該方向）。
- 不重開「每個 tab 都保留 live `IExplorerBrowser`」。

## Acceptance Criteria

1. `plan_realization`（或等價命名）存在於 `src/core`，且 `grep -E "HWND|windows\.h|HRESULT|ComPtr" ` 對新增程式碼無結果。
2. `apply_layout` 與 `realize_startup_panes` 都只執行 plan，`main.cpp` 中不再有第二處獨立的 realize 判斷式。
3. 新測試涵蓋 Scope 第 4 點全部案例並通過；NFR-002 上界以斷言表達。
4. `ctest --test-dir build --output-on-failure` 全綠，含 `startup_frame_order_check` 與 `panedock_launch_smoke`。
5. 實機以 `--diagnostic` 啟動，四宮格 Group 切至 single 後，`live_view_count` 讀數與既有 build 一致。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
Select-String -Path src/core/layout.* -Pattern 'HWND|windows\.h|HRESULT|ComPtr'   # 必須無輸出
Select-String -Path src/app_shell/main.cpp -Pattern 'plan_realization'
```

## Handoff requirements

在 `## 交接區` 記錄：最終函式簽章、`apply_layout` 與 `realize_startup_panes` 各自的呼叫點、八種版型的 plan 斷言表、`startup_frame_order_check.ps1` 的 pattern 更新，以及 `--diagnostic` live view 讀數的前後比對。

## 交接區
