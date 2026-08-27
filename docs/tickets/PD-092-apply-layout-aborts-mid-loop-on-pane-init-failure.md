# PD-092 — `apply_layout` 在單一 pane 初始化失敗時整段退出,遺留未完成排版與不一致的 pane 狀態

## 來源

2026-08-27 三方稽核(Claude / Codex / OpenCode)。OpenCode 與 Claude 各自獨立指出同一段程式碼:`apply_layout` 的 pane 迴圈遇到單一 pane 初始化失敗就直接 `return`,不繼續處理其餘 pane。

## 背景與現況

`apply_layout`(`src/app_shell/main.cpp:1947-1966` 一帶)以迴圈依序處理每個要顯示的 pane,若某個 pane 的初始化(`ExplorerHost::initialize`)失敗:

```cpp
if (FAILED(hr)) return hr; // 概念示意,實際見上述行號
```

整個函式直接以失敗結束。這代表:

- 迴圈中**尚未處理**的後續 pane,不會執行 `SetWindowPos`/`ShowWindow`,維持在呼叫前的狀態(可能是不可見、或位置對應舊版型)。
- 函式結尾原本該做的收尾(例如更新 live-view 計數)被整段跳過。
- 呼叫端目前只是記錄一行失敗訊息就繼續執行,使用者會看到一個排版錯亂、部分 pane 消失或位置錯誤的視窗,沒有任何錯誤提示解釋發生了什麼。

## 為什麼這是真的問題

單一 pane 初始化失敗(例如目標路徑暫時無法存取、Shell 端資源不足)理論上是可以恢復的情境,不應該讓整個版型套用流程半途而廢並讓其餘完全健康的 pane 也連帶維持不正確的狀態。這也是一個「部分失敗被放大成全域不一致」的模式,與專案已有的「顯示既有錯誤面板」機制(用於單一 pane 導覽失敗時)不一致——導覽失敗有優雅降級,版型套用失敗卻沒有。

## Fix 方向

把 `return hr` 的提前退出,改成「記錄第一個失敗的 pane 與其 `HRESULT`,繼續處理迴圈中剩餘的 pane(該 pane 顯示既有的錯誤/空狀態面板,不嘗試假裝初始化成功),迴圈結束後,若之前有記錄到失敗,才回傳該失敗的 `HRESULT`」。呼叫端(目前只是記錄一行訊息)的行為不需要改變——它已經是「記錄後繼續執行」的容錯呼叫方,問題純粹出在 `apply_layout` 內部提前退出。

## 綁定限制(引用)

- `AGENTS.md`:「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」—— 只需要把迴圈內的 `return` 改成「記錄 + continue」,迴圈外補一次性回傳,不需要重寫整個函式結構。
- `AGENTS.md`:「Only the visible pane's active tab holds a live `IExplorerBrowser`」—— 修正後,初始化失敗的 pane 不應該被誤標記為 `realized`,以免與 PD-087 的「已 realize 才 destroy」邏輯衝突;確認失敗的 pane 的 `realized` 旗標維持 `false`。

## 檔案與範圍

- `src/app_shell/main.cpp`:
  - `apply_layout`(約 `:1947-1966` 一帶)
  - 呼叫 `apply_layout` 並記錄失敗訊息的呼叫端(確認呼叫端邏輯不需要跟著改動,只是回傳值語意變得更精確)

## Scope

1. 迴圈內的 `if (FAILED(hr)) return hr;` 改為記錄首個失敗(`HRESULT` + pane index)並 `continue`,不中斷迴圈。
2. 迴圈結束後,若記錄到失敗,回傳該 `HRESULT`;若沒有失敗,維持原本的成功回傳路徑(含既有的 live-view 計數更新等收尾動作)。
3. 確認失敗的 pane 在迴圈其餘部分被正確跳過需要「假設已初始化成功」的後續步驟(例如不對一個初始化失敗的 pane 呼叫需要有效 `IExplorerBrowser` 的方法),沿用既有的錯誤/空狀態面板顯示邏輯。

## Non-goals

- 不改變 `ExplorerHost::initialize` 本身的失敗語意或重試邏輯。
- 不新增使用者可見的錯誤提示 UI(若目前完全沒有任何面板可顯示失敗狀態,只需確保該 pane 不呈現錯誤的舊資料/崩潰,不需要為本票新增全新的錯誤 UI 元件——除非既有程式碼已經有現成的「空/錯誤面板」可以直接沿用)。
- 不處理 PD-086/PD-087/PD-088/PD-090/PD-091 涵蓋的其他問題,雖然這些票都涉及 `apply_layout`/相鄰函式,本票只處理「迴圈中途失敗提前退出」這一件事。

## Acceptance Criteria

1. 人為製造一個 pane 初始化失敗的情境(例如以測試/除錯手段讓某個 pane 的 `initialize` 回傳失敗),確認迴圈中其餘 pane 仍正確完成排版與顯示,不因為一個 pane 失敗而全部維持舊狀態。
2. 失敗的 pane 不會被誤判為 `realized`,不會在後續操作中被當成正常存活的 view 處理。
3. 正常情況(所有 pane 初始化成功)的行為與現在完全一致,無 regression。
4. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 交接區

### 實作

- `src/app_shell/main.cpp` 的 `apply_layout` 現在記錄第一個失敗的 pane index
  與 `HRESULT`；初始化失敗時維持 `state.realized[index] == false`，跳過該
  `ExplorerHost` 後續需要有效 live view 的操作，繼續排版其餘 pane。
- 迴圈結束後仍會執行 `write_live_view_count()`，若有失敗則回傳第一個失敗的
  `HRESULT`，否則維持 `S_OK`。所有 `apply_layout` 呼叫端未變更。
- 沒有新增 app_shell/COM fake 測試；這段流程依賴 Win32 HWND 與真實
  `IExplorerBrowser`，不在專案的 `core` 自動測試 seam。

### Agent Checks

以下命令均成功：

```text
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- Configure：成功。
- Release build：成功，完成 `PaneDock.exe` link。
- CTest：`5/5` 通過（`panedock_diagnostic_flag`、`panedock_tab_overflow`、
  `panedock_core_model`、`panedock_core_layout`、`panedock_core_session`）。
- `git diff --check`：成功。

### Acceptance Criteria 驗證狀態

1. **部分由程式碼驗證，實機結果留給使用者**：已確認初始化失敗會記錄後
   `continue`，後續 pane 仍會進入排版迴圈；未以除錯注入或真實 Shell 故障在
   本次 Agent session 啟動 UI 重現。
2. **已由程式碼驗證**：失敗路徑在 `state.realized[index] = true` 之前離開，且
   不會執行該 host 的後續操作；未在實機執行後續互動確認。
3. **部分由建置與程式碼驗證，實機結果留給使用者**：正常成功路徑僅新增失敗
   記錄與結尾回傳判斷，並由 Agent Checks 確認無編譯／既有測試 regression；
   未在本次 session 操作真實多 pane UI。
4. **已驗證**：configure、`cmake --build build`、`ctest --test-dir build
   --output-on-failure` 均成功。
