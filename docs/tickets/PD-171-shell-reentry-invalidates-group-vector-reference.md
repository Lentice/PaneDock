# PD-171 — `apply_layout` 持有的 Group reference 在 Shell 呼叫重入時可能失效（dangling reference）

Phase 7 · app_shell · Depends on: PD-140

## 來源

2026-09-03 三方稽核（Codex 獨立提出；OpenCode 判定此區「乾淨」，兩者結論不同，本票採 fork 逐行核對後的結果）。判定為 CONFIRMED-NEW 的結構性風險：與 `PD-140` 當初被接受時採用的同一證據標準（原始碼推理，無 live repro）。

## 背景與現況

`apply_layout`（`src/app_shell/main.cpp:2801` 起）在函式一開始取得一個指向 `ApplicationState::groups` vector 元素的 **reference**，然後在整段迴圈中持續使用它：

```cpp
auto& group = active_group(state);                       // :2801 — reference 進 std::vector
const auto rects = layout_rects(window, state, group);   // :2802
const auto realization_plan = panedock::core::plan_realization(
    group, group.layout_template, state.realized, realization_mode);   // :2809-2810
...
for (std::size_t index = 0; index < state.explorers.size(); ++index) {  // :2827
    const bool visible =
        index < panedock::core::pane_count(group.layout_template);      // :2830 — 迴圈內持續使用
    ...
    // 迴圈內多處 ShellCallScope 包裹的 Shell 呼叫：:2964, :2978, :3010, :3014 ...
}
```

迴圈內的 `ShellCallScope`（`main.cpp:612-628`）包裹的 `IExplorerBrowser::Initialize`/`navigate`/`Destroy` 等呼叫**會重入 STA 訊息迴圈**（這正是 `PD-140` 建立 `ShellCallScope` 的前提，見該票 Priority 段落原文）。而 `PD-140` 的 gate 只讓「關閉／`WM_ENDSESSION`」在重入期間延後，**沒有**阻止一般的 `WM_COMMAND`（例如 New Group、Delete Group 按鈕）在重入期間執行。這些命令最終呼叫：

- `panedock::core::add_group` → `application.groups.push_back(...)`（`src/core/model.cpp:152`）
- `panedock::core::delete_group` → `application.groups.erase(group)`（`src/core/model.cpp:190`）
- `panedock::core::reorder_group` → `erase` + `insert`（`src/core/model.cpp:207-210`）

`push_back` 可能造成 vector reallocate，`erase`/`insert` 直接使 iterator/reference 失效。任何一種發生後，外層 `apply_layout` 迴圈接下來對 `group` 的存取就是對已釋放記憶體的存取（UB），可能表現為當機、切換到錯誤的 Group，或畫面卡住。

## 為什麼這是真的問題

`AGENTS.md` 明確把這類路徑列為必須處理的風險：

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`PD-140` 已經接受了「Shell 呼叫會重入訊息迴圈」這個前提並據此建立了 `shell_call_depth` 機制，但它的 scope 明確只覆蓋 shutdown：

> Make close requests during any app-owned `ExplorerHost` call defer until the outermost Shell call returns, and ignore queued interaction work while that close is pending.

也就是「關閉」被 gate 住了，但「在 Shell 呼叫重入期間執行另一個會改動 `groups` vector 的一般命令」沒有被 gate。這是 `PD-140` 留下的同類缺口，不是重複。

## Fix 方向

沿用 `PD-140` 已經建立的同一套 defer 模式，套用到會改動 `groups`/`tabs` vector 的一般命令上，二選一（實作者依實測程式碼結構選較小改動者，並在交接區記錄理由）：

- **A（推薦，改動最小且對稱於 PD-140）**：在 `shell_call_depth != 0` 期間，把會改動 Group/Tab vector 的 `WM_COMMAND` 延後（`PostMessageW` 一個既有慣例的 `WM_APP` 私有訊息，比照 `kDeferredLayoutMessage`/`kDragHoverMessage` 的既有模式），等最外層 Shell 呼叫返回後才真正執行。
- **B**：把 `apply_layout` 改為不跨 Shell 呼叫持有 reference——每次需要時用 id 重新查一次（`find_id`），並在 Shell 呼叫返回後重新驗證該 Group 仍然存在，不存在就安全中止本次 layout pass。

若選 A，必須確認延後的命令在 replay 時仍然合法（例如被刪掉的 Group id 已不存在時要安全 no-op）。若選 B，必須確認 `apply_layout` 現有的 `LayoutPassScope`（`main.cpp:2729-2748`）延後重排機制不會與新的中止路徑衝突。

## 綁定限制（引用）

- `AGENTS.md`：「Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.」
- `AGENTS.md`：「Read the relevant spec section and trace every caller before touching shared code. A guard in the shared function is a smaller diff than a guard in every caller」——若採 A，gate 應放在共用的命令分派點，不要在每個 handler 各加一份。
- `AGENTS.md`：「Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups」——fix 不得改變這條既有保證。
- `AGENTS.md`：「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」

## 檔案與範圍

- `src/app_shell/main.cpp`：`apply_layout`（:2801 起整段，特別是 :2801 的 `auto& group` 與迴圈內 :2964/:2978/:3010/:3014 的 `ShellCallScope`）、`LayoutPassScope`（:2729-2748）、`ShellCallScope`（:612-628）、`finish_shell_call`（:593-610）、`add_group`/`delete_group`/`move_group` 的 `WM_COMMAND` handler 呼叫點（:3225 一帶起）、`window_proc` 的訊息 gate（:5097-5103 一帶）。
- `src/core/model.cpp`：`add_group`（:147-157）、`delete_group`（:184-198）、`reorder_group`（:200-212）。
- `docs/tickets/PD-140-shell-call-reentry-shutdown-gate.md`（既有機制與其明確的 scope 邊界）。

## Scope

1. 確認並在交接區列出：目前哪些 `WM_COMMAND`/互動路徑會在 `shell_call_depth != 0` 期間改動 `groups` 或某個 pane 的 `tabs` vector。
2. 依 Fix 方向 A 或 B 實作 gate/重查，並記錄選擇理由。
3. 加上一個聚焦 self-check：可用 `core` 的純函式測試驗證「Group 被刪除後以 id 重查會安全失敗」，或用 source-level invariant check（比照 `tests/release/shell_reentry_gate_check.ps1` 的既有做法）驗證 `apply_layout` 不再跨 Shell 呼叫持有 vector reference。

## Non-goals

- 不重新設計 `PD-140` 既有的 shutdown defer 機制本身（該機制的另一個獨立缺陷見 PD-172）。
- 不把 Shell 呼叫移到背景執行緒（違反 `docs/design-spec.md §9.2`）。
- 不改變 session schema 或 Group/Tab 的資料模型欄位。
- 不要求提供 live repro 作為本票的前置條件——`PD-140` 當初也是以原始碼推理立票；但若實作過程中取得實機重現條件，請寫進交接區。

## Acceptance Criteria

1. `apply_layout` 在任何一次 `ShellCallScope` 包裹的呼叫返回後，都不會再使用一個可能已失效的 `groups` vector reference（由 self-check 或 source-level invariant check 證明）。
2. 在 Shell 呼叫重入期間送出的 Group 新增/刪除/排序命令，要嘛被安全延後後正確執行，要嘛安全 no-op，不會造成當機或切到錯誤的 Group。
3. Group 切換仍然保持 live view 不重建 pane HWND（`AGENTS.md` 既有保證，由既有測試涵蓋範圍證明不回歸）。
4. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "auto& group = active_group|ShellCallScope|shell_call_depth" src/app_shell/main.cpp
rg -n "groups\.(push_back|erase|insert)" src/core/model.cpp
```

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-09-03 實作交接

- 採 Fix A，因為 `PD-140` 已在同一個 STA message loop 建立 `ShellCallScope`／`PostMessageW` defer 慣例；在共用入口加 gate 比讓 `apply_layout` 每一個 Shell call 重新查 Group 更小，也保留既有 live `IExplorerBrowser` 與 pane HWND。
- `window_proc` 在 `shell_call_depth != 0` 時把 `WM_COMMAND` 轉成 `kDeferredCommandMessage`，把 `kTabStripSelectionMessage` 轉成 `kDeferredTabSelectionMessage`；私有訊息只有在 depth 為 0 且未進入 shutdown 時以 `SendMessageW` replay，若再次重入則繼續 post，shutdown gate 則安全丟棄。
- 互動路徑核對：Group 新增／複製／刪除／排序來自主視窗 `WM_COMMAND`；Group 選取來自 listbox `WM_COMMAND`；Group reorder 的 `group_list_proc` `WM_LBUTTONUP`；Tab 新增／切換來自 `tab_strip_proc` 發送的 `kTabStripSelectionMessage`；Tab reorder／跨 pane 搬移來自 `tab_strip_proc` `WM_LBUTTONUP`；Tab 關閉來自主視窗 `WM_PARENTNOTIFY` 中鍵與 tab context-menu 的 `WM_COMMAND`；splitter/sidebar 雙擊與拖曳的主視窗 mouse message 也延後。Owner-draw button 的 `WM_COMMAND`、context menu replay 與 drag-hover 經同一個主視窗 gate；不會在 Shell call 中改動 `groups` 或 pane `tabs` vector。
- 因此 `apply_layout` 的 `auto& group = active_group(state)` 可在既有 pass 中保留：所有會使 `ApplicationState::groups` 或 pane `tabs` reference 失效的 app-owned interaction 都被阻止於 `shell_call_depth != 0`，只在外層 Shell call 返回後 replay；Replay 內的既有 id/index 驗證會將已不存在的目標安全 no-op。
- 新增 `tests/release/shell_reentry_gate_check.ps1` 的 source-level invariant，確認 deferred command/tab message、Group/Tab mouse defer，及主視窗 gate 位於 message `switch` 前；此為靜態不變式檢查，不宣稱是真實 Shell extension re-entry 的 runtime proof。未改動 `src/core`、session schema、thread model 或 shutdown reducer。
- Agent checks：focused `powershell -NoProfile -ExecutionPolicy Bypass -File tests/release/shell_reentry_gate_check.ps1` PASS；Release configure PASS；`cmake --build build` PASS；受限 token 執行完整 CTest 首次只有 `panedock_launch_smoke` 因關閉後程序未退出而 FAIL，依既有 `PD-167`／`PD-140` 交接的 session storage 權限做 elevated rerun 後 `ctest --test-dir build --output-on-failure` 17/17 PASS；`git diff --check` PASS。未執行需要真實 Shell extension 的人工 re-entry 壓力測試。
