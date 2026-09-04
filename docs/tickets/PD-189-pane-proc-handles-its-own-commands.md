# PD-189 — pane 子控制項的命令與通知改由 pane proc 就地處理，`decode_pane_control` 收斂

Phase 7 · architecture · Depends on: PD-186, PD-187

- Source: 同 PD-187（2026-09-04 使用者「為了權責分割更乾淨」的重開決定）。PD-187 讓 pane 有自己的視窗但只做轉發，PD-188 收回繪製，本票收回**訊息處理**——三張合起來才是完整的權責分割。
- Priority: LOW-MEDIUM——機械性改動，風險集中在命令分派的覆蓋率（8 種控制項 × 4 個 pane）。**若前兩張做完後你覺得已經夠乾淨，本票可以獨立捨棄**，不影響 PD-187／PD-188 的成果。

## Outcome

pane 子控制項發出的 `WM_COMMAND`／`WM_DRAWITEM`／`WM_CTLCOLOR*`／`WM_MEASUREITEM` 不再轉發回主視窗，改由該 pane 自己的 proc 就地處理。主視窗的 `window_proc` 不再有任何 pane 分支。

`decode_pane_control` 的「解出 pane index」那一半失去用途（pane proc 天生就知道自己是哪個 pane），收斂為只解控制項種類。

行為與視覺零變更。

## 已確認的現況（2026-09-04 工作樹）

- PD-187 之後 pane proc 對 `WM_COMMAND`／`WM_DRAWITEM`／`WM_NOTIFY`／`WM_CTLCOLOR*`／`WM_MEASUREITEM` 是**原樣轉發**給主視窗，既有 `decode_pane_control` 分派路徑一行未改。本票就是把那個轉發拆掉。
- 主視窗端的 pane 命令入口：`handle_pane_command`（`main.cpp:4739`）、`draw_pane_control`（`:4782,4789`）。全域（非 pane）命令走 `handle_global_command`（`:5938,5947`）與 `draw_global_control`（`:4827`），**不在本票範圍**。
- **既有的、可直接照抄的模式**：`tab_strip_proc`（`main.cpp:3983-3985`）是一個自由函式，透過 subclass 的 `reference_data` 取得 `AppState*`、透過 subclass ID 取得 `pane_index`，並在函式開頭做兩道既有防線：
  ```cpp
  if ((state->closing_ || state->shutdown_deferred) && message != WM_PAINT &&
      message != WM_ERASEBKGND && message != WM_NCDESTROY) return 0;
  if (defer_shell_reentry_mouse_message(window, *state, message, wparam, lparam))
      return 0;
  ```
  **pane proc 必須沿用同樣的兩道防線**（關閉中／Shell 重入期間的訊息處理），否則會重新打開 PD-172／PD-173 修掉的洞。
- `address_edit_proc`（`:3429`）、`hover_tracking_proc`（`:4103`）同樣掛在控制項自身，本票**不動**它們——它們處理的是控制項自己的輸入與 hover，不是父視窗收到的通知。
- PD-186 之後，pane 局部的狀態取用是 `state.panes[i].pane_state()`；pane proc 內應直接用這個，不要再回頭 `active_group(state).panes[i]`。
- `encode_pane_control`（`src/app_shell/pane_control_id.h`）仍必須存在——子控制項還是需要 ID（`WM_COMMAND` 的 `LOWORD(wparam)`、`WM_DRAWITEM` 的 `CtlID`）。改變的只是 ID 裡不再需要編入 pane index。

## Binding constraints — quoted, do not go looking for them

`docs/tickets.md` 2026-09-03 條目，模組契約 (1)（**本票不覆寫**）：

> (1) **單向依賴**——協調層 → `Pane`，`Pane` 不得持有 `AppState*`、回呼介面或 `std::function` 成員；一個只有單一實作的介面在本專案是已否決模式。

**本票如何遵守**：pane proc 是 `main.cpp`（協調層）的**自由函式**，不是 `Pane` 的成員；它比照 `tab_strip_proc` 從視窗資料取得 `AppState*`。`Pane` 這個型別本身仍然沒有任何 `AppState*`／回呼／`std::function` 成員。

`AGENTS.md`：

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`：

> Read the relevant spec section and trace every caller before touching shared code…patching only the path the ticket names leaves sibling callers broken.

`docs/design-spec.md` §9.1：

> | `app_shell` | WinMain、STA 初始化、訊息迴圈、主視窗、命令路由 | 資料模型計算、Shell 呼叫 |

`docs/tickets.md` Agent 交付規則：

> 預設每個 ticket 的實作範圍為半天至兩天;若超過,先拆 ticket。

## Files to read and trace first

- `src/app_shell/main.cpp:3983-4100`：`tab_srip_proc` 的完整形狀——**本票的 pane proc 照這個模式寫**，特別是開頭兩道防線。
- `src/app_shell/main.cpp:4739` 起：`handle_pane_command`。
- `src/app_shell/main.cpp:4782-4830`：`draw_pane_control`、`draw_global_control`。
- `src/app_shell/main.cpp:5155` 起：`window_proc` 的 `WM_COMMAND`／`WM_DRAWITEM`／`WM_CTLCOLOR*`／`WM_MEASUREITEM` case。
- `src/app_shell/main.cpp:5938-5950`：`handle_global_command`（界線：哪些命令**不**屬於 pane）。
- `src/app_shell/pane_control_id.h`：`encode_pane_control`／`decode_pane_control` 與 `tests/unit/pane_control_id_test.cpp`。
- `src/app_shell/pane.h`／`pane.cpp`：PD-187 的 pane proc、PD-185 的 `pane_state()`。
- `docs/tickets/PD-172`、`PD-173`、`PD-177`：關閉／重入閘門的既有保證，本票不得削弱。

## Scope

1. pane proc 停止轉發，改為就地處理下列訊息，全部照 `tab_strip_proc` 的模式（開頭兩道防線一字不少）：
   - `WM_COMMAND`：來自本 pane 的 6 顆按鈕與資料夾選單按鈕、`address_bar_` 的 `EN_*` 通知
   - `WM_DRAWITEM`／`WM_MEASUREITEM`：本 pane 的 owner-draw 按鈕與 `status_bar_`
   - `WM_CTLCOLOR*`：本 pane 的子控制項
2. 處理函式仍是協調層的既有自由函式（`handle_pane_command`、`draw_pane_control` 等），pane proc 只負責「識別是哪個控制項 → 呼叫既有函式」。**不得**把這些函式搬進 `Pane` 或改寫成 `Pane` 的成員（那會需要 `Pane` 取得協調層服務，違反契約 (1)）。
3. 主視窗 `window_proc` 移除全部 pane 分支；保留 `handle_global_command`／`draw_global_control` 路徑不動。移除後必須確認**沒有任何 pane 控制項的訊息落空**——覆蓋率檢查見 Acceptance Criteria 第 2 點。
4. `decode_pane_control` 收斂為只解控制項種類；`encode_pane_control` 相應簡化（ID 不再編入 pane index）。同步更新 `tests/unit/pane_control_id_test.cpp`——**更新測試時必須先確認舊斷言在改動前確實會過**，不得以放寬斷言換綠燈。
5. pane proc 內取用狀態一律走 `state->panes[pane_index].pane_state()`（PD-186），不得回頭走 `active_group`。
6. 順手把本票碰到的函式簽章從 `AppState&` 收窄，並回報前後計數。

## Non-goals

- **不**把命令處理邏輯搬進 `Pane` 型別（契約 (1)）。
- **不**動 `address_edit_proc`／`hover_tracking_proc`／`tab_strip_proc` 三個既有 subclass proc。
- **不**動全域命令（版型按鈕、New/Duplicate/Delete Group、側邊欄）的路徑。
- **不**動跨 pane 拖曳（契約 (3)）。
- **不**削弱 `closing_`／`shutdown_deferred`／`defer_shell_reentry_mouse_message` 三道既有防線。
- **不**改繪製（PD-188 已完成或未做，兩者皆與本票無關）。
- **不**改 `core`、session、realize 政策。
- **不**引入 UI 自動化。

## Acceptance Criteria

1. 主視窗 `window_proc` 內已無任何 pane 控制項分支。
2. **覆蓋率逐項確認**：8 種 `PaneControl` × 4 個 pane，共 32 條路徑逐一實機點擊確認可用（清單見實機檢查），並在交接區列表回報。
3. pane proc 開頭的兩道防線與 `tab_strip_proc` 一致。
4. `decode_pane_control` 不再回傳 pane index；`pane_control_id_test` 已更新且附「舊斷言改動前會過」的證據。
5. `AppState&` 計數較 PD-187 完成時下降，數字記於交接區。
6. `ctest --test-dir build --output-on-failure` 全綠，含 `panedock_launch_smoke`、`panedock_shell_reentry_gate`。
7. 行為與視覺零變更。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 主視窗不得再處理 pane 命令
Select-String -Path src/app_shell/main.cpp -Pattern 'handle_pane_command|draw_pane_control' -Context 3,0 |
  Select-String -Pattern 'window_proc'
```

必須無結果（兩個函式仍存在，但呼叫點不得在 `window_proc` 內）。

```powershell
# pane proc 必須帶著兩道防線
Select-String -Path src/app_shell/main.cpp -Pattern 'defer_shell_reentry_mouse_message'
```

必須同時出現在 `tab_strip_proc` 與新的 pane proc 內。

```powershell
# Pane 型別仍不得反向依賴協調層
Select-String -Path src/app_shell/pane.h -Pattern 'AppState|std::function|callback'
```

必須無結果。

```powershell
$p = Start-Process build\PaneDock.exe -PassThru; Start-Sleep 3
$p.CloseMainWindow() | Out-Null
if (-not $p.WaitForExit(5000)) { throw 'process survived graceful close' }
```

```powershell
git diff --check
```

## 交給使用者的實機檢查清單

**本票的風險是「某個控制項的訊息落空但沒人發現」，請逐項點過。**

1. **在四個 pane 各做一次**（4 pane 版型下）：上一頁、下一頁、上一層、Refresh、View（下拉選單選一項）、Pinned（選一個位置）、資料夾內容選單按鈕。共 28 次點擊。
2. 在四個 pane 各操作一次位址列：輸入路徑 Enter、輸入時觀察自動完成下拉、按 Esc 取消。
3. 在四個 pane 各確認一次 footer 狀態列文字正確（項目數／選取數／選取大小）。
4. 每個 pane 的按鈕 hover 一次，確認 hover 視覺仍在（PD-083／PD-058）。
5. tab 點擊、拖曳排序、跨 pane 拖曳各 3 次。
6. 全域功能抽查（確認沒被誤傷）：版型按鈕、New／Duplicate／Delete Group、側邊欄拖曳排序、側邊欄寬度拖曳。
7. 檔案操作進行中關閉、拖曳進行中關閉，各確認乾淨結束。
8. 工作管理員無殘留 `PaneDock.exe`。

## Handoff requirements

在 `## 交接區` 記錄：pane proc 處理的訊息清單、32 條控制項路徑的逐項確認結果、兩道防線的實作位置、`decode_pane_control` 收斂前後的簽章與測試更新證據、`AppState&` 計數前後值、`ctest` 全量結果、以及使用者實機檢查 8 項的逐項回報。

## 交接區

（實作者填寫）
