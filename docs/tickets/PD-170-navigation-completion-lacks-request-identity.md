# PD-170 — 導覽完成/失敗回呼只有 pane index、沒有 request/tab/Group identity，pending 導覽可能寫進錯的 tab

Phase 7 · app_shell / explorer_host · Depends on: PD-020, PD-086

## 來源

2026-09-03 三方稽核（Codex 獨立提出，OpenCode 未涵蓋此項）。經 fork 對照現有原始碼與 `PD-020` 交接區核對，判定為 CONFIRMED-NEW：此為 PD-020 交接區明確記錄、明確聲明「留給後續真正遇到才處理」但從未被獨立開票追蹤的已知缺口。

## 背景與現況

`handle_navigation_complete`（`src/app_shell/main.cpp:2687-2710`）：

```cpp
void handle_navigation_complete(AppState& state, std::size_t pane_index,
                                const panedock::core::ShellLocation& new_location) {
    if (state.shutdown_deferred || state.closing_) return;
    if (!has_active_group(state) ||
        pane_index >= active_group(state).panes.size()) return;
    auto& tab = active_tab(active_group(state).panes[pane_index]);   // :2693
    ...
    tab.location = std::move(completed_location);                    // :2697/:2702
    ...
}
```

這個回呼只帶 `pane_index`，`tab` 是「呼叫回呼**當下**該 pane 的 active tab」，不是「發起這次導覽時的那個 tab」。`ExplorerHost::navigate` 的完成事件（`explorer_host.cpp:188` 一帶的 `OnNavigationComplete`）本身也沒有任何 request token 可以關聯回發起者。

`PD-020-address-bar-and-navigation-buttons.md` 的交接區（第 144、166 行）已經明確記錄這個缺口：

> 「使用者在 pending 期間從 Up/address/tab/Group 發起另一導覽，bool 仍無 request identity 可關聯。Scope 只授權 completion callback 與每-pane bool，未擴充 request token/failure callback。」
>
> 「交接區原文列出的『pending 中從 Up/address/tab/Group 發起另一導覽,bool 仍無 request identity 可關聯』這個更深的競態仍未解決,維持原交接區的紀錄...留給後續真正遇到才處理,不在本次一併擴充。」

`PD-086-group-switch-overwrites-incoming-folders-with-outgoing-live-state.md`（已完成）只在 `activate_group` 的迴圈期間用 `suppress_location_capture` guard 住 `capture_pane_location`/`save_now` 的呼叫，**沒有**保護 `handle_navigation_complete` 本身對 `tab.location` 的寫入——這兩張票保護的是不同的資料寫入點，不重疊。

## 為什麼這是真的問題

具體場景：pane 1 的 tab A 正在導覽到一個慢速網路路徑（尚未完成），使用者在等待期間切到 tab B（或切 Group、或在 pane 1 重新輸入另一個位址）。當 tab A 那個「舊」的 `OnNavigationComplete` 事件終於到達時，`handle_navigation_complete` 讀到的 `active_tab(active_group(state).panes[pane_index])` 已經是**現在**的 active tab（可能是 tab B，或另一個 Group 的 tab），於是把 tab A 的完成結果寫進了 tab B 的 `location`／history，造成資料錯亂；或者反過來，新導覽的完成事件被舊的 `suppress_history_record`/guard 邏輯誤判。這正好落在使用者本次要求稽核的「path 切換／tab 切換／group 切換」交集點上。

## Fix 方向

依 PD-020 交接區已經指出的方向：把目前的「每-pane bool」（`suppress_history_record`）擴充為可以關聯成功/失敗事件的 pending-navigation state——每次發起導覽時產生一個遞增的 request 世代號（沿用專案既有的「generation」慣例，例如 `DragHoverTarget::hover_generation_` 的模式），存在該 pane 的 pending 狀態裡；`OnNavigationComplete`/`OnNavigationFailed` 事件回呼時附帶當時的世代號，`handle_navigation_complete`/`handle_navigation_failed` 比對世代號與該 pane 目前的世代號是否一致，不一致就直接丟棄（不寫入 tab、不動 UI），因為代表這是一個已經被更新的導覽取代的過期事件。

## 綁定限制（引用）

- `docs/design-spec.md NFR-005`：「必要狀態（版型、location、tab、view mode、排序）必須精確還原,否則視為缺陷。」——寫錯 tab 的 location 直接違反這條。
- `AGENTS.md`：「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」——沿用專案既有的世代號慣例（`DragHoverTarget`），不要引入新的 request-tracking 框架。
- `AGENTS.md`：「Keep `src/core` free of HWND, COM and `windows.h`.」——世代號比對邏輯若能落在 `core`（純資料比較）應盡量落在 `core`，`ExplorerHost`/`main.cpp` 只負責傳遞世代號。

## 檔案與範圍

- `src/app_shell/main.cpp`：`handle_navigation_complete`（:2687-2710）、`handle_navigation_failed`（:2712-2718）、`suppress_history_record` 欄位（`AppState` 定義處）、所有呼叫 `state.explorers[pane_index].navigate(...)` 的入口（address/back/forward/up/tab切換/group切換）。
- `src/explorer_host/explorer_host.h/.cpp`：`set_navigation_callback`/`set_navigation_failed_callback`、`navigate`、`navigation_complete()`/`navigation_failed()` 的回呼簽章（若要傳遞世代號，簽章需要擴充）。
- `docs/tickets/PD-020-address-bar-and-navigation-buttons.md`（已記錄此缺口的原始交接區）、`PD-086-group-switch-overwrites-incoming-folders-with-outgoing-live-state.md`（相鄰但不重疊的既有保護）。

## Scope

1. 在 `ExplorerHost` 或呼叫端（依查證結果決定放在哪一層更小改動）為每個 pane 的導覽請求加上遞增世代號。
2. `navigate()` 每次被呼叫時遞增並記錄目前世代號；完成/失敗回呼附帶發起時的世代號。
3. `handle_navigation_complete`/`handle_navigation_failed` 比對世代號，不一致時安全丟棄（不寫入、不動 UI、不影響既有的 suppression bool 邏輯）。
4. 補上一個聚焦 self-check（若邏輯可下放到 `core` 的純函式，寫成 core 測試；否則依 `docs/testing.md` 人工核對並在交接區說明理由）。

## Non-goals

- 不處理 PD-168 的「同步阻塞」本身——本票假設導覽最終會非同步完成或失敗，只處理「完成事件到達時，狀態已經變了」的身分關聯問題。
- 不改變 `suppress_history_record`（back/forward 專用的既有旗標）的既有語意，只補上世代號比對作為額外防護，不移除既有機制。
- 不重新設計 PD-086 的 `suppress_location_capture` guard。

## Acceptance Criteria

1. 模擬「pane 導覽中途切 tab／切 Group／重新輸入位址」的場景（可用延遲觸發完成回呼的測試手法模擬），舊導覽的完成事件到達時不應寫入目前已經不是原發起 tab 的 `location`/history。
2. Back/Forward 既有的 pending-guard 行為（PD-020 既有測試涵蓋範圍）不受影響。
3. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "handle_navigation_complete|handle_navigation_failed|suppress_history_record" src/app_shell/main.cpp
```

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 實作紀錄（2026-09-03）

- `ExplorerHost` 為每次導覽配置遞增的 `NavigationGeneration`，以 queue 保留 Shell 事件的開始順序；`navigation_complete`/`navigation_failed` 會把 generation 傳給 app。由於 `IExplorerBrowserEvents` 本身沒有 request token，這是目前可取得的事件關聯邊界；過期 generation 在 host 層先略過 location/UI/callback 更新。
- `AppState` 每個 pane 保存 `(generation, group_id, tab_id)`，所有 app 導覽入口（address、history、Up、tab、Group、refresh、pinned 與 tab drag）在呼叫 Shell 前建立 identity。回呼會同時核對 generation 與目前 active Group/tab，不符即丟棄；`suppress_history_record` 的 back/forward 語意未變。
- 新增不含 HWND/COM 的 `core::navigation_request_matches` 與 `panedock_core_navigation` focused test，涵蓋 generation、Group 和 tab 任一欄位不符的情況。未改動 PD-168 的同步 Shell 呼叫，也未重新設計 PD-086 guard。

### 驗證

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：PASS
- `cmake --build build`：PASS
- `ctest --test-dir build --output-on-failure`：18/18 PASS（包含 `panedock_launch_smoke`）
- `rg -n "handle_navigation_complete|handle_navigation_failed|suppress_history_record" src/app_shell/main.cpp`：PASS
- 真實桌面上的延遲 Shell completion、切換 tab/Group 後的互動驗收未執行；需依 `docs/testing.md` 原型驗收協定手動驗證。
