# PD-213 — 把「單執行緒訊息迴圈」寫成明示的不變量，並移除為並行而存在的機制

Phase 7 · engineering rules · Depends on: —

- Source: 2026-09-18 使用者在第二輪稽核後提出的原則：
  「若點擊的 event(windows message) 天生就是循序的，能夠保證不會有 race
  condition，合適的話我們利用這個特性，可以排除掉一些不會發生的
  cases/flows，減少 code 的複雜度。」
- Priority: MEDIUM——它本身只刪掉少量程式碼，價值在於**讓之後每一張票都能
  合法地跳過一整類 case**。PD-210／PD-211／PD-212 的修法形狀都已經由這個
  原則決定，本票把依據寫進 repo，避免下一個 agent 又補上不需要的防護。

## 事實基礎

PaneDock 是單執行緒 STA 應用：

- `wWinMain` 只建立一個執行緒，`docs/design-spec.md` 的架構與
  `AGENTS.md`（「No network, no telemetry, no third-party runtime, no
  services, no drivers」）都沒有第二個執行緒。
- 全 repo 搜尋 `std::thread`／`CreateThread`／`_beginthread`／
  `QueueUserWorkItem`／`std::mutex`／`CRITICAL_SECTION`／`SRWLOCK`／
  `volatile`：**零個結果**。
- `IExplorerBrowser` 與 `IExplorerBrowserEvents` 都是 STA-bound，回呼由
  同一個執行緒的訊息迴圈派送。
- 因此 Windows 訊息是**循序派送**的：不存在兩個處理程序同時執行。

**危險因此不是並行，而是重入**：Shell 呼叫（拖放、`IFileOperation` 進度、
`BrowseToObject`、context menu）與 modal UI（`TrackPopupMenu`、
`MessageBoxW`）會 pump 我們的迴圈，在外層還沒返回時派送下一則訊息。重入是
**巢狀**而非**交錯**：每一次狀態更新在下一個 pump 點之前都是完整的。

## 這個區別帶來的具體結論

可以據以**排除**的 case（不要為它們寫程式碼）：

- 「A 執行到一半、B 在另一個執行緒看到中間狀態」——不存在。
  純量的多步更新不需要原子性保護。
- 「兩個處理程序同時修改同一個 `PaneState`」——不存在。
- 「旗標讀寫需要記憶體序」——不存在。
- 「檢查後使用（check-then-act）之間狀態被抽換」——**只有在中間有 pump
  點時**才存在。沒有 pump 就不必重新驗證。

仍然必須處理的（不要因為本票而放鬆）：

- 每一個會 pump 的呼叫前後都要重新驗證身分（`active()`、
  `pane_state()`、`is_shutting_down()`），這是既有慣用法，不是冗餘。
- 跨 pump 點持有的參考／指標仍可懸空（PD-206 修的正是這個）。
- 深度計數器 + 延遲清單（`shell_call_depth` / `ShellCallScope`）是**正確的**
  重入工具；不要換成鎖。
- 單一實例的跨**行程**協調（`main.cpp:4027` 附近的 close/relaunch race）
  是真正的並行，維持現狀。

## 要讀與追的檔案

- `src/com_ref_counted.h`（全檔）。
- `src/explorer_host/live_view_count.h`（全檔）。
- `src/explorer_host/explorer_host.cpp`：`LiveViewRegistration` 的
  `mark_initialized()` / `reset()` 呼叫點（`initialize` 與 `destroy`）。
- `src/app_shell/main.cpp`：`write_live_view_count`（`:376`）與三處
  `assert(live_view_count() == 0)`（`:2833`、`:4194`、`:4384`）。
- `AGENTS.md` 的 Engineering rules 區塊。
- `docs/development.md`：重入與關閉排序的既有規則。

## 範圍

### 1. 把不變量寫進 `AGENTS.md`

在 Engineering rules 中新增一條，緊接在既有的
「Shell APIs re-enter our message loop during drag, `IFileOperation` progress
and internal view work...」那一條之後（它們是同一件事的兩半）：

大意（英文撰寫，與該檔其餘規則一致）：本應用是單執行緒 STA，訊息循序派送，
所以危險是重入而非並行；不要引入鎖、原子或執行緒安全機制，也不要為
「另一個執行緒看到中間狀態」寫防護；要處理的是每一個 pump 點前後的身分
重新驗證與跨 pump 點的生命週期。唯一的例外是跨行程協調與交給 `shell32`
的 COM 物件引用計數。

### 2. `g_live_view_count` 的 `std::atomic` 降為 `unsigned`

`src/explorer_host/live_view_count.h`：它是**我們自己的**記帳，只在
`ExplorerHost::initialize` 成功後與 `destroy` 中被改動，兩者都只能從 UI
執行緒呼叫（`IExplorerBrowser` STA-bound）。`live_view_count()` 的讀取者
（診斷輸出與三個 `assert`）同樣在 UI 執行緒。

改為 `inline unsigned g_live_view_count{0};`，`fetch_add`／`fetch_sub`／
`load` 改為 `++`／`--`／直接讀，移除 `#include <atomic>`。
在該檔留一行註解說明**為什麼**可以是純量（單執行緒 + STA-bound），
否則下一個讀者會以為這是疏漏。

### 3. `ComRefCounted` 的 `std::atomic` **保留**，並補註解說明為什麼

`src/com_ref_counted.h` 的引用計數**不是**我們的內部狀態：這些物件
（`DragHoverTarget`／`ViewCallback`／`Site`／`ProgressSink`）被交給
`shell32`，`AddRef`／`Release` 由第三方程式碼呼叫，可能經由 proxy 或在
我們無法控制的時機發生。這是一個**信任邊界**，`AGENTS.md` 明示不得為了
精簡而移除信任邊界上的防護。

補一行註解記錄這個判斷，讓它成為明示的決定而不是巧合——否則下一個
執行本原則的 agent 會把它一起改掉。

## 非目標

- **不**移除任何 `is_shutting_down()`／`active()`／`pane_state()` 的重新驗證。
  它們防的是重入，不是並行。
- **不**移除或簡化 `shell_call_depth`、`ShellCallScope`、
  `layout_in_progress`、`destroying_`、`context_menu_active_`、
  `invoking_`、navigation generation 比對。全部都是重入防護。
- **不**改 `main.cpp:4027` 附近的單一實例跨行程協調。
- **不**改 `ComRefCounted` 的行為（只加註解）。
- 不新增執行緒，也不因為「反正是單執行緒」而把任何阻塞呼叫留著不管——
  NFR-003 仍然要求 UI 不阻塞，那是另一條線（見 `docs/tickets.md` §候選）。

## 驗收條件

1. `AGENTS.md` 有一條明示的單執行緒／重入規則，且區分了「可排除的並行 case」
   與「仍須處理的重入 case」。
2. `g_live_view_count` 是純量，`live_view_count.h` 不再 include `<atomic>`，
   並帶有解釋為什麼安全的註解。
3. `src/com_ref_counted.h` 的 `std::atomic` 保留，並有註解說明它位於信任邊界。
4. 三處 `assert(live_view_count() == 0)` 與診斷輸出行為不變。
5. 全 repo 仍然沒有鎖／執行緒／`volatile`。
6. 既有測試全綠，`panedock_launch_smoke` 不退化（它依賴
   `panedock.live_view_count=` 的輸出）。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

外加一條靜態把關（放進既有的 release check 之一，或新增一個小的
`.ps1`）：`src/` 底下不得出現 `std::thread`、`std::mutex`、
`CRITICAL_SECTION`、`SRWLOCK`、`CreateThread`、`volatile`。這條規則若沒有
自動把關，就會在幾張票之後被悄悄違反——而它正是本票想長期保住的前提。
`src/com_ref_counted.h` 的 `std::atomic` 需在該 check 中明確豁免。

## 交接區

2026-09-18 實作完成。

- `AGENTS.md` Engineering rules 新增「**The hazard is reentrancy, never
  concurrency.**」一條，緊接在既有的 Shell re-entry 規則之後。它明確區分了
  「可排除的並行 case」（記憶體序、多步純量更新的原子性、sibling 修改
  `PaneState`）與「仍須處理的重入 case」（每個 pump 點前後重新驗證身分、
  不得跨 pump 點持有參考），並點出兩個真實例外：跨行程的 single-instance
  relay，以及交給 `shell32` 的 COM 引用計數。
- `src/explorer_host/live_view_count.h`：`std::atomic<unsigned>` → `unsigned`，
  移除 `<atomic>`，並留註解說明為什麼安全（寫入者只有
  `ExplorerHost::initialize`／`destroy`，兩者 STA-bound；讀取者也在同執行緒）。
- `src/com_ref_counted.h`：`std::atomic` **保留**，加註解說明它位於信任邊界
  （物件交給 `shell32`，`AddRef`／`Release` 由第三方在我們無法控制的時機呼叫）。
  這是為了讓它成為明示的決定，否則下一個執行本原則的 agent 會把它一起改掉。
- 新增 `tests/release/single_threaded_check.ps1` 與 ctest 項目
  `panedock_single_threaded`：掃 `src/` 禁止 `std::thread`／`std::mutex`／
  各種 lock／`std::atomic`／`CRITICAL_SECTION`／`SRWLOCK`／`CreateThread`／
  `QueueUserWorkItem`／`volatile <type>`，`src/com_ref_counted.h` 明確豁免。
  該 check 另外**反向**斷言 `com_ref_counted.h` 仍使用 atomic——信任邊界上的
  防護被移除同樣是退化。
- `ctest`：34/34 通過。

### 這條原則在本輪如何實際減少了程式碼

- PD-212 因此採「取代語意」而不是「容量上限 + 驅逐策略」：既然 hold 與 flush
  之間不會有任何處理程序執行，重播順序完全由我們決定，「只有最後一筆有意義」
  就是正確語意而非近似，佇列長度也天然被訊息種類數界住，不需要上限邏輯。
- PD-210 的修法是三個純量的同執行緒更新，不需要任何原子性保護。
- PD-211 的修法是既有的深度計數器，而不是鎖或「選單期間的狀態快照」。
