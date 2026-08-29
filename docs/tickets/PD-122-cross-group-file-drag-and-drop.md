# PD-122 — 跨 Group 檔案拖放

Phase 7 · app_shell, Shell/OLE integration · Depends on: PD-034, PD-050, PD-090, PD-091, PD-121

- Source: 使用者需求(2026-08-29), `docs/design-spec.md` FR-002／FR-008、NFR-003
- Origin: 使用者要求檔案可以從一個 Group 的 pane 拖到另一個 Group 的 pane。
- Priority: HIGH——這是 PaneDock 多 Group 工作流的直接檔案交換方式,並且會進入已知的 OLE message-loop re-entry 路徑。

## Goal

讓使用者可從 Group A 的原生 Shell view 開始拖曳檔案,在側邊欄目標 Group 列懸停後自動切換,再把游標移到目標 Group 的 pane 內容區放開,由原生 Shell view 完成搬移或複製。

現有 PD-034 已提供 Group row 的 OLE hover target,PD-090 已把 hover callback 延後到私有 message 執行,PD-091 已把導航完成的 session 保存合併。本 ticket 將這條既有能力正式定義為跨 Group 檔案拖放流程,只補必要的整合修正與驗收,不另造一條檔案操作路徑。

## 已確認的產品決策

1. 正式操作流程是:從來源 pane 拖曳 → 在目標 Group row 停留約 800ms → Group 切換 → 游標移到目標 pane 的 Shell view 內容區 → 放開。
2. Group row 與 tab header 只負責 hover 切換,`Drop` 一律不接受檔案;使用者若直接在 row/header 放開,不執行任何複製或搬移。
3. 實際複製／搬移、同磁碟與跨磁碟預設語意、Ctrl modifier、進度 UI 與衝突提示全部由 Shell `IDataObject`／原生 view 決定。
4. 目標 Group 的 active pane 是預設落點。若要落到另一個 pane,使用者在切換後把游標移到該 pane;不新增跨 Group 隱藏目標狀態。
5. 若要落到目標 Group 的非 active tab,先在該 tab header 懸停約 800ms 觸發既有 tab switch,再把游標移進該 tab 的 Shell view 內容區放開。
6. 不為跨 Group 拖放建立背景執行緒、第二個 OLE drag loop、客製 `IDataObject` 或浮動拖曳視窗。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` FR-008:
> 支援 pane 之間、以及與其他應用程式之間的拖放,經由 OLE drag and drop 與 Shell `IDataObject`。

`docs/design-spec.md` NFR-003:
> Group 切換、版型切換、tab 切換不得因為某個 Shell location 緩慢或無法連線而凍結 UI。

`AGENTS.md`:
> File operations go through Shell `IDataObject` and `IFileOperation`. Never assemble a path string and call the filesystem directly — that loses virtual items, progress UI and conflict handling.

`AGENTS.md`:
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`:
> Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups.

`docs/tickets/PD-090-drag-hover-group-switch-reenters-ole-drag-loop.md`:
> `timer_expired` 只負責判斷「該切換了」,然後 `PostMessageW` 一個私有訊息給主視窗,把真正的 `activate_group` 呼叫延後到目前的 Shell/OLE 回呼完全返回之後才執行。

## Files to read and trace first

- `AGENTS.md`、`docs/design-spec.md` §5 FR-002／FR-008、§6 NFR-003、§9.2。
- `docs/development.md` 的 OLE、STA、COM lifetime 與 re-entry 規則。
- `src/app_shell/main.cpp` 的 `DragHoverTarget`, `register_tab_drag_hover_targets`, `make_sidebar_drag_hover_target`, `kDragHoverMessage`, `activate_group`, `apply_layout` 與 `WM_TIMER`／私有 message 分支。
- `src/sidebar/sidebar.cpp`／`.h` 的 `RegisterDragDrop`／`RevokeDragDrop` lifetime。
- `src/explorer_host/explorer_host.cpp`／`.h` 的 `initialize`, `navigate`, `destroy` 與 Shell view 生命週期。
- `docs/tickets/PD-034-drag-hover-auto-switch.md`、`PD-050-tab-drag-behaviors-on-custom-strip.md`、`PD-090-drag-hover-group-switch-reenters-ole-drag-loop.md`、`PD-091-session-save-synchronous-on-every-navigation.md`、`PD-121-cross-group-clipboard-copy-paste.md`。
- `docs/testing.md` 的 Drag and drop、Namespace 與本 ticket 的 Cross-Group acceptance protocol。

## Scope

1. 沿用 PD-034 的 Group row hover target 與既有 800ms 計時,不新增第二套拖曳偵測。
2. 確認懸停到期經 PD-090 的 `PostMessageW`／generation guard 執行 Group switch,不在 `WM_TIMER` 或 `DragOver` stack 內直接切換。
3. 確認 Group switch 的一般保存使用 PD-091 的防抖路徑,不在 OLE 拖曳迴圈中同步 flush session。
4. 在 `docs/testing.md` 增加跨 Group drag/drop 的真人驗收步驟,涵蓋同磁碟 move、跨磁碟 copy、目標 pane、取消與重入。
5. 只做必要的 source/build/static checks;不以靜態檢查宣稱真實 Shell drag/drop 已通過。

## Non-goals

- 不接受在 Group row、tab header 或 sidebar 上直接放開的檔案 drop。
- 不做跨 Group 的自有 `IDropTarget` 檔案轉送或自有 `IFileOperation`。
- 不做浮動拖曳縮圖、拖曳歷史、背景傳輸佇列或跨重開機續傳。
- 不改 PD-034 的 800ms 固定門檻,不新增設定項。
- 不改 `src/core`、session schema、Group／pane／tab identity 或 Shell location 格式。
- 不用 UIAutomation、WinAppDriver 或合成滑鼠完成驗收。

## Acceptance

1. 從 Group A 的 pane 拖曳檔案到 Group B row,停留約 800ms 後 Group B 成為 active,且其版型、pane 與 active tab 正確還原。
2. 切換完成後把游標移到 Group B 的指定 pane 內容區放開;同磁碟預設 move、跨磁碟預設 copy,來源與目標結果正確。
3. 拖到非 active pane 時,落點是游標下的 pane;是否同步取得 focus 依實際 Shell 行為記錄,但不得落到錯誤 pane。
4. 在 Group row 或 tab header 直接放開不執行檔案操作;移入 Shell view 後放開仍保留原生進度／衝突 UI。
5. 未達 800ms 即移開或按 `Esc` 取消,不切換 Group、不留下卡住的 drag state。
6. 懸停切換發生後,在仍持有拖曳項目時繼續移動與放開,不當機、不死結、不出現 stale Group/tab 內容。
7. 大檔案或大量檔案拖放期間切換 Group／tab 或拖曳 splitter,PaneDock 保持可回應且 Shell 操作結果正確。
8. `cmake --build build`、`ctest --test-dir build --output-on-failure` 與 `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kDragHoverMessage|PostMessageW|timer_expired|invoke_hover|activate_group|schedule_session_save|save_now" src/app_shell/main.cpp
# 預期:timer_expired 只排入私有 message;Group switch 不在 OLE hover 的同步 callback 中
# 直接寫 session;關閉路徑仍保留 force save。
rg -n "RegisterDragDrop|RevokeDragDrop|IDropTarget" src/app_shell src/sidebar
git diff --check
```

```powershell
.\build\PaneDock.exe
# 真人桌面:依 Acceptance 1–7 執行;拖曳連續操作不得由 Agent 合成,
# 每項記錄 PASS、FAIL 或「未驗證,需真實桌面」。
```

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-30 實作交接

- 沿用既有 PD-034 `DragHoverTarget`、Group row `RegisterDragDrop`、800ms hover 與原生 Shell view drop target；沒有新增第二套 OLE drag loop、客製 `IDataObject` 或檔案複製邏輯。
- PD-090 的 `PostMessageW`／generation guard 與 PD-091 的 session debounce 已接在這條路徑；本輪共用 PD-121 將 `activate_group` 的一般保存改成 `schedule_session_save`，避免 OLE hover callback 同步 flush。
- `docs/testing.md` 已新增 E20–E23,明確要求切換 Group 後移入目標 pane 內容區才放開，並覆蓋直接放在 Group row/tab header、取消、非 active pane 與操作中 re-entry。
- Agent checks：`cmake --build build` 通過；`ctest --test-dir build --output-on-failure` 通過，6/6；`git diff --check` 通過；source/static checks 確認 timer 只排入 `kDragHoverMessage`、Group/tab target 有成對 Register/Revoke。
- 從 PaneDock Group A 拖到 Group B 的真實桌面拖放、同／跨磁碟語意、非 active pane drop 與慢速操作仍未驗證,需依 E20–E23 在真實互動桌面執行；沒有以 source inspection 宣稱 PASS。
