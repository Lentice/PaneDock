# PD-091 — 每次資料夾導覽完成都同步寫入 session(多次 JSON 解析 + 兩次強制 flush),UI 執行緒可能卡頓

## 來源

2026-08-27 三方稽核(Claude / Codex / OpenCode)。OpenCode 與 Claude 各自獨立指出同一段程式碼與同一個成本組成。

## 背景與現況

`handle_navigation_complete`(`src/app_shell/main.cpp:1795-1812` 一帶)在每次 `OnNavigationComplete` 都呼叫 `save_now`(`main.cpp:1784`)。`save_now` 最終呼叫 `panedock::core::write_session`(`src/core/session.cpp:520-570` 一帶,實際序列化/驗證/flush 邏輯約 `:522-588`),目前每次呼叫的成本包含:

1. 重新序列化整份 `preserved_json`(`serialize_session`,`session.cpp:504` 一帶)。
2. 寫入暫存檔,`FlushFileBuffers`(強制落盤)。
3. `read_file(primary)` **重新讀取並解析剛寫入前的舊檔案**,做寫入前驗證。
4. `copy_file` 複製一份到 `.bak`,再對備份檔 `FlushFileBuffers`。
5. Atomic rename。

也就是每次使用者點一下切換資料夾,就有 3 次 JSON 解析(序列化目前狀態、驗證讀回、隱含在 `is_valid` 檢查中)加上 2 次強制磁碟 flush。切換 Group 時若同時有 4 個 pane 各自完成導覽,這個成本會被觸發到 4 次。

## 為什麼這是真的問題

這條路徑正好落在 `docs/design-spec.md`/`docs/performance-baseline.md` 的效能 NFR 量測範圍內(一般操作的回應時間),且是在 UI 執行緒上同步執行——如果目標路徑是慢速網路磁碟或使用者磁碟正忙,`FlushFileBuffers` 可能明顯延遲,期間整個訊息迴圈(以及所有 pane 的 Shell view)都會被卡住。這也與 PD-090(拖曳懸停觸發的 Group 切換)疊加:懸停自動切換這種非使用者直接點擊的路徑,一樣會觸發這整套同步寫入。

## Fix 方向

**防抖/合併寫入,而非改變 atomic replace 的正確性。**

- 導覽完成時只標記「有未儲存的變更」(dirty flag),不立即寫檔。
- 用一個現有程式碼已經在用的機制(`WM_TIMER`,例如懸停偵測已有先例)設一個短暫的防抖計時器(數值由實作者依既有慣例挑選,例如幾百毫秒等級,記錄在交接區並說明理由);計時器到期時,若 dirty flag 仍為真,才真正呼叫一次 `write_session`,一次 Group 切換觸發的多個 pane 導覽完成會被合併成一次寫入。
- **必須保留的既有保證(見下方 PD-078 依賴):** 應用程式正常關閉(`WM_CLOSE`/`WM_DESTROY`)與 `WM_QUERYENDSESSION` 這些既有的收尾時機,必須確保「若有 dirty 但尚未落盤的變更,一定會在這些時機同步 flush 完成」,不能讓防抖機制製造出「使用者剛做的操作,程式正常關閉後卻沒存到」的退化。
- 額外的次要優化(依實作者評估是否值得做,不是本票的核心目標):`write_session` 目前寫入後又 `read_file(primary)` 重新解析做驗證,可以改成直接驗證剛序列化好的記憶體內容(你已經知道這份資料是合法的,不需要再讀回解析一次)——這能減少一次 JSON 解析,但不是本票的必要範圍,若時間有限可以只做防抖、不做這項。

## 綁定限制(引用)

- `AGENTS.md`:「All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.」—— 本票不改變寫入機制本身的 atomic-replace 正確性,只改變「多久觸發一次寫入」。
- **本票依賴 PD-078(`確保電腦當機/斷電時 session.json 不會毀損`,已完成)的既有保證不能被防抖機制破壞** —— PD-078 處理的是「寫入過程中斷電/當機」的資料完整性,本票處理的是「寫入頻率」,兩者正交,但實作時必須確認防抖期間累積的未落盤變更,在正常關閉路徑上仍會被寫入,且寫入本身仍遵守 PD-078 建立的 crash-safe 流程。
- `AGENTS.md`:「Event-driven idle path only. No busy loops, no polling timers.」—— 防抖計時器是「因狀態變更而啟動、到期後自行停止」的一次性計時器,不是常駐輪詢,符合既有專案在拖曳懸停偵測上已經採用的相同模式,不違反此規則。

## 檔案與範圍

- `src/app_shell/main.cpp`:
  - `save_now`(約 `:1784` 一帶)
  - `handle_navigation_complete`(約 `:1795-1812` 一帶)
  - `activate_group` 中每個 pane 觸發的儲存呼叫(約 `:2061-2071` 一帶)
  - 應用程式關閉/`WM_QUERYENDSESSION` 的既有收尾邏輯(需找到並確認防抖變更會在此處被強制落盤)
- `src/core/session.cpp`:
  - `write_session`(約 `:520-588` 一帶),若要順便移除多餘的讀回驗證,改動範圍在此

## Scope

1. 引入 dirty flag + 防抖計時器,取代「每次導覽完成都立即同步寫入」。
2. 確認並補上:應用程式正常關閉路徑、`WM_QUERYENDSESSION` 路徑在有未落盤變更時強制同步寫入一次。
3. (可選)移除 `write_session` 內對剛寫入 primary 檔案的多餘讀回解析驗證,改為驗證記憶體中的序列化結果。

## Non-goals

- 不改變 `session.json`/`.bak` 的 atomic-replace 檔案格式或版本 schema。
- 不引入外部函式庫或執行緒(防抖用既有的 `WM_TIMER` 機制即可,不需要背景執行緒)。
- 不處理 PD-090(拖曳懸停重入)的訊息延後執行邏輯本身——本票只確保疊加在該路徑上的儲存成本被合併,不重新設計拖曳懸停的觸發機制。

## Acceptance Criteria

1. 連續快速切換多個資料夾(例如在同一個 pane 內連點 5 個不同子資料夾),`write_session` 實際被呼叫的次數應明顯少於導覽完成的次數(防抖生效的證據,可在交接區用計數器或其他方式說明如何驗證)。
2. Group 切換觸發多個 pane 同時完成導覽時,最終只產生一次(或遠少於 pane 數量的)實際磁碟寫入。
3. 應用程式在有未落盤變更的狀態下正常關閉,重新開啟後,`session.json` 反映的是關閉前的最新狀態,沒有遺失最後一次操作(驗證防抖沒有破壞既有的資料持久性保證)。
4. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過,包含既有涵蓋 PD-078 crash-safe 寫入行為的測試不受影響。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

> **驗證政策提醒:** 單一點擊/單一操作 + 截圖由 Agent 自行完成即可;需要連續多次切換資料夾並觀察防抖行為的驗證,若能用非互動方式(例如直接呼叫相關函式多次並觀察 mock/計數結果)驗證則優先採用;若必須用真實 UI 連續操作驗證,留給使用者在實機上進行,不要用 computer-use 工具連續操作。完成後在交接區寫清楚驗證方式。

## 交接區

### 實作內容

- `src/app_shell/main.cpp` 的 `AppState` 新增 `session_dirty` 與主視窗 HWND。
  `handle_navigation_complete` 現在只標記 dirty 並以 `SetTimer` 排程保存，不再
  直接呼叫 `write_session`。
- 防抖計時器使用 `kSessionSaveTimerId = 0xD050` 與
  `kSessionSaveDelayMilliseconds = 500`。每次導航完成都重設同一個 timer，
  `WM_TIMER` 收到後先 `KillTimer`，再對 dirty 狀態呼叫既有 `save_now`，因此是
  因變更啟動、到期即停的一次性 timer，不是常駐輪詢。500ms 取幾百毫秒等級，
  可合併快速連續導覽與多 pane 完成事件，同時不讓一般操作長時間沒有保存。
- `save_now` 只有寫入成功才清除 dirty 並取消 timer；寫入失敗會保留 dirty。
  若 `SetTimer` 失敗，會記錄診斷訊息並退回同步 `save_now`，避免失去保存機會。
- `WM_CLOSE`、`WM_QUERYENDSESSION` 仍同步呼叫 clean-shutdown save；`WM_DESTROY`
  若仍有 dirty 狀態也會同步補寫一次。這三條路徑都可在 PD-086 的
  `suppress_location_capture` guard 仍有效時強制寫出現有 model，且關閉前先取消
  debounce timer。
- `src/core/session.cpp` 未修改；PD-078 的 `write_session` atomic replace、temp/
  backup durability hook 與 `FlushFileBuffers` 路徑完整保留。可選的寫入後讀回解析
  優化未納入本票。

### Agent Checks

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：PASS。
- `cmake --build build`：PASS，LLVM-MinGW Clang/Ninja Release build。
- `ctest --test-dir build --output-on-failure`：PASS，5/5 tests passed；包含
  `panedock_core_session` 的 PD-078 crash-safe session 測試。
- `git diff --check`：PASS。
- 聚焦 source self-check：PASS；確認導航完成區段沒有 `save_now`，只排程
  `schedule_session_save`，並確認 timer 先 `KillTimer`、三個關閉時機都走強制同步
  save。

### Acceptance Criteria

| # | 結果 | 證據 |
|---|---|---|
| 1 | 未驗證，需真實桌面 | source self-check 確認導航完成不再直接寫入並會重設 500ms debounce；連續點擊五個資料夾及實際 `write_session` 計數尚未在 UI 執行，留給使用者驗證。 |
| 2 | 未驗證，需真實桌面 | 最新 PD-086 狀態下 Group 切換仍由 guard 保護，完成後保留單一路徑保存；四 pane 同步/非同步完成的實際磁碟寫入次數尚未在實機觀察，留給使用者驗證。 |
| 3 | 未驗證，需真實桌面 | source 已確認 timer pending 時 `WM_CLOSE`/`WM_QUERYENDSESSION` 及 dirty 的 `WM_DESTROY` 會同步保存；關閉後重開並比較 session 檔案的端到端驗證尚未執行。 |
| 4 | PASS | 指定 configure/build/ctest 全數通過，CTest 5/5 passed。 |
