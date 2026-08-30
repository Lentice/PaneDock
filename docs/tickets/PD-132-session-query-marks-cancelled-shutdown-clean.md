# PD-132 — 被取消的 Windows shutdown 會留下錯誤 clean marker

Phase 7 · app_shell shutdown correctness · Depends on: PD-025, PD-032, PD-125

- Source: 2026-08-30 close/startup audit loop；Microsoft Win32 `WM_QUERYENDSESSION`／`WM_ENDSESSION` contract。
- Priority: HIGH——shutdown 被取消後若 PaneDock 再被 force kill／crash，下一次啟動不會顯示不乾淨關閉提示；真正事故被誤判為乾淨。

## Goal

只有 Windows 確認 session 確實結束（`WM_ENDSESSION(TRUE)`）或正常 `WM_CLOSE` 時才寫 `clean_shutdown=true`。`WM_QUERYENDSESSION` 必須快速同意詢問，不得提前改變 durable marker。

## 已確認的根因與 caller trace

`src/app_shell/main.cpp` 的 `WM_QUERYENDSESSION` 無條件執行 `save_now(*state, true, true)` 再回傳 `TRUE`；`WM_ENDSESSION(FALSE)` 什麼都不做。具體路徑：

1. PaneDock 啟動時 `save_now(state)` 寫 `clean_shutdown=false`。
2. Windows 發出 `WM_QUERYENDSESSION`；PaneDock 提前寫成 `true`。
3. 另一程式阻止登出，PaneDock 收到 `WM_ENDSESSION(FALSE)` 並繼續執行。
4. 使用者未再變更任何狀態，之後 force kill／crash；檔案仍為 `true`，下一次啟動不提示。

`save_now(..., true)` 的其他 callers 是正常 `begin_shutdown` 與 `WM_DESTROY` dirty fallback；沒有 guard 能修正第 3 步。

## 覆寫 PD-032

PD-032 決策 1/2 曾明確接受「shutdown 取消後少抓到一次意外」，並假設 query 階段 atomic write 固定為毫秒級。本票以兩項新證據覆寫：使用者本次要求涵蓋 shutdown cancellation、force kill、disk slow／anti-virus block；且 Win32 文件建議在 `WM_ENDSESSION` 執行 normal close sequence，而 query 階段只是決定是否同意終止。PD-032 ticket 歷史文件不修改。

## Binding constraints

`docs/design-spec.md` §9.4：

> 1. 擷取現行狀態並原子寫入 session document
> 2. destroy 全部 live `IExplorerBrowser`
> 3. destroy pane HWND
> 4. destroy 主視窗
> 5. 退出訊息迴圈
> 6. `CoUninitialize`

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks.

Microsoft Win32 documentation：`WM_QUERYENDSESSION` 收集是否可終止的回覆；處理完 query 後系統送 `WM_ENDSESSION`。server application 應在 query 回傳 TRUE，於 `WM_ENDSESSION` 執行 normal close sequence。

## Scope

1. `WM_QUERYENDSESSION` 只回傳 `TRUE`；移除 session write、timer cancellation與 placement capture。
2. `WM_ENDSESSION(TRUE)` 維持既有 `begin_shutdown`，由共享路徑原子寫 `clean_shutdown=true` 後依 §9.4 teardown。
3. `WM_ENDSESSION(FALSE)` 保持 app 與 `clean_shutdown=false` 不變。

## Non-goals

- 不攔阻 Windows shutdown、不新增 BlockReason UI、thread、timer、timeout 或 schema。
- 不承諾 `EWX_FORCE`／OS 強制終止可執行 cleanup；這些路徑沒有訊息，應保留 false marker 並在下次啟動提示。
- 不修改 PD-032 歷史 ticket。

## Acceptance / Agent checks

1. query branch 不呼叫 `save_now`；confirmed end-session 仍經 `begin_shutdown`。
2. 模擬 `WM_QUERYENDSESSION` → `WM_ENDSESSION(FALSE)` 後 durable marker 保持 false；再 force kill、重啟應顯示既有 unclean warning（需真實桌面時如實標記）。
3. `cmake --build build`、CTest、`git diff --check` 通過。

```powershell
rg -n -A18 "case WM_QUERYENDSESSION" src/app_shell/main.cpp
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

## 交接區

<!-- 實作 agent append-only -->

### 2026-08-30 — implemented

- `WM_QUERYENDSESSION` 現在只回傳 `TRUE`，不再取消 timer、capture placement 或寫 `clean_shutdown=true`；`WM_ENDSESSION(TRUE)` 仍透過 `begin_shutdown` 完成 save → explorer teardown → window/message-loop teardown。
- 刪除 query 階段同步磁碟 I/O，同時修正 shutdown cancellation 的 false-clean marker；沒有新增 helper、thread、timer、schema 或 UI。
- Microsoft Win32 文件已透過 Context7 的 Microsoft Learn index 查核：query 收集是否可結束，normal close sequence 應在 end-session 執行。
- `cmake --build build` PASS；CTest 6/6 PASS（含 launch smoke）；source check 與 `git diff --check` PASS。
- 未在此工作階段真正登出／取消登出或 force kill，以免破壞使用者 session；runtime cancellation warning 仍待真實桌面驗證，未宣稱 PASS。
