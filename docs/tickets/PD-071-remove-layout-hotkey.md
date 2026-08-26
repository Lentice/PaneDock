# PD-071 — 移除 Ctrl+Shift+L 版型熱鍵:註冊失敗會讓程式完全無法啟動

Phase 7 · app_shell · Depends on: PD-009

- Source: 使用者回報(2026-08-26),附「PaneDock could not register its layout hotkey.」對話框截圖,並指示「先拿掉所有的 hotkey,現在還不需要」。
- Origin: 使用者原文追加項。
- Priority: HIGH——這不只是「暫時不需要」的功能移除,而是修掉一個會讓程式**完全無法啟動**的缺陷。

## 已確認的根因(有程式碼與實機佐證,不是猜測)

`src/app_shell/main.cpp` 的 `WM_CREATE`(移除前第 2899-2907 行)把全域熱鍵註冊失敗當成**致命錯誤**:

```cpp
if (!RegisterHotKey(window, kLayoutToggleHotkeyId,
                    MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT, 'L')) {
    MessageBoxW(window, L"PaneDock could not register its layout hotkey.",
                L"PaneDock", MB_ICONERROR | MB_OK);
    revoke_drag_hover_targets(*state);
    destroy_explorers(*state);
    return -1;                       // ← 中止建立主視窗
}
```

`RegisterHotKey` 的全域熱鍵在整個系統中是**獨佔**的:同一組合鍵只要已被任何一個 process 註冊,後來者一律失敗。因此:

- **任何**其他程式(或 PaneDock 自己的另一個實例)持有 Ctrl+Shift+L,PaneDock 就完全開不起來——不是少一個快捷鍵,是連主視窗都不會出現。
- Ctrl+Shift+L 是極常見的組合鍵,被佔用不是邊緣情況。
- 這個組合鍵提供的功能只是「循環切換版型」,而工具列的五顆版型按鈕本來就做得到同一件事。**用一個純便利性的快捷鍵,換掉整個程式的可啟動性,這個交換不成立。**

### 這個缺陷造成的連鎖影響

`return -1` 之後程序沒有主視窗但仍存在,表現為 `Get-Process` 的 `MainWindowHandle` 為 `0`、`GetWindowRect` 回傳 `0,0,0,0`、`PrintWindow` 回傳 `False`。這在自動化驗證時**極易被誤判成「本環境不支援截圖」**;PD-055 與 PD-056 的實作 agent 都曾因此誤判並放棄實機驗證(詳見 PD-068)。殘留程序也會鎖住 `build\PaneDock.exe`,使 `cmake --build` 以 `unable to remove file: Permission denied` 失敗。

## 已確認的產品決策

1. **整個熱鍵註冊移除,而不是只把失敗處理改成「忽略並繼續」。** 使用者明確指示現階段不需要熱鍵;保留一個註冊全域獨佔資源、卻只提供工具列已有功能的路徑,是沒有收益的風險面。日後若要重新加入,應改用視窗層級的 accelerator table(`TranslateAccelerator`)而不是全域 `RegisterHotKey`——那不會與其他程式衝突。**這是本票對日後重開方向的建議,不在本票範圍內實作。**

2. **一併移除因此失去呼叫者的 `toggle_layout` 與 `next_layout`。** 兩者在移除 `WM_HOTKEY` 之後沒有任何其他呼叫點,留著就是死碼。

3. **保留訊息迴圈中的 `IExplorerBrowser` accelerator 轉發(`translate_accelerator`)。** 那是把鍵盤事件轉發給 Shell view(F2 改名、Ctrl+C 複製等),**不是我們註冊的熱鍵**,移除會讓檔案區的鍵盤操作失效。使用者說的「拿掉所有的 hotkey」指的是我們自己註冊的全域熱鍵。

4. **不新增設定項讓使用者自訂熱鍵。** 超出本票與現階段範圍。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Read the relevant spec section and trace every caller before touching shared code.

——移除 `WM_HOTKEY` 前必須確認 `toggle_layout` / `next_layout` 沒有其他呼叫者。

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks.

——移除 `WM_CREATE` 的失敗分支時,該分支裡的 `destroy_explorers` 一併消失是正確的,因為不再有那條失敗路徑;`WM_CLOSE` 的 `destroy_explorers` 必須保留。

`docs/design-spec.md` NFR(穩定性):
> 應用程式不得在正常操作或關閉流程中當機。

## Files to read and trace first

- `src/app_shell/main.cpp` 常數區——`kLayoutToggleHotkeyId`。
- `src/app_shell/main.cpp` `WM_CREATE` 尾端——`RegisterHotKey` 與其失敗分支。
- `src/app_shell/main.cpp` `WM_HOTKEY` 分支。
- `src/app_shell/main.cpp` `WM_CLOSE` 與 `WM_DESTROY`——兩處 `UnregisterHotKey`。
- `src/app_shell/main.cpp` `toggle_layout` / `next_layout` 的定義與全部呼叫點。
- `src/app_shell/main.cpp` 訊息迴圈的 `translate_accelerator`——**確認後保留,不要移除。**

## Scope

1. 移除 `kLayoutToggleHotkeyId`、`RegisterHotKey` 呼叫與其失敗處理、`WM_HOTKEY` 分支、兩處 `UnregisterHotKey`。
2. 移除因此無呼叫者的 `toggle_layout` 與 `next_layout`。

## Non-goals

- 不移除 `translate_accelerator`(決策 3)。
- 不新增替代的 accelerator table。
- 不新增熱鍵設定項。
- 不改版型切換的其他路徑(工具列按鈕、`set_layout`、`apply_layout`)。

## Acceptance

1. **同時啟動兩個 PaneDock 實例,兩者都取得有效的 `MainWindowHandle`**(移除前第二個必然失敗)。
2. 啟動時不再出現 `PaneDock could not register its layout hotkey.` 對話框。
3. 工具列的五顆版型按鈕切換版型功能不變。
4. 檔案區的鍵盤操作(F2 改名等)不受影響。
5. 全 repo 不再有任何 `RegisterHotKey` / `UnregisterHotKey` / `WM_HOTKEY` / `toggle_layout` / `next_layout` 的參照。
6. 關閉後不留殘留程序,不新增 crash dump。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
8. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "HotKey|hotkey|WM_HOTKEY|next_layout|toggle_layout" src\
git diff --check
```

## Handoff requirements

- 移除的每一處位置。
- 兩個實例同時啟動的 `MainWindowHandle` 實測值。
- `translate_accelerator` 保留的確認。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-26 實作交接(dispatcher 直接修復,commit `cc63324`)

本票由 dispatcher 直接實作,未派給實作 agent。票據為事後補寫,用意是把這次改動與其根因留在 repo 的歷史記錄中——AGENTS.md 要求工作以 ticket 追蹤,而這次是先依使用者當下的指示動手、後補票。**這是流程上的例外,不是常態;後續改動仍應先開票。**

**移除的六處:** `kLayoutToggleHotkeyId` 常數;`WM_CREATE` 的 `RegisterHotKey` 與整個失敗分支(含 `MessageBoxW` 與 `return -1`);`WM_HOTKEY` 分支;`WM_CLOSE` 與 `WM_DESTROY` 各一處 `UnregisterHotKey`;以及失去呼叫者的 `toggle_layout` 與 `next_layout`。**純刪除,共 36 行,沒有新增任何一行。**

`translate_accelerator` 依決策 3 保留並確認未受影響。

**驗證結果:**

| 驗收 | 結果 |
|---|---|
| 1. 兩個實例同時啟動 | **PASS** — A `MainWindowHandle=1383230`、B `=1576238`,兩者皆有效 |
| 2. 熱鍵對話框 | 不再出現 |
| 5. 全 repo 殘留參照 | `rg` 無輸出 |
| 6. 殘留程序 / crash dump | 兩個實例皆正常關閉;連續 2 輪啟動關閉,crash dump `before=10 after=10`(0 新增) |
| 7. build / ctest | `cmake --build build` 成功;`100% tests passed out of 4` |
| 8. `git diff --check` | 通過 |

驗收 3、4 未逐項實測,理由是本次為純刪除且刪除範圍完全不觸及工具列按鈕路徑(`WM_COMMAND` → `set_layout`)與 `translate_accelerator`;版型按鈕的實機切換在同日 PD-056 的驗證中已確認正常。
