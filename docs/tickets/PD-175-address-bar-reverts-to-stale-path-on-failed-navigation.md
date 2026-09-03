# PD-175 — 位址列在導覽失敗後被還原成舊路徑，使用者剛輸入的內容沒有保留

Phase 7 · app_shell · Depends on: PD-020, PD-022

## 來源

2026-09-03 三方稽核（Codex 獨立提出）。經 fork 對照現有原始碼核對，判定為 CONFIRMED-NEW：`PD-022` 建立的錯誤面板不涵蓋位址列 `EDIT` 控制項本身。

## 背景與現況

使用者在位址列輸入一個無法解析的路徑並按 Enter 時：

1. `submit_address`（`src/app_shell/main.cpp:3648-3661`）讀取 `EDIT` 的文字，直接交給 `ExplorerHost::navigate(location(std::move(text)))`，之後**不再回頭處理該 `EDIT`**。
2. `ExplorerHost::navigate` 解析失敗時呼叫 `navigation_failed()`（`explorer_host.cpp:626`／`:633`），並保留 `location_` 為使用者輸入的值（`explorer_host.cpp:616-619` 的註解明確說明此意圖）。
3. `navigation_failed()` 回呼到 `handle_navigation_failed`（`main.cpp:2712-2718`），該函式呼叫 `refresh_navigation_chrome(state, pane_index)`。
4. `refresh_navigation_chrome`（`main.cpp:1544-1556`）用 **model 裡最後一次成功的** location 覆寫位址列：

```cpp
text = display_text_for_parsing_name(
    state, active_tab(active_group(state).panes[pane_index]).location.parsing_name);
...
SetWindowTextW(state.pane_chrome[pane_index].address_bar(), text.c_str());   // :1555
```

結果：pane 內的錯誤面板（`PD-022`）顯示使用者剛輸入的失敗路徑，但**位址列已經跳回舊路徑**。使用者無法看到、也無法修改剛才輸入的內容（例如只是打錯一個字，現在得整段重打）。

注意 `PD-020` 的交接區（第 146 行）曾記錄「使用者輸入保持原樣」為當時的預期行為：

> 「address navigation 無法解析時不會收到 successful completion callback，因此不會 `record_navigation` 或覆寫 EDIT；使用者輸入保持原樣」

該描述在當時成立，但 `PD-020` 後續的 Reviewer 修正（同文件第 163-165 行）為了修「back/forward 永久停用」而新增了 `set_navigation_failed_callback` → `handle_navigation_failed` → `refresh_navigation_chrome` 這條路徑，**副作用就是位址列現在會被覆寫**。本票即修正這個副作用，不是重開既有決策。

## 為什麼這是真的問題

`docs/design-spec.md` FR-012（不可解析的 location）與其在 §296 的摘要要求「無法解析的 location：tab 內可復原錯誤，保留設定（FR-012）」。目前錯誤面板與位址列顯示兩份互相矛盾的路徑，且使用者的輸入被丟棄——這與 `PD-022` 建立可復原錯誤狀態＋重試按鈕的整體意圖直接衝突（重試按鈕要重試的是失敗的那個路徑，位址列卻顯示另一個路徑）。

## Fix 方向

讓 `handle_navigation_failed` 不要無條件覆寫位址列。最小改法方向（實作者依實際程式碼結構挑選並記錄理由）：

- **A（推薦）**：把 `refresh_navigation_chrome` 拆成「更新按鈕狀態」與「同步位址列文字」兩件事——`handle_navigation_failed` 只需要前者（它的原始目的是修 back/forward 按鈕永久停用，見 `PD-020` 第 163-165 行），不需要後者。`refresh_navigation_buttons`（`main.cpp:1546` 已被 `refresh_navigation_chrome` 呼叫）看起來已經是現成的前者，實作時確認即可。
- **B**：`handle_navigation_failed` 改為用 `ExplorerHost` 已保留的 `location_`（即使用者輸入值）去同步位址列，而非 model 的舊 location。

兩種都必須確認不影響「非 address 來源的導覽失敗」（例如 back/forward 失敗、Shell view 內部導覽失敗、啟動還原失敗）時位址列應該顯示什麼，並在交接區說明每一種來源的預期顯示結果。

## 綁定限制（引用）

- `docs/design-spec.md FR-012`／§296：「無法解析的 location：tab 內可復原錯誤,保留設定（FR-012）」。
- `AGENTS.md`：「Display names are never identifiers.」——位址列顯示的是顯示用文字，不得因為本票的改動被當成 identity 寫回 model 或持久化。
- `AGENTS.md`：「Read the relevant spec section and trace every caller before touching shared code.」——`refresh_navigation_chrome` 是共用函式，改它前必須追完所有呼叫者（`main.cpp:2717`、`:3490` 等）。
- `AGENTS.md`：「Prefer the smallest working change.」

## 檔案與範圍

- `src/app_shell/main.cpp`：`refresh_navigation_chrome`（:1544-1556）、`refresh_navigation_buttons`（:1546 呼叫處，定義在其上方）、`handle_navigation_failed`（:2712-2718）、`submit_address`（:3648-3661）、`refresh_navigation_chrome` 的所有其他呼叫者（至少 `:2717`、`:3490`，以 `rg -n "refresh_navigation_chrome" src/app_shell/main.cpp` 實際結果為準）。
- `src/explorer_host/explorer_host.cpp`：`navigate` 的失敗路徑（:624-628、:631-634）、`navigation_failed()`（:984 一帶）、`location_` 的保留語意（:616-619 的註解）。
- `docs/tickets/PD-020-address-bar-and-navigation-buttons.md`（第 146 行的原始預期、第 163-165 行造成本副作用的修正）、`PD-022-unresolvable-location-error-and-retry.md`（錯誤面板與重試的既有行為）。

## Scope

1. 讓導覽失敗時位址列保留使用者剛輸入的內容（或至少與錯誤面板顯示同一個路徑），同時**保留** `PD-020` 修正的原始目的：back/forward 按鈕不會永久停用。
2. 確認並在交接區列出四種失敗來源（address 輸入、back/forward、Shell view 內部導覽、啟動還原）各自的位址列預期顯示。
3. 加上一個聚焦 self-check 或 source-level 檢查驗證 `handle_navigation_failed` 不再從 model 的舊 location 覆寫位址列。

## Non-goals

- 不改變 `PD-022` 的錯誤面板 UI、文字或重試按鈕行為。
- 不新增路徑預驗證、自動修正或自動完成行為（`PD-020` 明確不做預驗證；`PD-044` 已另外處理自動完成）。
- 不改變 `tab.location` 的寫入時機或 session 持久化語意（失敗的路徑不應該被寫進 model，這是既有正確行為）。

## Acceptance Criteria

1. 在位址列輸入一個不存在/無法解析的路徑並按 Enter 後，位址列仍顯示使用者輸入的內容，且與 pane 內錯誤面板顯示的路徑一致。
2. Back/Forward 導覽失敗後，那兩顆按鈕不會永久停用（`PD-020` 第 163-165 行修正的行為不回歸）。
3. 成功導覽後位址列顯示規範化後的路徑（既有行為不變）。
4. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "refresh_navigation_chrome|refresh_navigation_buttons|handle_navigation_failed|submit_address" src/app_shell/main.cpp
```

> **驗證政策提醒：** 單次「輸入錯誤路徑 + Enter + 截圖」屬於 Agent 可自行完成的單一操作驗證；四種失敗來源的完整人工比對可留給使用者，在交接區寫清楚哪幾項未由 Agent 實測。

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-09-03 實作交接

#### 完成內容

- 採用 Fix 方向 A。`handle_navigation_failed()` 清除 back/forward 的 suppression flag 後，只呼叫既有的 `refresh_navigation_buttons()`；`refresh_navigation_chrome()` 保留給成功導覽、tab refresh 與 history 呼叫端，仍負責同步正常狀態下的規範化位址列文字。
- 因此 address Enter 失敗時不再從 model 的最後成功 location 呼叫 `SetWindowTextW`，EDIT 保留使用者剛輸入的文字；既有 `ExplorerHost::navigation_failed()` 仍以同一個 `location_` 更新 PD-022 錯誤面板，兩者一致。沒有修改 `tab.location`、session persistence、錯誤面板或 Retry 行為。
- 新增 `tests/release/address_bar_failure_check.ps1` 並註冊為 `panedock_address_bar_failure` CTest，確認 failure handler 只刷新 buttons、未呼叫完整 chrome refresh／`SetWindowTextW`，且正常 chrome helper 仍保有位址列同步。

#### 四種失敗來源的位址列預期

- Address input：保留該次 Enter 的原始 EDIT 文字，並與 PD-022 錯誤面板的失敗 `location_` 一致。
- Back/Forward：既有 `navigate_tab_history()` 在送出 history navigation 後會同步完整 refresh，失敗時位址列顯示 model 選定的 history target；failure callback 只重新啟用按鈕，不再額外覆寫 EDIT。
- Shell view 內部導覽：失敗 callback 不改位址列，維持最後一次成功導覽顯示的文字；錯誤狀態仍由既有 `ExplorerHost` 路徑處理。
- Startup restore：`refresh_startup_chrome()` 先以 persisted tab location 填入位址列；初始 Shell 失敗不會因本票 handler 再次覆寫，故維持該 persisted location，與錯誤面板使用的 `location_` 一致。

#### Agent checks

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：通過。
- `cmake --build build`：原 `build` 在最後連結時因既有 PID 37396 持有 `build\\PaneDock.exe` 回報 `ld.lld: failed to write output 'PaneDock.exe': Permission denied`；未使用 `/F` 強殺。隔離的 `build-pd175` 以相同 toolchain configure/build 通過，只有既有的 missing-field-initializers warnings。
- `ctest --test-dir build --output-on-failure`：18/19 通過；新增 `panedock_address_bar_failure` 通過，唯一失敗為 `panedock_launch_smoke` 偵測到既有 PID 37396 正在執行。隔離 `build-pd175` 的 `ctest --test-dir build-pd175 --output-on-failure -E panedock_launch_smoke` 為 18/18 通過。
- `rg -n "refresh_navigation_chrome|refresh_navigation_buttons|handle_navigation_failed|submit_address" src/app_shell/main.cpp`：通過；結果確認 failure handler 在 `2818` 呼叫 `refresh_navigation_buttons`，完整 chrome refresh 仍由 `1928`、`1937`、`3600` 使用。
- `git diff --check`：通過。
