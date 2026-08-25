# PD-032 — 系統關機／登出／重開機時誤報「不乾淨關閉」的修正

Phase 5 · app_shell · Depends on: PD-025

- Source: `AGENTS.md`、`docs/design-spec.md` §9.4、`docs/tickets/PD-025-crash-recovery-path.md`
- Origin: 2026-08-25,使用者回報「常常會遇到」PD-025 做的「PaneDock did not shut down cleanly last time」警示對話框,附上實際截圖。經追查程式碼(非臆測),找到明確的根本原因,見下方「根本原因」。
- Priority: HIGH——這是一個會在**正常關機流程**下反覆觸發的假警報,使用者會誤以為程式常常崩潰或誤信 extension 有問題而去嘗試 `--diagnostic`,實際上程式完全沒有不正常終止。假警報一旦頻繁出現,會讓真正的崩潰警示失去使用者的信任(狼來了效應)。

## 根本原因(已讀程式碼確認,非推測)

`src/app_shell/main.cpp`:
- `save_now(AppState& state, bool clean_shutdown = false) noexcept`(第 812 行)——**所有**執行期呼叫預設寫入 `clean_shutdown = false`,包含 `wWinMain` 啟動時第一次 `save_now(state)`(第 1955 行,沿用 PD-025 決策 2:啟動後立刻標記為「尚未乾淨關閉」,這是刻意設計,為了抓到「啟動後尚未做任何變更就崩潰」的情境)。
- 唯一把 `clean_shutdown` 寫回 `true` 的地方是 `WM_CLOSE` 分支(第 1868–1877 行):`capture_window_placement` → `save_now(*state, true)` → `destroy_explorers` → `DestroyWindow`。
- **`window_proc`(整個函式,約第 1700–1885 行)沒有處理 `WM_QUERYENDSESSION` 或 `WM_ENDSESSION`。** Windows 在使用者登出、重新啟動、關機、或某些「強制關閉應用程式」的系統流程中,對頂層視窗送出的是 `WM_QUERYENDSESSION`(詢問是否可以結束)接著是 `WM_ENDSESSION`(確認結束),**不是** `WM_CLOSE`——`WM_CLOSE` 只在使用者主動關閉視窗(按右上角 X、Alt+F4、工作列右鍵「關閉視窗」)時才會送達。目前的程式碼完全沒有攔截前兩者,`DefWindowProcW` 對它們的預設行為不會呼叫我們的 `save_now`,行程隨後被系統終止,`clean_shutdown` 欄位停留在啟動時寫入的 `false`,下次啟動就一定顯示「不乾淨關閉」警示——**即使這次關機完全正常,使用者也沒有強制關閉任何東西。**

這解釋了「常常」發生:任何一次透過 Windows 的「開始」選單重新啟動、關機、登出,或系統自動重開機(例如 Windows Update),都會被誤判為不乾淨關閉。這不是 PD-025 的邏輯錯誤,是 PD-025 範圍完成時遺漏的一個終止路徑——PD-025 文件與驗收清單只提到 `Stop-Process -Force` 模擬崩潰,沒有涵蓋系統關機訊息。

## 已確認的產品決策

1. **新增 `WM_QUERYENDSESSION` 處理:呼叫與 `WM_CLOSE` 相同的「擷取＋標記乾淨」路徑(`capture_window_placement` + `save_now(*state, true)`),但不呼叫 `destroy_explorers`/`DestroyWindow`,回傳 `TRUE` 允許結束。** 不在這裡 destroy view,是因為 `WM_QUERYENDSESSION` 只是「詢問」,系統關機可能被其他應用程式取消(`WM_ENDSESSION` 的 `wParam` 會是 `FALSE`),此時 PaneDock 應該繼續正常運作,不能已經把自己的 view 都摧毀了。只要 session document 已經寫成 `clean_shutdown = true`,就算之後關機被取消、使用者繼續用一陣子又真的當機,體驗上只是「少抓到那一次意外」,遠好於現在的「每次都誤報」。
2. **新增 `WM_ENDSESSION` 處理:當 `wParam == TRUE`(結束確實會發生)時,呼叫 `destroy_explorers(*state)`(比照 `WM_CLOSE` 的收尾),不重複呼叫 `save_now`(`WM_QUERYENDSESSION` 已經寫過)。當 `wParam == FALSE`(結束被取消)時什麼都不做——不需要把 `clean_shutdown` 改回 `false`,因為程式仍在執行中,下一次真正的 mutation(`save_now(*state)` 預設值)本來就會自然改回 `false`,不需要本票特別處理。** 依 `AGENTS.md`「Never destroy a parent HWND while a view is alive... on shutdown destroy all views before the message loop exits」,`WM_ENDSESSION` 之後系統會強制結束行程,`destroy_explorers` 是 best-effort(系統可能不給完整時間跑完),但呼叫它比完全不呼叫更接近規則要求,且成本是幾行程式碼。
3. **不新增計時器或逾時保護。** `WM_QUERYENDSESSION` 的處理只有一次 session document 寫入(既有的 atomic replace,已知在毫秒等級),不會拖慢系統關機到需要額外保護的程度;比照現有 `WM_CLOSE` 完全沒有逾時保護的既有先例。
4. **不處理 `WM_POWERBROADCAST`(睡眠/休眠)。** 睡眠/休眠不會終止行程,`clean_shutdown` 欄位語意是「行程是否正常結束」,不是「電腦是否進入省電模式」,喚醒後行程還在原地繼續跑,沒有「重新啟動讀到 false」的問題,不在本票範圍內。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`docs/design-spec.md` §9.4 關閉序列:
> 1. 擷取現行狀態並原子寫入 session document 2. destroy 全部 live `IExplorerBrowser` 3. destroy pane HWND

`docs/tickets/PD-025-crash-recovery-path.md` 已確認的產品決策 2:
> 標記時機:啟動讀取成功後立刻寫一次 `clean_shutdown = false`;正常關閉的那一次寫入寫 `true`。執行期的 `save_now()` 一律寫 `false`。

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `window_proc`——`WM_CLOSE`(第 1868 行附近)分支的確切內容,新分支要對齊它的收尾動作;`save_now`(第 812 行)簽章;`capture_window_placement`;`destroy_explorers`。
- Win32 `WM_QUERYENDSESSION`/`WM_ENDSESSION` 官方語意(`lParam` 的 `ENDSESSION_*` 旗標本票不需要判斷,只看 `wParam`)。
- `docs/tickets/PD-025-crash-recovery-path.md` 全文,特別是它的驗收清單第 2 條(用 `Stop-Process -Force` 模擬崩潰的既有驗證方式)——本票要新增一條對應的、模擬系統關機的驗證方式。

## Scope

1. `window_proc` 新增 `case WM_QUERYENDSESSION:` 分支:`state != nullptr` 時執行 `capture_window_placement(window, *state); save_now(*state, true);`,回傳 `TRUE`。
2. `window_proc` 新增 `case WM_ENDSESSION:` 分支:`if (wparam) { if (state != nullptr) destroy_explorers(*state); }`,回傳 `0`(依 MSDN,`WM_ENDSESSION` 沒有回傳值語意,但 window procedure 仍需回傳一個值,比照其餘分支回傳 `0`)。
3. 兩個分支都放在既有 `WM_CLOSE`/`WM_DESTROY` 分支旁邊,維持目前 `switch` 的排列風格,不新增額外的輔助函式(邏輯本身只是重用既有函式呼叫)。

## Non-goals

- 不處理 `WM_POWERBROADCAST`/睡眠喚醒(已確認的產品決策 4)。
- 不新增逾時保護或非同步寫入(已確認的產品決策 3)。
- 不修改 `PD-025` 已定義的 `clean_shutdown` 欄位語意、序列化位置或 `SessionDocument` schema。
- 不修改 `core::session.h`/`session.cpp`——本票純粹是 `app_shell` 多攔截兩個既有訊息,呼叫既有函式。
- 不新增自動化的「模擬登出」整合測試(Windows 登出/關機在自動化測試環境下難以安全模擬,比照 `docs/tickets.md`「端到端 UI 自動化」已否決方向的精神,用下方的手動驗證方式取代)。

## Acceptance

1. 正常關閉(點 X 或 Alt+F4)行為與改版前一致,`session.json` 的 `clean_shutdown` 為 `true`,下次啟動不顯示警示。
2. 用 `Stop-Process -Force` 模擬崩潰(PD-025 既有的驗證方式)時,下次啟動仍然正確顯示「不乾淨關閉」警示——本票不能讓真正的崩潰偵測失效。
3. 在真實桌面上執行「開始 → 重新啟動」或「登出」,PaneDock 正在執行,重開機/重新登入後啟動 PaneDock,**不**顯示「不乾淨關閉」警示,且 `session.json` 記錄的 Group/pane/tab/location 與關機前一致(因為 `WM_QUERYENDSESSION` 時已經 `capture_window_placement`/`save_now(true)`)。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "WM_QUERYENDSESSION|WM_ENDSESSION" src\app_shell\main.cpp
# 預期:兩個訊息都有對應的 case 分支
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動(可自動化的部分):啟動後直接用 Stop-Process -Force 終止,重開確認警示仍會出現(Acceptance 2)
# 手動(需要真實桌面,不建議在 CI/自動化環境執行):啟動 PaneDock,執行一次真正的
# Windows 重新啟動或登出/登入,確認重開機後啟動 PaneDock 不顯示「不乾淨關閉」警示(Acceptance 3)
```

## Handoff requirements

- 兩個新分支的最終程式碼位置(行號可能因 PD-028~031 的同期改動而變動,交接時以函式名稱定位)。
- Acceptance 3(真實重開機/登出驗證)是否已經在真實桌面上執行過,若沒有,誠實記錄「僅完成程式碼與 Stop-Process -Force 驗證,重開機驗證留待你方便時手動確認」,不得宣稱已完成未驗證的項目。
- 若測試時發現 `WM_ENDSESSION` 在某些 Windows 版本下並未實際送達 `destroy_explorers` 完整執行時間(行程被系統提前砍斷),記錄下來,判斷是否需要接受這個已知限制(目前判斷:可接受,因為 `WM_QUERYENDSESSION` 階段已經完成最重要的 session 存檔,`destroy_explorers` 只是錦上添花的資源清理,行程結束後 OS 本來就會回收所有 handle)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-25 — 實作完成:WM_QUERYENDSESSION / WM_ENDSESSION 處理

**執行環境備註**:本次實作在一個從舊 commit(`cac3a7a`,只到 PD-006)分叉的 git worktree 中進行;worktree 的 `main` 分支引用一開始也停在 `2e596cb`(PD-028)。已先執行 `git merge main --ff-only` 把 worktree 的工作分支快轉到 `2e596cb`,取得包含 `src/app_shell/main.cpp` 完整內容的版本,再開始改動。本 ticket 文件與 `docs/tickets.md` 的 PD-032 列在該 worktree 的 git 歷史中原本不存在(尚未被合併進 `main`),因此本次連同 ticket 文件與總覽表列一併在 worktree 中建立/更新,內容取自共用 checkout(`E:\GitHub\PaneDock`)當時的工作目錄版本,交由協調者合併回主線時核對是否與其他並行 agent 的版本一致。

**兩個新分支的最終位置(以函式名稱定位)**:在 `src/app_shell/main.cpp` 的 `window_proc` 內,緊接在既有 `case WM_DESTROY:` 分支之後、`default: break;` 之前,新增:

```cpp
case WM_QUERYENDSESSION:
    if (state != nullptr) {
        capture_window_placement(window, *state);
        save_now(*state, true);
    }
    return TRUE;
case WM_ENDSESSION:
    if (wparam) {
        if (state != nullptr) destroy_explorers(*state);
    }
    return 0;
```

與 Scope 1–3 描述完全一致,未新增輔助函式,沿用既有 `capture_window_placement`/`save_now`/`destroy_explorers`。

**Agent checks 執行結果**:
- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` — 成功(LLVM-MinGW Clang 22.1.8)。
- `cmake --build build` — 全部 20 個目標成功建置,`PaneDock.exe` 產出。僅有既有的、與本票無關的 `missing field 'history' initializer` 警告(`src/core/model.cpp`、`session.cpp` 及對應測試檔),改動前即存在,本票未觸碰這些檔案。
- `ctest --test-dir build --output-on-failure` — 4/4 測試通過(`panedock_diagnostic_flag`、`panedock_core_model`、`panedock_core_layout`、`panedock_core_session`)。
- `rg -n "WM_QUERYENDSESSION|WM_ENDSESSION" src\app_shell\main.cpp` — 兩個 case 分支皆命中(行 1803、1809)。
- `git diff --check` — 無輸出,通過(無空白字元問題)。

**Acceptance 1(正常關閉)**:未額外重測——`WM_CLOSE` 分支本票未改動一個字元,行為與改版前一致,且既有的 4 個 ctest 全數通過未受影響。

**Acceptance 2(`Stop-Process -Force` 模擬崩潰,驗證未破壞既有崩潰偵測)—— 已在本機實際執行,結果如下**:
1. 啟動 `.\build\PaneDock.exe`,3 秒後讀取 `%LOCALAPPDATA%\PaneDock\session.json`,確認 `"clean_shutdown":false`(執行期預設值,符合 PD-025 決策)。
2. 執行 `Stop-Process -Name PaneDock -Force` 強制終止,行程確認消失;強制終止後重讀 `session.json`,`clean_shutdown` 仍為 `false`(因為既有 `WM_CLOSE` 路徑與新增的 `WM_QUERYENDSESSION` 路徑都沒有機會執行——`Stop-Process -Force` 直接終止行程,不送任何視窗訊息,這正是 PD-025 原本要偵測的「非正常終止」情境)。
3. 重新啟動 `.\build\PaneDock.exe`,以 Win32 `EnumWindows` 列舉該行程的頂層可見視窗,確認除了 class 為 `PaneDockMainWindow` 的主視窗外,還有一個 class 為 `#32770`(標準對話框類別)的視窗——即 PD-025 的「did not shut down cleanly」警示對話框,證實崩潰偵測仍正確觸發。
4. 測試完畢以 `Stop-Process -Name PaneDock -Force` 清理殘留行程。

結論:本票新增的 `WM_QUERYENDSESSION`/`WM_ENDSESSION` 處理只攔截系統送出的關機/登出訊息,不影響 `Stop-Process -Force` 這種直接終止行程的路徑,真正的崩潰偵測未被破壞。

**Acceptance 3(真實桌面重新啟動/登出)—— 誠實記錄:尚未執行**。本次工作在 agent 沙箱/worktree 環境中進行,無法安全觸發真正的 Windows 系統重新啟動或登出流程(會影響整台機器與其他並行 agent 的工作階段)。僅完成程式碼實作與 Acceptance 2 的 `Stop-Process -Force` 驗證,**Acceptance 3 的真實重開機/登出驗證留待你方便時在真實桌面上手動確認**——步驟可直接沿用本 ticket「Agent checks」區塊最後一段的手動驗證指示:啟動 PaneDock、執行一次真正的「開始 → 重新啟動」或「登出」,重新登入後啟動 PaneDock,確認不出現「不乾淨關閉」警示,且 `session.json` 的 Group/pane/tab/location 與重開機前一致。

**`WM_ENDSESSION` 的已知限制**:本次驗證環境中 `WM_ENDSESSION` 未被自然觸發(需要真正的系統關機/登出訊息,見上一段),因此 `destroy_explorers` 在 `WM_ENDSESSION` 分支中的實際執行時間未在真實關機場景下驗證。依 ticket 原先的判斷維持不變:可接受,因為最關鍵的 session 存檔已經在 `WM_QUERYENDSESSION` 階段以 `save_now(*state, true)` 完成,`destroy_explorers` 只是錦上添花的資源清理,即使行程被系統提前砍斷、`destroy_explorers` 沒有跑完,OS 本來就會回收所有 handle,不影響下次啟動的乾淨關閉判定。

**建置警告**:建置輸出的 4 則 `missing field 'history' initializer` 警告均與本票無關,出現在 PD-028 快轉後的既有程式碼中(`src/core/model.cpp`、`src/core/session.cpp` 及對應測試檔),本票的 Non-goals 明確排除修改 `core::session.h`/`session.cpp`,故未處理。
