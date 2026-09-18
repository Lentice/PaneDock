# PD-212 — 延遲的指標／hover 訊息只保留最後一筆意圖

Phase 7 · switching path robustness · Depends on: PD-205

- Source: 2026-09-18 第二輪切換路徑稽核。**Codex finding 2 與 Claude finding 4
  是同一項**，兩邊獨立得到同一結論與同一修法方向。作者已複驗。
- Priority: HIGH——這是 PD-205 沒做完的那一半。PD-205 消除了自旋，但沒有讓
  去重對真正會堆積的那類訊息生效。

## 根本原因

`hold_deferred_message`（`src/app_shell/deferred_messages.h:33-42`）的去重
身分是 `(target, message, wparam, lparam)` **全等**：

```cpp
inline bool hold_deferred_message(DeferredMessages& held,
                                  const DeferredMessage& message) {
    if (std::find(held.begin(), held.end(), message) != held.end())
        return false;
    held.push_back(message);
    return true;
}
```

對 `WM_COMMAND` 有效——sidebar 的 `LBN_SELCHANGE` 每次 `wparam`／`lparam`
都相同，連點被收斂成一筆，重播時 `handle_sidebar_command`（`main.cpp:3257`）
讀的是**當下**的選取，結果收斂到使用者最後的意圖。Group 切換因此剛好是對的。

但對真正會堆積的兩類訊息**完全無效**：

- `WM_LBUTTONDOWN`／`WM_LBUTTONUP`／`WM_LBUTTONDBLCLK`／`WM_CONTEXTMENU`
  的 `lparam` 是**游標座標**（`main.cpp:2646` 的 `point_from_lparam`）→
  點不同 tab、甚至同一 tab 的不同像素，都是不同 entry。
- `kDragHoverMessage` 的 `lparam` 是 hover generation（`main.cpp:272`）→
  每次必然不同，一律新增。

佇列也沒有容量上限。

### 情境

慢速 `BrowseToObject` 抽 nested message loop（PD-209 已確認它無 deadline）
→ 使用者在 tab strip 與 sidebar 上反覆點擊 → 每個不同座標各留一筆 →
Shell 呼叫返回後全部以 `PostMessageW` 依序重播，**用的是當初按下時的舊座標**
→ 每一次重播的 tab 點擊又同步進入一次 `BrowseToObject`。

結果：一次慢導覽被放大成 N 次慢導覽，UI 長時間無回應；長時間 stall 下佇列
持續成長。重播出來的座標點在使用者早已離開的 tab／Group 列上，每一下都可能
再觸發一次無界阻塞的 Group 切換。

另一個佐證（Claude）：`WM_MOUSEMOVE` **不**在延遲清單上
（`main.cpp:3609-3612`、`sidebar.cpp:387`），所以重播的 DOWN／UP 與從未被
延遲的 MOVE 之間的時間關係已經被打散——保留多筆舊 DOWN／UP 連使用者意圖都
不能正確重現。

## 單執行緒前提（本票的正當性來源）

Windows 訊息在單一執行緒上循序派送，**重播順序完全由我們決定**，而且在
`shell_call_depth` 歸零、flush 發生之前不會有任何處理程序執行。因此：

- 「一連串舊座標的點擊」沒有任何可保留的使用者意圖，**只有最後一次有**。
  取代語意（replace）不是近似，在這個執行模型下它就是正確語意。
- 不需要容量上限、不需要驅逐策略、不需要時間戳。取代語意讓佇列長度天然
  被 `(target, message)` 的種類數界住，這是個小常數。
- 不需要鎖、原子或執行緒安全機制：hold 與 flush 都在同一個執行緒，且
  flush 先 `take_deferred_messages()` 一次取空（PD-205 已如此），重入期間
  新 hold 的訊息進的是新的空佇列。

這正是本票能比「加上限 + 加驅逐」更小的原因——把並行不會發生的 case
排除掉之後，剩下的是一個更簡單的資料結構，而不是更複雜的。

## 要讀與追的檔案

- `src/app_shell/deferred_messages.h`（全檔，47 行）。
- `tests/unit/deferred_messages_test.cpp`（全檔，三個既有 case）。
- `src/app_shell/main.cpp`：`defer_shell_reentry_message`（`:614`）、
  `defer_shell_reentry_mouse_message`（`:628`）、
  `flush_deferred_shell_messages`（`:536`）、`finish_shell_call`（`:551`）、
  `kDragHoverMessage` 的 post 點（`:272`）與處理（`:3603`）、
  `main_window_proc` 的延遲分支（`:3598-3612`）、
  `tab_strip_proc`（`:2628`）、`AppState::handle_pane_control_message`
  （`:4352`）、sidebar subclass 的延遲點（`:2694`）。
- `src/sidebar/sidebar.cpp`：`DragHoverTarget` 的 `pending_generation_` 與
  `invoke_hover` 的 stale 判斷（證明 hover 本來就只在意最後一筆）。

## 範圍

`src/app_shell/deferred_messages.h`：

1. 新增一個判斷「這則訊息的語意是**取代**而非**排隊**」的純函式：

   ```cpp
   // A pointer or hover message carries a position that is only meaningful at
   // the moment it was posted. Replaying a queue of stale positions
   // reproduces no user intent -- only the last one does.
   inline bool deferred_message_replaces_previous(UINT message) noexcept;
   ```

   涵蓋 `WM_LBUTTONDOWN`、`WM_LBUTTONDBLCLK`、`WM_LBUTTONUP`、
   `WM_CONTEXTMENU`，以及 hover 訊息。hover 是 `WM_APP + 51`，
   `deferred_messages.h` 不應該知道 `kDragHoverMessage` 這個 app 專屬常數，
   所以改由呼叫端傳入判斷結果或把常數搬進這個 header——**實作者二選一，
   並在交接區寫明選了哪個與為什麼**。較小的做法是讓
   `hold_deferred_message` 多收一個 `bool replaces` 參數，由 `main.cpp`
   的 `defer_shell_reentry_message` 依 `message` 決定。

2. `hold_deferred_message`：`replaces` 為真時，找到同一
   `(target, message)` 的既有 entry 就**覆寫它的 `wparam`／`lparam`**
   （保持原本的佇列位置，使先後順序仍反映第一次互動發生的時機）；
   找不到才 `push_back`。`replaces` 為假時行為與現狀完全相同（全等去重）。

3. 回傳值語意需要在註解裡講清楚：覆寫算不算「新增」。建議讓覆寫也回
   `false`（與「已經持有了」一致），呼叫端目前都 `(void)` 忽略回傳值。

`src/app_shell/main.cpp`：`defer_shell_reentry_message` 依 `message`
決定 `replaces`，`kDragHoverMessage` 一併納入。

## 非目標

- **不加容量上限**。取代語意之後佇列長度被訊息種類數界住；加上限是為一個
  已經不存在的問題寫程式碼。
- 不改哪些訊息會被延遲（清單維持 `main.cpp:3598-3612` 現狀）。
- 不改 `WM_COMMAND` 的全等去重（它已經是對的，且 Group 切換依賴它）。
- 不把 `WM_MOUSEMOVE` 加進延遲清單：延遲一個高頻訊息會製造新的堆積面，
  而 MOVE 沒有任何 model 副作用。
- 不改 `flush_deferred_shell_messages` 的一次取空語意（PD-205 已正確）。
- 不解決「重播的座標可能已經失效」的根本問題（例如改存 tab id 而非座標）：
  取代語意之後只剩最後一筆，而最後一筆的座標正是使用者最後真正點的位置。

## 驗收條件

1. 慢速 Shell 呼叫期間對同一個 tab strip 點擊 N 個不同 tab，只有**最後一筆**
   被重播，因此只觸發一次導覽。
2. 不同 HWND（tab strip、sidebar、pane window）的同類訊息**各自**保留一筆，
   不會互相取代。
3. `kDragHoverMessage` 只保留最後一筆。
4. `WM_COMMAND` 的既有行為完全不變。
5. 佇列長度在長時間 stall 下不再隨點擊次數成長。
6. 既有測試全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`tests/unit/deferred_messages_test.cpp` 需擴充，至少涵蓋：
不同座標的同類指標訊息只留最後一筆且 `lparam` 是最後那個值；不同 target
不互相取代；`WM_COMMAND` 仍走全等去重。

## 交接區

2026-09-18 實作完成。

- `src/app_shell/deferred_messages.h`：
  - 新增 `deferred_message_replaces_previous(UINT)`，涵蓋
    `WM_LBUTTONDOWN`／`WM_LBUTTONDBLCLK`／`WM_LBUTTONUP`／`WM_CONTEXTMENU`。
  - `hold_deferred_message` 多收一個 `bool replaces`。為真時找到同一
    `(target, message)` 的既有 entry 就**覆寫其 `wparam`／`lparam` 並保持
    原本的佇列位置**（先後順序仍反映該互動第一次抵達的時機），回 `false`；
    為假時行為與 PD-205 完全相同（全等去重）。
  - 依 ticket 給的二選一，選了「由呼叫端傳入 `replaces`」：
    `kDragHoverMessage` 是 `WM_APP + 51`，是 app 專屬常數，不該讓這個 header
    知道。`main.cpp` 的 `defer_shell_reentry_message` 因此把 hover 與
    `deferred_message_replaces_previous(message)` 的結果 or 起來。
- **未加容量上限**（ticket 非目標）：取代語意之後佇列長度被
  `(target, message)` 的種類數界住，加上限是為一個已不存在的問題寫程式碼。
  這正是「單執行緒 ⇒ 重播順序由我們決定」這個前提（PD-213）讓修法變小的地方。
- 測試：`tests/unit/deferred_messages_test.cpp` 從 3 個 case 擴充到 8 個，
  涵蓋不同座標只留最後一筆且 `lparam` 是最後那個值、取代是 per-target 與
  per-message、取代保持原佇列位置、`WM_COMMAND` 仍走全等去重、
  呼叫端可為自己的 `WM_APP` id 強制取代。
- `ctest`：34/34 通過。
