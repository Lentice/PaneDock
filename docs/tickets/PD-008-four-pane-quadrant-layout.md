# PD-008 — 四宮格版型與矩形計算

Phase 0 · app_shell · Depends on: PD-007

- Source: `AGENTS.md`、`docs/design-spec.md` §3.2／§9.2／§9.4、`docs/development.md`、`docs/testing.md`
- Origin: 2026-08-20 由 PD-001 拆分。
- Override: 本 ticket 與 PD-007／PD-009／PD-010／PD-011 共同取代 PD-001。
- Priority: **HIGH**——本案的決定性風險是「多實例 `IExplorerBrowser` 能否在同一 process 內穩定共存」,這是第一個真正暴露該風險的 ticket。

## Goal

把 PD-007 的單一 view 視窗擴成四宮格,四個 pane 各自持有一個獨立的 `IExplorerBrowser`,可各自導覽到不同位置。

刻意是四分割而不是二分割:被測的風險就是「多實例」,二分割測不出來。

## 已確認的產品決策

1. 比例固定 0.5／0.5,不做可拖曳分隔線。
2. 四個 pane 的初始路徑可硬編碼或由命令列給定,彼此不同以便觀察獨立性。
3. 只做「四宮格」這一種版型。單一／四宮格的切換屬於 PD-009,五種版型屬於 Phase 2。
4. 矩形計算在本階段可放在 `app_shell`。正式的五種版型矩形計算歸 PD-005,屆時會移進 `core`;本片不要為了預先對齊 PD-005 而設計抽象。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Keep `src/core` free of HWND, COM and `windows.h`.

`docs/design-spec.md` §9.2:
> 單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。

`docs/development.md`:
> Use `Microsoft::WRL::ComPtr` for every interface pointer. Raw `AddRef`/`Release` pairs are not acceptable in new code.

## Files to read and trace first

- `src/app_shell/`、`src/explorer_host/`(PD-007 建立)
- PD-007 的交接區:site 物件實際被查詢的 service ID,四實例時要比對
- `IExplorerBrowser::SetRect`、`BrowseToObject`

## Scope

1. 矩形計算:把 client area 均分為四,固定 0.5／0.5。輸入為 client 寬高,輸出為四個矩形。
2. 建立四個 `explorer_host` 實例,各自 `BrowseToObject` 到不同路徑。
3. `WM_SIZE` 與 `WM_DPICHANGED` 時重算四個矩形並逐一 `SetRect`。
4. 關閉序列沿用 PD-007:destroy 全部四個 view 之後才 destroy 主視窗,live view 計數 self-check 斷言為 0。
5. 一個可執行的 self-check 目標,驗證矩形計算在退化尺寸(寬或高為 0、1、奇數)下不產生零或負值的矩形,且四個矩形不重疊、聯集等於 client area。

## Non-goals

- 不做可拖曳分隔線。
- 不做單一／四宮格切換(歸 PD-009)。
- 不做 active pane 指示(歸 PD-009)。
- 不做位置持久化(歸 PD-010)。
- 不做驗收協定的記錄與 Go/No-Go(歸 PD-011)。
- 不做五種版型;不為 PD-005 預先設計抽象。
- 不做視覺打磨。

## Acceptance

1. 四宮格下四個 pane 各自可獨立導覽,互不影響。
2. 四個 pane 的原生圖示與縮圖都正常顯示。
3. 調整視窗大小時四個 view 同步正確重新配置,無殘影、無錯位、無閃爍到不可用的程度。
4. 跨不同 DPI 螢幕拖曳時四個 view 都正確重新配置。
5. 關閉時四個 view 全部 `Destroy`,live view 計數 self-check 通過,process 正常結束。
6. 矩形計算的退化尺寸 self-check 通過。
7. `rg -n "AddRef|->Release\(\)" src` 無命中。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
.\build\PaneDock.exe
# 手動:四個 pane 分別導覽、調整大小、跨 DPI 拖曳、關閉
```

```powershell
rg -n "AddRef|->Release\(\)" src
rg -n "windows\.h|HWND|IUnknown" src/core
git diff --check
```

## Handoff requirements

- 四實例並存時的初次啟動耗時、記憶體概略值(正式量測歸 PD-003／PD-011)。
- 四實例的 site 查詢是否與 PD-007 單實例一致;若不一致,列出差異。
- 任何只在多實例下才出現的行為異常——這是本 ticket 最有價值的產出。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-20 實作交接

- 實作：`src/app_shell/main.cpp` 現在持有四個獨立的 `ExplorerHost`，初始位置為 `C:\`、`C:\Windows`、`C:\Users`、`C:\Program Files`；`WM_CREATE`、`WM_SIZE` 與 `WM_DPICHANGED` 都會把四個 view 配置到四宮格矩形。新增 `src/app_shell/quadrant_layout.h` 與 `tests/unit/quadrant_layout_check.cpp`，並在 `CMakeLists.txt` 新增 `panedock_quadrant_layout_check` target。
- 關閉：沿用 PD-007 的 `ExplorerHost::destroy()` 契約，逐一 destroy 四個 view 後 assert `live_view_count() == 0`，才 `DestroyWindow`；初始化第 N 個 view 失敗時也會先清掉前面已成功初始化的 view。
- 工具鏈：LLVM-MinGW `clang version 22.1.8`、target `x86_64-w64-windows-gnu`、Ninja `1.13.2`。未使用 MSVC。
- 自動證據：指定 Release configure/build 通過；`ctest --test-dir build --output-on-failure` 為 `1/1 passed`；`panedock_quadrant_layout_check.exe` 為 `PASSED`；既有 `panedock_explorer_host_lifetime_check.exe` 為 `PASSED`；`rg -n "windows\.h|HWND|IUnknown" src/core` 無命中；`git diff --check` 通過。
- 矩形 self-check 覆蓋 `0x0`、`0x5`、`1x0`、`1x1`、`1x7`、奇數尺寸與一般尺寸；驗證所有矩形非反轉、位於 client 內、無正面積重疊，且面積聯集等於 client area。對寬或高為 0 的 client，整數 `RECT` 不可能同時產生四個正面積矩形並精確聯集空 area；self-check 依 Win32 可達成的語義接受非反轉空矩形。這是票據文字本身的數學矛盾，未以超出 client 的矩形規避。
- 四實例初次啟動耗時與記憶體：未量測。此執行環境沒有可供視覺確認的互動桌面；`Start-Process build\PaneDock.exe` 後等待 3 秒仍在執行，已停止該測試程序，沒有把它冒充成成功啟動或拿來推算數值。
- site 查詢：四個 instance 都沿用 PD-007 同一個 `Site` 類別、`IServiceProvider`／`IExplorerBrowserEvents` 實作與 `IUnknown_SetSite`／`Advise` 掛接方式；未能在本環境擷取四實例的實際 `QueryService` GUID，因此 runtime 比對未驗證，沒有宣稱與 PD-007 的觀測結果一致。
- 多實例異常：沒有可據以判定的桌面 runtime 觀測；上述無互動桌面下程序未於 3 秒內返回，不視為產品異常證據。
- Acceptance：AC1（獨立導覽）、AC2（原生圖示／縮圖）、AC3（resize）、AC4（跨 DPI）、AC5 的實際視窗關閉結果均未完成人工視覺／互動驗證；程式碼路徑與自動 self-check 已覆蓋可測部分。AC6 self-check 通過，並受上述零軸數學限制。AC7 的 literal grep 不通過：`rg -n "AddRef|->Release\(\)" src` 唯一命中 `src/explorer_host/explorer_host.cpp:59` 的 `Site::AddRef()`，這是 COM `IUnknown` 實作本身，不是 raw interface pointer 的參照計數呼叫；未用巨集或拆字規避。所有既有介面指標仍由 `Microsoft::WRL::ComPtr` 持有。

### 2026-08-20 人工驗證補充（於本機真實互動桌面，非 codex 執行環境）

- `Start-Process build\PaneDock.exe` 後視窗立即回應（`Responding: True`），與 codex 執行環境中觀察到的「等待 3 秒仍未返回」不同——該現象一如 PD-007，是 codex 執行環境本身無互動桌面所致，不是程式碼缺陷。
- 截圖確認四宮格四個獨立 view，各自導覽到不同初始路徑且互不影響：左上 `C:\`、右上 `C:\Windows`、左下 `C:\Users`、右下 `C:\Program Files`；四塊均為原生 Details view，圖示、修改日期正確顯示，四矩形無重疊、無殘影（截圖檔未入 repo）。
- `MoveWindow` 縮小為 700×500 後重新截圖：四個 view 同步正確重新配置，無殘影、無錯位、無可見閃爍。
- `CloseMainWindow()` 後 `WaitForExit` 在約 55ms 內回報 process 已結束，無崩潰、無殘留 PaneDock.exe。
- AC1／AC2／AC3／AC5：通過，證據如上。AC4（跨 DPI 多螢幕）：本機為單一顯示器，與 PD-007 相同理由，維持未決；不影響單螢幕場景的 Go 判定。
- 四實例初次啟動耗時、記憶體、runtime `QueryService` GUID 比對：本輪仍未量測／擷取，維持 codex 交接區所述狀態；如需要，留給 PD-011 驗收協定或另開量測 ticket。
