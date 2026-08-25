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

### 2026-08-25 實作交接

**重現與根因(有程式碼證據)**

先在 `ExplorerHost::initialize`/`navigate`/`set_visible`/`navigation_complete` 加上臨時 `OutputDebugStringW` instrumentation(含 `GetTickCount`、view HWND、`IsWindowVisible`、`GetUpdateRect`),用一支自寫的 PowerShell DBWIN capture 腳本(P/Invoke `DBWIN_BUFFER`/`DBWIN_DATA_READY`,不依賴 DebugView)在非互動啟動下擷取實際發生順序。關鍵一筆記錄:

```
navigation_complete view=00000000001616A0 visible=0 has_update=0 update=(0,0,0,0)
BrowseToObject returned hr=00000000 view=00000000001616A0
set_visible(1) view=00000000001616A0
```

證實:

1. 第一個 pane 的 `IExplorerBrowser::OnNavigationComplete`(進而觸發 `ExplorerHost::navigation_complete`)是在 `BrowseToObject` **回傳之前**同步觸發的——也就是 Shell view 在 `initialize()`(呼叫堆疊仍在 `WM_CREATE` → `apply_layout` 內)完成前就已經把內容建進 view 裡。
2. 在這個時間點,view HWND 的 `IsWindowVisible` 是 `FALSE`,且 `GetUpdateRect` 回傳空的 invalid region(`has_update=0`)——內容是在**隱藏狀態**下被建立/繪製到位的,且當下沒有留下任何「待重繪」的髒區。
3. `set_visible(true)`(在 `apply_layout` 迴圈尾端呼叫 `ShowWindow(window, SW_SHOW)`)是在那之後才發生的,而且原本的程式碼在 `ShowWindow(SW_SHOW)` 之後完全沒有補任何 `InvalidateRect`/`RedrawWindow` 呼叫。`ShowWindow(SW_SHOW)` 對一個「內容已經在隱藏狀態下畫好、目前沒有 dirty region」的視窗不保證會強制整個子視窗樹重繪——這就是空白的根因:內容其實已經在,只是从沒被要求「畫出來」,直到滑鼠移動造成的 `WM_NCHITTEST`/`WM_SETCURSOR`/游標命中測試連帶讓 Explorer view 自己重新驗證並繪製。

對應到已確認的產品決策 1:根因確實是「`BrowseToObject` 完成的當下,view 沒有收到讓它真正繪出內容的強制重繪」,但更精確地說——不是「非同步、之後才完成」,而是「**同步完成、但完成時視窗是隱藏的,而且完成後沒有人再強制它重繪**」。

**採用的修法(對應決策 2)**

在 `explorer_host.cpp`(共用程式碼,而非 `main.cpp` 各呼叫點)補了兩處 `RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN)`,對應決策 2 選項 (a)/(c) 的組合,理由如下:

1. **`ExplorerHost::navigation_complete()`**——在處理 pidl 之前,對當下的 view HWND 強制重繪一次。這是 `navigate()`(不論是 `initialize()` 內第一次呼叫,還是 Group 切換/tab 切換呼叫的後續 `navigate()`)完成的**唯一共用完成點**,涵蓋:
   - 初次載入(view 當時仍隱藏,`RedrawWindow` 先把 invalid region 種下,等之後 `set_visible(true)` 顯示時仍有髒區可畫,不會被讀成「本來就沒事要畫」)。
   - Group 切換與分頁切換的既有 `navigate()` 呼叫(此時 view 通常已可見,`RDW_UPDATENOW` 會立即強制真正重繪)。

   注意:`main.cpp` 的 `navigation_callback_`(`handle_navigation_complete`)**不能**拿來當作這個修法的掛載點——追蹤發現初次載入時 `OnNavigationComplete` 觸發於 `initialize()` 呼叫堆疊內,而 `set_navigation_callback` 要等 `initialize()` 回傳之後 `apply_layout` 才會設定,所以初次載入那一次 `navigation_callback_` 根本還沒被設定,呼叫不到。這也是為什麼決策 2 選項 (c) 提到的「在 `navigation_callback` 完成時」對初次載入無效,必須把修法放進 `ExplorerHost` 本體。

2. **`ExplorerHost::set_visible()`**——只在 `visible=true` 且*先前不可見*(`IsWindowVisible` 由 false 變 true)時,才多補一次同樣的 `RedrawWindow`。這是為了涵蓋「版面配置切換」路徑:既有已 realize 但目前隱藏的 pane(例如從 1-pane 切到 2-pane 時原本被隱藏的第 2 個 pane)只會走 `set_rect()` + `set_visible(true)`,不會重新呼叫 `navigate()`,所以 (1) 的掛載點蓋不到它,需要在可見度真正翻轉的那一刻補強制重繪。用 `was_visible` 判斷式把這個呼叫限制在「隱藏→顯示」這唯一一次轉換,刻意不讓它在每次 `apply_layout`(包含每次 `WM_SIZE` 觸發的視窗縮放)都重複執行,避免對已可見的 pane 造成不必要的整窗重繪(對照 `docs/performance-baseline.md` 的疑慮與 AGENTS.md 的 event-driven 原則)。

**Group 切換與版面配置切換是否共用同一路徑**

- **Group 切換 / 分頁切換**:兩者都呼叫既有 `ExplorerHost::navigate()`,最終觸發 `navigation_complete()`——與初次載入共用同一個修法掛載點,不需要在 `main.cpp` 另外加程式碼。
- **版面配置切換(1/2/4 pane)**:對「新建立」的 pane 走 `initialize()`(共用 `navigation_complete()` 的修法);對「既有 realize 但重新顯示」的 pane 走 `set_rect()` + `set_visible()`,不會觸發 `navigate()`,根因與初次載入相同(內容已在但沒人強制重繪),但**掛載點不同**——因此在 `set_visible()` 另外加了一份等價的強制重繪,而不是假設兩者共用同一行程式碼。這點與 Scope 第 3 項「若答案是 same code path,只需記錄;若不同,視情況一併處理」的授權一致:根因相同、觸發事件不同,已在同一張票內用兩個掛載點一次處理完。

**手動驗收的誠實狀態**

此環境沒有 Computer Use/GUI 互動工具,無法用真人滑鼠操作驗收。改用替代方式,且**過程中沒有送出任何合成滑鼠事件**(僅用 `Start-Process` 非互動啟動、鍵盤 `SendKeys`、`PostMessage(WM_CLOSE)` 做乾淨關閉):

1. 寫了一支 PowerShell screenshot 腳本(`System.Windows.Forms`/`System.Drawing` 截全螢幕),在非互動啟動 `PaneDock.exe` 後立刻(約 500ms~2s)截圖,確認全部 4 個 pane 立刻顯示真實檔案清單(`C:\`、`C:\Windows` 等),完全没有空白區塊。
2. 用 `Ctrl+Shift+L`(既有的版面配置切換 hotkey,鍵盤觸發、非滑鼠)在啟動後連續切換 1-pane → 2-pane,兩次切換後都立刻截圖確認新出現的 pane 立刻有內容,不需要任何滑鼠移動。
3. 沒有測試到 Group 切換的真實截圖(側欄清單需要滑鼠點擊或鍵盤方向鍵切換選取,目前預設只有一個 Group,未額外建立第二個 Group 來測試);這一段是**依程式碼路徑分析**(與初次載入/分頁切換共用 `navigate()` → `navigation_complete()`)推斷已覆蓋,而非實際截圖驗證,如需更嚴謹的信心,建議之後補一次「建立第二個 Group、用鍵盤在側欄按方向鍵切換、截圖確認」的驗證。
4. 修法前(未加 fix、只加 trace instrumentation 的版本)用同樣的非互動截圖方式測了初次載入,**沒有重現出空白畫面**(這台開發機上 `C:\`、`C:\Windows` 這類本機資料夾列舉太快,可能在測試視窗內就已經走完 Shell 內部重繪路徑,掩蓋掉這個競態)。但 DBWIN trace 已經證明「navigation_complete 觸發時 view 隱藏且沒有 dirty region、之後也没有任何強制重繪呼叫」這個程式碼層級的缺口確實存在,不是憑空猜測;本票採用的修法是直接補上這個缺口。

**效能影響**

`navigation_complete()` 每次導覽完成只觸發一次;`set_visible()` 的額外呼叫用 `was_visible` guard 限制在「隱藏→顯示」那一次轉換,不會在每次 `WM_SIZE`/`apply_layout` 重複執行。手動測試期間(啟動、兩次版面切換)沒有觀察到明顯延遲或閃爍。未做長時間或大資料夾的效能量測,若未來 `docs/performance-baseline.md` 要記錄真實數字,建議在該文件另開量測項目。

**其他**

- Build:`cmake --build build` 成功(僅重新編譯 `explorer_host` 相關目標)。
- Test:`ctest --test-dir build --output-on-failure` 4/4 全過。
- `git diff --check` 無輸出(通過)。
- 異動檔案僅 `src/explorer_host/explorer_host.cpp`;所有臨時 `OutputDebugStringW` instrumentation 已在驗證後移除,未留在最終 diff 裡。
