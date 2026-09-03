# PD-173 — 拖曳進行中關閉：OLE 拖曳生命週期沒有納入任何 shell-call/shutdown gate

Phase 7 · app_shell / core · Depends on: PD-090, PD-140, PD-162

## 來源

2026-09-03 三方稽核（Codex 獨立提出）。經 fork 對照現有原始碼核對，判定為 CONFIRMED-NEW，並修正了 Codex 原始描述的一項事實：專案內**沒有** `DoDragDrop` 呼叫（拖出行為完全由原生 `IShellView` 提供），PaneDock 只實作自家 chrome 的 `IDropTarget`。缺口本身成立。

## 背景與現況

PaneDock 的拖放相關程式碼有兩塊：

1. **自家 chrome 的放置目標**：`DragHoverTarget`（`src/app_shell/main.cpp:270-388`）實作 `IDropTarget`，註冊在側邊欄與 tab strip（`RegisterDragDrop`，`main.cpp:3398`）。它的 `DragEnter`（:309）、`DragOver`（:322）、`DragLeave`（:335）、`Drop`（:340）**都沒有觸碰 `shell_call_depth`**，也沒有設定任何「拖曳進行中」的狀態旗標。
2. **pane 內的原生拖出**：由 `IExplorerBrowser`/`IShellView` 自己處理，PaneDock 沒有參與，因此 app 完全不知道使用者正在從某個 pane 拖檔案出去。

同時，`close_requested`（`src/core/shutdown.cpp:7-18`）的判斷條件裡完全沒有任何拖曳相關狀態：

```cpp
case ShutdownEvent::close_requested:
    if (state_.closing_ || state_.quit_requested || ... ) return ShutdownAction::none;
    if (state_.file_operation_in_progress) {          // ← 只認得「檔案操作進行中」
        state_.close_after_file_operation = true;
        return ShutdownAction::prompt_transfer;
    }
    state_.shutdown_deferred = true;
    return ShutdownAction::defer;
```

也就是說：拖曳進行中收到關閉請求時，`shell_call_depth` 通常是 0（拖曳本身沒有進 `ShellCallScope`），reducer 會直接走 `defer` → 隨即 `deferred_shutdown_ready` 成功 → save → `destroy_explorers`（`main.cpp:2720-2727`）銷毀所有 live view、`revoke_drag_hover_targets` 撤銷放置目標——**而使用者手上還拖著檔案、來源 `IDataObject` 與原生拖曳 session 仍然存活**。

`PD-090`（已完成）只把懸停自動切換的「動作本身」改成 `PostMessageW` 延後，它的 scope 與交接區完全沒有涵蓋「拖曳期間收到關閉」這件事。`PD-123`（複製進行中關閉）處理的是 `IFileOperation` 進行中，不是拖曳中。

## 為什麼這是真的問題

`AGENTS.md` 把拖曳點名為專案自己認定的最高風險重入場景：

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

在原生拖曳 session 還活著時銷毀來源 view 與撤銷放置目標，可能讓拖曳停在半途（放不下去、游標卡在拖曳狀態）、讓 Shell 端對已銷毀物件回呼，或直接當機。`PD-140` 為此建立的 gate 依賴 `shell_call_depth`，但拖曳這條路徑從來沒有把深度加上去，所以那個 gate 對拖曳完全沒有作用——這是 `PD-140` 機制的覆蓋缺口，不是重複。

## Fix 方向

比照 `file_operation_in_progress` 已經被 reducer 認得的既有模式（`shutdown.cpp:13-16`、`:85-91`），為「拖曳進行中」加上對稱的狀態與 gate：

1. 在 `core` 的 `ShutdownState` 增加一個 `drag_in_progress` 布林與對應的 `drag_started`/`drag_finished` 事件（純資料，不含 HWND/COM，維持 `core` 的測試 seam）。
2. `DragHoverTarget::DragEnter` 設定、`DragLeave`/`Drop` 清除該狀態（注意：`DragEnter`/`DragLeave` 可能在多個放置目標間交替觸發，需要用計數或明確的「目前有幾個目標處於拖曳中」語意，避免在 tab strip 與側邊欄之間移動時誤判為拖曳結束；實作者需在交接區說明選用的語意與理由）。
3. `close_requested`/`end_session` 在 `drag_in_progress` 時延後關閉（而不是立刻 save/destroy），等拖曳結束後再繼續既有序列。
4. 針對「原生 pane 內拖出」這條 app 看不到的路徑，在交接區明確記錄目前能偵測到的邊界（例如是否可透過 `IExplorerBrowser` 的事件或 `IDropTarget` 攔截得知），若確認無法偵測，就把它列為本票已知的殘餘風險，並說明為何不影響本票要修的那條路徑。

## 綁定限制（引用）

- `AGENTS.md`：「Shell APIs re-enter our message loop during drag...Host-side locking and shutdown sequencing must be reentrancy-safe.」
- `AGENTS.md`：「Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.」
- `AGENTS.md`：「Keep `src/core` free of HWND, COM and `windows.h`.」——新增的狀態與事件放 `core`，`IDropTarget` 實作只負責發事件。
- `docs/design-spec.md §9.4`：關閉序列固定為 save → destroy views → destroy parent → exit loop，本票只改變「何時開始」，不改變序列本身。
- `AGENTS.md`：「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」——完全比照 `file_operation_in_progress` 的既有模式。

## 檔案與範圍

- `src/core/shutdown.h/.cpp`：`ShutdownState`、`ShutdownEvent`、`close_requested`（:7-18）、`end_session`（:20-35）、`file_operation_*` 事件（:74-91，作為要比照的既有模式）。
- `src/app_shell/main.cpp`：`DragHoverTarget`（:270-388，特別是 `DragEnter` :309、`DragOver` :322、`DragLeave` :335、`Drop` :340）、`register_tab_drag_hover_targets`（:3398 一帶的 `RegisterDragDrop`）、`revoke_drag_hover_targets`（:641 一帶）、`destroy_explorers`（:2720-2727）、`begin_shutdown`。
- `tests/`：既有 `panedock_core_shutdown_test`。
- `docs/tickets/PD-090-drag-hover-group-switch-reenters-ole-drag-loop.md`、`PD-123-close-during-shell-copy-validation.md`（`IFileOperation` 版本的同類問題與其既有解法）、`PD-140-shell-call-reentry-shutdown-gate.md`。

## Scope

1. 在 `core` 增加拖曳進行中的狀態與事件，並讓 `close_requested`/`end_session` 在該狀態下延後關閉。
2. 在 `DragHoverTarget` 的 `IDropTarget` 方法發出對應事件，處理多目標交替 enter/leave 的計數語意。
3. 拖曳結束（`Drop` 或取消）後，若期間有待處理的關閉請求，繼續既有的關閉序列。
4. 在 `panedock_core_shutdown_test` 新增 reducer 測試：拖曳中 `close_requested` 不得回傳會立即 save/destroy 的 action；拖曳結束後關閉序列必須能繼續。

## Non-goals

- 不實作 `DoDragDrop`（拖出仍完全由原生 `IShellView` 負責），也不接管原生拖曳行為。
- 不重新設計 `PD-090` 的懸停延後機制。
- 不處理 `PD-123` 已經涵蓋的 `IFileOperation` 進行中關閉。
- 不為「app 偵測不到的原生 pane 內拖出」發明偵測手段——查證後若確認無法偵測，記錄為殘餘風險即可。

## Acceptance Criteria

1. 新增的 reducer 測試證明：拖曳進行中的 `close_requested`/`end_session` 不會立即進入 save/destroy views。
2. 拖曳結束後（`Drop` 或 `DragLeave` 取消），待處理的關閉請求會被正確繼續，且仍遵守 §9.4 的序列。
3. 在多個放置目標間移動滑鼠（側邊欄 ↔ tab strip）不會讓拖曳狀態誤判為已結束（由測試或交接區記錄的驗證方式證明）。
4. 既有拖曳懸停自動切換（`PD-034`/`PD-090`）與跨 Group 拖放（`PD-122`）行為不回歸。
5. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "DragEnter|DragLeave|Drop\(|RegisterDragDrop|RevokeDragDrop|drag_in_progress" src/app_shell/main.cpp src/core/shutdown.cpp
```

> **驗證政策提醒：** 真實的「拖曳中按關閉」需要按住滑鼠持續操作，屬於使用者實機驗證範圍，不要用 computer-use 工具連續操作（見 `docs/testing.md` 與專案既有慣例）。Agent 端以 reducer 測試與 source-level 檢查為證據，並在交接區寫清楚哪幾項留給使用者實機驗證。

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-09-03 — 實作交接

- `src/core/shutdown.h/.cpp` 新增 `drag_started`／`drag_finished` 事件、`drag_in_progress` 與 active-target count。`close_requested`／`end_session` 在拖曳中仍記錄 deferred shutdown 並回傳 `defer`；若 continuation 在拖曳中被 message loop 取走，`deferred_shutdown_ready` 會清除 queued flag 而等待拖曳結束。最後一個 target 結束後才重新使用既有 `run_shutdown_action` continuation，因此 save → destroy views → destroy window 的 §9.4 序列不變。
- `DragHoverTarget` 為每個 `IDropTarget` 保留 `drag_active_`，避免重複通知；sidebar 與各 tab strip 共用 core count，讓 target 交接期間只要仍有 target active 就維持 `drag_in_progress`。`Drop` 與取消用的 `DragLeave` 都會完成記帳。未新增 timer、thread 或 `DoDragDrop`。
- 本票只涵蓋 app 自己註冊的 sidebar/tab-strip `IDropTarget`。pane 內拖出由原生 `IShellView`／ExplorerBrowser 管理，app 沒有可觀察的 `DoDragDrop` 或 pane drop-target 邊界，故這條 native drag-out 仍是已知殘餘風險；它不影響本票修正的 app-owned drop-target 關閉路徑。
- 驗證：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`、`cmake --build build`、`ctest --test-dir build --output-on-failure`（17/17 通過），以及票面指定的 `rg` source-level check 通過。第一次受 sandbox 限制的 CTest launch smoke 因 `%LOCALAPPDATA%\PaneDock` 不可寫而留下關閉等待，改用可寫 session storage 的權限重跑後通過。
- 依驗證政策，未用 computer-use 執行持續按住滑鼠的實機拖曳關閉測試；sidebar/tab-strip 的真實拖曳期間按 Close、Drop 與取消 DragLeave 仍留給使用者在 Windows 10/11 目標環境驗證。
