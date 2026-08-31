# PD-150 — Group row 按 F2 開始 rename

Phase 7 · app_shell · Depends on: PD-017, PD-021, PD-023

## Goal

讓使用者在 sidebar 的 Group row 取得鍵盤焦點時按 `F2`，直接進入既有的 Group inline rename；Shell view 取得焦點時的 `F2` 維持 Windows 原生檔案 rename。

## 已確認的產品決策

1. `Group row` 是 sidebar Group list 中代表一個 Group 的可選取列；它不是新的 domain object。
2. 只有 `GetFocus()` 位於 Group list 時，plain `F2` 才啟動 Group rename。
3. Shell view、address bar、tab strip 或其他控制項取得焦點時，不啟動 Group rename；Shell view 的 F2 仍由 `IExplorerBrowser` accelerator 路徑處理。
4. 重用 `Sidebar::begin_rename()`，保留現有 Enter commit、Escape／失焦 cancel、`kRenameCommitMessage`、`core::rename_group`、sidebar refresh 與 session save 行為。
5. 這是可逆的訊息路由修正，不建立 ADR。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.8：
> pane 內部的一切互動由 Shell view 處理:多選手勢、右鍵選單、拖放、就地重新命名、鍵盤操作。

`docs/design-spec.md` FR-001：
> 使用者可新增、重新命名、複製、刪除、重新排序 Group。

`docs/design-spec.md` FR-014：
> 提供 pane 間移動焦點、切換 tab、新增／關閉 tab、上層導覽的快速鍵。快速鍵一律送往 active pane。

`docs/development.md` Architecture rules：
> `app_shell` | WinMain, STA init, message loop, main window, command routing to the active pane | Model computation, Shell calls

`docs/development.md` Change workflow：
> Make the smallest change that satisfies the acceptance criteria. Reuse before adding.

`AGENTS.md`：
> Keep `src/core` free of HWND, COM and `windows.h`.

`AGENTS.md`：
> Event-driven idle path only. No busy loops, no polling timers.

`AGENTS.md`：
> App UI text must be English. No Chinese strings ship in the binary.

`docs/tickets.md` Agent 交付規則：
> UI ticket 的 Agent checks 應驗證建置、視窗生命週期、狀態資料、訊息與可測的 Win32 結果；視覺人工驗證不屬於本追蹤表，屬於 `docs/testing.md` 的原型驗收協定。

`docs/design-spec.md` §12.5：
> 否決。對 live Shell view 進行 UIAutomation／WinAppDriver 測試極易 flaky，維護成本高於其訊號價值。

## Files to read and trace first

- `docs/design-spec.md` §4.8、FR-001、FR-014、§9.1、§12.4-§12.5。
- `docs/development.md` Architecture rules、UI language、Change workflow；`docs/testing.md` Automated checks、Single seam 與人工驗證規則。
- `docs/tickets/PD-017-group-sidebar.md`——既有 Group mutation、`Sidebar::begin_rename()` 與 inline editor 決策。
- `docs/tickets/PD-021-active-pane-keyboard-shortcuts.md`——message loop 的 keyboard routing 與 Shell accelerator 優先順序。
- `docs/tickets/PD-023-shell-file-operations-acceptance.md`——Shell view 的 F2 file rename 驗收邊界。
- `src/sidebar/sidebar.h`、`src/sidebar/sidebar.cpp`——`Sidebar::begin_rename()`、editor commit/cancel 與 `kRenameCommitMessage`。
- `src/app_shell/main.cpp`——`group_list_proc`、`WM_COMMAND` 的 Group rename command、`kRenameCommitMessage` handler、`address_bar_has_focus()` 與 message loop 的 `translate_accelerator` 順序。
- Trace every caller of `Sidebar::begin_rename()` and every path that handles `WM_KEYDOWN`／`WM_SYSKEYDOWN` before editing.

## Scope

1. 在 `src/app_shell/main.cpp` 的主 message loop、Shell `translate_accelerator` 之前加入 plain `F2` 的 Group-list focus guard。
2. Guard 命中時呼叫既有 `state.sidebar.begin_rename()` 並消費該按鍵，避免 active Shell view 先攔截或處理這個 F2。
3. 僅在 Group list 取得 focus 時生效；不得改變 Shell view、address bar、tab strip 或其他控制項的 F2 行為。
4. 以現有 sidebar inline editor 與 `WM_COMMAND`／`kRenameCommitMessage` 路徑完成 rename，不新增 helper、control、dialog、timer、schema 欄位或 dependency。

## Non-goals

- 不修改 `src/core`、Group identity、session schema 或 `core::rename_group`。
- 不重寫 `Sidebar::begin_rename()`，不改變 Enter／Escape／失焦語意。
- 不攔截 Shell view 的 F2 file rename、Ctrl+C、Delete 或其他原生鍵盤操作。
- 不註冊 global hotkey，不使用 `RegisterHotKey`、polling、timer 或 UI framework。
- 不把 F2 擴展成 address bar、tab strip、sidebar footer 或其他控制項的 Group rename 快捷鍵。
- 不新增 UIAutomation／WinAppDriver 測試或測試 framework。
- 不順手修正既有 sidebar focus frame、owner-draw、drag reorder 或其他 UI 問題。

## Acceptance criteria

1. 啟動 Release build，讓 focus 落在一個 Group row，按 plain `F2`；既有 inline editor 出現，且原 Group name 被選取。
2. 在 editor 按 Enter，Group name 更新到清單，`session.json` 的 Group name 也更新；既有 context-menu `Rename Group` 流程仍可用。
3. 重新操作一次後按 Escape，再操作一次後以 focus loss 結束；兩次都保留原 Group name，不寫入取消的文字。
4. 讓 focus 落在 Shell view，對可丟棄檔案按 `F2`；Windows 原生 file rename 仍啟動，且 Group name 不變。
5. 讓 focus 落在 address bar、tab strip 或其他非 Group list 控制項，按 `F2`；不啟動 Group inline editor。
6. 空 Group list 或沒有可選取 Group row 時按 `F2` 不當機、不建立 editor、不改 session。
7. F2 路徑不新增 Shell view、改變 active pane、觸發 Group switch 或引入 idle CPU／disk activity。
8. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
9. `git diff --check` 通過。

這是 `app_shell` 的 Win32 message routing 修正，不新增 `core` 可測邏輯；依 `docs/testing.md` 的 single-seam 決策，不另建 unit-test framework。F2 focus boundary 與 inline editor/file rename 結果需在真實桌面驗證；若環境不可互動，必須記錄 `未驗證，需真實桌面`，不得以 source inspection 宣稱通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n -C 5 "VK_F2|GetFocus|begin_rename|translate_accelerator|kRenameCommitMessage|WM_KEYDOWN|WM_SYSKEYDOWN" src\app_shell\main.cpp src\sidebar\sidebar.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 真實桌面人工操作：Group row focus + F2；Enter commit；Escape/focus-loss cancel。
# 再在 Shell view 對 disposable file 按 F2，確認原生 file rename 仍可用。
# 也在 address bar、tab strip 與其他控制項按 F2，確認不會開 Group editor。
# 不使用 UIAutomation／WinAppDriver，不合成鍵盤或滑鼠輸入。
```

測試後使用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉，不要使用 `Stop-Process -Force`。

## Handoff requirements

- 記錄 F2 guard 的實際位置、focus 判斷與是否只修改 `src/app_shell/main.cpp`。
- 記錄 Shell `translate_accelerator` 與 Group-list F2 guard 的最終優先順序，確認 Shell view 的 file F2 未被攔截。
- 記錄 Group row 的 Enter commit、Escape／focus-loss cancel、session persistence 結果。
- 記錄 address bar、tab strip、其他控制項與空 Group list 的 F2 結果。
- 記錄 CMake、build、CTest、`rg` 與 `git diff --check` 結果；不能互動時明確標記 `未驗證，需真實桌面`。

## 交接區

<!-- 實作 agent 填寫，append-only -->

### 2026-08-31 實作交接

- `src/app_shell/main.cpp:6145-6150` 是唯一產品程式修改：主 message loop 在 Shell `translate_accelerator` 之前檢查 plain `WM_KEYDOWN`／`VK_F2`、無 Ctrl/Alt/Shift，且 `GetFocus() == state.sidebar.window()`；命中後呼叫既有 `state.sidebar.begin_rename()` 並消費按鍵。沒有修改 `sidebar`、`core`、session schema 或 Shell host。
- 既有 context-menu `Rename Group` caller、`Sidebar::begin_rename()`、Enter/Escape/失焦 editor 路徑、`kRenameCommitMessage`、`core::rename_group`、sidebar refresh 與 save path 均未改動。Shell view focus 時 guard 不命中，訊息仍落到既有 `translate_accelerator`。
- Release configure 與 `cmake --build build` PASS；排除 launch smoke 的 CTest 為 10/10 PASS；`rg` 路徑檢查與 `git diff --check` PASS。
- 完整 `ctest --test-dir build --output-on-failure` 為 10/11：`panedock_launch_smoke` 在關閉主視窗後等待程序退出逾時；獨立重跑一次得到相同結果。失敗後均沒有殘留 PaneDock process。這個關閉逾時已在 PD-149 交接區記錄，且本票唯一分支只在 Group list 收到 F2 時執行，launch smoke 不會送出 F2；本票未擴大修改 shutdown。因 Acceptance 8 尚未全綠，tracker 保留 `in_progress`。
- 依 `docs/testing.md` 不合成鍵盤／滑鼠輸入；Group row F2、Enter commit、Escape／focus-loss cancel、Shell view native file F2 與其他控制項的 focus boundary 目前標記為 `未驗證，需真實桌面`。
