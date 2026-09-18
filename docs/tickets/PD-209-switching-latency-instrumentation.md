# PD-209 — Group 切換／tab realize 的延遲計時儀器

Phase 7 · switching path performance · Depends on: PD-024, PD-026

- Source: 2026-09-18 使用者要求「改善 tab／Group 切換的速度與健壯性」。
- Priority: MEDIUM——它本身不改善任何延遲，但它是**唯一**能讓後續延遲修法
  合法的前置條件。

## 覆寫聲明

本票覆寫兩項既有決定，依 `AGENTS.md`「When a ticket overrides an earlier
decision, state the override inside the new ticket」記錄於此：

1. **PD-026（2026-08-24）刻意排除產品內計時儀器**，理由是「三者都沒有
   blocking 門檻，加儀器要動產品程式碼」。
2. `docs/tickets.md` §候選 的「產品內的計時儀器(Group 切換／tab realize／
   cold start 延遲)」一列，觸發條件為「若使用者實際回報切換有感延遲」。

**新證據**：
- 使用者於 2026-09-18 直接要求改善切換速度，觸發條件成立。
- 同日的雙方稽核（Claude finding 2、Codex finding 2）在原始碼層面證明
  `navigate_realized_panes`（`src/app_shell/main.cpp:732-751`）在 UI 執行緒
  **序列化**最多四次導覽，而真正做 binding／列舉的
  `ExplorerHost::browser_->BrowseToObject`（`src/explorer_host/explorer_host.cpp:638`）
  **完全沒有時限**——`navigation_bind_context()` 的 1000 ms
  `dwTickCountDeadline`（`explorer_host.cpp:24,55`）只掛在
  `SHCreateItemFromParsingName` 上，而 SMB／網路 provider 常無視
  `BIND_OPTS`。這是一個具體、可歸因的無界阻塞面，不是猜測。
- `docs/performance-baseline.md:14-16` 的三列（Group switch latency／
  Group switch latency, one unreachable network path／Tab realize latency on
  activation）全為 **Not measured / Not exercised**，而同一份文件規定
  「任何優化提案必須先有本表的量測數字」。目前的狀態讓那條規則變成死結：
  沒有儀器就沒有數字，沒有數字就不能改。本票解開這個死結。

## 要讀與追的檔案

- `src/app_shell/diagnostic_mode.h`：`diagnostic_requested` 的旗標解析。
- `src/app_shell/main.cpp`：`write_live_view_count`（`:374`）——**既有的
  診斷 stdout 輸出範本，本票沿用同一條通道與同一種
  `panedock.<key>=<value>` 格式**；`AppState::diagnostic_mode`；
  `perform_group_transition`（`:1939`）；`navigate_realized_panes`（`:732`）；
  `apply_layout`（`:1590`）；`realize_startup_panes`。
- `src/app_shell/pane.cpp`：`Pane::realize`、`Pane::navigate_to`（`:790`）、
  `finish_tab_change`（`:1153`）。
- `docs/performance-baseline.md`：要填的三列與量測方法欄位。
- `tests/` 下 `panedock_launch_smoke` 如何讀取診斷 stdout。

## 範圍

1. `src/app_shell/diagnostic_mode.h` 旁新增一個極小的計時輔助
   （header-only，僅 `QueryPerformanceCounter` + `QueryPerformanceFrequency`）：

   ```cpp
   class ScopedTiming final {
   public:
       ScopedTiming(bool enabled, const char* key) noexcept;
       ~ScopedTiming() noexcept;  // enabled 時寫出 panedock.<key>_ms=<double>
   };
   ```

   `enabled` 為 false 時建構與解構都不做任何事（不呼叫
   `QueryPerformanceCounter`），確保非診斷模式下零成本。
   輸出沿用 `write_live_view_count` 的 `WriteFile(STD_OUTPUT_HANDLE, ...)`
   路徑；把該函式的寫出邏輯抽成一個共用的
   `write_diagnostic_line(const char* key, ...)`，不要複製第二份。

2. 插入三個量測點：
   - `perform_group_transition`（`:1939`）整體 →
     `panedock.group_switch_ms`。
   - `navigate_realized_panes`（`:732`）整體 →
     `panedock.group_switch_navigate_ms`（切出「導覽佔多少」這個關鍵比例）。
   - `Pane::finish_tab_change`（`pane.cpp:1153`）的 `navigate_to` 區段 →
     `panedock.tab_realize_ms`。

3. 用儀器實測並填寫 `docs/performance-baseline.md:14-16` 三列：
   - Group switch latency：四個 pane、本機路徑。
   - Group switch latency, one unreachable network path：其中一個 pane 指向
     不存在的 UNC（例如 `\\10.255.255.1\share`），量到實際阻塞秒數。
   - Tab realize latency on activation：切換到一個未 realize 的 tab。

   每一列都要寫出量測命令、機器與樣本數，取代目前的
   「Not measured / Not exercised」。

## 非目標

- **不做任何延遲優化**。本票只產生數字。導覽的非阻塞化是後續票的範圍
  （見 `docs/tickets.md` §候選）。
- 不加 cold start 計時（PD-026 的第三項）：切換路徑之外，本票不擴張。
- 不新增命令列旗標——沿用既有的 `--diagnostic`。
- 不引入依賴、不寫檔案、不開執行緒。計時只走既有的診斷 stdout。
- 不在非診斷模式下留下任何執行期成本。

## 驗收條件

1. `PaneDock.exe --diagnostic` 在 Group 切換、tab 啟用時各輸出對應的
   `panedock.*_ms=` 行。
2. 未帶 `--diagnostic` 時沒有任何計時輸出，且計時程式碼不執行
   `QueryPerformanceCounter`。
3. `docs/performance-baseline.md:14-16` 三列填入實測數字、量測方法與機器，
   不再是「Not measured / Not exercised」。
4. `write_live_view_count` 與新的計時輸出共用同一個寫出函式，格式一致。
5. 既有測試全綠，`panedock_launch_smoke` 不退化。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

外加：以 `--diagnostic` 啟動並手動切換 Group／tab，貼出 stdout 中的
`panedock.*_ms=` 行作為交接區證據。

## 交接區

2026-09-18 儀器部分完成，**量測部分尚未執行**（本票仍未完成）。

- `write_live_view_count` 的寫出邏輯抽成
  `panedock::app_shell::write_diagnostic_line(key, value)`，與新的
  `ScopedTiming` 一起放在 `src/app_shell/diagnostic_mode.h`（原本規劃放
  `main.cpp`，但 `Pane` 也需要用，放 header 才不用複製第二份）。
  header 因此新增 `<windows.h>`／`<array>`／`<charconv>`／`<system_error>`。
- `ScopedTiming(enabled, key)`：`enabled` 為假時建構子直接 return、`start_`
  保持 0，解構子也直接 return——不呼叫 `QueryPerformanceCounter`。
- 三個量測點：
  - `perform_group_transition` → `panedock.group_switch_ms`
  - `navigate_realized_panes` → `panedock.group_switch_navigate_ms`
  - `Pane::finish_tab_change` 的 `navigate_to` 區段 → `panedock.tab_realize_ms`
- `Pane` 取得診斷旗標的方式：`PaneHost` 新增純虛擬
  `bool diagnostic_timing_enabled() const noexcept`。**不叫
  `diagnostic_mode()`**——`AppState` 已經有一個同名的 `bool` 成員，會衝突。
  `tests/unit/test_pane_host.h` 的 `TestPaneHost` 回傳 `false`。
- `ctest`：33/33 通過。`panedock_launch_smoke` 通過即證明
  `write_diagnostic_line` 的輸出格式沒有退化（該測試會解析
  `panedock.live_view_count=N`）。

### 剩下的工作（下一位接手者）

填寫 `docs/performance-baseline.md:14-16` 三列。需要**實機互動**（點 sidebar
切 Group、點 tab），無法自動化——UI 自動化是本專案已否決的方向，而
`panedock_launch_smoke` 只啟動與關閉。步驟：

1. 準備一份 `%LOCALAPPDATA%\PaneDock\session.json`，含兩個四 pane Group；
   其中一個 Group 的一個 pane 指向不可達 UNC（例如 `\\10.255.255.1\share`）。
2. `build\PaneDock.exe --diagnostic > timing.txt`，來回切換 Group 與 tab 各
   10 次以上，關閉。
3. 從 `timing.txt` 取 `group_switch_ms`／`group_switch_navigate_ms`／
   `tab_realize_ms` 的中位數與最大值，連同機器與樣本數填進
   `docs/performance-baseline.md`。
