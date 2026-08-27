# PD-090 — 拖曳懸停自動切換 Group/tab 時,在 OLE 拖曳迴圈內同步做 Shell view 建立/銷毀與 session 寫入

## 來源

2026-08-27 三方稽核(Claude / Codex / OpenCode)。OpenCode 與 Claude 各自獨立指出同一個重入路徑:`DragHoverTarget` 的懸停計時器在 `DoDragDrop` 的 OLE 訊息迴圈內觸發 Group 切換,而 Group 切換會做 Shell view 的 realize/destroy 與同步磁碟寫入。

## 背景與現況

`DragHoverTarget::timer_expired`(`src/app_shell/main.cpp:249-266` 一帶)是 PD-034(拖曳懸停自動切換)實作的核心:拖曳外部檔案或內部項目、懸停在側邊欄 Group 項目或 tab 上一段時間後,`WM_TIMER` 觸發,呼叫 `hover_callback_`,最終導向 `activate_group`(`main.cpp:2074-2096`、`2221-2245` 一帶的呼叫點)。

問題在於:`WM_TIMER` 這個訊息是在 `RegisterDragDrop`/`DoDragDrop` 啟動的 **OLE 拖曳迴圈**期間被分派的——OLE 拖曳迴圈本身會重入應用程式的訊息幫浦(message pump)來維持拖曳游標更新、`DragEnter`/`DragOver`/`Drop` 事件正常運作。此時 `activate_group` 執行的完整鏈路是:

```
WM_TIMER(拖曳迴圈重入期間觸發)
  → hover_callback_
    → activate_group
      → apply_layout(可能 CoCreateInstance + IExplorerBrowser::Initialize,PD-087 修正後還會有 Destroy)
      → SetFocus
      → save_now(同步磁碟寫入,見 PD-091)
```

也就是在使用者手上還拖著檔案、來源 `IDataObject` 仍然存活的當下,同步建立/銷毀多個 Shell view、搶走焦點、並做同步磁碟 I/O。

## 為什麼這是真的問題

`AGENTS.md` 明確點名這類路徑:「Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.」拖曳是專案自己認定的高風險重入場景。目前 `invoking_` 這類旗標(若存在)只防止**我們自己**重複觸發,無法防止 Shell 端在這段同步工作進行時對拖曳迴圈本身的其餘處理造成的交錯執行。一個較慢的 shell extension、或使用者在懸停切換的瞬間持續移動滑鼠觸發額外 `DragOver`,都可能讓這段路徑的行為變得難以推理,且目前這段路徑同時疊加了 PD-086(可能污染 session)與 PD-091(同步磁碟寫入)兩個問題,三者疊加在拖曳這個最敏感的重入場景裡風險最高。

## Fix 方向

不要在 `WM_TIMER` 處理常式裡直接做 Group 切換的實際工作。改為:`timer_expired` 只負責判斷「該切換了」,然後 `PostMessageW` 一個私有訊息(例如既有慣例的 `WM_APP` 系列自訂編號)給主視窗,把真正的 `activate_group` 呼叫**延後到目前的 Shell/OLE 回呼完全返回、訊息幫浦回到正常佇列處理之後**才執行。

額外建議(依實作者評估是否必要,記錄在交接區):若切換發生時偵測到目前確實處於拖曳進行中(例如既有的拖曳狀態旗標),可以考慮暫緩 `save_now` 到拖曳結束(`Drop`/拖曳取消)之後再一併寫入,避免在拖曳當下疊加磁碟 I/O——但若 PD-091 已經把 `save_now` 改成防抖機制,這個額外考量可能自然被涵蓋,實作順序上建議先確認 PD-091 的狀態再決定是否需要在本票額外處理。

## 綁定限制(引用)

- `AGENTS.md`:「Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.」—— 本票直接對應此規則。
- `AGENTS.md`:「Event-driven idle path only. No busy loops, no polling timers.」—— 用 `PostMessageW` 延後執行,不引入新的輪詢或忙等。
- `AGENTS.md`:「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」—— 用既有的 `WM_APP` 自訂訊息模式(若專案已有其他地方用類似模式,沿用同一套慣例;若沒有,選擇最小的新增)。

## 檔案與範圍

- `src/app_shell/main.cpp`:
  - `DragHoverTarget::timer_expired`(約 `:249-266` 一帶)
  - `activate_group` 由拖曳懸停觸發的呼叫路徑(約 `:2074-2096`、`:2221-2245` 一帶)
  - `WndProc`(需新增處理延後執行的私有訊息分支)

## Scope

1. `timer_expired` 改為只送出一個延後執行的私有訊息,不在計時器回呼中直接呼叫 `activate_group`。
2. `WndProc` 新增分支處理該訊息,在正常的訊息迴圈時機(不在 OLE 拖曳迴圈重入期間)執行實際的 Group/tab 切換。
3. 確認拖曳操作本身(游標回饋、`DragOver`/`Drop` 行為)不受延後執行影響,使用者體感上懸停切換的時間點應與現在一致或幾乎無感差異。

## Non-goals

- 不重新設計 PD-034 的懸停判定邏輯(多久算「懸停」、哪些目標算合法懸停目標)——這些維持不變,本票只改變「判定成立後,實際切換動作的執行時機」。
- 不在本票內處理 PD-091(同步磁碟寫入)的完整修正,只在交接區記錄兩者的交互作用,依實作者判斷是否需要在本票額外做最小處理。
- 不改變拖曳來源/目標的 `IDataObject` 處理邏輯。

## Acceptance Criteria

1. 拖曳一個檔案(或內部項目)懸停在另一個 Group 項目上足夠時間,Group 切換行為與現在視覺上一致(切換時機、動畫/回饋無感差異)。
2. 懸停切換觸發後,拖曳操作本身(游標、後續放開檔案的 drop 行為)仍正常運作,不因延後執行而中斷或出現視覺錯誤。
3. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。
4. 若可行,提供一個聚焦的方式驗證「延後執行」機制本身有生效(例如在交接區描述如何確認 `activate_group` 沒有在 `WM_TIMER` 的呼叫堆疊內直接被觸發,可用中斷點或最小化的程式碼追蹤說明,不強制要求自動化測試)。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

> **驗證政策提醒:** 拖曳懸停切換的驗證涉及連續的滑鼠拖曳動作,屬於「連續多步驟操作」,不要用 computer-use 工具嘗試自動化拖曳驗證(會搶走使用者當下正在使用的實體滑鼠)。單一點擊/單一操作等級的驗證(例如確認程式仍可正常啟動、Group 列表正常顯示)可由 Agent 自行完成並截圖,拖曳互動本身的驗證留給使用者在實機上進行,並在交接區明確記錄。

## 交接區

（實作完成後由實作者填寫）
