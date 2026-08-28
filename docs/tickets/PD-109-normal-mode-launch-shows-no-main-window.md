# PD-109 — 一般模式啟動後主視窗未出現

Phase 7 · app_shell · Depends on: PD-093, PD-106

- Source: 使用者回報「PaneDock.exe 本身開啟後空白/沒畫面」(2026-08-28)；由執行 PD-098 驗證的 Codex session(`codexpd`)獨立重現。
- Priority: HIGH——這是啟動路徑的可重現故障，會擋住所有後續 UI 驗收。

## 已確認的現況(有實測證據)

1. `codexpd` 對 `build\PaneDock.exe` 一般模式(未帶 `--diagnostic`)啟動兩次，每次以 `FindWindowW`/`EnumChildWindows` 輪詢 13 次、每次間隔 400ms(共 5.2 秒),兩次都沒有找到任何主視窗。
2. 同一時間點只存在一個 `PaneDock.exe` process(PID 35748),`Get-Process` 顯示 `MainWindowHandle=0`；已排除「單一實例 mutex 把新啟動轉導到既有視窗」的可能(`wWinMain`,`main.cpp:4245-4255`)，因為當時只有這一個 process,不是第二個等待 activate 的 process。
3. 對該 PID 送出不帶 `/F` 的 `taskkill /PID` 回報成功,但 process 沒有結束,長時間維持 `Responding=True`。
4. `codexpd` 當時的工作樹上，`src/app_shell/main.cpp` 唯一的未 commit 差異是 PD-098 的 glyph 常數與 fallback 繪製切換(`kNavigationGlyphs[4]` 與 `draw_navigation_fallback_glyph` 的 `case 4` 分支)——純繪製程式碼，不觸及視窗建立、`WM_CREATE`、`apply_layout` 或 shell view 初始化路徑，可排除為本症狀成因。
5. 本機目前的 `%LOCALAPPDATA%\PaneDock\session.json` 有 55 個 Group（測試累積），但 active Group(`default`)的四個 pane 與其 active tab 全部指向本機路徑(`C:\`、`C:\Windows`、`C:\Users`、`C:\Program Files`、資源回收桶已知資料夾 GUID)，沒有離線磁碟機或網路路徑；不能排除 55 個 Group 造成 `refresh_sidebar` 或 owner-draw 列表的額外耗時，但沒有證據顯示這會造成「永遠沒有視窗」而非「延遲出現」。
6. `wWinMain`(`main.cpp:4244-4386`)的視窗建立序列：`CreateWindowExW`(`:4335`，若失敗直接清理並 return，不會停留)→ `ShowWindow`/`UpdateWindow`(`:4347-4348`)→ 視情況彈出三種 `MessageBoxW` 警告(`:4349-4373`，本機 `session.json` 目前 `clean_shutdown:true` 且非 corruption 復原，理論上不會觸發)→ `PostMessageW(kDeferredRealizeMessage)`(`:4376`)或其失敗時的同步 fallback `apply_layout`(`:4382`)。`WM_CREATE`(`:3398-3572`)本身已同步呼叫一次 `apply_layout(window, *state)`(`:3552`，未帶 `deferred=true`)，失敗時彈出 `MessageBoxW`「PaneDock could not open the Shell view.」並 `return -1`（此時 `CreateWindowExW` 會回傳 `nullptr`，行為與觀察到的「process 存活但無視窗」不符）。
7. 目前不清楚 `codexpd` 兩次啟動測試當下，`session.json` 是否恰好與本票第 5 點檢視時相同（`codexpd` 的 build 與本次檢視之間，另一個背景 fork 也曾啟動/關閉 PaneDock 做驗證，可能已改變 `active_group_id` 或新增/移除 Group）——實作 agent 必須在動手修正前重新確認當下的 session 內容與啟動時序，不能沿用本票列出的舊快照當作啟動當下的事實。

## Binding constraints — quoted, do not weaken

`docs/design-spec.md §9.3 啟動序列`:

> 3. 讀取 session document(失敗則退回備份,再失敗則以預設 Group 啟動)
> 4. 建立主視窗與側邊欄,套用視窗位置
> 5. 套用 active Group 的版型,**先 realize active pane 的 active tab**
> 6. 其餘 pane 延後 realize,避免被網路或離線路徑阻塞

`AGENTS.md`:

> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

> Keep changes scoped to the ticket, and update the affected documents when behavior changes.

`docs/development.md`:

> Shell APIs re-enter our message loop. Any host-side lock, and any "close the view then wait for its event" sequence, must be reentrancy-safe or it will deadlock.

## Files to read and trace first

- `src/app_shell/main.cpp:4244-4386`——`wWinMain` 的 mutex、DPI、session 讀取、`CreateWindowExW`、`ShowWindow`、三種啟動警告 `MessageBoxW`、`kDeferredRealizeMessage` 派送與同步 fallback。
- `src/app_shell/main.cpp:3398-3572`——`WM_CREATE` 內的所有子控制項建立與第一次同步 `apply_layout` 呼叫。
- `src/app_shell/main.cpp:1971-2150`（`apply_layout` 全文，先前 PD-108 已詳細讀過）——尤其是 `deferred` 參數如何決定哪些 pane 被 realize、`state.explorers[index].initialize(...)`/`navigate(...)` 的呼叫點。
- `src/explorer_host/explorer_host.cpp`——`ExplorerHost::initialize`/`navigate` 的同步 COM 呼叫是否可能因為特定 shell location 阻塞或丟出未處理的例外/HRESULT。
- `docs/tickets/PD-093-startup-eagerly-realizes-all-panes-violates-deferred-realize-spec.md`——目前「先 realize active pane、其餘延後」修正的實際範圍與驗收證據，確認 fix 是否真的只 realize 一個 pane。
- `docs/tickets/PD-106-graceful-shutdown-shell-teardown.md` 交接區——本票直接沿用的階段計時 probe 方法論（`[PD106-PROBE]`/`[PD106-PROBE2]`，一次性 stderr instrumentation，量測到 ms 等級，完成後移除），這次要對**啟動**路徑（`wWinMain` 進入 → `CreateWindowExW` 返回 → `WM_CREATE` 內每個子步驟 → 第一次 `apply_layout` → `ShowWindow`/`UpdateWindow` → 訊息迴圈開始收到第一個訊息）做相同等級的證據蒐集，而不是對關閉路徑。

## Fix 方向 / Scope

1. 先重現：使用 Release 一般模式（不帶 `--diagnostic`）啟動 `build\PaneDock.exe`，用真實 `EnumWindows`/`FindWindowW`/`EnumChildWindows`（不得猜測座標或用 `CopyFromScreen`）與足夠長的輪詢視窗（不得只用 5 秒就判定「沒有視窗」；先抓到底是「視窗真的沒建立」還是「建立了但顯著延遲」）確認症狀仍然存在於目前 HEAD。
2. 若仍可重現，比照 PD-106 的方法論，在 `wWinMain`/`WM_CREATE`/`apply_layout`/`ExplorerHost::initialize`/`navigate` 沿線加一次性、有時間戳記的 stderr probe（例如 `[PD109-PROBE]`），量出實際卡住或耗時異常的那一步，而不是憑猜測改動作順序或直接加 timeout/重試掩蓋。
3. 找到確切阻塞點後，用符合 §9.3（先 realize active pane、其餘延後）與既有單一 STA/COM apartment 規則的最小修正解決；若阻塞點在 `IExplorerBrowser::Initialize`/`Navigate` 本身的 Shell 回入，比照 PD-106 決策，不建立背景執行緒或非同步 runtime。
4. 若第 1 步無法重現（例如症狀其實跟隨測試環境的 `session.json` 內容或啟動時序而非產品碼本身），必須記錄清楚的重現/無法重現條件與證據，不得把單次沒重現當作「已修好」；如果最終判定是測試環境本身的殘留狀態（例如某個 fork 遺留的鎖或程序）而非產品碼缺陷，在交接區記錄清楚並把本票標記為需要人工複查，而不是自行宣稱驗收通過。
5. 完成後移除所有暫時 debug probe，在交接區留下實測的各階段耗時與最終結論（含「找到真因並修正」與「重現條件不成立、判定非產品碼缺陷」兩種可能結果各自需要的證據標準）。

## Non-goals

- 不改變 §9.3 的「先 realize active pane、其餘延後」順序本身；本票只修正該順序被違反或被阻塞的地方。
- 不在本票修改 `src/core`，不為 `IExplorerBrowser` 新增 fake／抽象測試 seam。
- 不清理 `%LOCALAPPDATA%\PaneDock\session.json` 裡累積的測試用 Group／預設值；若懷疑 Group 數量或內容是本次症狀的成因,只需在交接區記錄觀察與判斷依據,不在本票新增「Group 數量上限」或「啟動時裁剪 session」之類的新行為（如需要,另開票）。
- 不用 `TerminateProcess`、`taskkill /F`、強制關閉或跳過既有關閉序列來讓症狀「看起來」消失。
- 不新增啟動 timeout、重試迴圈或背景 worker 之類會製造 polling/busy loop 的解法。

## Acceptance Criteria

1. 明確判定本票的兩種結果之一，並附實測證據：
   (a) 找到真正的啟動阻塞/失敗根因並修正，Release 一般模式下連續 3 次啟動都能在合理時間內（記錄實測 ms，並與 PD-106 已驗收的關閉時間量級比較是否合理）看到可見的 `PaneDockMainWindow`；或
   (b) 依既有證據判定目前 HEAD 的啟動路徑本身沒有缺陷，原始回報是測試環境（殘留程序、非本票管轄的 session 內容等）造成的偽陽性，並列出支持此判定的實測步驟與結果。
2. 若做了修正,`apply_layout` 對 active pane 以外的 pane 仍然遵守延後 realize；不得為了「快一點顯示視窗」而把其餘 pane 一併同步 realize（那是 PD-093 已修正、明文禁止重犯的方向）。
3. `cmake --build build`、`ctest --test-dir build --output-on-failure`、`git diff --check` 全數通過。
4. `rg` 確認暫時 debug prefix、新增的 timeout/重試/background thread 均未留在產品程式碼。

## Agent Checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
rg -n "WM_CREATE|apply_layout|kDeferredRealizeMessage|PostMessageW\(window" src\app_shell\main.cpp
```

實機 check 必須使用 Release `build\PaneDock.exe`（一般模式與 `--diagnostic` 都要各測過至少一次)、真實 `EnumWindows`/`FindWindowW`/`EnumChildWindows` 幾何,以及足夠長、有明確逾時判準的輪詢；不得以猜測座標、`CopyFromScreen` 或強制終止代替。若加入暫時計時器,只能是一次性的診斷 instrumentation,完成後刪除。

## Handoff requirements

- 記錄重現當下的 `session.json` 內容摘要（Group 數量、active Group 的 pane/tab 路徑）與啟動指令、環境（有無其他 PaneDock.exe 或 `codexpd`/其他 agent 正在跑的建置/測試）。
- 記錄每個啟動階段的實測耗時或卡住點,以及最終判定（修正 or 判定非產品碼缺陷）的完整依據。
- 若判定非產品碼缺陷,記錄下次再有人回報同樣症狀時應該先檢查什麼（例如殘留 process、mutex、特定 session 內容）。

## 交接區

<!-- 實作 agent 填寫,append-only -->
