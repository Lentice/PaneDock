# PD-087 — 版型從多 pane 縮到少 pane 時,被隱藏的 pane 沒有 destroy 其 `IExplorerBrowser`,重新變多 pane 時顯示過期資料夾

## 來源

2026-08-27 三方稽核(Claude / Codex / OpenCode)。三份報告**各自獨立**都指出同一段程式碼:`apply_layout` 隱藏多餘 pane 的分支只 `ShowWindow(SW_HIDE)`,沒有呼叫 `destroy()`。這是本次稽核中收斂程度最高(三方一致)的發現之一。

## 背景與現況

`apply_layout`(`src/app_shell/main.cpp:1970-1990` 一帶)與 `set_layout`(`main.cpp:2414`/`2437` 一帶)負責依目前 Group 的版型設定要顯示幾個 pane。當版型從 4 pane 改成 1 pane 時,多出來的 3 個 pane 只是:

```cpp
// 隱藏分支(概念示意,實際行號見上述引用)
ShowWindow(container, SW_HIDE);
state.visible[index] = false;
```

**沒有**呼叫 `explorers[index].destroy()`,`state.realized[index]` 仍然是 `true`。這代表:

- 這 3 個已初始化的 Shell view(含各自的縮圖快取、shell extension DLL、資料夾監看)在整個 session 期間持續存活,只是不可見。
- `live_view_count()` 在單 pane 版型下仍然回報 4,與實際「應該只剩 1 個可見 pane」不符。
- 之後若版型再改回多 pane,`apply_layout` 針對「已經 realize」的 pane 分支只呼叫 `set_rect` 調整位置,**不會重新導覽**——於是這些 pane 顯示的是縮小版型之前、甚至更早的舊資料夾,而不是目前 Group 分頁記錄的路徑。

## 為什麼這是真的問題

`docs/design-spec.md` 與 `AGENTS.md`(「Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs persist as data and are realized on activation. This is what keeps memory bounded」)明確把「只有可見 pane 的 active tab 持有 live view」當成記憶體上限的保證條件。目前的實作违反了這個條件——這不是可見度優化的問題,是文件明確承諾但未兌現的行為,也連帶讓「版型縮小又放大」變成一個會顯示錯誤資料夾的 user-visible bug。

## Fix 方向

在 `apply_layout` 隱藏 pane 的分支,若該 pane 目前 `realized[index] == true`,先呼叫 `explorers[index].destroy()` 再隱藏,並把 `realized[index]` 設回 `false`:

```cpp
if (state.realized[index]) {
    state.explorers[index].destroy();
    state.realized[index] = false;
}
ShowWindow(container, SW_HIDE);
state.visible[index] = false;
```

`realized[index] == false` 之後,版型再放大時該 pane 會走既有的「尚未 realize」分支——這個分支本來就會完整 `initialize` + `navigate`,因此規模放大時會自然拿到正確的目前路徑,不需要額外寫「重新導覽」的邏輯。

Group 切換(`activate_group`)路徑不受影響,因為它從不會把已顯示的 pane 數量往下縮(縮放 pane 數量只發生在版型變更,不是 Group 切換),符合 `AGENTS.md`「Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups」的既有規則——本票只處理版型變更時 pane **數量**減少的情境,不處理、也不影響 Group 切換時同一批 pane 的保活邏輯。

## 綁定限制(引用)

- `AGENTS.md`:「Only the visible pane's active tab holds a live `IExplorerBrowser`. ... This is what keeps memory bounded」—— 本票直接修正這條規則目前未被滿足的缺口。
- `AGENTS.md`:「Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks.」—— 目前隱藏分支正是這條規則的違反案例。
- `AGENTS.md`:「Group switching keeps live views alive and re-navigates them. Do not destroy and recreate pane HWNDs to switch Groups」—— 注意這條規則講的是 **HWND**(pane 容器視窗)不要因為 Group 切換而摧毀重建;本票摧毀的是 **`IExplorerBrowser`**(Shell view 物件),且觸發時機是版型 pane 數量縮小,不是 Group 切換,兩者不衝突。容器 HWND 本身不受本票影響。

## 檔案與範圍

- `src/app_shell/main.cpp`:
  - `apply_layout`(約 1947-1990 行,含已 realize 分支與隱藏分支)
  - `set_layout`(約 2414-2437 行一帶,呼叫 `apply_layout` 的路徑)
  - `live_view_count` 相關寫入(確認縮小後正確回報存活 view 數)

## Scope

1. 在 `apply_layout` 隱藏 pane 的分支加上「已 realize 才 destroy,並清 `realized` 旗標」的邏輯。
2. 確認版型放大時,原本「尚未 realize」分支的既有 `initialize`+`navigate` 邏輯能正確處理「這個 pane 之前被縮小又放大」的情境(不需要新增分支,只需要確認既有分支涵蓋這個情況)。
3. 確認 `live_view_count()` 在縮小版型後正確反映減少的存活 view 數(若有獨立的計數欄位需要同步更新,見 `src/explorer_host/live_view_count.h`)。

## Non-goals

- 不處理 Group 切換路徑(那邊「保活、重新導覽」是既有正確行為,不動)。
- 不新增「延遲銷毀」或「LRU 快取」之類的優化——`AGENTS.md` 的規則是「只有可見 pane 持有 live view」,直接照規則做,不要自行加額外的保留策略。
- 不改變 `kExplorerCount`(pane 上限 4)這個常數。

## Acceptance Criteria

1. 一個 4-pane Group,切到 1-pane 版型後,`live_view_count()` 回報 1(或等價機制確認只剩 1 個 live `IExplorerBrowser`)。
2. 同一個 Group 從 1-pane 版型切回 4-pane 版型,4 個 pane 顯示的都是該 Group 目前分頁記錄的正確路徑,不是縮小前的舊資料夾。
3. 反覆縮放版型(4→1→4→2→4)多次,不出現殘留的舊路徑畫面,也不出現記憶體持續增長(可用工作管理員粗略觀察 handle count 或 working set 是否隨縮放次數線性增長)。
4. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

> **驗證政策提醒:** 單一點擊/單一操作 + 截圖由 Agent 自行完成即可;需要多次縮放版型並持續觀察記憶體變化的長時間驗證,留給使用者在實機上驗證。完成後在交接區寫清楚哪些是自己驗證過的、哪些留給使用者。

## 交接區

### 實作

- `src/app_shell/main.cpp` 的 `apply_layout` 在隱藏多餘 pane 前，若該 pane 已
  `realized`，現在會呼叫 `ExplorerHost::destroy()` 並清除
  `state.realized[index]`。
- `ExplorerHost::destroy()` 會釋放該 view 的 `LiveViewRegistration`，因此
  `live_view_count()` 會隨版型縮小正確遞減；版型放大時同一 pane 會重新走既有的
  `initialize` + `navigate` 路徑。
- 沒有新增自動測試：這段邏輯直接依賴 Win32 HWND 與真實 `IExplorerBrowser`，不在
  專案的 `core` 自動測試 seam；以本票 Agent Checks 加上真實桌面驗證取代 fake。

### Agent Checks

以下命令均成功：

```text
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- Configure：成功。
- Release build：成功，完成 `PaneDock.exe` link。
- CTest：`5/5` 通過（`panedock_diagnostic_flag`、`panedock_tab_overflow`、
  `panedock_core_model`、`panedock_core_layout`、`panedock_core_session`）。
- `git diff --check`：成功。

### Acceptance Criteria 驗證狀態

1. **部分由程式碼驗證，實機結果留給使用者**：已確認隱藏已 realize pane 會
   `destroy()`、清除 `realized`，且計數由 `LiveViewRegistration` 遞減；未在本次
   Agent session 啟動真實 Shell UI 量測 4→1 後的 stdout 數值。
2. **部分由程式碼驗證，實機結果留給使用者**：已確認 1→4 時清除後的 pane 會走
   既有未 realize 分支，使用目前 Group tab 的 parsing name 初始化並導覽；未以真實
   視窗截圖逐 pane 比對路徑。
3. **留給使用者**：需要在實機反覆執行 4→1→4→2→4，觀察無舊路徑殘留，並以工作
   管理員或 handle/working-set 取樣確認沒有隨次數線性增長。
4. **已驗證**：configure、`cmake --build build`、`ctest --test-dir build
   --output-on-failure` 均成功。
