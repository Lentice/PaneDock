# PD-245 — Worker STA 的 Shell namespace 列舉與 parent 值

Phase 8 · PD-234 拆分 · 目標 1–2 天

## 來源與交付

來源：[PD-234](PD-234-managed-list-shell-namespace.md)、[自建列表規格](../research/2026-10-16-managed-list-spec.md) Implementation Decisions／Testing Decisions。狀態與依賴只見 `docs/tickets.md` Ticket 總覽。

交付一個結果：既有有界 Source 能在自己的 STA 列舉 This PC／My Computer、Desktop 及其 Shell folder children，回傳 owned child identity、canonical location 與 parent 值。此票不切換 Pane 虛擬導覽路由；下一切片整合。

## Overrides

覆寫 PD-234 單票實作的工作切分，不縮減 namespace 支援。追查發現目前 enumeration worker 只接受 filesystem syntax，且 Batch 沒有 resolved location／parent；`Request` equality 同時用 binding 與原 location 拒收 stale batches，不能直接將 request.location 改成 canonical name。未重開已否決的 fake ExplorerBrowser、UI 自動化、程序隔離或 plugin framework。

依使用者 2026-10-20 指示，覆寫 PD-226 作為本票實作推進阻擋條件的決定：阻擋 PD-226 的外部實機證據延後，允許下游實作繼續。本票實作依賴僅保留 PD-243；PD-226 的未解驗收不由本票認證或結清，總覽仍保留其 `in_progress`。此覆寫不宣稱真實 Shell namespace PASS，也不降低 PD-241 的實機 gate。

## 綁定限制（引用）

- design-spec FR-009：「可存取網路磁碟機、USB 磁碟區、OneDrive 佔位檔,以及標準 Shell 位置(桌面、文件、本機)。」本票交付標準虛擬位置，不增加 OneDrive 特性。
- design-spec NFR-003：「Group／版型／tab 切換與 pane input 不得因為某個 Shell location、metadata provider 緩慢或無法連線而凍結 UI。」FR-012：「不得因此刪除任何已儲存的設定。」
- design-spec §10：「持久化的 identity 為 parsing name ＋ known-folder identity ＋ fallback path。display name 永不作為 identity。」
- development：「Use Microsoft::WRL::ComPtr for every interface pointer.」「A pane-only feature is implemented in Pane or one of its sub-modules, not in the coordinator.」「Never destroy a parent HWND while a hosted view is alive.」
- AGENTS：「Keep src/core free of HWND, COM and windows.h.」「Never persist a PIDL or a COM pointer.」「Event-driven idle path only. No busy loops, no polling timers.」「New non-trivial logic needs one focused runnable test or self-check.」未知 session 欄位保留、schema version、atomic replace／backup 不變。
- worker 只能在 `src/managed_list/list_worker.cpp`；自行建立 STA 及 COM，所有 interface／PIDL 在該 STA 釋放、先於 CoUninitialize；只傳 owned value。無 UI-owned COM、HWND、PaneState／AppState 存取，不把 locks 加進 UI STA。
- 沿用 4 enumeration slots（含 abandoned）、1 pending／2 queued batches per slot、128 rows/batch、100,000 rows／24 MiB active rows 的容量策略。慢 provider 不能強制中止；cancel 失效 interest，shutdown 不 join 無界 OS 呼叫，卡住的 slot 不釋放重用。不得另建無界 namespace pool；不修改 PD-226 的工作或文件。
- 正式預設待 PD-241；legacy initialized host 仍先 Destroy。不得改選單／檔案操作、加服務、telemetry、第三方 runtime 或 elevation。

## 必讀檔案與 caller

- `AGENTS.md`、`docs/development.md`、`docs/design-spec.md` FR-005/006/009/010/012、NFR-002/003/005、§9–11、`docs/testing.md`、`docs/performance-baseline.md`、來源規格及兩份 `docs/adr/0001-cpp-wrl-win32-over-rust-and-csharp.md`、`0002-stable-pane-identity-across-layout-switch.md`。
- PD-243 交付的 `src/managed_list/list_worker.h` Row identity；`Request`、`Batch`、Source；`list_worker.cpp` enumerate、enumerate_and_watch、run、publish、interested、acquire、invalidate、LocalSource submit/take/cancel 及全部 caller；`list_worker_test.h` 的 controlled ReadGate。
- `src/shell_core/shell_core.cpp`、`.h` 的 capture_location／resolve_location 及全部 caller；`src/explorer_host/explorer_host.cpp` navigate／navigate_up／on_navigation_complete 的 identity 行為。共用 helper 只回值、不回 raw COM。
- `src/app_shell/pane_managed_list.cpp` process_content_results／resolve_list_navigation／apply_list_changes／cached rescan 是未來結果消費者；不得破壞原 request equality。
- `tests/unit/managed_list_capacity_test.cpp`、`managed_list_changes_test.cpp`、`managed_list_restore_test.cpp`、`test_pane_host.h`、`tests/CMakeLists.txt`。

## 具體範圍

1. 在現有 enumeration slot 增加 Shell folder 列舉分支，沿用 filesystem backend；虛擬 parsing name 不可替換成 fallback filesystem path。known-folder resolve／canonical capture／child absolute parsing identity／Shell parent lookup 全在 worker STA；只保留 folder 可導覽語意，不把 display name 當 identifier。
2. Batch 增加獨立 resolved location／parent outcome（含 root 無 parent、失敗與 notification capability），原 Request 不變。所有 batch 的 metadata 一致且可檢查；根 Desktop、This PC、無 filesystem path 的 folder、bind 失敗均能表達。無法 bind 保留輸入 identity 並容許手動重試，不靜默跳到 fallback 改變虛擬位置。
3. 每個 provider pump／呼叫前後檢查 interest；回傳仍經 serial／generation／binding 過濾。對名稱、parsing／known-folder／fallback 長度與 queued owned bytes 設明確上限，新增欄位計入既有預算；超限回 limited／error，不能擷取完整無界集合再分批。
4. 虛擬位置無可靠通知時回傳明確 manual-refresh capability，不把成功列舉誤報為 watching 或 watch failure。PD-232 本機 watcher／overflow recovery 保持；不加入 Shell notification framework 或 polling。

## Commander 實作提示

[既有研究](../research/2026-10-16-commander-performance.md) 記錄 Double Commander 將列表與 icon 載入分階段、Explorer++ 延後可見項目的昂貴資料與拒收過期結果。應採 incremental enumeration／progressive reveal：第一批可直接使用，不等待所有 metadata；inactive tab 不預熱。identity 與 display name 分離是本專案契約。bounded work／cancel／零 polling idle 成本須以本專案測試證明，不能由其他產品推論。Shell-native fallback 只限確實需要的既有相容路徑，不把所有 virtual folder 無條件丟回 ExplorerBrowser，也不以慢 provider 當理由重試風暴；Pane 路由在 PD-246 決定。

## Non-goals

不改 Pane 路由／history／session，不做 plugin framework、任意 namespace 相容性保證、右鍵／file operations／OneDrive；不修改 completed ticket、PD-226 或 stage-reveal layout。

## Acceptance criteria

- [x] Source 可表達 virtual identity、child／canonical／parent 與 manual notification capability，原 request token 不被 canonicalization 改寫。
- [x] controlled provider delay／late delivery／capacity exhausted／cancel／shutdown 案例證明沿用同一 slot cap 與 bounded batches；不 fake IExplorerBrowser 或假裝 controlled calls 是真實 COM。
- [x] 增加 owned value metadata 的 focused CTest；本機 change／capacity checks 保持通過，無 UI COM 或無界等待。
- [x] 真實 This PC／Desktop children、known-folder bind、provider STA teardown 仍明列未驗證，需真實桌面；不因 deferred validation 宣稱真實 Shell PASS。

## Agent checks

新增 `tests/unit/managed_list_namespace_worker_test.cpp`，以 `tests/CMakeLists.txt` table 註冊 `managed_list_namespace_worker`，只跑 owned-value／controlled worker 測試（無 live Shell bind）。

```powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target panedock_managed_list_namespace_worker_test panedock_managed_list_capacity_test panedock_managed_list_changes_test
ctest --test-dir build --output-on-failure -R '^panedock_managed_list_(namespace_worker|capacity|changes)$'
git diff --check
```

使用者延後實機／UI 驗證，不執行 launch smoke 或 GUI。交付須記錄實際命令、容量與 teardown 未解風險；交接區只放未解風險與不可重推結論。超過兩天先再拆，不擴寫此票。


## Agent checks 紀錄（2026-10-20）

使用 LLVM-MinGW Release，實際執行：

```powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target panedock_managed_list_namespace_worker_test panedock_managed_list_capacity_test panedock_managed_list_changes_test
ctest --test-dir build --output-on-failure -R '^panedock_managed_list_(namespace_worker|capacity|changes)$'
git diff --check
```

Configure 成功；三個 targets 建置成功；三個 CTests 全部通過（3/3，最後一次共 1.59 秒）；diff whitespace check 通過。受限環境的首次 build 停滯且沒有編譯子程序，核對命令後只停止該次 CMake／Ninja（PID 9604／31672），相同 build／CTest 改在 elevated 執行環境完成；這不是應用程式要求 elevation。

Focused test 僅使用 owned-value／controlled worker：保留原 Request equality、canonical／child／parent 與 root、bind error／重試、manual-refresh capability、名稱與三個 location 欄位長度限制、batch bytes、兩個 queued batches 的 backpressure、四個共用 slots（包含 abandoned）、1,000 次容量耗盡重試、cancel／late delivery 與 non-joining destruction。每個 batch 上限 2 MiB（含 vector capacity、字串 capacity、Request、resolved／parent）；四個 slots × 兩個 queued batches 合計最多 16 MiB，另各有一個建構中的有界 batch。既有 128 rows/batch、100,000 rows／24 MiB active rows 限制仍適用。這些是值與容量檢查，並非 process memory 或真實 provider 效能量測。

未執行 GUI、launch smoke 或 live Shell bind；未宣告真實 Shell namespace PASS。所有列出的 acceptance checks 已勾選，focused Agent checks 通過 3/3。依使用者 2026-10-20 指示，阻擋 PD-226 的外部實機證據延後，允許下游實作繼續；本票不認證或完成 PD-226，其總覽仍保留 `in_progress`。狀態與依賴僅以總覽為準。

## 交接區

- 未解風險：This PC／Desktop 真實 children、known-folder bind、無 filesystem path 的 provider 與 STA COM teardown 仍未驗證，需真實桌面。列舉需要 Win32／Shell COM，不能放入純模型 `src/core`；controlled owned-value CTest 不取代此實機檢查。
- 未解風險：PD-226 所需真實慢／斷線來源、桌面回應性及 pending shutdown／取消證據仍未齊備；依使用者 2026-10-20 指示延後這些外部實機證據，允許下游實作繼續，不代表本票認證或完成 PD-226（總覽仍為 `in_progress`）。任意 provider 呼叫或 COM Release 卡住時，只保證失效 interest、保留 bounded slot 且 UI 不 join；不能保證 provider 終止或安全完成真實 teardown，PD-241 gate 仍需實證。
