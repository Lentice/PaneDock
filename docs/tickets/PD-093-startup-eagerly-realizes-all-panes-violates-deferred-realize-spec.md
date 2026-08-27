# PD-093 — 啟動時同步 realize 所有可見 pane(而非只有 active pane),違反 spec §9.3 的延後 realize 規則

## 來源

2026-08-27 三方效能研究(Claude / Codex / OpenCode 各自以 Herdr tab 對整個 repo 做唯讀研究,聚焦啟動速度、關閉速度、記憶體重複、CPU 使用)。Claude、Codex、OpenCode **三方各自獨立**指出同一段程式碼與同一個根因,是本次研究中收斂程度最高的發現。Claude 另外獨立指出同一根因下的第二個症狀(`SHAutoComplete` 對所有 pane 都同步呼叫),OpenCode 也獨立提到同一項,一併併入本票(見下方 Scope 第 2 點),避免為同一根因開兩張票。

## 背景與現況

`docs/design-spec.md` §9.3 啟動序列明確定義:

> 5. 套用 active Group 的版型,**先 realize active pane 的 active tab**
> 6. 其餘 pane 延後 realize,避免被網路或離線路徑阻塞

但目前 `WM_CREATE`(`src/app_shell/main.cpp:3364` 一帶)直接呼叫 `apply_layout`,而 `apply_layout`(`main.cpp:1835-2004` 一帶,pane 迴圈約 `:1951-1958`)把「目前版型下所有可見的 pane」一視同仁地同步 `ExplorerHost::initialize()`(`explorer_host.cpp:297-368`),不分 active/非 active。`initialize()` 內含 `CoCreateInstance(CLSID_ExplorerBrowser)` + `Initialize` + `Advise` + 首次 `navigate()`,而 `navigate()`(`explorer_host.cpp:379` 一帶)呼叫的 `SHCreateItemFromParsingName` 是同步呼叫,對無法連線的 UNC 路徑或已移除的磁碟機會阻塞。

四宮格版型下,啟動時同步等待四個資料夾各自完整列舉完成,`UpdateWindow(window)` 才會在 `main.cpp:3968` 被呼叫——代表使用者在看到任何畫面之前,就要等最慢的那個 pane(可能是離線網路路徑)完成列舉。

同一段迴圈中,`SHAutoComplete`(`main.cpp:3352-3353`)也對每個 pane 各呼叫一次,同樣不分該 pane 現在是否可見/active,而 `SHAutoComplete` 會載入 shell autocomplete 基礎設施(browseui/shell32 的 autocomplete 物件與檔案系統列舉器),是有實際成本的首次呼叫。

## 為什麼這是真的問題

這不是效能微調,是 spec 明文規定卻沒有落實的行為:spec 把「先 realize active pane,其餘延後」列為啟動序列的第 5、6 步,理由正是「避免被網路或離線路徑阻塞」——而目前的實作剛好重現了 spec 想避免的那個場景。使用者還原一個含網路磁碟機路徑的 Group 時,主視窗會在看得見畫面前整個卡住。

## Fix 方向

啟動時 `apply_layout` 只同步 realize active pane 的 active tab;其餘可見 pane 標記為「稍後 realize」,在主視窗顯示、訊息迴圈開始跑之後,用一個一次性事件(例如 `PostMessageW` 一個私有 `WM_APP` 訊息,在收到後才對其餘 pane 逐一呼叫 `initialize()`)完成剩餘 pane 的 realize,而不是新增輪詢或計時器。`SHAutoComplete` 只在該 pane 真正被 realize 的當下才呼叫(即已經在 `initialize()`/`navigate()` 附近,只是現在被同步提前到啟動期間全部呼叫,把它挪到延後 realize 的同一個時機即可,不需要額外邏輯)。

實作者可依現有程式碼慣例挑選最小改動的等價方案,記錄在交接區——核心要求只有「啟動時只有 active pane 的 active tab 在 `UpdateWindow` 之前被同步 realize,其餘 pane 在窗口可互動之後才 realize」。

## 綁定限制(引用)

- `docs/design-spec.md` §9.3:「套用 active Group 的版型,先 realize active pane 的 active tab」「其餘 pane 延後 realize,避免被網路或離線路徑阻塞」—— 本票直接修正這條規則目前未被滿足的缺口。
- `AGENTS.md`:「Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs persist as data and are realized on activation.」—— 本票延伸同樣的「realize-on-activation」精神到啟動時機本身:non-active 但目前可見的 pane,在啟動當下也應視為「尚未 activate」,延後到主視窗可互動之後才 realize。
- `AGENTS.md`:「Event-driven idle path only. No busy loops, no polling timers.」—— 延後 realize 的觸發機制必須是一次性事件(例如啟動後自行 post 一次的訊息),不能是輪詢或計時器迴圈。
- `AGENTS.md`:「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」—— `ExplorerHost::initialize()`/`navigate()` 的邏輯不需要改變,只需要改變「哪些 pane、什麼時機」呼叫它們。

## 檔案與範圍

- `src/app_shell/main.cpp`:
  - `WM_CREATE` 處理與 `apply_layout` 的呼叫時機(約 `:3352-3364` 一帶)
  - `apply_layout` 的 pane 迴圈(約 `:1951-1958` 一帶)
  - `SHAutoComplete` 呼叫點(約 `:3352-3353` 一帶)
- `src/explorer_host/explorer_host.cpp`:`initialize()`(約 `:297-368` 一帶)、`navigate()`(約 `:379` 一帶)——僅確認呼叫時機改變後行為仍正確,不預期需要修改這兩個函式本身。

## Scope

1. 啟動路徑改為「只同步 realize active pane 的 active tab」,主視窗顯示、`UpdateWindow` 呼叫之後,才透過一次性事件觸發其餘可見 pane 的 realize。
2. `SHAutoComplete` 的呼叫時機隨著所屬 pane 的 realize 時機一起延後,不再是啟動時對所有 pane 一次做完。
3. 確認延後 realize 完成後,所有 pane 顯示的路徑與版型套用結果與現在「同步全部 realize」的最終狀態完全一致,只是「什麼時候完成」改變,不改變「最終顯示什麼」。

## Non-goals

- 不改變 `ExplorerHost::initialize()`/`navigate()` 的內部邏輯或 COM 呼叫序列。
- 不改變「只有可見 pane 的 active tab 持有 live view」這個既有的執行期規則(本票只影響啟動當下的時機,不影響穩態下哪些 pane 有 live view)。
- 不新增啟動進度 UI(例如 loading spinner)——現有行為是「窗口先顯示,pane 內容陸續補上」即可,不需要額外視覺元件。
- 不處理 PD-086~PD-092 涵蓋的其他問題。

## Acceptance Criteria

1. 啟動一個四宮格版型、其中一個 pane 指向無法連線的網路路徑的 Group,主視窗應在該離線 pane 完成列舉**之前**就顯示並可互動(可用「active pane 立即可操作,其餘 pane 顯示載入中或稍後補上內容」驗證)。
2. 啟動完成後(所有延後 realize 都跑完),四個 pane 顯示的路徑與版型套用結果,和目前「全部同步 realize」的最終結果完全一致,無視覺 regression。
3. `SHAutoComplete` 呼叫次數與時機隨對應 pane 的 realize 時機一起延後,不在啟動期間對非 active pane 提前呼叫。
4. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

> **驗證政策提醒:** 單一點擊/單一操作 + 截圖由 Agent 自行完成即可;需要量測實際啟動延遲(冷啟動計時、離線網路路徑重現)的部分,留給使用者在實機上驗證,不要用 computer-use 工具連續操作搶走使用者的滑鼠鍵盤。完成後在交接區寫清楚哪些是自己驗證過的、哪些留給使用者。

## 交接區

（實作完成後由實作者填寫）
