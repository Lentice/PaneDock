# PD-097 — 拖曳分隔線時,幾何重排本身仍在每個 `WM_MOUSEMOVE` 都執行,需節流

Phase 7 · app_shell · Depends on: PD-095

- Source: 使用者需求(2026-08-28)。
- Origin: 使用者原文:「resize pane size via pane separator should apply throttling, to prevent the render storm. long refresh interval is acceptable to lower cpu usage.」
- Priority: MEDIUM——不影響功能,PD-095 已把「內容重算」從逐幀路徑移除,但「幾何重排」本身仍是無節流的逐幀工作,使用者判斷這部分仍造成不必要的重繪量(render storm)。

## 背景與現況

PD-095 已把 `apply_layout` 拆成「幾何重排」與「內容相關重算」兩類工作,`update_splitter_drag`(`main.cpp:2553-2573`)在 `WM_MOUSEMOVE` 時呼叫 `apply_layout(window, state, false, /*recompute_content=*/false)`,只做前者。但「只做前者」不等於「便宜」——幾何模式仍然會:

- 對每個可見 pane 重新計算矩形,並對其容器、tab 條、導覽列、狀態列等多個子視窗呼叫 `SetWindowPos`(`main.cpp:3866-3872` 呼叫點)。
- 每次 `SetWindowPos` 觸發的 `WM_WINDOWPOSCHANGED`/`WM_SIZE`/`WM_PAINT` 連鎖,即使沒有 `recompute_content`,仍是實際的視窗管理與繪圖系統呼叫開銷。

目前這整條路徑**完全沒有節流**:滑鼠每產生一次 `WM_MOUSEMOVE`(拖曳期間通常是每秒數十到上百次),就完整執行一次上述幾何重排。這是 PD-095 刻意保留的行為(PD-095 的 Non-goals 明確排除節流,只處理「幾何 vs 內容」的分類),不是 PD-095 的缺陷。

## 為什麼這是真的問題

使用者的判斷是:即使只做幾何重排,逐幀執行仍構成「render storm」——沒有必要讓分隔線的視覺跟隨精細到每一個原始滑鼠事件,犧牲一點跟手的流暢度(允許較長的刷新間隔)換取明顯更低的 CPU/繪圖負載,是可接受的取捨。

## Fix 方向

對 `update_splitter_drag` 在 `WM_MOUSEMOVE` 路徑上的呼叫加節流(throttle),而不是每次 `WM_MOUSEMOVE` 都直接呼叫 `apply_layout`:

- 節流視窗內到達的 `WM_MOUSEMOVE` 只更新「最新滑鼠位置」這個輕量狀態,不立即呼叫 `apply_layout`。
- 節流視窗到期時(用既有的 `WM_TIMER` 機制,見下方既有模式),用最後一次記錄到的滑鼠位置執行一次 `apply_layout(window, state, false, false)`。
- **必須是「節流」不是「防抖」**:`schedule_session_save`(`main.cpp:1868-1875`,PD-091)用的是每次呼叫都重設同一個 timer 的防抖(debounce)模式——目的是「安靜下來才存檔」,適合不需要跟手感的背景工作。分隔線拖曳不能照搬同一模式,否則滑鼠持續移動時 timer 永遠被重設、永遠不會到期,拖曳期間畫面會完全凍結直到放開滑鼠才動一次,使用者會覺得「拖曳沒有反應」。正確行為是:節流視窗到期後立即套用一次最新位置,並重新開始下一個節流視窗(只要拖曳仍在進行),讓分隔線每隔固定間隔前進一次,而不是等到全部安靜下來。
- `WM_LBUTTONUP`(`main.cpp:3874-3883`)的行為不變:立即用最終滑鼠位置呼叫一次完整的 `apply_layout(window, state, false, true)`(含內容重算),並確保任何還在等待中的節流 timer 被取消,不會在放開滑鼠之後又補一次過期的幾何重排。
- 節流間隔的具體毫秒數由實作者決定並記錄在交接區,鎖定原則是「使用者接受較長間隔以換取更低 CPU」——不需要追求貼近 60fps,可以明顯放寬(例如百毫秒級)。

## 綁定限制(引用)

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

本票的 timer 只在 `WM_LBUTTONDOWN` 偵測到分隔線拖曳開始後啟動,`WM_LBUTTONUP`/`WM_CAPTURECHANGED`(滑鼠 capture 被搶走,例如 Alt+Tab)必須取消 timer——不得在拖曳結束後继续存在。這與既有的 `kSessionSaveTimerId`(`main.cpp:59`,PD-091)是同一種「事件觸發、狀態機自行取消」的做法,不是背景輪詢。

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

沿用既有的 `WM_TIMER` 分派結構(`main.cpp:3884-3906`,已有 `kSessionSaveTimerId`/`kDragHoverSidebarTimerId`/`kDragHoverTabTimerIdBase` 三種 timer id 並列處理的先例),新增一個 timer id 常數即可,不需要新的計時抽象。

`docs/tickets/PD-095-splitter-drag-full-relayout-and-item-count-rescan-per-mousemove.md` 的既有決策(本票沿用,不覆寫):
> 拖曳分隔線逐幀只做幾何重排,內容相關的重算(選取計數、tab 文字量測、診斷輸出)在拖曳期間不重複執行……不新增計時器或輪詢,只改變既有事件觸發時做的工作量。

本票是在 PD-095 之上再加一層節流,PD-095 本身「幾何 vs 內容」的分類與 `recompute_content` 參數維持不變,不重寫。

## 檔案與範圍

- `src/app_shell/main.cpp`:
  - `update_splitter_drag`(`:2553-2573`)——節流邏輯的核心修改處。
  - `WM_MOUSEMOVE` 的分隔線分支(`:3866-3872`)——改為「記錄最新位置 + 視需要啟動節流 timer」,不再直接呼叫 `update_splitter_drag`。
  - `WM_LBUTTONUP` 的分隔線分支(`:3874-3883`)——確認取消節流 timer 的時機正確,不與既有的最終完整重算衝突。
  - `WM_TIMER`(`:3884-3906`)——新增節流 timer id 的分派分支,比照既有三種 timer id 的寫法。
  - `kSessionSaveTimerId` 等 timer id 常數所在區塊(約 `:59` 附近)——新增節流 timer id 常數。
  - `AppState`(`splitter_drag` 欄位所在的結構,`:400` 附近)——視實作需要新增「最新滑鼠位置」與「節流 timer 是否啟動中」的欄位。

## Scope

1. `update_splitter_drag` 在拖曳期間的呼叫頻率改為節流,而非每個 `WM_MOUSEMOVE` 都執行。
2. 節流到期時套用的是「最新一次」滑鼠位置,不是節流視窗開始時的舊位置。
3. `WM_LBUTTONUP` 仍然立即套用最終位置的完整重算,且正確清理任何等待中的節流 timer。
4. 拖曳以外的時間(未按住分隔線、或放開滑鼠後),不得有任何殘留的 timer 繼續觸發。

## Non-goals

- 不改變 PD-095 已經做好的「幾何 vs 內容」分類本身,也不擴大節流到 `WM_SIZE`(主視窗邊框縮放)——那是 `docs/tickets.md` 候選表已列的獨立項目,若要處理需另開票。
- 不改變節流結束後最終呈現的版面配置結果,只改變過程中中繼畫面更新的頻率。
- 不引入跨拖曳工作階段持續存在的背景 timer 或輪詢。
- 不改變 `WM_LBUTTONUP` 既有的「完整重算 + `save_now`」行為。

## Acceptance Criteria

1. 連續拖曳分隔線期間,`apply_layout`(幾何模式)的實際呼叫次數應明顯低於 `WM_MOUSEMOVE` 事件數,呈現「間隔性前進」而非每個原始滑鼠事件都重排一次。
2. 拖曳期間分隔線仍會跟隨滑鼠持續前進(不是等到放開滑鼠才動一次)——節流是降低頻率,不是變成防抖/凍結。
3. 放開滑鼠瞬間,分隔線與所有 pane 的最終位置精確對應放開當下的滑鼠座標,不因節流而有殘留誤差或延遲補一次的視覺跳動。
4. 拖曳結束(含滑鼠 capture 被系統搶走的情況)後,不遺留任何持續觸發的 timer;滑鼠靜止時程式回到 0% CPU。
5. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
6. `git diff --check` 通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "update_splitter_drag|kSessionSaveTimerId|WM_TIMER|splitter_drag" src\app_shell\main.cpp
git diff --check
```

**驗證原則(本專案共同約定):只做單次點擊/滑鼠移入 + 截圖的驗證由 Agent 或本人執行;需要連續、多步驟操控滑鼠鍵盤的拖曳流暢度測試交給使用者本人執行**,若某項驗收條件無法用單次動作完成,如實在交接區標記未驗證並說明需要使用者手動驗證的具體步驟(例如實際拖曳分隔線的跟手感受、CPU 使用率量測)。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 節流間隔的具體數值與選擇理由。
- 節流機制的實作方式(timer id、狀態欄位、與既有 `kSessionSaveTimerId`/`kDragHoverSidebarTimerId` 等 timer 並列時的分派邏輯)。
- `WM_LBUTTONUP`/capture 遺失情境下 timer 清理的驗證結果。
- 未驗證項目與原因(若有,例如實際拖曳跟手感受、CPU 使用率量測留給使用者)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
