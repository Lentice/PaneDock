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

2026-09-18 儀器完成。2026-10-20 取得本機 Group/tab 基線；2026-10-21 取得 pending UNC Group-switch timing 與單次 UNC error completion observation。

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
- 2026-10-20 的單次隔離 GUI 重試仍未取得延遲樣本：stdout 停在
  `panedock.live_view_count=0,1,3`，等待後 `sky.list_windows()` 仍沒有
  PaneDock 視窗，且正常關閉要求在 5 秒內未完成。這不是 Group／tab 延遲
  證據；診斷啟動的視窗就緒問題仍待查明。
- 2026-10-20 再次以單次 `Start-Process` 啟動隔離設定（兩個 Group 均只用
  本機測試資料夾）重試，未呼叫第二次 launch。等待 3 秒並重新列舉後仍無
  PaneDock 視窗；stdout 仍只有 `panedock.live_view_count=0,1,3`，沒有任何
  latency 行。對同一 PID 發出正常關閉要求後 5 秒仍未結束；最後觀察到該
  PID `Responding=true`、標題空白。沒有啟動第二個實例，也沒有取得有效樣本。
- 後續 Computer Use 重查時，`sky.list_apps()` 與 `sky.list_windows()` 均未列出
  PaneDock。對原測試 PID 再送一次正常 `CloseMainWindow` 後，程序回報
  `Responding=true`、標題 `PaneDock — Diagnostic Mode`，但該 HWND 仍未出現在
  Computer Use 的視窗清單，無法安全地進行 Group/tab 互動；未再啟動新實例。
- 2026-10-20 重新執行一次隔離診斷啟動，PID 27008。LOCALAPPDATA 隔離設定有
  成功寫出 session；stdout 仍只有 `panedock.live_view_count=0,1,3`，stderr
  空白，Computer Use 的 fresh `list_apps()`／`list_windows()` 均未列出視窗；
  Task Manager 顯示程序執行中、CPU 00。正常 `CloseMainWindow` 回報已送達，
  等待 15 秒程序仍存活。檢查隔離 session 發現它沿用 3-pane Groups 且帶有
  大量 tabs，並非票面要求的兩個四-pane 測試組；此次不能當作延遲或相容性樣本。
  根因仍未證實；需先以最小、正確的四-pane 測試 session 重現，再釐清啟動視窗
  不可見／正常關閉未完成的原因。依 AGENTS.md 單實例規則，此 PID 未退出前不
  再啟動另一份 PaneDock。
- 使用者於同日手動啟動 `build\\PaneDock.exe` 後確認主視窗正常出現。Computer Use
  fresh enumeration 也列出唯一 `PaneDock` 視窗，截圖顯示正常模式與已載入的
  Groups；因此前述不可見現象只在隔離 `Start-Process` 診斷啟動中觀察到，尚未
  證明是產品一般啟動缺陷。此正常模式沒有 `--diagnostic` 計時輸出。對現有視窗
  嘗試一次 tab 操作失敗；依程序重新列舉並重選後，`activate_window` 仍回報
  `failed to activate captured window`，已停止 UI 操作；沒有完成 Group/tab 延遲量測。
- 使用者依隔離命令啟動 Diagnostic Mode 後，Computer Use 成功列舉唯一視窗並
  擷取畫面；標題為 `PaneDock — Diagnostic Mode`，隔離 session 顯示一個本機
  `Group 1`、4 panes、每 pane 一個 tab。對 `New Group` 的單次操作回報失敗；
  fresh re-list 仍列出同一視窗，但重新選取後 `activate_window` 再次回報
  `failed to activate captured window`，故操作結果未確認且未重試。尚無第二個
  Group 或任何延遲樣本；依 Computer Use 恢復規則停止自動 UI 操作。

### 測量邊界（本票單一路徑樣本之外）

`docs/performance-baseline.md` 記錄本機 Group/tab 各 n=20、pending UNC 期間 Group switch handler timing，以及另一筆同一 TEST-NET path 的 completion/error observation。兩筆 UNC 樣本分別回答切換是否等待列舉、以及這次單一 OS request 何時回錯；都不是其他 provider、一般權限網路、容量耗盡或任意慢 provider 的推論。若未來有具體價值再擴充 provider 矩陣，不重跑已完成的本機量測。

### 2026-10-21 隔離 UNC pending switch（部分量測）

Claude 以目前 Release `build\PaneDock.exe`（SHA-256 `E630EAC2325DFBE6896BC05B49D41B72887D2E95E3F8C4605398320694E23586`）啟動兩組、每組四個 pane 的新 session：Group A 四個本機 marker folder；Group B 三個本機 folder 加 `\\192.0.2.1\PD226`。使用絕對隔離 `LOCALAPPDATA=build\manual-test\pd209u\run-261021153356\la`，確認載入兩個正確 Groups 後，只做 A→B 與一次 B→A；stdout／stderr 明確導向隔離檔案，stderr 為空。A→B 輸出 `panedock.group_switch_navigate_ms=6.1204` 與 `panedock.group_switch_ms=15.8231`；點擊後約 177 ms 時三個本機 pane 各顯示 1 item，UNC pane 顯示 `0 items - Loading...`。約 3.4 秒觀察期間 `SendMessageTimeout(WM_NULL)` 均回 `0 ms, hung=False`，UNC 仍 pending。B→A 為 6.057 ms／14.4854 ms，UI 同樣回應。

正常關閉約 145 ms、無殘留 PaneDock process；正式 session 檔案時間與大小不變。輸出／截圖在 ignored `build\manual-test\pd209u\run.out.txt` 與 `run-261021153356`。此結果填補 baseline 網路 Group-switch row 的 handler timing 與 pending UI responsiveness 單次樣本；不等於量到 provider 最終阻塞／完成時間、不可達性全貌、capacity exhaustion 或 stuck-provider shutdown。三列已不再全為未量測，但本票其餘 checks／完整測試證據仍須遵循使用者目前「只跑相關測試」指示；本次僅記錄部分證據。

### 2026-10-21 單次 UNC 錯誤完成時間觀察

Claude 使用同一 Release `build\PaneDock.exe`（SHA-256 `E630EAC2325DFBE6896BC05B49D41B72887D2E95E3F8C4605398320694E23586`）與隔離 `LOCALAPPDATA=build\manual-test\pd209-completion\run-261021160646\la`。Windows 11 Pro 10.0.26300 x64、Intel Core i7-12700K、63.7 GB RAM；PaneDock 以提升權限執行。對 `\\192.0.2.1\PD226` 只送出一次唯讀列舉：首個 2 ms 樣本顯示 `0 items - Loading...`，100 ms 間隔記錄 187 筆；於 20,988 ms 首次觀察到 `0 items - Cannot read this location. Refresh to retry. Error 53`。每筆都呼叫 `SendMessageTimeout(WM_NULL)`，最大 ping 2 ms，未逾時。此為外部讀取列表狀態的觀測時間，不是 provider 內部精確完成時刻；也只量到錯誤回傳，沒有量成功或取消路徑。

觀察結束後 `WM_CLOSE` 約 187 ms 退出，PaneDock process count 為 0；正式 session metadata 未變、stdout 只有 live-list 計數、stderr 空白，未碰 clipboard、未做檔案操作或修改 source。Process exit code 未取得。CSV、stdout／stderr 與前後截圖保留在 ignored `build\manual-test\pd209-completion\run-261021160646\`。
