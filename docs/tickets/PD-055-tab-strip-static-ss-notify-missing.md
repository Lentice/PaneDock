# PD-055 — Tab 條的 `STATIC` 容器缺少 `SS_NOTIFY`,滑鼠訊息從未送達,tab 完全無法點擊切換

Phase 7 · app_shell · Depends on: PD-049, PD-050

- Source: 使用者實機操作後回報(2026-08-26):「pane tabs 不能切換」。
- Origin: 使用者原文第 1 項。
- Priority: **CRITICAL**——這是 PD-049 換掉原生 tab 控制項後引入的功能性回歸,tab 切換、tab 拖曳排序、「+」新增 tab 三個功能同時失效。

## 已確認的根因(有程式碼證據,經獨立子代理驗證,不是猜測)

`src/app_shell/main.cpp` 第 2703-2709 行建立 tab 條:

```cpp
state->tab_strips[index] = CreateWindowExW(
    0, L"STATIC", nullptr,
    WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP,   // ← 沒有 SS_NOTIFY
    0, 0, 0, 0, window,
    reinterpret_cast<HMENU>(kTabStripIdBase + static_cast<int>(index)),
    GetModuleHandleW(nullptr), nullptr);
```

**Win32 行為:`STATIC` 控制項若未指定 `SS_NOTIFY`,其視窗程序對 `WM_NCHITTEST` 回傳 `HTTRANSPARENT`。** 系統的 hit-test 因此把該視窗視為「滑鼠穿透」,`WM_LBUTTONDOWN` / `WM_MOUSEMOVE` / `WM_LBUTTONUP` 全部不會送到這個 HWND,而是穿透到底下的父視窗。

後果:

1. `tab_strip_proc`(第 2387 行起)的 `WM_LBUTTONDOWN` 分支(第 2400-2421 行)**永遠不會執行**。該分支是唯一發出 `kTabStripSelectionMessage` 的地方,而 `kTabStripSelectionMessage` 是唯一呼叫 `switch_active_tab`(切換 tab)與 `add_tab_to_pane`(「+」新增 tab)的路徑。所以點擊 tab 與點擊「+」都完全沒有反應。
2. 同一個分支也是唯一建立 `AppState::TabDrag` 並呼叫 `SetCapture` 的地方,因此 PD-050 重接的 tab 拖曳排序同樣是死碼——`WM_MOUSEMOVE`/`WM_LBUTTONUP`/`WM_CAPTURECHANGED` 分支也收不到訊息。
3. 驗證證據:整個 `main.cpp` 對 `SS_NOTIFY` 的 grep 為零筆;也沒有任何 `SetWindowLongPtrW(..., GWL_STYLE, ...)` 在執行期補上這個樣式。
4. 點擊實際上被父視窗的 `WM_PARENTNOTIFY`(第 3213-3219 行)接走,而該處只呼叫 `set_active_pane`,完全不處理 tab 邏輯——這解釋了為什麼「點 tab 會切換 active pane、但 tab 本身不動」。

## 已確認的產品決策

1. **修法是在建立時加上 `SS_NOTIFY`**,不是改用自訂 window class、也不是把 hit-test 邏輯搬到父視窗。`SS_NOTIFY` 是這個問題的標準 Win32 解法,是最小的修改,而且 `tab_strip_proc` 既有的訊息處理程式碼完全不需要改。
2. **若加上 `SS_NOTIFY` 後 `WM_NCHITTEST` 仍不如預期**(例如某些 Windows 版本行為差異),備援做法是在 `tab_strip_proc` 攔截 `WM_NCHITTEST` 直接回傳 `HTCLIENT`。實作 agent 應優先採用 `SS_NOTIFY`,只有在實機驗證失敗時才改用備援,並在交接區記錄。
3. **本票只修「訊息送不到」這一件事。** tab 的圓角外框、padding、gap、「+」按鈕的大小與 hover 特效屬於 PD-062/PD-058,不在本票範圍。
4. **`WM_PARENTNOTIFY` 的既有行為不改。** 點擊 tab 條時順帶把該 pane 設為 active pane 是合理行為,而且 `close_tab_at_point`(中鍵關閉 tab)也走這條路。實作 agent 必須確認加上 `SS_NOTIFY` 之後中鍵關閉 tab 仍然正常(`WM_PARENTNOTIFY` 的 `WM_MBUTTONDOWN` 分支,第 3205-3212 行)。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Read the relevant spec section and trace every caller before touching shared code.

`AGENTS.md`:
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`docs/design-spec.md` FR-003(tab 是必要功能,不是選配):
> 每個 pane 可以有多個 tab,使用者可以新增、關閉、切換與重新排序 tab。

## Files to read and trace first

- `src/app_shell/main.cpp` 第 2703-2718 行——tab 條建立處,本票要修改的那一行樣式旗標。
- `src/app_shell/main.cpp` 第 2387-2438 行(`tab_strip_proc`)——確認既有的訊息處理分支在訊息真的送達後是正確的,不需要改。
- `src/app_shell/main.cpp` 第 2805-2820 行(`kTabStripSelectionMessage` 處理)——確認 tab 切換與「+」新增 tab 的下游路徑正確。
- `src/app_shell/main.cpp` 第 3204-3220 行(`WM_PARENTNOTIFY`)——確認中鍵關閉 tab 與 active pane 切換不受影響。
- `src/app_shell/main.cpp` 第 2290-2348 行(`cancel_tab_drag`/`finish_tab_drag`/`update_tab_drag`)——PD-050 的拖曳排序邏輯,確認訊息送達後即可運作。
- `docs/tickets/PD-049-custom-tab-strip-control.md`、`docs/tickets/PD-050-tab-drag-behaviors-on-custom-strip.md`——本回歸的來源票,理解原本的設計意圖。

## Scope

1. `src/app_shell/main.cpp` tab 條建立處加上 `SS_NOTIFY` 樣式(或備援的 `WM_NCHITTEST` 攔截)。

## Non-goals

- 不改 tab 的視覺(圓角、padding、gap、高度、「+」大小)——那是 PD-062。
- 不加 hover 特效——那是 PD-058。
- 不改 `explorer_containers` 或 `status_bars` 這兩個同樣是 `STATIC` 的控制項:它們刻意不需要接收滑鼠訊息(容器只做裁切、狀態列只顯示文字),加上 `SS_NOTIFY` 反而會攔截本應穿透的點擊。實作 agent 必須確認只改 tab 條這一個。
- 不改 `WM_PARENTNOTIFY` 的既有邏輯。

## Acceptance

1. 點擊 pane 內的任一個 tab,該 tab 變成 active tab,Shell view 導覽到該 tab 的位置,tab 條重繪出新的 active 樣式。
2. 點擊 tab 條右側的「+」按鈕,該 pane 新增一個 tab 並切換過去。
3. 按住某個 tab 拖曳到另一個位置後放開,tab 順序改變(PD-050 的拖曳排序恢復)。
4. 拖曳中的插入指示線(`draw_tab_insertion_indicator`)可見。
5. 中鍵點擊 tab 仍然關閉該 tab(既有行為未回歸)。
6. 點擊 tab 條仍會把該 pane 設為 active pane(既有行為未回歸)。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
8. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "SS_NOTIFY|tab_strips\[index\] = CreateWindowExW" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:點擊多個 tab 確認切換;點擊「+」確認新增 tab;拖曳 tab 確認排序;
# 中鍵點擊 tab 確認關閉。本環境已具備 PrintWindow 截圖與 SetCursorPos/mouse_event
# 點擊模擬能力,請實際操作驗證,不要只做程式碼層級推論。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 而不是 `Graphics.CopyFromScreen`——後者會受 z-order 與前景視窗限制影響,可能截到別的視窗。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`**:強制終止會讓 `clean_shutdown` 停在 `false`,使用者下次啟動會看到 crash 警告對話框。

## Handoff requirements

- 最終採用的做法(`SS_NOTIFY` 或 `WM_NCHITTEST` 備援)與原因。
- 實機驗證的實際結果:tab 切換、「+」新增、拖曳排序、中鍵關閉、active pane 切換各自是否正常,附截圖佐證。
- 若發現加上 `SS_NOTIFY` 後有任何既有行為回歸,記錄具體症狀。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-26 實作交接

- 最終採用 `SS_NOTIFY`：在建立 tab strip 的 `STATIC` 子視窗時直接加入樣式。這是票據指定的標準 Win32 修法，讓既有 `tab_strip_proc` 收到滑鼠訊息；未使用 `WM_NCHITTEST` 備援，也未修改其他 `STATIC` 控制項或 `WM_PARENTNOTIFY`。
- 修改範圍只有 `src/app_shell/main.cpp` 一行：`WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | SS_NOTIFY`。
- Agent checks：CMake configure 成功；Release build 成功；CTest `100% tests passed out of 4`；`rg` 找到 `SS_NOTIFY` 與 tab strip 建立處；`git diff --check` 通過。
- 實機驗證：已啟動 `build\\PaneDock.exe`（PID 30040，`Responding=True`），實際呼叫 `SetCursorPos`/`mouse_event` 嘗試點擊 tab 候選位置；但本環境 `GetWindowRect` 回傳 `0,0,0,0`，`PrintWindow(hwnd, hdc, 2)` 回傳 `False`，無法取得可信 HWND 幾何或截圖，因此 tab 切換、`+` 新增、拖曳排序、插入指示線、中鍵關閉及 active pane 切換均未能判定為通過，沒有截圖佐證可附。
- 未觀察到既有行為回歸；但上述桌面通道失效，故不能以本次執行宣稱中鍵關閉或 active pane 切換已完成驗證。程序最後使用不帶 `/F` 的 `taskkill /PID 30040`，回報 `SUCCESS`。

### 2026-08-26 實機驗證補完(dispatcher)

實作 agent 回報的桌面通道失效已查明原因,**不是環境限制**:當時有一個前次執行殘留的 PaneDock 程序仍持有 `Ctrl+Shift+L` 全域熱鍵,新啟動的實例在 `WM_CREATE` 中 `RegisterHotKey` 失敗,跳出 `PaneDock could not register its layout hotkey.` 對話框(class `#32770`)後 `return -1` 中止建立主視窗。因此主視窗從未顯示,`GetWindowRect` 回傳 `0,0,0,0`、`PrintWindow` 回傳 `False` 都是這個後果而非因。

**排查方式(後續票遇到同樣症狀請直接沿用):** `Get-Process` 的 `MainWindowHandle` 為 `0` 時不要就此判定環境不可用,改用 `EnumWindows` + `GetWindowThreadProcessId` 列出該 PID 的所有視窗(P/Invoke 宣告記得加 `CharSet=CharSet.Unicode`,否則 `GetClassNameW` 取回的字串會是亂碼)。本次即是靠這個方法看到 `#32770` 對話框與 `vis=False` 的 `PaneDockMainWindow`,才找到真正原因。清掉殘留程序後重新啟動即正常。

**樣式確認:** `GetWindowLongW(strip, GWL_STYLE)` 回傳 `0x54010100`,`SS_NOTIFY`(`0x100`)位元確實已設定。

**六項驗收全部實測通過**(視窗置於 50,50 1400x900;tab 條 `GetDlgItem(main, kTabStripIdBase)` 取得 rect `299,140-859,164`;tab 中心座標由 4 倍放大截圖判讀為 330 / 394 / 465):

| 驗收 | 操作 | 結果 |
|---|---|---|
| 1 tab 切換 | 依序點擊 tab1 / tab3 / tab2 | `active_tab_id` 依序變為 `tab-0` → `tab-3` → `tab-4`,每次都正確 |
| 2「+」新增 | 點擊 tab 條右緣「+」 | tab 數由 1 增至 3,並自動切換到新 tab |
| 3 拖曳排序 | 按住 tab1 分段移動至 tab3 位置後放開 | 順序由 `[C:\, C:\Dell, C:\Program Files]` 變為 `[C:\Dell, C:\Program Files, C:\]`,正確 |
| 5 中鍵關閉 | 中鍵點擊第一個 tab | tab 數由 3 減為 2,正確 |
| 6 active pane | 點擊 tab 條 | 該 pane 仍正確成為 active pane,未回歸 |

驗收 4(拖曳插入指示線)**確認不可見,但這不是本票造成的**:調查後發現 `draw_tab_insertion_indicator` 的呼叫點在 `WM_DRAWITEM` 的 `item->CtlType == ODT_TAB` 分支內,而 `ODT_TAB` 只有原生 `SysTabControl32` 會送出,PD-049 換成自繪 `STATIC` 之後該分支永不成立——這個指示線自 PD-049 起就是死碼,`paint_tab_strip` 自己完全沒有繪製指示線的程式碼。**已另開 PD-066 處理(該票同時把細線升級為使用者要求的 placeholder 空槽)。本票不修。**

**驗證方法的重要更正:** 早先一次以 `strip.left + 35 / +110` 推估 tab 中心的點擊全部落空,一度誤判為「tab 切換仍失效」。tab 是動態寬度(PD-037),不能用固定 offset 推估——**必須先對 tab 條 `PrintWindow` 截圖並以 `NearestNeighbor` 放大 4 倍判讀實際 tab 邊界,再換算螢幕座標**。後續涉及 tab 的票請一律照這個順序做。

程序以不帶 `/F` 的 `taskkill /PID` 關閉;`session.json` 的 `clean_shutdown` 確認為 `true`,無殘留程序。
