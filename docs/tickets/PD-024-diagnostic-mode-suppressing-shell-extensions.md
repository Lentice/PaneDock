# PD-024 — 診斷模式:抑制第三方 shell extension(NFR-006)

Phase 5 · app_shell · Depends on: PD-015

- Source: `AGENTS.md`、`docs/design-spec.md` §NFR-006 / §11 / §14 / §3.2、`docs/development.md` §Product boundary、`docs/tickets.md` §已否決的方向、`docs/roadmap.md` Phase 5 第一條
- Origin: 2026-08-24,`docs/roadmap.md` Phase 5「Diagnostic mode suppressing third-party shell extensions (NFR-006)」。`docs/tickets.md` §候選 從專案建立起就掛著「診斷模式:抑制第三方 shell extension — Phase 5 前必須開」,本 ticket 即為該候選的開票。
- Priority: HIGH——NFR-006 明文寫「需要一個可抑制第三方 shell extension 的診斷模式」,它是 Phase 5 五個條列之一,也是 PD-025 崩潰復原能提供給使用者的唯一自救動作。

## Goal

提供一個**同一個 process 內、啟動時決定、使用者可觀察**的診斷模式:以命令列旗標啟動時,PaneDock 阻止第三方(非 Microsoft 簽章)DLL 載入本 process,使 in-process 的第三方 shell extension 無法載入,藉此判定「這個 hang / crash 是 PaneDock 的錯,還是某個 extension 的錯」。

這是 NFR-006 的直接交付:PaneDock 保證自身程式碼正確,但不保證第三方 extension 不會弄垮 process;沒有診斷模式就沒有辦法歸因,而不能歸因的崩潰報告等於不能修。

## 已確認的產品決策

1. **診斷模式是同一個 process 內的載入抑制,不是 process 隔離。** `docs/tickets.md` §已否決的方向最後一列(「C# UI 殼層 ＋ C++ shell host DLL 混合」)寫著:唯一值得重開多 process 的情境是「改為**獨立 process** 隔離第三方 extension 崩潰,且需先有實際崩潰紀錄」;`docs/design-spec.md` §3.2 也把「以獨立 process 隔離第三方 shell extension」列為明確不在範圍,§14 把它列為未來可能重啟的方向。**本 ticket 不重開那個方向,也不需要覆寫它**:NFR-006 要的是一個「可抑制」的診斷模式(讓 extension 不載入),不是「隔離」(讓 extension 在別的 process 崩潰而不影響我們)。兩者的目標、架構與成本都不同。實作者若發現自己開始寫第二個 process、IPC、或 surrogate host,就是走錯方向了,停下來。
2. **抑制手段是 `SetProcessMitigationPolicy(ProcessSignaturePolicy, ...)` 的 `MicrosoftSignedOnly`。** 它是文件化的 Win32 per-process API、無需管理員權限、無需寫任何 registry、只影響本 process 的後續 DLL 載入,恰好對應「in-process 的外部程式碼」這個 NFR-006 的定義。設定後**不可逆**,因此必須在 `wWinMain` 的最早期、任何 Shell view 建立之前呼叫。
3. **不動 registry,不動任何 machine-global 設定。** `HKLM`/`HKCU` 的 approved-shell-extensions 或 policy 機制都是全機器生效、會影響 Windows 檔案總管本身、且會在 PaneDock 結束後留下痕跡,違反 `AGENTS.md`「Do not ... modify anything outside this repository without explicit approval」與 NFR-007「無管理員權限」。任何以 registry 實作本 ticket 的做法一律退回。
4. **診斷模式只能由啟動時的命令列旗標進入,不能在執行期切換。** 決策 2 的 policy 不可逆,做成執行期選單只會變成「按下去之後必須重開」的假開關。旗標名稱固定為 `--diagnostic`(同時接受 `/diagnostic`,因為 Windows 使用者習慣斜線)。**合理的預設值,不是規格明文要求;若使用者實際使用後想要其他名稱或想要 UI 入口再調整。**
5. **診斷模式必須在 UI 上看得見。** 主視窗標題在診斷模式下為 `PaneDock — Diagnostic Mode`(一般模式維持 `PaneDock`)。理由:診斷模式下右鍵選單會少掉使用者熟悉的項目、雲端同步圖示會消失,沒有明顯標示會被當成 bug 回報。英文,見 `AGENTS.md` 的 UI 語言規則。
6. **policy 設定失敗不是致命錯誤。** 舊 build、受管理的環境或已被其他 mitigation 覆寫時 `SetProcessMitigationPolicy` 可能失敗。失敗時記錄一個診斷事件(`OutputDebugStringW`,英文)並**繼續以一般模式執行**,但標題列不得顯示 `Diagnostic Mode`——標示一個沒有生效的模式比沒有模式更糟。
7. **診斷模式不改變任何持久化行為。** 不寫入 session document、不新增設定檔欄位、不記住「上次是診斷模式」。它是一次性的除錯執行方式。
8. **不做 extension 清單、不做「只停用某一個 extension」。** 那需要列舉並判定每個 in-proc handler 的來源,是一個獨立產品功能;NFR-006 只要求「可抑制」。YAGNI。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §NFR-006:
> 第三方 shell extension 是 in-process 的外部程式碼。PaneDock 保證自身程式碼正確,不保證寫得差的 extension 不會 hang 或 crash 本 process。因此需要一個可抑制第三方 shell extension 的診斷模式,且崩潰復原路徑不是選配。

`docs/design-spec.md` §3.2(明確不在範圍):
> - 以獨立 process 隔離第三方 shell extension(見 §14)

`docs/design-spec.md` §14:
> 以獨立 process 隔離第三方 shell extension。目前明確不在範圍(§3.2),但這是唯一能真正解決 NFR-006 穩定性上界的架構,也是唯一值得重新考慮多 process 架構的情境。重啟需先有實際的 extension 崩潰紀錄。

`docs/design-spec.md` §NFR-007:
> 無網路、無遙測、無第三方 runtime、無服務、無 driver、無管理員權限。

`docs/design-spec.md` §11:
> - COM 失敗:記錄診斷事件,不得靜默忽略,不得使整個視窗不可用

`AGENTS.md`:
> **App UI text must be English.** No Chinese strings ship in the binary.

> Code, identifiers, test names and diagnostic event names are English.

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> Reach for the standard library and Win32 before adding a dependency.

> **Keep `src/core` free of HWND, COM and `windows.h`.**

> Event-driven idle path only. No busy loops, no polling timers.

`docs/development.md` §Product boundary 的優先序:
> 2. Stability of the Shell host — a crash costs the user their arrangement

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `wWinMain`(檔尾,約 1320 行起)——啟動序列目前是 `OleInitialize` → `SetProcessDpiAwarenessContext` → `register_window_class` → `session_directory()` → `read_session` → `CreateWindowExW`。**診斷模式的 policy 必須插在 `OleInitialize` 之前**:`OleInitialize` 本身就會拉進 COM/OLE 的載入路徑,而 policy 只影響設定之後的載入。
- `src/app_shell/main.cpp` 的 `CreateWindowExW(... L"PaneDock" ...)`——標題字串目前寫死在這一行,決策 5 要改的就是它。
- `src/app_shell/main.cpp` 的 `wWinMain` 簽章——第三個參數 `PWSTR` 目前被忽略。命令列可用 `GetCommandLineW()` ＋ `CommandLineToArgvW()`(`shell32`,本專案已連結)取得;`CommandLineToArgvW` 的回傳值必須以 `LocalFree` 釋放。
- `src/explorer_host/explorer_host.cpp` 的 `initialize()`——確認沒有任何在 `wWinMain` 之前執行的靜態初始化會提前載入第三方 DLL。
- `docs/tickets.md` §已否決的方向最後一列——決策 1 引用的原文,確認本 ticket 的範圍沒有踩到它。
- `docs/tickets/PD-022-unresolvable-location-error-and-retry.md` 的 交接區——診斷模式下導覽失敗率會上升,錯誤面板是既有的呈現路徑,本 ticket 不改它。

## Scope

1. 在 `src/app_shell/main.cpp` 加一個 file-local helper,解析命令列並回傳是否要求診斷模式:接受 `--diagnostic` 與 `/diagnostic`,大小寫不敏感,其他參數一律忽略(不報錯、不顯示 usage——這不是 CLI 工具)。
2. 在 `wWinMain` 最開頭(`OleInitialize` 之前)、且僅在旗標存在時,呼叫:
   ```cpp
   PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY policy{};
   policy.MicrosoftSignedOnly = 1;
   const bool active = SetProcessMitigationPolicy(ProcessSignaturePolicy, &policy, sizeof(policy)) != 0;
   ```
   `active` 為 false 時以 `OutputDebugStringW` 記錄一個英文診斷事件(例如 `PaneDock: diagnostic mode requested but SetProcessMitigationPolicy failed\n`),並視同一般模式(決策 6)。
3. 主視窗標題依 `active` 決定:`L"PaneDock — Diagnostic Mode"` 或 `L"PaneDock"`(決策 5)。標題字串為單一 `const wchar_t*`,不做字串組裝。
4. 一個 runnable self-check,驗證命令列解析這段**非平凡邏輯**:新增 `tests/unit/diagnostic_flag_check.cpp`(比照 `tests/unit/explorer_host_lifetime_check.cpp` 的既有寫法與 `tests/CMakeLists.txt` 的註冊方式),涵蓋:無參數、`--diagnostic`、`/diagnostic`、`--DIAGNOSTIC`、`--diagnostics`(不應命中)、`--diagnostic` 出現在第三個參數、參數中含引號路徑。**解析函式必須可從測試呼叫**——把它抽成一個接受 `int argc, wchar_t** argv`(或 `std::span<const wchar_t* const>`)的純函式,放在 `src/app_shell/` 的一個小標頭中;`wWinMain` 只負責取得 argv 並呼叫它。這個純函式不碰 `windows.h` 以外的東西,也**不得放進 `core`**(它是 app_shell 的命令列語意,不是資料模型)。
5. 更新 `docs/development.md` 或 `docs/testing.md` 其一,用一段話寫下診斷模式的用途與啟動方式(`.\build\PaneDock.exe --diagnostic`),使後續執行 MVP 驗收的人知道它存在。**只寫一處**,不要兩處都寫。

## Non-goals

- **不做獨立 process、surrogate host、IPC 或任何形式的 extension 隔離**(決策 1)。這是 `docs/design-spec.md` §3.2 明確排除、§14 保留待日後的方向,重開需要實際崩潰紀錄與明文覆寫,本 ticket 兩者都沒有。
- 不寫 registry、不改 machine-global policy、不要求管理員權限(決策 3)。
- 不做執行期切換、不做選單項目、不做「重新啟動進入診斷模式」的按鈕——後者是 PD-025 崩潰復原的範圍,不是本 ticket 的。
- 不列舉、不顯示、不個別停用單一 extension(決策 8)。
- 不加設定檔欄位、不改 session schema、不改 `core`(決策 7)。
- 不加 log 檔。診斷事件走既有的 `OutputDebugStringW`,與 repo 現況一致;加檔案 log 是另一個決策,`AGENTS.md` 的「no telemetry」與 idle disk I/O 門檻都要一併考慮,不順手做。

## Acceptance

1. `.\build\PaneDock.exe`(無參數)行為與本 ticket 之前完全相同:標題為 `PaneDock`,第三方 extension 正常運作。
2. `.\build\PaneDock.exe --diagnostic` 啟動後標題為 `PaneDock — Diagnostic Mode`,應用程式可正常導覽本機資料夾。
3. 在一台**裝有第三方 shell extension** 的機器上(`docs/testing.md` §Required test environments 第二項),診斷模式下對檔案按右鍵,原生選單仍出現,但第三方 extension 貢獻的項目消失或減少;一般模式下同一個檔案的選單含這些項目。**兩次的觀察結果都要記進交接區**(需真實桌面,見 Handoff)。
4. `/diagnostic` 與 `--diagnostic` 等效;`--diagnostics`、`--diag`、無參數都不進入診斷模式。由 self-check 驗證。
5. `SetProcessMitigationPolicy` 失敗時應用程式仍正常啟動,標題不顯示 `Diagnostic Mode`,且 debug output 有一筆英文診斷事件。
6. 診斷模式不改變 `%LOCALAPPDATA%\PaneDock` 下的任何檔案內容格式:診斷模式啟動並正常關閉後,`session.json` 仍可被一般模式讀取,schema version 不變。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過,新增的 self-check 已註冊於 CTest 並通過。
8. `rg -n "windows\.h|HWND|IUnknown" src/core` 無輸出;`git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# core 邊界未污染;確認沒有偷偷走 registry 或第二個 process
rg -n "windows\.h|HWND|IUnknown" src/core
rg -n "RegOpenKey|RegSetValue|HKEY_|CreateProcess|ShellExecute" src
# 預期:無命中(若有,對照 Non-goals 逐一說明)
rg -n "SetProcessMitigationPolicy|MicrosoftSignedOnly|CommandLineToArgvW" src
git diff --check
git status
# 預期:改動集中在 src/app_shell/*、tests/unit/*、tests/CMakeLists.txt、docs/*、本文件
```

```powershell
# 程序層級冒煙:兩種模式都能啟動並存活
foreach ($args in @('', '--diagnostic')) {
    $p = Start-Process .\build\PaneDock.exe -ArgumentList $args -PassThru
    Start-Sleep -Seconds 3
    $p.Refresh()
    "{0,-14} Responding={1} MainWindowTitle='{2}' Handles={3}" -f `
        ($(if ($args) { $args } else { '(no args)' })), $p.Responding, $p.MainWindowTitle, $p.HandleCount
    Stop-Process -Id $p.Id -Force
}
# 預期:兩者 Responding=True;第二筆標題含 'Diagnostic Mode'。
# 這是程序層級檢查,不是視覺驗收;Acceptance 3 需真實互動桌面。
```

## Handoff requirements

- 實際採用的旗標解析寫法與 self-check 涵蓋的 case 列表。
- `SetProcessMitigationPolicy` 在本機的回傳值與 `GetLastError()`(成功也記)。若該環境無法成功設定,寫清楚原因與環境條件——這會直接影響 PD-025 的「重新啟動進入診斷模式」是否有意義。
- Acceptance 3 的兩次右鍵選單觀察結果,含該機器上已安裝的第三方 shell extension 清單。**若沒有互動桌面,標為「未驗證,需真實桌面」並寫出使用者該怎麼執行,不要猜測或編造選單內容。**
- 診斷模式下是否有任何功能明顯壞掉(例如某個雲端資料夾整個無法列出、導覽大量失敗)。這是預期行為的一部分,但要記下來,免得日後被當成 regression。
- 明確聲明本 ticket 沒有引入第二個 process、沒有寫 registry——決策 1 與決策 3 是本 ticket 最容易被實作偏移的兩點。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-24 實作交接

#### 完成內容

- 新增 `src/app_shell/diagnostic_mode.h` 的純函式最終簽章：`bool panedock::app_shell::diagnostic_requested(int argc, const wchar_t* const* argv) noexcept`。它只用 `std::wstring_view` 與 ASCII 大小寫折疊比較，從 argv[1] 起掃描，接受 `--diagnostic`／`/diagnostic`，大小寫不敏感，其他參數忽略；沒有引入 `windows.h`、COM、core 依賴或配置狀態。
- `wWinMain` 透過 `CommandLineToArgvW(GetCommandLineW(), &argc)` 取得 argv，呼叫純函式後立即 `LocalFree(argv)`。命中旗標時，在 `OleInitialize(nullptr)` **之前**設定 `PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY.MicrosoftSignedOnly = 1`，只有 `SetProcessMitigationPolicy(ProcessSignaturePolicy, &policy, sizeof(policy)) != 0` 才把 `diagnostic_mode` 設為 true；失敗只寫 `PaneDock: diagnostic mode requested but SetProcessMitigationPolicy failed` 到 `OutputDebugStringW` 並按一般模式繼續。
- 主視窗建構使用單一 `const wchar_t* const title`：成功 policy 時為 `PaneDock — Diagnostic Mode`，否則為 `PaneDock`。沒有 runtime toggle、session 欄位、log 檔或 registry 寫入。
- 新增 `tests/unit/diagnostic_flag_check.cpp` 並註冊為 CTest `panedock_diagnostic_flag`；涵蓋無參數、精確 `--diagnostic`、`/diagnostic`、`--DIAGNOSTIC`、不命中 `--diagnostics`、旗標在第三個 argv、含空格 quoted-path argv 後的旗標。測試 table wiring 將 `src` 加到測試 include path，未改 core。
- 在 `docs/development.md` 新增一次診斷模式用途與啟動方式：`\.\build\PaneDock.exe --diagnostic`（亦接受 `/diagnostic`）。沒有新增第二個 process、surrogate、IPC、registry、machine-global policy 或 administrator requirement。

#### Acceptance 與互動限制

- Acceptance 1（無參數一般模式標題、第三方 extension 正常）：**未驗證,需真實桌面**。本 session 沒有可操作桌面，也沒有觀察右鍵選單或 extension。程序 smoke 只能確認 process 存活；PowerShell 在此無桌面環境的 `MainWindowHandle` 曾指向 `UAC_InputIndicatorOverlayWnd`，因此不能把 `MainWindowTitle` 空值當成 PaneDock 標題結果。
- Acceptance 2（`--diagnostic` 標題及本機資料夾導覽）：**未驗證,需真實桌面**。命令列 parser self-check 與數次程序啟動已通過，但沒有真實視窗可點擊／導覽；一次 `/diagnostic` smoke 讀到 `PaneDock — Diagnostic Mode` 且 `Responding=True`，其餘 title 讀值受上述無桌面 handle 限制，不能冒充視覺驗收。
- Acceptance 3（第三方 shell extension 右鍵選單一般／診斷兩次觀察）：**未驗證,需真實桌面**。使用者請在有第三方 extension 的 Windows 機器上：先以 `\.\build\PaneDock.exe` 啟動，於同一個檔案右鍵並記錄 Microsoft／第三方項目；關閉後以 `\.\build\PaneDock.exe --diagnostic`（或 `/diagnostic`）啟動同一路徑，再右鍵同一檔案，逐項記錄消失／保留的 extension 項目與已安裝 extension 清單。兩次必須使用同一檔案、同一 Shell view，結果補回本交接區。
- Acceptance 4（旗標等效與否命中）：**PASS（self-check）**。`diagnostic_flag_check` 覆蓋上述七組案例；`--diagnostics`、無參數與非旗標路徑不命中。
- Acceptance 5（強制 policy failure 後一般模式繼續、標題不顯示診斷、debug event）：**未驗證,需真實桌面／受控 policy 環境**。正常 process smoke 未強制製造 `SetProcessMitigationPolicy` failure，也沒有 DebugView／debugger 讀取 `OutputDebugStringW` 的互動工具；failure branch 已存在且不會使啟動 return early。成功呼叫後 `GetLastError()` 沒有可依 Win32 契約解讀的值，未捏造數字。
- Acceptance 6（診斷啟動／正常關閉後 session schema 與一般模式 round-trip）：**未驗證,需真實桌面**。本 session 只做啟動後程序層級終止，沒有正常 `WM_CLOSE`、session.json snapshot 與一般模式重開比較；程式碼沒有新增持久化欄位，但這不能替代 round-trip 證據。
- Acceptance 7（build／CTest／self-check）：**PASS**。LLVM-MinGW configure/build 成功；CTest 4/4 通過（新增 `panedock_diagnostic_flag` 加既有三項 core tests）；self-check 輸出 `PASSED: diagnostic_flag_check`。
- Acceptance 8（core boundary 與 diff check）：**PASS**。`rg -n "windows\.h|HWND|IUnknown" src/core` 無輸出；`rg -n "RegOpenKey|RegSetValue|HKEY_|CreateProcess|ShellExecute" src` 無輸出；`git diff --check` 通過。policy references 只出現在 `src/app_shell/main.cpp` 的 `CommandLineToArgvW`、`MicrosoftSignedOnly`、`SetProcessMitigationPolicy` 路徑。

#### Agent checks

- 程序 smoke 依票據命令嘗試無參數、`--diagnostic`、`/diagnostic`、`--diagnostics` 四次，各 process 均未宣告 crash；結果受無互動 desktop 的 MainWindowHandle／Responding 不穩定限制，已在 Acceptance 1–2 明確降級。一次輸出曾為 `/diagnostic: Responding=True, MainWindowTitle='PaneDock — Diagnostic Mode', Handles=615`；另一次 `--diagnostic` 曾回報 `Responding=False`，後續重跑不一致，因此不作成功或失敗的 UI 證據。
- 本機環境資訊：Windows NT `10.0.26200.0`、DisplayVersion `25H2`；無法透過 `Get-CimInstance` 取得完整 edition（拒絕存取），不推測。未使用任何測試 volume、network drive、USB 或 OneDrive。
- 工作開始前已有未追蹤 `.claude/`，本輪未觸碰；未修改 `docs/tickets.md`／`docs/roadmap.md`；未 commit。

### 2026-08-24 最終檢查補記

- 最終 self-check 另補覆蓋 `--diag`（明確不命中）；因此 parser 案例完整包含無參數、`--diagnostic`、`/diagnostic`、大小寫、`--diagnostics`、`--diag`、第三個 argv 與含空格路徑。
- 追加案例後重跑：`cmake --build build` 成功、CTest 4/4 通過、`panedock_diagnostic_flag_test.exe` 輸出 `PASSED: diagnostic_flag_check`、core/safety boundary grep 無輸出、`git diff --check` 通過。最終 tracked diff 集中於 `src/app_shell/main.cpp`、新增 `src/app_shell/diagnostic_mode.h`、`tests/CMakeLists.txt`、新增 self-check、`docs/development.md` 與本 ticket 交接區。
- 最後一次程序 smoke：無參數 `Responding=True`（標題欄讀值受無桌面 handle 限制）；`--diagnostic` `Responding=True`、讀到 `PaneDock — Diagnostic Mode`；`/diagnostic` 同樣 `Responding=True` 且讀到診斷標題；`--diagnostics` `Responding=True` 且不進診斷標題。每個測試 process 均於記錄後終止，沒有宣稱這替代互動桌面驗收。
