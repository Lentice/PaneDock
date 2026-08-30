# PD-134 — 主視窗還原到已不存在監視器的位置：app「開啟」了但沒有任何可見 UI

Phase 7 · app_shell startup · Depends on: (none)

- Source: 2026-08-30 startup audit loop。使用者要求「audit AP startup … 避免 AP 無法順利開啟或開啟後沒有 UI …」稽核確認。
- Priority: HIGH——「開啟後沒有 UI」是使用者明確點名的情境，且這是一般 Windows 應用程式的經典實務 bug。

## 已確認的根因（有程式碼證據）

`wWinMain`（`src/app_shell/main.cpp:5743`）以**未驗證**的 `state.application.window_placement` 建立主視窗：

```cpp
const auto& placement = state.application.window_placement;
HWND window = CreateWindowExW(
    0, kWindowClassName, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
    placement.x, placement.y, placement.width, placement.height, ...);
ShowWindow(window, placement.maximized ? SW_SHOWMAXIMIZED : show_command);
```

`window_placement` 是上一次執行時由 `capture_window_placement`（`main.cpp:3519`）從 `GetWindowPlacement` 的 `rcNormalPosition`（螢幕座標，副螢幕時可為負值）原樣存下的。若該位置位於某個**已不存在**的監視器上（關機後拔除外接螢幕、筆電 dock 移除、解析度改動後該座標落出虛擬桌面範圍），則視窗會還原到螢幕外。`CreateWindowExW` / `ShowWindow` 都成功，app「開啟」了但**使用者看不到任何視窗**。程式碼中沒有任何對虛擬桌面範圍或監視器 work area 的 clamp（`rg` 確認無 `MonitorFromWindow`/`GetMonitorInfo`/`SM_CXVIRTUALSCREEN`）。

`placement.maximized` 的情況不受影響：`SW_SHOWMAXIMIZED` 會由 Windows 最大化成當前監視器。問題只發生在一般（還原）狀態的視窗。

## Binding constraints — quoted, do not weaken

`AGENTS.md`：

> App UI text must be English. No Chinese strings ship in the binary.

`AGENTS.md`：

> Keep `src/core` free of HWND, COM and `windows.h`.

`AGENTS.md`：

> Reach for the standard library and Win32 before adding a dependency.

`docs/design-spec.md` NFR-004：Per-Monitor-V2 DPI，混合 DPI 多監視器是正常情境。

## 要讀取與 trace 的檔案

- `src/app_shell/main.cpp:3519`（`capture_window_placement` 存下未被驗證的座標）、`:5739`-`:5759`（`wWinMain` 建立並顯示主視窗）。
- `src/core/model.h:62`（`ApplicationState::WindowPlacement`）。
- `src/app_shell/tab_overflow.h`＋`tests/unit/tab_overflow_test.cpp`（既有「app_shell 純幾何 + static_assert 自測」模式，本票沿用）。

## Fix 方向 / Scope

在 `wWinMain` 建立主視窗前，用虛擬桌面範圍驗證一般（未最大化）視窗矩形；若矩形完全落在虛擬桌面之外（或尺寸退化），把位置重設為 `CW_USEDEFAULT`（Windows 會在螢幕內 cascade 出一個看得見的位置），避免「開啟後沒有 UI」。

1. 新增 `src/app_shell/window_placement.h`：純函式 `placement_is_offscreen(x, y, width, height, virtual_x, virtual_y, virtual_width, virtual_height)`，回傳「完全在虛擬桌面外或尺寸退化（width/height ≤ 0）」，僅用標準庫、可 `constexpr`，沿用 `tab_overflow.h` 的純幾何＋`static_assert` 自測慣例。
2. `wWinMain` 建立主視窗前：
   ```cpp
   auto placement = state.application.window_placement;
   const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
   const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
   const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
   const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
   if (vw > 0 && vh > 0 && !placement.maximized &&
       placement_is_offscreen(placement.x, placement.y, placement.width,
                              placement.height, vx, vy, vw, vh)) {
       placement.x = CW_USEDEFAULT;
       placement.y = CW_USEDEFAULT;
       if (placement.width <= 0) placement.width = 1000;
       if (placement.height <= 0) placement.height = 700;
   }
   ```
   `vw > 0 && vh > 0` 防住特殊/無桌面情境下 metric 暫時為 0 造成的誤 reset。
3. 用 `placement`（而非原始的 `state.application.window_placement`）傳給 `CreateWindowExW` 與 `ShowWindow`。

為何有效：完全在虛擬桌面外的矩形被拉到螢幕內可見位置的 cascade slot；`placement.maximized` 不走驗證；純幾何部分以 static_assert 自測。

## Non-goals

- 不新增監視器熱插拔（`WM_DISPLAYCHANGE`）執行期重排、不新增 work-area 感知「最大化到哪個監視器」邏輯——執行期 `WM_DPICHANGED`/monitor 變更屬既有行為，本票只防「開啟即無 UI」的還原失敗。
- 不做「部分在螢幕外」的重排（若矩形與虛擬桌面有交會，使用者可抓回視窗，不再強制重排）。
- 不改 `src/core`。
- 不新增 toast/tray/splash；沿用既有「開啟即有視窗」模型。

## Acceptance Criteria

1. `cmake --build build`、`ctest --test-dir build --output-on-failure`、`git diff --check` 全數通過，且新增的 `panedock_window_placement` 測試通過。
2. `rg -n "placement_is_offscreen|SM_CXVIRTUALSCREEN" src/app_shell`：`window_placement.h` 有純函式，`main.cpp` 在 `CreateWindowExW` 前呼叫並以驗證後的 placement 建立/顯示視窗。
3. 手動/script 驗證：把測試中暫時把 `window_placement` 設為完全離螢幕座標（如 `x=100000,y=100000`）後啟動，app 應在螢幕內出現視窗（至少可抓回），不應「開了卻看不到」。正常落在螢幕內的 session 座標不被改變（無誤 reset）。此為可復現的人工檢查；若無法以 script 驅動視窗堆疊，於交接區記錄。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
rg -n "placement_is_offscreen|SM_CXVIRTUALSCREEN" src/app_shell
```

## Handoff requirements

- 記錄純函式與 `wWinMain` 驗證點的分工；記錄「為何只在還原、不在執行期」的決定。
- 記錄 build／CTest／diff 結果，以及是否實際以離螢幕 session 驗證過「app 仍出現在螢幕內」。
- 任何未驗證項目均須標明，不得以編譯通過代替根因。

## 交接區

- 實作結果:新增 `src/app_shell/window_placement.h`(純 `constexpr` `placement_is_offscreen`,僅 `<algorithm>`),`wWinMain` 在 `CreateWindowExW` 前取得虚拟桌面 metric(`SM_XVIRTUALSCREEN`/`SM_YVIRTUALSCREEN`/`SM_CXVIRTUALSCREEN`/`SM_CYVIRTUALSCREEN`),若 `!maximized` 且矩形完全在虚拟桌面外或尺寸退化,把 x/y 重設為 `CW_USEDEFAULT`(尺寸退化時補回 1000×700)。`maximized` 走 Windows 的 `SW_SHOWMAXIMIZED`,不受影響;部分重疊矩形不改動,讓使用者可抓回。
- 分工:純幾何在 `window_placement.h`(可 static_assert 測試),Win32 metric 與「何時 reset」留在 `wWinMain`。
- 為何只在還原:執行期監視器變更(DVI/HDMI 熱插拔)屬既有 NFR-004/`WM_DPICHANGED` 行為,本票只防「開啟即無可見 UI」的還原失敗,不新增熱插拔重排邏輯。
- Agent checks: `cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake -DCMAKE_BUILD_TYPE=Release`、`cmake --build build`、`ctest --test-dir build --output-on-failure`(7/7 通過,含新增 `panedock_window_placement`)、`git diff --check`、`rg -n "placement_is_offscreen|SM_CXVIRTUALSCREEN" src/app_shell` 皆符合預期。
- 未驗證項:未以「離螢幕 session」實際重啟桌面驗證 app 出現在螢幕內(需改 session.json 座標＋重啟);靜態自測已覆蓋離螢幕/部分重疊/退化/負原點/`CW_USEDEFAULT` 等分支,人工實機驗證留予使用者。
