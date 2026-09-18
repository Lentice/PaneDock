# PD-211 — PaneDock 自己的 popup 選單必須持有 `ShellCallScope`

Phase 7 · switching path robustness · Depends on: PD-171, PD-205

- Source: 2026-09-18 第二輪切換路徑稽核（Claude finding 3）。機制 CONFIRMED，
  可達性 PLAUSIBLE。作者已複驗四個呼叫點皆無 scope，且對照組確實有。
- Priority: HIGH——最壞後果是**資料遺失**：使用者在 Group 3 上按 Delete，
  刪掉的是 Group 5。

## 根本原因

`TrackPopupMenu` 會 pump 本執行緒的訊息迴圈。整個專案的重入防護前提是
「會 pump 的地方 `shell_call_depth != 0`」，`main_window_proc`（`:3598-3612`）
才能把 `WM_COMMAND`／`kDragHoverMessage`／滑鼠訊息延後。

對照組**有**做對：`ExplorerHost::show_folder_context_menu_at` 在
`TrackPopupMenuEx`（`src/explorer_host/explorer_host.cpp:434`）之前就持有
`ShellCallScope`（`:382`），所以 Shell context menu 開著時 depth 不為 0。

PaneDock 自己的四個選單**一個都沒有包**：

| 選單 | 位置 |
|---|---|
| view mode | `src/app_shell/pane.cpp:1050` |
| pinned locations | `src/app_shell/pane.cpp:1117` |
| tab context | `src/app_shell/pane.cpp:1149` |
| sidebar group context | `src/app_shell/main.cpp:3416` |

選單 pump 期間 depth 是 0，所以延遲機制**不在生效**，任何 posted 訊息都會在
選單的巢狀迴圈裡被派送。這直接打在 `apply_layout` 自己寫下的載重不變量上
（`main.cpp:1696-1700`）：

> It is sound only because add_group/delete_group can be reached solely through
> WM_COMMAND, and the main window defers WM_COMMAND while shell_call_depth != 0

### 可觸發的事件序列

1. 慢速導覽期間使用者連點 sidebar → `WM_COMMAND` 被 hold
   （`main.cpp:3599`，PD-205 的佇列）。
2. Shell 呼叫結束 → `finish_shell_call`（`:551`）在 depth 歸零時 flush →
   `PostMessageW(kDeferredCommandMessage)`。
3. 此時已有一個 PaneDock 選單開著（例如 flush 是由選單 pump 內派送的
   `kDeferredLayoutMessage` → `apply_layout` 的 `ShellCallScope` 解開所觸發）。
4. 那則 posted 訊息在選單 pump 裡被派送 → `SendMessageW(WM_COMMAND)`
   （`:3635`）→ `handle_sidebar_command`（`:3257`）→ `activate_group` →
   **完整的 Group transition 在選單開著時重入執行**。

破壞性後果在 sidebar group context menu（`main.cpp:3358-3425`）最明顯：
`index` 在選單開啟**前**解析，選單關閉後
`SendMessageW(WM_COMMAND, kDeleteGroupId)` → `delete_group`（`:2152`）
**重新解析當前選取**。重播的點擊已經把選取換掉了 → 刪錯 Group。

### 已排除的鄰近疑慮

`pane.cpp:1129` 的 `const auto &tabs = pane_state()->tabs;` 與 `target_tab`
iterator 只在 `TrackPopupMenu` **之前**使用；選單之後只用按值複製的
`tab_id`。這一處**沒有**懸空引用，不要順手改。

## 單執行緒前提（決定修法形狀的依據）

Windows 訊息在單一執行緒上是循序派送的，不存在兩個處理程序真正同時執行。
因此本缺陷不是 race condition，而是**重入**：巢狀的訊息迴圈在外層還沒返回時
就派送了下一則訊息。這決定了修法形狀：

- 需要的是**一個深度計數器 + 延遲清單**，也就是專案已有的
  `ShellCallScope` / `shell_call_depth`。不需要鎖、不需要原子、不需要
  「選單期間的狀態快照」。
- 判準因此可以寫得很簡單且完整：**任何會 pump 訊息迴圈的呼叫都必須持有
  `ShellCallScope`**。不需要逐一分析「這個選單會不會剛好撞到那個訊息」，
  只要讓前提成立，既有的延遲清單就把整類問題關掉了。
- 選單關閉後才執行的 `SendMessageW` **不**包進 scope：那時候我們要的正是
  它能正常執行。

## 要讀與追的檔案

- `src/explorer_host/explorer_host.cpp:375-440`：正確的對照範本
  （`ShellCallScope` 在 `:382`，`TrackPopupMenuEx` 在 `:434`，
  `context_menu_active_` 另有自己的非重入旗標）。
- `src/app_shell/main.cpp`：`ShellCallScope`（`:558` 附近）、
  `finish_shell_call`（`:551`）、`main_window_proc` 的延遲分支
  （`:3598-3612`）、`kDeferredCommandMessage` 分支（`:3625-3640`）、
  `handle_sidebar_command`（`:3257`）、sidebar group context menu
  （`:3358-3425`）、`delete_group`（`:2152`，它的 `MessageBoxW` 註解描述的是
  同一類破口）、`apply_layout` 的載重註解（`:1696-1700`）。
- `src/app_shell/pane.h`：`Pane::ShellCall` 的定義（`Pane` 側等價物）。
- `src/app_shell/pane.cpp`：`:1050`、`:1117`、`:1149` 三個選單，以及它們
  選單後的 `SendMessageW` / `close_tab` / `close_tabs` 呼叫。

## 範圍

把四個 `TrackPopupMenu` 呼叫**本身**各自包進既有的 scope，完全比照
`explorer_host.cpp:382`：

1. `src/app_shell/pane.cpp:1050`（view mode）→ `Pane::ShellCall`
2. `src/app_shell/pane.cpp:1117`（pinned locations）→ `Pane::ShellCall`
3. `src/app_shell/pane.cpp:1149`（tab context）→ `Pane::ShellCall`
4. `src/app_shell/main.cpp:3416`（sidebar group context）→ `ShellCallScope`

scope 只涵蓋 `TrackPopupMenu` 那一個呼叫，**不涵蓋**後面的 `DestroyMenu`、
`SendMessageW`、`close_tab`／`close_tabs`／`delete_group`。

每個呼叫點在 scope 離開後必須檢查 `is_shutting_down()`／`active()` 才繼續：
scope 解開時 `finish_shell_call` 可能 post 出
`kDeferredShutdownMessage`，而選單期間收到的 `WM_CLOSE` 會讓
`shutdown_deferred` 為真。`main.cpp:3422` 已有
`if (state.is_shutting_down()) return true;`；`pane.cpp:1054`、`:1121` 已有
`if (!active() || command == 0) return;`；`pane.cpp:1152` 的 tab context
**目前沒有**，需要補一個 `if (!active()) return;`。

## 非目標

- 不新增機制、不新增旗標。`ShellCallScope` 與 `Pane::ShellCall` 都已存在。
- 不改延遲訊息清單的內容（哪些訊息要被延遲）。
- 不改 `delete_group` 的「先解析 id 再開 MessageBoxW」寫法——那個修法是對的，
  本票讓它的前提也成立。
- 不動 `pane.cpp:1129` 的 `tabs` 參考（見上方已排除）。
- 不為 `TrackPopupMenu` 加自己的非重入旗標（`context_menu_active_` 的類比）：
  選單天生不會同時開兩個，而重入保護由 depth 提供。

## 驗收條件

1. 四個 popup 選單開啟期間 `shell_call_depth != 0`。
2. 選單期間抵達的 `WM_COMMAND`／`kDragHoverMessage`／滑鼠訊息被延遲，
   選單關閉後才派送。
3. 選單關閉後的命令執行行為不變（view mode 切換、pin、close tab／close
   tabs、Group 命令）。
4. 選單期間收到 `WM_CLOSE` 時，scope 離開後每個呼叫點都不再繼續動 model。
5. 既有測試全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

外加：`tests/release/shell_reentry_gate_check.ps1` 已經是這條不變量的靜態
把關者。**新增四條斷言**，要求每個 `TrackPopupMenu(` 呼叫的前文都出現對應的
`ShellCall`／`ShellCallScope`。這比執行期測試更適合此缺陷——選單是 modal，
無法自動化，而靜態比對正好能擋住「日後新增第五個選單又忘了包」。

## 交接區

2026-09-18 實作完成。

- 四個 `TrackPopupMenu` 各自包進既有 scope，只涵蓋呼叫本身：
  `pane.cpp` 的 view mode／pinned／tab context 用 `ShellCall shell_call(pane_host())`，
  `main.cpp` 的 sidebar group context 用 `ShellCallScope shell_call(state)`。
  三處 `const int command = ...` 因此改為先宣告 `int command = 0;` 再在
  scope 內賦值。
- tab context menu 依 ticket 要求補上選單後的 `if (!active()) return;`
  （另兩個 pane 選單與 sidebar 選單本來就有對應檢查）。
- **靜態把關**：`tests/release/shell_reentry_gate_check.ps1` 新增一段，對
  `main.cpp` 與 `pane.cpp` 內每一個 `TrackPopupMenu(Ex)?(` 呼叫，檢查其前
  400 字元內出現 `(ShellCallScope|ShellCall) shell_call(`，並斷言呼叫點數量
  不少於 4。選單是 modal、無法自動化驅動，而「第五個選單忘了包」的失敗是
  靜默的，所以這裡用靜態比對而非執行期測試。
  已用反證確認有效：移掉 view mode 選單的 scope 後
  `panedock_shell_reentry_gate` 失敗，改回即通過。
- 未動 `pane.cpp` 的 `const auto &tabs = pane_state()->tabs;`
  （ticket 已排除：它只在 `TrackPopupMenu` 之前使用）。
- `ctest`：34/34 通過。人工複驗（尚未執行）：慢速導覽期間開啟 sidebar group
  context menu，選單期間連點其他 Group，關閉選單後按 Delete，應刪除選單開啟時
  所指的那個 Group。
