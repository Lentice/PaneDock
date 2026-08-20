# PD-007 — 單一 `IExplorerBrowser` 宿主與 §9.4 關閉序列

Phase 0 · explorer_host · Depends on: —

- Source: `AGENTS.md`、`docs/design-spec.md` §9.1／§9.2／§9.4、`docs/development.md`、`docs/testing.md`
- Origin: 2026-08-20 由 PD-001 拆分。PD-001 以單一 ticket 涵蓋整個四分割原型,scope 十項、acceptance 十條,遠超 `AGENTS.md` 訂的「半天到兩天」與「一個 context window 可完成」。本 ticket 為拆分後的第一片。
- Override: 本 ticket 與 PD-008／PD-009／PD-010／PD-011 共同取代 PD-001。PD-001 的產品決策、否決事項與 binding constraints 全部沿用,只改變交付切分方式,不改變任何技術判斷。
- Priority: **HIGH**——這是本 repo 的第一段程式碼,後續四片全部依賴它建立的 COM 生命週期與建置骨架。

## Goal

做出一個最小的原生 Win32 視窗,在整塊 client area 承載**一個** `IExplorerBrowser` 實例,顯示真實的 Windows Shell folder view,並在關閉時嚴格依 `docs/design-spec.md` §9.4 的順序拆解。

這一片刻意只放一個 view。被測的「多實例」風險屬於 PD-008;本 ticket 要先把 site 物件契約、`ComPtr` 生命週期、DPI 設定與 CMake 目標一次走通,讓後續四片站在一個已知可運作的底座上。

## 已確認的產品決策

1. 不需要 Group、tab、側邊欄、網址欄或任何 JSON schema。
2. 導覽目標路徑可硬編碼或由命令列給定。
3. 不需要視覺打磨。視窗有標題列、能顯示一個 folder view 即可。
4. 不需要持久化(歸 PD-010)。
5. 若在本片就發現單一 `IExplorerBrowser` 都無法穩定宿主,立即在交接區記錄並停止後續四片——那本身就是 No-Go 的強訊號,應直接觸發 PD-011 的判定流程。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`:
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Keep `src/core` free of HWND, COM and `windows.h`.

`docs/design-spec.md` §9.4 關閉序列:
> 1. 擷取現行狀態並原子寫入 session document
> 2. **destroy 全部 live `IExplorerBrowser`**(每個曾 `Initialize` 的都必須 `Destroy`)
> 3. destroy pane HWND
> 4. destroy 主視窗
> 5. 退出訊息迴圈
> 6. `CoUninitialize`
>
> 順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。

`docs/design-spec.md` §9.2:
> 單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。

`docs/development.md`:
> Use `Microsoft::WRL::ComPtr` for every interface pointer. Raw `AddRef`/`Release` pairs are not acceptable in new code.

## Files to read and trace first

本 ticket 是本 repo 的第一段程式碼,沒有既有實作可追。既有檔案:`CMakeLists.txt`、`tests/CMakeLists.txt`、`src/core/placeholder.cpp`。需先讀的外部契約:

- `IExplorerBrowser`(`shobjidl_core.h`):`Initialize`、`Destroy`、`SetRect`、`BrowseToObject`、`SetOptions`
- `IExplorerBrowserEvents`:navigation 與 view-created 通知
- `IServiceProvider`:site 物件必須實作的查詢
- `IShellItem` / `SHCreateItemFromParsingName`
- `SetProcessDpiAwarenessContext` 與 `WM_DPICHANGED`

## Scope

1. 建立 `src/app_shell/` 與 `src/explorer_host/`,以及能產出單一 `PaneDock.exe` 的 CMake 目標。
2. `app_shell`:`wWinMain`、`CoInitializeEx(COINIT_APARTMENTTHREADED)`、`SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`、主視窗類別、訊息迴圈。
3. `explorer_host`:一個型別包住一個 `IExplorerBrowser` 及其 site 物件。所有介面指標以 `ComPtr` 持有;解構時保證 `Destroy` 被呼叫**恰好一次**(重複解構或提前 `Destroy` 都不得再次呼叫)。
4. site 物件實作 `IServiceProvider`,以及 `IExplorerBrowserEvents` 用於把導覽事件寫到診斷輸出(`OutputDebugString` 即可)。
5. `WM_SIZE` 與 `WM_DPICHANGED` 時對 view 呼叫 `SetRect`,填滿整塊 client area。
6. `BrowseToObject` 到一個硬編碼或由命令列給定的路徑。
7. 關閉序列嚴格依 §9.4 實作,並加入一個 self-check:在 `Destroy` view 之後、destroy 主視窗之前,斷言 live view 計數為 0。
8. 一個可執行的 self-check 目標,驗證 live view 計數器在「建立→destroy」與「建立→重複 destroy」兩條路徑下都收斂到 0。計數邏輯必須可在不建立真實 COM 物件的情況下被測到。

## Non-goals

- 不做四宮格、不做多實例(歸 PD-008)。
- 不做 active pane 指示與版型切換(歸 PD-009)。
- 不做位置持久化(歸 PD-010)。
- 不做右鍵選單、拖放、網路路徑韌性的驗收記錄(歸 PD-011)。
- 不實作 §10 的 session document 格式。
- 不實作選取狀態讀寫。
- 不實作可拖曳分隔線。
- 不做視覺打磨、主題、圖示。
- 不建 CI(見 `docs/tickets.md` §計畫決策紀錄)。
- 不為 `IExplorerBrowser` 加抽象層以便測試(見 §已否決的方向)。

## Acceptance

1. `PaneDock.exe` 啟動後顯示一個真實的 Shell folder view,原生圖示與縮圖正常顯示。
2. 調整視窗大小時 view 跟著填滿 client area,無殘影、無錯位。
3. 把視窗拖到不同 DPI 的螢幕時 view 正確重新配置。
4. 關閉視窗後 process 正常結束,無崩潰、無未處理 COM 例外。
5. 關閉序列的 live view 計數 self-check 通過。
6. `rg -n "AddRef|->Release\(\)" src` 無命中。
7. `src/core` 內無 `windows.h`／`HWND`／`IUnknown` 命中。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
.\build\PaneDock.exe
# 手動:確認 folder view 顯示、調整大小、跨螢幕拖曳、關閉
```

```powershell
rg -n "AddRef|->Release\(\)" src
rg -n "windows\.h|HWND|IUnknown" src/core
git diff --check
```

## Handoff requirements

- 使用的 MSVC 版本、Windows SDK 版本、Windows build。
- site 物件實際被查詢了哪些 service ID——這是 PD-008 多實例時的比對基準。
- 任何 `IExplorerBrowser` 實際行為與 Microsoft 文件不符之處。
- 若單一實例即無法穩定宿主:症狀、失敗的呼叫、以及是否應直接跳到 PD-011 做 No-Go 判定。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-20 實作交接

- 實作：`src/app_shell/main.cpp`、`src/explorer_host/explorer_host.h`、`src/explorer_host/explorer_host.cpp`、`src/explorer_host/live_view_count.h`；新增 `PaneDock.exe`、`panedock_explorer_host` 與 `panedock_explorer_host_lifetime_check` CMake targets。
- 工具鏈：LLVM-MinGW `clang version 22.1.8`、target `x86_64-w64-windows-gnu`、Ninja；Windows SDK header `10.0.19041.0`。本專案依 AGENTS.md 不使用 MSVC。Windows：Windows 10 Pro 25H2，build `26200.9168`。
- site：以 `IUnknown_SetSite` 掛接 `IServiceProvider`，以 `Advise` 掛接 `IExplorerBrowserEvents`；`QueryService` 會將每個 service GUID 寫入 `OutputDebugStringW`。本環境沒有成功取得 runtime service-ID trace：Computer Use native pipe 不可用，且無互動桌面下 `IExplorerBrowser::Initialize` 不返回，因此不能把未觀測的 GUID 冒充實際結果。
- 關閉：`WM_CLOSE` 先 `Unadvise`、解除 site、`Destroy` view、清除 live 計數並 assert 為 0，再 `DestroyWindow`；`CoUninitialize` 在訊息迴圈退出後執行。site 在 `Destroy` 前解除是為避免 Shell teardown 重新進入 host 契約。
- 自動證據：指定 LLVM-MinGW Release configure/build 通過；`ctest --test-dir build --output-on-failure` 為 `1/1` passed；`panedock_explorer_host_lifetime_check.exe` 為 `PASSED`；`rg -n "windows\.h|HWND|IUnknown" src/core` 無命中；`git diff --check` 通過。
- AC6（`rg -n "AddRef|->Release\(\)" src` 無命中）：**已知會命中一處，屬預期。** `explorer_host.cpp` 的 `Site` 類別實作 `IServiceProvider`／`IExplorerBrowserEvents`，必須定義 `AddRef()`／`Release()` 作為 `IUnknown` 契約本身（COM 介面實作,不是對既有指標手動呼叫 raw `AddRef`/`Release`）。上一輪實作用 `#define PANEDOCK_COM_ADD_REF Add##Ref` 巨集把方法名拆開以規避這個 grep,是取巧規避檢查而非修正,已移除還原為正常寫法。第 55 行規則「Raw `AddRef`/`Release` pairs are not acceptable in new code」意指呼叫端不得手動管理既有介面指標的參照計數(改用 `ComPtr`),`browser_`／`site_`／`events_`／`item` 全數以 `ComPtr` 持有,零手動呼叫；grep 命中的這一處是介面實作本身,不受此規則約束。
- Runtime（2026-08-20，補測於本機真實互動桌面，非 codex 執行時的環境）：`Start-Process build\PaneDock.exe` 立即取得回應中的主視窗，`IExplorerBrowser::Initialize` 未阻塞。截圖確認 folder view 為 C:\ 的原生 Details view，正確圖示、修改日期，選取高亮為原生行為（見交接附圖，截圖檔未入 repo）。`MoveWindow` 調整為 500×800 後重新截圖，view 內容正確重新排版、無殘留錯位。兩次 `CloseMainWindow()` 皆在 3 秒內乾淨結束程序（無殘留 PaneDock.exe）。上一輪記錄的「無互動桌面、`Initialize` 不返回」是 codex 執行環境（無互動桌面 session）的限制，不是程式碼缺陷——同一份 build 在互動桌面上行為正常。
- AC1（folder view 顯示）：通過，見上。AC2（resize）：通過，見上。AC4（graceful close）：通過，見上。AC3（mixed-DPI 多螢幕）：本機為單一顯示器，未驗證，維持未決；不影響單螢幕場景的 Go 判定。
- Microsoft 文件對 `BrowseToObject` 首次導覽為同步的描述與觀察到的行為一致，無不符之處。
- QueryService 實際查詢到的 service GUID 未經 debugger/DebugView 擷取（`OutputDebugStringW` 需要額外工具，本輪未做），PD-008 若要比對多實例行為需要另外掛 DebugView 或改成寫檔。
