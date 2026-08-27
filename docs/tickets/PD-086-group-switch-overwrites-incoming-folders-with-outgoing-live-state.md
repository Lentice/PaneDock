# PD-086 — 切換 Group 時,同步完成的導覽會把「舊 Group 目前開啟的路徑」寫進「新 Group」的儲存分頁

## 來源

2026-08-27 三方稽核(Claude / Codex / OpenCode 各自以 Herdr tab 對整個 repo 做唯讀稽核,涵蓋 race condition、架構、control flow)。Claude 與 Codex 各自獨立發現同一個根因(不同措辭,同一段程式碼、同一個資料流),是本次稽核信心最高的單一發現。詳見 `docs/tickets.md` 本批次的稽核記錄。

## 背景與現況

`activate_group`(`src/app_shell/main.cpp:2049-2073` 一帶)切換 Group 的流程大致是:

1. 設定 `state.application.active_group_id` 為目標 Group。
2. 走訪目前已 realize 的 pane,對每個 pane 呼叫 `explorers[pane].navigate(...)`,把該 pane 導覽到新 Group 對應分頁的路徑。

`IExplorerBrowser::BrowseToObject` 在許多情況下(例如目標已經是暫存的 cache、或是很快的本機資料夾)會**同步**完成,導致 `OnNavigationComplete` 立刻觸發 → `handle_navigation_complete`(`main.cpp:1795` 一帶)→ `save_now`(`main.cpp:1784`)→ `capture_locations`(`main.cpp:1324` 一帶)**在上述迴圈跑到一半時就執行**。

此時的狀態是:`active_group_id` 已經指向新 Group,但迴圈只導覽到第一個 pane,pane 1..N 仍然顯示**舊** Group 的資料夾。`capture_pane_location` 會把每個已 realize pane「目前畫面上的路徑」寫回 `active_group()` 的分頁 —— 而 `active_group()` 現在解析出來的是**新** Group。於是新 Group 尚未導覽到的 pane 就被寫入舊 Group 的路徑,`save_now` 再把這個錯誤狀態存進 `session.json`。

`delete_group`(`main.cpp:2135-2143` 一帶)有相同的模式(先改變 active group 判定依據,再觸發 capture)。

## 為什麼這是真的問題

這直接打破產品的核心承諾 ——「一鍵還原完整版面配置」(見 `AGENTS.md` 開頭:「a **Group** is a named, saved working context that restores an entire pane arrangement in one click」)。而且這不是畫面短暫閃爍的問題:`save_now` 會把被污染的狀態**持久化**寫進 `session.json`,是靜默的資料遺失 —— 使用者下次開啟這個 Group 時,部分 pane 會恢復到不相關的舊路徑,且沒有任何錯誤訊息或提示。

## Fix 方向

核心原則:**在「新 Group 的所有 realize pane 都已經確認導覽到新 Group 對應的路徑」之前,不能把任何 pane 的目前位置寫回 session。**

建議的其中一種做法(實作者可依現有程式碼慣例挑選最小改動的等價方案,記錄在交接區):

- 在 `activate_group`(以及 `delete_group` 走到的同一段邏輯)執行期間,設一個暫時的 guard(例如 `bool suppress_capture` 或每個 pane 一個「這個 pane 目前綁定哪個 group id」的欄位),在 guard 生效期間 `capture_pane_location` 直接跳過該 pane 的寫回。
- 迴圈內每個 pane 觸發的 `navigate` 若同步完成,允許 `navigation_failed`/UI 更新正常跑,但**跳過** `save_now`/`capture_locations` 這一段,直到迴圈內所有 pane 都已呼叫完 `navigate`(不需要等非同步完成,只需要「不會再有 pane 用舊 Group 的路徑觸發 capture」這個保證)。
- 迴圈結束後(所有 pane 的 `navigate` 都已發出),再做一次正常的 `save_now`,此時即使還有 pane 尚未真正完成非同步導覽,`capture_pane_location` 讀到的是「已經被要求導覽到新路徑」的狀態,不會再誤寫舊 Group 的資料。

## 綁定限制(引用)

- `AGENTS.md`:「Group switching keeps live views alive and re-navigates them.」—— 本票不改變「保活並重新導覽」這個既有正確行為,只修正「重新導覽尚未全部發出前,不該觸發 capture/save」這個時序漏洞。
- `AGENTS.md`:「All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace...」—— 本票的問題不是寫入本身不安全,而是寫入的**內容**在特定時序下是錯的;atomic replace 機制本身不需要變更。
- `AGENTS.md`(engineering rules 開頭):「Read the relevant spec section and trace every caller before touching shared code. A guard in the shared function is a smaller diff than a guard in every caller」—— `capture_pane_location`/`capture_locations` 是共用函式,`activate_group` 與 `delete_group` 都會觸發到,guard 應該放在能同時涵蓋兩者呼叫路徑的位置,不要各自在 `activate_group` 與 `delete_group` 分別修補。

## 檔案與範圍

- `src/app_shell/main.cpp`:
  - `activate_group`(約 2049-2073 行)
  - `delete_group`(約 2135-2143 行,同樣模式)
  - `capture_locations` / `capture_pane_location`(約 1324 行一帶)
  - `handle_navigation_complete`(約 1795 行一帶)、`save_now`(約 1784 行)

## Scope

1. 找出(或新增)一個能同時涵蓋 `activate_group` 與 `delete_group` 呼叫路徑的 guard 機制,防止「新 active group 已設定、但尚未所有 pane 都導覽完成」這段期間觸發的 capture 寫壞 session。
2. 迴圈結束(所有 pane 的 `navigate` 呼叫都已發出)後,補一次正常的 `save_now`,確保最終狀態仍會被持久化。
3. 若可行,新增一個 focused 的可執行 self-check(依 `AGENTS.md`「New non-trivial logic needs one focused runnable test or self-check」),驗證「Group 切換時,同步完成的導覽不會污染新 Group 的分頁資料」這個場景 —— 若這段邏輯與 HWND/COM 耦合太深難以在 `src/core` 測試,至少在交接區清楚記錄用什麼方式做了等價驗證(例如刻意建構會同步完成的導覽情境,觀察 session.json 內容)。

## Non-goals

- 不重新設計整個 session 儲存/防抖機制(那是 PD-091 的範圍,若兩票都要做,建議先做本票,PD-091 再疊加)。
- 不改變 Group 切換時「保留 live view、重新導覽」的既有策略。
- 不處理迴圈中途某個 pane `navigate` 失敗的情境(那是既有的 `navigation_failed` 路徑,行為不變)。

## Acceptance Criteria

1. 建立至少兩個都有多個 pane 且路徑不同的 Group,快速切換(例如連續點擊側邊欄兩個 Group 項目),重複多次,`session.json` 中每個 Group 的分頁路徑應始終等於該 Group 最後一次被還原/操作時的正確路徑,不會出現「新 Group 的分頁被寫成另一個 Group 的路徑」。
2. 針對本機資料夾(通常會同步完成導覽)重現此問題的最小情境,修正後應觀察不到污染。
3. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。
4. 不影響現有 Group 切換的視覺行為(pane 內容正確更新,無視覺 regressions)。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

> **驗證政策提醒:** 單一點擊/單一操作 + 截圖由 Agent 自行完成即可;需要連續多步驟操作反覆驗證 race 是否重現的部分,留給使用者在實機上驗證,不要用 computer-use 工具連續操作搶走使用者的滑鼠鍵盤。完成後在交接區寫清楚哪些是自己驗證過的、哪些留給使用者。

## 交接區

### 實作內容

- 在 `src/app_shell/main.cpp` 的 `AppState` 新增暫時性的
  `suppress_location_capture` guard。
- `capture_pane_location`、`capture_locations` 與 `save_now` 在 guard 生效時
  都不會擷取 live pane 路徑或寫入 session。
- `activate_group` 以及刪除 active Group 後的 realized-pane `navigate` 迴圈，
  在第一個 `navigate` 前開啟 guard，待所有 `navigate` 呼叫發出後關閉；既有
  迴圈結尾的 `save_now` 負責最後一次正常擷取與持久化。
- 保留既有的 live view 與重新導覽策略，未重建 pane HWND，也未改動
  `ExplorerHost`/Shell 回呼行為。

### 驗證結果

- Fix mechanism：同步 `BrowseToObject` 回呼在剩餘 pane 尚未發出 `navigate` 時，
  會被 guard 阻止進入 capture/save；所有目標 pane 都發出導覽後才由既有的
  `save_now` 保存完整狀態。
- Agent Checks：
  - `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：PASS。
  - `cmake --build build`：PASS，Release `PaneDock.exe` 成功建置。
  - `ctest --test-dir build --output-on-failure`：PASS，5/5 tests passed。
  - `git diff --check`：PASS。
- 未新增 app_shell/COM fake self-check；依 `docs/testing.md` 的測試政策，
  `core` 是唯一自動測試 seam，而本票的 guard 位於 HWND/COM 回呼時序，抽出
  測試 seam 會擴大本票範圍。已以完整編譯與既有 ctest 驗證無回歸；同步導覽
  污染情境仍需真實 Shell view 才能等價觀察。

### Acceptance Criteria

1. 未驗證,需真實桌面：建立至少兩個多 pane、不同路徑的 Group，連續切換並檢查
   `session.json`。
2. 未驗證,需真實桌面：以本機資料夾重現同步完成導覽，確認新 Group 沒有被舊
   Group 路徑污染。
3. PASS：上述 configure/build/ctest 全數通過。
4. 未驗證,需真實桌面：需在實機確認 pane 內容更新與視覺無 regression；程式碼
   保留原有 live view/`navigate`/`apply_layout` 流程。
