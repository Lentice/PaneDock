# PD-121 — 跨 Group 剪貼簿複製貼上

Phase 7 · app_shell, Shell integration · Depends on: PD-017, PD-019, PD-021, PD-023, PD-086, PD-091

- Source: 使用者需求(2026-08-29), `docs/design-spec.md` FR-002 / FR-007、NFR-003
- Origin: 使用者要求在 PaneDock 的某個 Group 複製檔案,切換到另一個 Group 後貼上。
- Priority: HIGH——這是 Group 作為「完整工作情境」後仍能交換檔案的基本操作,也直接落在 Group 切換與 Shell clipboard 的交界。

## Goal

讓使用者可以在 Group A 的 Shell view 選取檔案並使用原生 Copy／`Ctrl+C`,切換到 Group B 後在目標 pane 使用原生 Paste／`Ctrl+V`,完成與 Windows File Explorer 一致的複製結果。來源與目標都必須繼續由 `IExplorerBrowser` 內的 Shell view 處理。

這不是新增一套複製引擎:目前 Shell view 已持有原生 clipboard `IDataObject`,Group 切換只應改變可見 view 的導覽位置,不應攔截、序列化或重建 clipboard。

## 已確認的產品決策

1. 本 ticket 只承諾 **Copy／Paste**(`Ctrl+C`／`Ctrl+V` 與 Shell context menu),不包含 Cut／Paste 的跨 Group 移動語意。
2. 貼上目標是切換後 Group 的 active pane。使用者若要貼到另一個 pane,先點選該 pane 再貼上;不新增「記住跨 Group 目標 pane」的隱藏狀態。
3. 複製多個檔案、資料夾、同名衝突與原生進度／衝突 UI 都沿用 Shell view,PaneDock 不直接組路徑、不自行呼叫檔案系統。
4. Group switch 的 session 保存使用既有 PD-091 防抖路徑;切換不應在 clipboard 操作前後同步執行多次完整 session 寫入。
5. 不把 clipboard 內容寫入 session document。clipboard 是作業系統層級的暫態狀態,重開後不保證仍存在。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` FR-007:
> 複製、移動、刪除、重新命名經由 Shell `IFileOperation`,含原生進度對話框與衝突提示。剪貼簿操作經由 Shell `IDataObject`。

`docs/design-spec.md` NFR-003:
> Group 切換、版型切換、tab 切換不得因為某個 Shell location 緩慢或無法連線而凍結 UI。

`AGENTS.md`:
> File operations go through Shell `IDataObject` and `IFileOperation`. Never assemble a path string and call the filesystem directly — that loses virtual items, progress UI and conflict handling.

`AGENTS.md`:
> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups.

`AGENTS.md`:
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

## Files to read and trace first

- `AGENTS.md`、`docs/design-spec.md` §5 FR-002／FR-007、§6 NFR-003、§9.2／§9.3。
- `docs/development.md` 的 Shell、STA、atomic session 與 UI 語言規則。
- `src/app_shell/main.cpp` 的 `activate_group`, `set_active_pane`, message loop、`translate_accelerator`, `schedule_session_save` 與 `save_now`。
- `src/explorer_host/explorer_host.cpp`／`.h` 的 `initialize`, `navigate`, `translate_accelerator` 與 view lifetime。
- `docs/tickets/PD-014-explorer-host-ole-and-accelerator-wiring.md`、`PD-017-group-sidebar.md`、`PD-019-tab-strip-and-realize-on-activation.md`、`PD-021-active-pane-keyboard-shortcuts.md`、`PD-023-shell-file-operations-acceptance.md`、`PD-086-group-switch-overwrites-incoming-folders-with-outgoing-live-state.md`、`PD-091-session-save-synchronous-on-every-navigation.md`。
- `docs/testing.md` 的 Shell file operations、Clipboard 與本 ticket 的 Cross-Group acceptance protocol。

## Scope

1. 沿用目前原生 Shell view 的 clipboard／accelerator 路徑,確認 `Ctrl+C`／`Ctrl+V` 不被 PD-021 的 app-shell 快速鍵攔截。
2. 將 `activate_group` 完成後的非關閉保存改走既有 `schedule_session_save`,避免跨 Group 操作把同步 session flush 放進 UI 操作熱路徑;正常關閉仍由既有強制 `save_now` 保證落盤。
3. 在 `docs/testing.md` 增加跨 Group Copy／Paste 的明確真人驗收步驟,涵蓋切換 Group 後貼至 active pane、切換 tab 後貼上、來源與結果完整性。
4. 以最小 source check、build、CTest 驗證實作;Shell clipboard 的真實跨 Group 行為留給互動桌面驗收。

## Non-goals

- 不做 Cut／Paste 的跨 Group 移動。
- 不做自有 `IDataObject`、clipboard history、clipboard persistence 或 background copy engine。
- 不做把檔案直接貼到 Group row、tab header 或隱藏 pane 的特殊 drop 語意。
- 不改 session schema,不保存 clipboard 內容。
- 不改 `src/core` 契約,不新增執行緒、服務、第三方 runtime 或網路功能。
- 不在本 ticket 內實作跨 Group OLE 拖放;那是 PD-122。

## Acceptance

1. Group A 的 active pane 選取一個檔案,使用 `Ctrl+C`;切換到 Group B,在其 active pane 使用 `Ctrl+V`,檔案正確複製且來源仍存在。
2. 以 Shell context menu 的 Copy／Paste 重做一次,結果與 Windows File Explorer 一致。
3. Group A 複製多個檔案或一個資料夾,切換到 Group B 的另一個 pane 後貼上,所有項目正確出現。
4. 切換 Group 後先切換目標 pane 的 active tab,再貼上;結果進入目前 active tab 的資料夾,沒有貼到來源 Group 或舊 tab。
5. 目標含同名檔案時,原生 conflict UI 與 Replace／Skip／Keep both 行為正常。
6. 大檔案或大量檔案貼上時,原生進度 UI 出現;操作期間切換 Group／tab 不當機、不死結,且完成後來源與目標狀態正確。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 與 `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "translate_accelerator|Ctrl|schedule_session_save|activate_group|save_now" src/app_shell/main.cpp src/explorer_host
# 預期:Shell accelerator 位於 app-shell 快速鍵前;activate_group 的一般完成保存使用既有防抖路徑;
# 關閉路徑仍保留 force save。
git diff --check
```

```powershell
.\build\PaneDock.exe
# 真人桌面:依 Acceptance 1–6 執行,每項記錄 PASS、FAIL 或「未驗證,需真實桌面」;
# 不以 source inspection 代替 Shell clipboard 的 runtime 結果。
```

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-30 實作交接

- 沿用 `IExplorerBrowser` 內原生 Shell view 的 `IDataObject` clipboard 與既有 `ExplorerHost::translate_accelerator`;沒有新增 clipboard manager、`IFileOperation` wrapper 或直接檔案系統呼叫。
- `src/app_shell/main.cpp` 的 `activate_group` 完成保存改用既有 `schedule_session_save`。跨 Group 切換不再在 UI／OLE 操作路徑同步 flush session;`WM_CLOSE`、`WM_DESTROY` 與 `WM_QUERYENDSESSION` 的 force save 保持不變。
- `docs/testing.md` 已新增 E18–E23,明確覆蓋跨 Group Copy／Paste、目標 active tab、衝突 UI 與操作中的 re-entry。
- Agent checks：`cmake --build build` 通過；`ctest --test-dir build --output-on-failure` 通過，6/6；`git diff --check` 通過；source/static checks 命中 Shell accelerator 優先、Group hover deferred message 與既有 drag target lifetime。
- Group A→Group B 的真實 Shell clipboard Copy／Paste、資料夾／多選與慢速操作仍未驗證,需依 E18–E23 在真實互動桌面執行；沒有以 source inspection 宣稱 PASS。
