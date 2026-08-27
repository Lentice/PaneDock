# PD-095 — 拖曳分隔線與視窗縮放時,每個 `WM_MOUSEMOVE`/`WM_SIZE` 都跑完整 `apply_layout`,含最多 1000 項的 Shell property 重掃

## 來源

2026-08-27 三方效能研究(Claude / Codex / OpenCode)。三方**各自獨立**指出同一組呼叫鏈:`update_splitter_drag` 每次滑鼠移動都呼叫完整 `apply_layout`,而 `apply_layout` 內的 `refresh_status_bar` 又會對選取項目逐一查詢 Shell property,最多到 1000 次 COM 呼叫。這是本次研究中收斂程度最高、且是唯一被三方一致列為「熱路徑」等級 CPU 問題的發現。

## 背景與現況

`update_splitter_drag`(`main.cpp:2458-2477`)在拖曳分隔線期間,每個 `WM_MOUSEMOVE`(`main.cpp:3726-3733`)都呼叫一次完整的 `apply_layout`(`main.cpp:1835-2004`)。同樣的路徑也會被 `WM_SIZE`(視窗縮放,`main.cpp:3700-3702` 一帶)觸發。每次 `apply_layout` 除了重新定位 pane 之外,還做了大量與「單純移動分隔線位置」無關的工作:

- `InvalidateRect(window, nullptr, TRUE)` 全窗 erase(`:1838`),連帶觸發 `paint_client_background` 重算 `layout_rects`、重繪 brand bar 與 4 個 pane card(各自建立/刪除數個 GDI brush/pen)。
- 每個 pane:`apply_pane_container_region` 做 `CreateRoundRectRgn`/`CombineRgn`/`SetWindowRgn`(`main.cpp:1586-1613`)、`apply_tab_item_size` 做 `GetDC`+逐 tab `GetTextExtentPoint32W`+兩個新配置的 `std::vector`(`main.cpp:1166-1288`)。
- 每個 pane:`refresh_status_bar` → `item_counts`(`explorer_host.cpp:453-492`):兩次 `ItemCount` COM 呼叫,若有選取項目,再逐一 `GetItemAt` + `QueryInterface(IShellItem2)` + `GetUInt64(PKEY_Size)`,上限 `kSelectionSizeItemLimit` = 1000 項(`explorer_host.cpp:20`)。這個上限本身是既有的、刻意的防線(見 `explorer_host.cpp:18-20` 的 `ponytail:` 註解),但**呼叫頻率**才是問題——現在是每次滑鼠移動、每個 pane 都重跑一次,而不是只在選取真的改變時才跑。
- `write_live_view_count()`(見 `main.cpp:333-349`)每次 `apply_layout` 都對 stdout 做一次 `WriteFile`,拖曳期間變成每個 mousemove 一次診斷輸出。

拖曳一次分隔線通常持續數百毫秒,期間會產生數十到數百個 `WM_MOUSEMOVE`,每個都重跑上述全部工作。

## 為什麼這是真的問題

這是三份報告一致認定的「最熱」路徑:拖曳分隔線是使用者互動中最頻繁觸發同一段程式碼的場景之一,而目前每一幀都在做「選取項目數量根本沒變、pane 內容也沒變」時完全不需要重算的工作(item_counts 的 Shell property 查詢、tab 文字量測、GDI 資源建立)。在選取項目較多時,肉眼可見卡頓。

## Fix 方向

把「單純移動分隔線」與「pane 內容/選取狀態相關的重算」分開:

- `update_splitter_drag` 的逐幀更新應該只做「重新計算受影響 pane 的矩形並 `SetWindowPos`」,不需要每幀都重跑 `refresh_status_bar`/`item_counts`、`apply_tab_item_size`、`write_live_view_count()`,以及全窗 `InvalidateRect`。這些只需要在拖曳**結束**時(`WM_LBUTTONUP`)做一次即可,或是分離出一個「只重排幾何、不重算內容」的輕量路徑供逐幀呼叫。
- `item_counts` 的結果應該被快取,只在選取真的變更(既有的 selection-changed callback)或導覽完成時才重新查詢,而不是每次 layout pass 都重查一次;`apply_layout`(非因選取或導覽觸發的呼叫,例如純粹分隔線拖曳幾何變化)不需要觸發 `refresh_status_bar`。
- `write_live_view_count()` 是 PD-026 的診斷輸出,應該加上診斷模式(`--diagnostic`)或等價的 gate,不在每次 layout pass 都無條件寫 stdout(若既有程式碼已經有相關旗標可以直接沿用,不需要新增機制)。

實作者可依現有程式碼慣例挑選最小改動的等價方案,記錄在交接區——核心要求是「拖曳分隔線逐幀只做幾何重排,內容相關的重算(選取計數、tab 文字量測、診斷輸出)在拖曳期間不重複執行」。

## 綁定限制(引用)

- `AGENTS.md`:「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」—— 不需要重寫 `apply_layout` 的整體結構,只需要拆分出「純幾何重排」與「內容相關重算」兩類工作,讓逐幀拖曳只觸發前者。
- `explorer_host.cpp:18-20` 既有的 `ponytail:` 註解:「global lock, per-account locks if throughput matters」類型的既有 1000 項上限不變——本票不改變這個上限本身,只改變觸發頻率。
- `AGENTS.md`:「Event-driven idle path only. No busy loops, no polling timers.」—— 本票不新增計時器或輪詢,只改變既有事件觸發時做的工作量。

## 檔案與範圍

- `src/app_shell/main.cpp`:
  - `update_splitter_drag`(約 `:2458-2477`)
  - `apply_layout`(約 `:1835-2004`),特別是 `refresh_status_bar`/`item_counts` 呼叫點(約 `:1978`)、`write_live_view_count()` 呼叫點(約 `:1854`/`:2002`)
  - `WM_SIZE` 處理(約 `:3700-3702`)
- `src/explorer_host/explorer_host.cpp`:
  - `item_counts`(約 `:453-492`)——若要加快取,改動範圍在此
  - `set_selection_changed_callback`(約 `:1970` 對應呼叫點,確認選取變更時仍會正確觸發重新查詢)

## Scope

1. 拆分 `apply_layout` 中「幾何重排」與「內容相關重算」(status bar/item counts、tab 文字量測、診斷輸出),讓 `update_splitter_drag` 的逐幀呼叫只觸發前者。
2. `item_counts` 的結果快取,只在選取真的變更或導覽完成時才重新查詢 Shell property。
3. `write_live_view_count()` 加上既有診斷模式的 gate,不在一般模式下每次 layout pass 都寫 stdout。

## Non-goals

- 不改變 `kSelectionSizeItemLimit` = 1000 這個既有上限。
- 不改變 splitter 拖曳結束後(`WM_LBUTTONUP`)的最終一次完整 `apply_layout`——那次仍應完整重算,確保最終狀態正確。
- 不處理 PD-090(拖曳懸停自動切換 Group/tab)涵蓋的 OLE 拖放重入問題,那是不同的拖曳場景(檔案拖放,不是分隔線拖曳)。

## Acceptance Criteria

1. 拖曳分隔線期間,`item_counts`/`refresh_status_bar` 的實際呼叫次數應遠低於 `WM_MOUSEMOVE` 事件數(例如整段拖曳只在放開滑鼠時觸發一次,而不是每幀一次)。
2. 拖曳結束後,所有 pane 的狀態列（項目數量、選取大小等）與版型都正確反映最終狀態,無視覺 regression。
3. 選取項目數量變更(在拖曳之外的一般操作中)仍會正確、即時地更新狀態列——本票不能讓選取計數變成過期資料。
4. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

> **驗證政策提醒:** 單一點擊/單一操作 + 截圖由 Agent 自行完成即可;實際拖曳分隔線的流暢度與大量選取項目下的卡頓改善,需要在實機上持續互動才能感受,留給使用者驗證,不要用 computer-use 工具連續操作搶走使用者的滑鼠鍵盤。完成後在交接區寫清楚哪些是自己驗證過的、哪些留給使用者。

## 交接區

### 實作

- `src/app_shell/main.cpp` 的 `apply_layout` 新增內容重算旗標。分隔線
  `WM_MOUSEMOVE` 只做 pane/chrome 的幾何排版與 `SetWindowPos`；不做全窗
  `InvalidateRect`、tab 文字量測、status bar/item count 或 live-view stdout
  輸出。`WM_LBUTTONUP` 傳入完整模式，最後一次仍會重算所有內容。
- `ExplorerHost::item_counts` 現在快取成功結果；`selection_changed()` 與
  `navigation_complete()` 會先失效快取，因此既有選取變更 callback 與導覽完成
  callback 仍即時刷新 status bar，layout 重跑只讀快取，不重複查詢 Shell property。
  `kSelectionSizeItemLimit` 維持 1000。
- `write_live_view_count` 沿用既有 `--diagnostic` 狀態；一般模式不再寫 stdout。
- 沒有新增 COM fake 測試：`explorer_host` 依賴真實 `IExplorerBrowser`，不屬於
  專案的 `core` 自動測試 seam；另執行既有 ExplorerHost runtime self-check。

### Agent Checks

以下命令均成功：

```text
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
.\build\panedock_explorer_host_lifetime_check.exe
git diff --check
```

- Configure：成功。
- Release build：成功，完成 `PaneDock.exe` link。
- CTest：`5/5` 通過（`panedock_diagnostic_flag`、`panedock_tab_overflow`、
  `panedock_core_model`、`panedock_core_layout`、`panedock_core_session`）。
- `panedock_explorer_host_lifetime_check`：成功，Initialize、導覽、view mode
  與 Destroy／live-view 清理檢查通過。
- `git diff --check`：成功。

### Acceptance Criteria 驗證狀態

1. **程式碼已驗證，實際拖曳留給使用者**：`WM_MOUSEMOVE` 走幾何模式，
   `WM_LBUTTONUP` 才走完整模式；未以 computer-use 連續拖曳或量測每次事件。
2. **程式碼與建置已驗證，視覺結果留給使用者**：放開滑鼠的完整 layout 仍會
   更新所有 pane 的 geometry、tab 與 status bar；未在本次 session 操作真實 UI
   進行視覺 regression 確認。
3. **程式碼路徑已驗證，實機選取操作留給使用者**：Shell selection callback
   會先清除 counts cache 再呼叫既有 `refresh_status_bar`；navigation complete
   也維持既有 `selection_changed()` 路徑。未在真實桌面執行大量選取操作。
4. **已驗證**：configure、`cmake --build build`、`ctest --test-dir build
   --output-on-failure` 均成功。
