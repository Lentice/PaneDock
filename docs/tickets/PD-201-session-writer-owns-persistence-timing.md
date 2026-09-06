# PD-201 — session 存檔的時機收進 `app_shell::SessionWriter`

Phase: 7
Depends on: PD-091（500ms debounce）、PD-200（協調層拆分的前一刀）

## 為什麼

`docs/tickets.md` 2026-09-01 架構審查已把 `SessionWriter` 列為候選（當時評為 Speculative）。PD-200 完成後重新評估，觸發條件成立：協調層的拆分正在進行，而這是盤點下來接縫最窄的一塊。

拆之前，「什麼時候該寫檔」這件事散在六處：`AppState` 的三個欄位（`session_document`／`session_directory`／`session_dirty`）、一個 timer 常數、`save_now`／`schedule_session_save`／`cancel_session_save_timer` 三支自由函式、`WM_TIMER` 分支、`WM_DESTROY` 的 fallback，以及 `wWinMain` 尾端另一份手寫的 `write_session` 呼叫。把它們綁在一起的規則——**dirty 表示欠一次寫入；成功的寫入同時清掉旗標與待處理的 timer；失敗的寫入必須留著旗標以便重試**——沒有寫在任何一個地方，而是在每個呼叫點各自重述一次。`tests/release/shutdown_state_check.ps1` 之所以存在，正是因為這條規則用讀的看不出來。

## 綁定約束

`AGENTS.md`：

> All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.

> **Every persisted config/setting file must be designed for forward extensibility.** A read that encounters a field it does not recognize preserves that field rather than silently dropping it on the next write-back.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`core::write_session` 已經負責 atomic replace 與備份保留，`SessionDocument::preserved_json` 已經負責未知欄位的保留。本票**不重做**這兩件事，只接手「何時呼叫它」。

## Scope

**1. 新增 `src/app_shell/session_writer.{h,cpp}`**

`SessionWriter` 擁有：`SessionDocument`、目錄、dirty 旗標、debounce timer（`kTimerId`／`kDelayMilliseconds`）、以及 `flush_session_file` 這個 durability hook（從 `main.cpp` 移入，`core::write_session` 做 atomic replace，這支做 `FlushFileBuffers`）。

介面：`set_directory`／`directory`／`adopt`／`document`／`mark_dirty`／`dirty`／`arm_timer`／`cancel_timer`／`write`／`write_clean_marker`。

`write()` 是不變量所在：先 `dirty_ = true`，寫入 `core::write_session`，**只有成功後**才 `dirty_ = false` 並取消 timer；失敗時記錄並回傳 false，旗標留著。

`write_clean_marker()` 刻意與 `write()` 分開：它在 Shell、parent HWND 與 COM 都消失之後才呼叫，那時沒有 timer 要取消、也沒有人會再讀 dirty，而「順序」本身就是這支的全部意義。原本 `wWinMain` 尾端那段註解逐字移入。

**2. `AppState` 三個欄位收成一個**

`session_document`／`session_directory`／`session_dirty` → `panedock::app_shell::SessionWriter session`。

**3. 協調層的兩支自由函式保留，並且是刻意的**

`save_now(AppState&, bool clean_shutdown, bool force_during_transition)` 與 `schedule_session_save(AppState&)` **不搬進 `SessionWriter`**，因為它們承載的是協調層才有的兩件事：`suppress_location_capture` 這個 Group 切換期間的擷取閘，以及寫入前的 `capture_locations(state)`（會碰 pane 與 Shell）。`SessionWriter` 只收到協調層準備好的 model 快照。

保留它們同時讓 `shutdown_state_check.ps1` 的多數斷言（5 個同步存檔點的計數、reducer 路由、`save_now(..., true)` 禁令）原封不動繼續成立。

## Non-goals

- 不動 `core/session.cpp` 的序列化、atomic replace、備份或 schema version。
- 不把 `capture_locations`、`suppress_location_capture` 或 shutdown reducer 事件搬進 `SessionWriter`。
- 不改變 debounce 延遲、timer id 數值或任何存檔時機。零行為變更。
- 不改 session 檔案格式，不做遷移。

## 驗收

1. `AppState` 不再有 `session_document`／`session_directory`／`session_dirty` 三個欄位。
2. `main.cpp` 不再有 `flush_session_file`，也不再有第二份手寫的 `write_session` 呼叫。
3. 失敗的寫入必須留著 dirty 旗標。
4. 存檔時機、延遲與 timer id 與改動前完全相同。
5. 既有 build 與完整 CTest 通過，`shutdown_state_check.ps1` 未被弱化。

## Agent 檢查

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`tests/release/shutdown_state_check.ps1` 三處指向舊寫法的斷言改指向新呼叫點（等強度），並**新增**一段掃描 `session_writer.cpp` 的檢查：`SessionWriter::write` 內 `dirty_ = true` 必須在 `core::write_session(` 之前、`dirty_ = false` 必須在其之後，且失敗的 `return false;` 要出現在 `dirty_ = false;` 之前。

## 交接區

**結果**（2026-09-06）：`main.cpp` **4,470 → 4,452 行**。CTest **24/24**，build 無新增警告。

**行數幾乎沒動，這是預期的，也是這張票的重點**：淨減只有 18 行，因為搬走的程式碼幾乎等量地變成新檔案。收益不在行數而在三件事：`AppState` 少兩個欄位；「dirty／timer／document」三者的關係從六處各自重述變成一個型別的不變量；以及那條不變量現在**有一個經過反證的檢查在守**。用行數評估這一刀會得到錯誤結論。

**新檢查做過反證，這很重要**：先把 `SessionWriter::write` 改成在 `core::write_session` 之前就 `dirty_ = false`（等同「失敗的寫入被靜默遺忘」），確認 `shutdown_state_check` 由通過轉為失敗，再還原確認回到通過。**第一次的反證嘗試其實沒有改到檔案**（字串替換沒命中）而檢查照樣通過，差點就把一個空檢查當成有效——反證必須確認變異真的落地，只看「檢查通過」不算數。這個教訓同樣適用於 PD-199 交接區記的那個空斷言。

**沒有新增單元測試**：`SessionWriter` 綁 `SetTimer`／`KillTimer`／`CreateFileW`，不能進 `core`。`core::write_session` 的 atomic replace 與 round-trip 已由 `core_session_test` 覆蓋，本票沒有改它。時機邏輯由上述來源掃描守著，**那是結構檢查不是行為檢查**。

**人工未驗**：儲存空間不足或 `%LOCALAPPDATA%` 唯讀時，失敗的寫入是否確實在下一次 debounce 觸發時重試（`write()` 保留 dirty 的那條路徑）；以及正常關閉後 `session.json` 的 `clean_shutdown` 是否為 true、強制結束後是否為 false。

**下一刀**：協調層剩下可拆的已在 PD-200 交接區列為「評估後不拆」（Group CRUD 協調、版面幾何、`pane.cpp`），各附理由。除非有新證據，`main.cpp` 的拆分到此為止。
