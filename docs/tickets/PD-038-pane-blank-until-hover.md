# PD-038 — Pane 初次載入後畫面空白,需要滑鼠 hover 才刷新

Phase 6 · app_shell, explorer_host · Depends on: PD-030

- Source: 使用者比對 `.\build\PaneDock.exe` 實際畫面與理想稿後回報(2026-08-25)。
- Origin: 「pane 一開始是空白的,需要滑鼠 hover 才會刷新」。
- Priority: HIGH——這是功能性缺陷,不是視覺差距:使用者開啟程式或切換 Group 後第一眼看到的是空白區塊,必須做一個滑鼠動作才能看到本來就該顯示的檔案清單,直接影響「開啟就能用」的核心體驗。

## 已確認的產品決策

1. **根因是 `IExplorerBrowser` 建立/`Initialize`/`BrowseToObject` 完成的當下,Shell view 的原生 `SysListView32`(或其宿主 HWND)還沒有收到讓它真正繪出內容的訊息,直到之後某個會觸發重繪或版面重算的訊息(滑鼠移動造成的 `WM_NCHITTEST`/`WM_SETCURSOR`,或 Explorer view 自己在收到滑鼠事件時重新 layout)才畫出來。** 這是已知的 `IExplorerBrowser` 宿主常見坑:`IExplorerBrowser` 的 `Initialize`/`BrowseToObject` 不保證同步觸發一次完整的可視繪製,尤其在視窗尚未第一次被 `WM_SIZE`/`WM_SHOWWINDOW` 完整處理過的情況下。本票不假設「就是這個原因」,把「用程式碼證據找出實際卡在哪一步」列進 Scope 第 1 項,而不是直接跳去套一個猜測的修法。
2. **修法方向鎖定在 host 端主動觸發一次重繪/重新版面配置,而不是要求使用者做任何動作。** 候選手段依優先順序:(a) `Initialize`/`navigate` 完成後對 `IExplorerBrowser` 呼叫 `IExplorerBrowser::SetPropertyBag`/existing view 的 `IFolderView2::Refresh` 等價操作,或直接對取得的 view HWND 呼叫 `InvalidateRect(..., TRUE)` + `UpdateWindow`;(b) 若上述不夠,對外層 pane 容器 HWND 補送一次 `WM_SIZE`(用目前的 rect 重新 post 一次,強制觸發 Explorer view 内部的 layout 重算路徑);(c) 若以上都不能穩定重現修好,退回在 `navigation_callback` 完成時明確地对 `state.explorers[index]` 的宿主區域呼叫 `RedrawWindow` 加上 `RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN`。**採用能解決問題的最小手段即可,不必三個都做**,實作 agent 應該先驗證問題重現、再依序嘗試,把「哪個手段真正解決了」寫進交接區。
3. **不透過「监听滑鼠移動並手動觸發刷新」來解決。** 那是在複製使用者目前用來繞過 bug 的手動動作,而不是修根因;會導致每次滑鼠移動都做一次不必要的重繪工作,違反 `AGENTS.md` 的「Event-driven idle path only」精神(重繪應該綁在真正的狀態改變事件——初始化完成、導覽完成——而不是滑鼠位置)。
4. **確認這個問題是否也發生在「切換 Group」(既有 view 被重新導覽,而非新建立)與「切換版面配置(layout)」時,並在同一次修正內一併處理,不分成兩張票。** 這兩種情境都會呼叫既有的 `navigate`/`set_rect` 路徑,如果根因出在「導覽完成後沒有强制重繪」,理論上是同一個根因,理當同一個修法一次解決;若調查後發現這兩種情境的根因不同,才視情況拆票並在交接區說明理由。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

`AGENTS.md`:
> Read the relevant spec section and trace every caller before touching shared code. A guard in the shared function is a smaller diff than a guard in every caller, and patching only the path the ticket names leaves sibling callers broken.

## Files to read and trace first

- `src/explorer_host/explorer_host.cpp` 的 `initialize`、`navigate`、`set_rect`、`set_visible`——找出目前 `Initialize`/`BrowseToObject`/`SetRect` 呼叫序列,確認有沒有任何強制重繪呼叫,或是否倚賴 Shell 自己的非同步繪製。
- `src/app_shell/main.cpp` 第 1416 行起的 `apply_layout`(pane 初次 `initialize` 的呼叫點)與 `capture_pane_location`/`handle_navigation_complete`(既有的 `navigate` 完成回呼路徑)——確認初次建立與後續切換 Group/tab 分別經過哪些函式。
- Windows SDK `IExplorerBrowser`/`IFolderView2` 文件——確認是否有官方建議的「導覽完成後強制刷新」API,避免發明一個 Shell 本身已經提供正規做法的手段。
- `docs/performance-baseline.md`——若修法涉及額外的 `InvalidateRect`/`RedrawWindow` 呼叫,確認不會變成每次導覽都觸發整個視窗重繪造成的效能疑慮(單一 pane 的重繪不等於整個應用程式重繪,預期影響可忽略,但仍需在交接區記錄有沒有觀察到異常)。

## Scope

1. 重現問題並用程式碼追蹤/記錄(可用 `OutputDebugStringW` 暫時 instrumentation,驗證後移除)确认卡住的確切位置:是 `Initialize` 之後、`navigate` 之後,還是兩者都有。
2. 依決策 2 選定的手段,在 `explorer_host.cpp` 或 `main.cpp` 對應的初始化/導覽完成路徑補上強制重繪呼叫。
3. 驗證 Group 切換、layout 切換兩種既有路徑(決策 4)是否也需要同一處補丁,若答案是「same code path already covered」則不需要额外改動,只需在交接區記錄驗證過程。

## Non-goals

- 不透過監聽滑鼠事件手動觸發刷新(已確認的產品決策 3)。
- 不改變 `IExplorerBrowser` 的初始化參數、`Advise`/`Unadvise` 生命週期管理邏輯(那是既有正確的部分,問題出在畫面呈現,不在物件生命週期)。
- 不新增輪詢或計時器來定期強制刷新(違反 `AGENTS.md` 的 event-driven idle path 規則)。

## Acceptance

1. 全新啟動程式、載入一個有內容的 Group 後,不需要任何滑鼠移動,每個 pane 立刻顯示正確的檔案清單。
2. 切換到另一個 Group 後,新載入的 pane 立刻顯示內容,不需要滑鼠移動。
3. 切換版面配置(1/2/4 pane)後,新出現或改變大小的 pane 立刻顯示內容,不需要滑鼠移動。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動(關鍵驗收,必須完成,不能只跑 build/test 就宣稱完成):
# 啟動程式後完全不移動滑鼠,確認畫面立刻有內容;
# 切換 Group、切換 layout 時重複驗證同樣結果
```

## Handoff requirements

- 實際重現到的根因(是 `Initialize` 沒有觸發同步繪製、還是別的原因),與採用的修法對應決策 2 的哪一個選項。
- 若三個候選手段都不夠、需要別的做法,記錄實際採用的做法與原因。
- Group 切換與 layout 切換是否共用同一個修法路徑的驗證結果。
- 若 Computer Use 環境限制導致無法用真實滑鼠完成手動驗收,誠實記錄,並說明用什麼替代方式(例如非互動啟動＋截圖比對開啟瞬間畫面)驗證過這個修正。

## 交接區

<!-- 實作 agent 填寫,append-only -->
