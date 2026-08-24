# PD-021 — 送往 active pane 的鍵盤快速鍵:切換/新增/關閉 tab、上一頁/下一頁/上層

Phase 3 · app_shell · Depends on: PD-019, PD-020

- Source: `AGENTS.md`、`docs/design-spec.md` FR-014、`docs/tickets/PD-014-explorer-host-ole-and-accelerator-wiring.md`、`docs/tickets/PD-016-splitters-five-layouts-and-dpi-scaling.md`
- Origin: 2026-08-24,`docs/roadmap.md` Phase 3「Keyboard shortcuts routed to the active pane」。是 Phase 3 四項清單裡最後一項,也是 Phase 3 的收尾 ticket——完成後 `docs/roadmap.md` Phase 3 的四個條列全部有對應交付。
- Priority: MEDIUM——PD-019/PD-020 建好的滑鼠路徑已經能操作全部功能,本 ticket 是鍵盤路徑的補完,不阻塞其他功能。

## Goal

`docs/design-spec.md` FR-014 要求「pane 間移動焦點、切換 tab、新增／關閉 tab、上層導覽的快速鍵。快速鍵一律送往 active pane」。pane 間移動焦點(`F6`/`Shift+F6`)已經在 PD-016 做完;本 ticket 補上其餘四種:切換 tab、新增 tab、關閉 tab、上層導覽,全部只作用在目前的 active pane。

## 已確認的產品決策

1. **按鍵綁定沿用 Windows 檔案總管(2023 起內建分頁版)與大多數瀏覽器的既有慣例**,理由与 PD-016 選 `F6` 做 pane 焦點切換相同——使用者不需要重新學習:
   - `Ctrl+T`:在 active pane 新增一個 tab(呼叫 PD-019 的 `add_tab_to_pane`)。
   - `Ctrl+W`:關閉 active pane 的 active tab(呼叫 PD-019 的 `close_tab_in_pane`)。
   - `Ctrl+Tab` / `Ctrl+Shift+Tab`:切換到 active pane 的下一個/上一個 tab(呼叫 PD-019 的 `switch_active_tab`,依陣列順序循環,語意比照 PD-016 的 `F6`/`Shift+F6` 循環邏輯)。
   - `Alt+Left` / `Alt+Right`:呼叫 PD-020 的上一頁/下一頁按鈕邏輯(`core::navigate_tab_back`/`navigate_tab_forward` + `suppress_history_record` + `navigate()`)。
   - `Backspace`(在網址列以外的地方按下時)：呼叫 PD-020 的上層導覽邏輯(`ExplorerHost::navigate_up()`)。**已與使用者確認採用 `Backspace`**——Windows 檔案總管的傳統上層鍵,原生使用者肌肉記憶最強;`Alt+Up` 不採用。
   `docs/design-spec.md` FR-014 沒有列出具體按鍵,以上是合理的預設值,不是規格明文要求;若使用者手動測試後覺得某個綁定不順手,記錄在交接區,不要自行改規格。
2. **攔截點沿用 PD-016/PD-014 已建立的模式:訊息迴圈內、`ExplorerHost::translate_accelerator` 回傳非 `S_OK` 之後,才檢查上述按鍵組合。** 不使用 `RegisterHotKey`(那是行程全域熱鍵,PD-016 決策 6 已經說明為什麼 pane 內快速鍵不用這個機制)。這確保 Shell view 自己認得的鍵盤操作(例如在檔案清單內按 `Ctrl+C`/`F2`)優先由 Shell view 處理,不會被本 ticket 攔截。
3. **网址列(PD-020 建立的 `EDIT` 控制項)聚焦時,`Ctrl+T`/`Ctrl+W`/`Ctrl+Tab` 等仍然要生效**(這些是視窗層級快速鍵,不是網址列的文字編輯操作),但 `Backspace` 若選了「刪除字元」的既有 `EDIT` 語意會衝突——若決策 1 選擇 `Backspace` 做上層導覽,必須排除網址列或任何 `EDIT`/tab 條擁有鍵盤焦點的情況(用 `GetFocus()` 檢查目前焦點視窗是否為某個 `state.address_bars[*]`);若決策 1 最終只採用 `Alt+Up`,則不存在这个冲突,可以省略这项检查。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` FR-014:
> 提供 pane 間移動焦點、切換 tab、新增／關閉 tab、上層導覽的快速鍵。快速鍵一律送往 active pane。

`docs/tickets/PD-016-splitters-five-layouts-and-dpi-scaling.md` 已確認的產品決策 6:
> 這條快速鍵**不**透過 `RegisterHotKey`(那是行程層級全域熱鍵,容易跟其他應用衝突且不必要),而是在既有訊息迴圈裡、`ExplorerHost::translate_accelerator` 回傳 `S_FALSE` 之後攔截 `WM_KEYDOWN`,比照 PD-014 建立的攔截模式。

`AGENTS.md`:
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.
（本 ticket 只在既有訊息迴圈內加判斷分支,不引入新的重入路徑。）

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `wWinMain` 訊息迴圈——`translate_accelerator` 呼叫之後、`VK_F6` 判斷那一段(PD-016 建立),本 ticket 的按鍵判斷加在同一段裡。
- `docs/tickets/PD-019-tab-strip-and-realize-on-activation.md` 交接區——`add_tab_to_pane`/`close_tab_in_pane`/`switch_active_tab` 最終簽章。
- `docs/tickets/PD-020-address-bar-and-navigation-buttons.md` 交接區——上一頁/下一頁/上層導覽最終呼叫方式與 `suppress_history_record` 的正確用法、`address_bars` 陣列。
- `docs/tickets/PD-014-explorer-host-ole-and-accelerator-wiring.md` 交接區——`translate_accelerator` 攔截模式的既有背景。

## Scope

1. 在訊息迴圈裡、`state.explorers[active].translate_accelerator(&message) == S_OK` 判斷之後、既有 `VK_F6` 判斷旁邊,依 `message.message == WM_KEYDOWN` 與 `GetKeyState(VK_CONTROL/VK_MENU/VK_SHIFT)` 組合判斷決策 1 列出的按鍵組合,對應呼叫 PD-019/PD-020 建立的函式,處理後 `continue`(不再 `TranslateMessage`/`DispatchMessageW`)。
2. `Ctrl+Tab`/`Ctrl+Shift+Tab` 的「下一個/上一個 tab」邏輯:取得 active pane 目前 `active_tab_id` 在 `PaneState.tabs` 中的 index,依 `(index + 1) % tabs.size()` 或反向循環,呼叫 `switch_active_tab`。
3. `Backspace` 做上層導覽,加上決策 3 的焦點檢查(`GetFocus()` 排除網址列)。
4. 沒有新的 `core` 變更、沒有新的 `ExplorerHost` 變更——本 ticket 純粹是訊息迴圈裡的按鍵分派,呼叫既有(PD-019/PD-020)函式。

## Non-goals

- 不新增任何可自訂快速鍵的設定 UI——按鍵是寫死的常數,YAGNI。
- 不處理 pane 間移動焦點(`F6`/`Shift+F6`)——PD-016 已完成,本 ticket 不重複。
- 不修改 `core`、`ExplorerHost` 的既有簽章。
- 不做「最近關閉的 tab」復原(`Ctrl+Shift+T`)——spec 未要求,YAGNI。

## Acceptance

1. `Ctrl+T` 在目前 active pane 新增一個 tab 並成為 active,其餘 pane 不受影響。
2. `Ctrl+W` 關閉 active pane 的 active tab;若是該 pane 最後一個 tab,依既有 `core::close_tab` 行為導覽到預設 location 而非留下空 pane。
3. `Ctrl+Tab`/`Ctrl+Shift+Tab` 在 active pane 的 tab 之間依序循環切換,不影響其他 pane 的 active tab。
4. `Alt+Left`/`Alt+Right` 觸發與 PD-020 滑鼠按鈕相同的上一頁/下一頁行為(含 `suppress_history_record` 語意——不會把「按上一頁」誤記成新的一筆歷史)。
5. `Backspace` 觸發與 PD-020 上層按鈕相同的行為;在网址列擁有鍵盤焦點時按 `Backspace` 是正常的文字刪除,不觸發上層導覽。
6. 全部快速鍵只影響目前 active pane,對其他三個 pane 沒有副作用(FR-014「快速鍵一律送往 active pane」)。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過(本 ticket 不改動 `core`,既有三個測試不受影響)。
8. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
git diff --check
git status
# 預期:改動只集中在 src/app_shell/main.cpp 與本文件
```

```powershell
Start-Process .\build\PaneDock.exe
Start-Sleep -Seconds 2
Get-Process PaneDock | Select-Object Responding
# 手動(不涉及鍵盤/滑鼠自動化,留給使用者):Ctrl+T / Ctrl+W / Ctrl+Tab / Alt+Left/Right / 上層鍵
```

## Handoff requirements

- 是否觀察到任何按鍵組合被 Shell view 的 `TranslateAcceleratorW` 攔截而沒有送到 app_shell 的判斷(比照 PD-016 對 `F6` 的觀察方式記錄)。
- 完成後在 `docs/roadmap.md` Phase 3 段落補一筆「done」與交付 ticket 清單(PD-018/019/020/021),比照 Phase 1/Phase 2 段落的既有寫法——**這件事留給執行本 ticket 的 agent 做,不是本 ticket 撰寫者的工作**,因為要等 PD-018~021 全部完成才能下 Phase 3 完成的判斷。

## 交接區

<!-- 實作 agent 填寫,append-only -->
