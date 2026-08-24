# PD-025 — 崩潰復原路徑:不乾淨關閉偵測、退回備份告知、備份不被損壞檔覆寫(FR-013／NFR-006)

Phase 5 · core + app_shell · Depends on: PD-024

- Source: `AGENTS.md`、`docs/design-spec.md` FR-013 / §NFR-006 / §11 / §10、`docs/development.md` §Product boundary、`docs/tickets/PD-006-session-document-persistence.md`、`docs/tickets/PD-013-config-file-extensibility-convention.md`
- Origin: 2026-08-24,`docs/roadmap.md` Phase 5「Crash recovery path」。`src/app_shell/main.cpp` 目前留著一行 `TODO: surface session recovery in application chrome`,那是 FR-013 後半句尚未交付的直接證據。
- Priority: HIGH——FR-013 是 MVP 驗收清單的一項(`docs/testing.md`「FR-013 recovery from a corrupt session document」),NFR-006 又明文寫「崩潰復原路徑不是選配」。本 ticket 同時修一個會**破壞使用者唯一備份**的既有缺陷(見 Scope 3)。

## Goal

讓 PaneDock 在「上一次沒有正常關閉」與「session document 損壞」兩種情況下,做到 FR-013 要求的三件事:復原到可用狀態、告知使用者發生過退回、且不因此弄丟任何仍然有效的設定。

現況盤點(讀過程式碼後的事實,不要重新推論):

- **復原到可用狀態**:已經成立。`save_now()` 在 13 個狀態變更點被呼叫(Group/tab/版型/位置變更),`read_session()` 已有 primary → backup → default 三段退回,`is_valid()` 在啟動時 assert。崩潰最多丟掉最後一次變更之後的東西。
- **告知使用者已退回**:**未交付**。`recovered_from_corruption` 目前只寫進 `OutputDebugStringW`,使用者看不到,旁邊就是那行 TODO。
- **偵測上一次是否崩潰**:**不存在**。沒有任何 clean-shutdown 標記。
- **備份保護**:**有缺陷**。`write_session()` 無條件把現有 primary 複製成 backup。從 backup 復原之後的第一次寫入,會把**損壞的 primary 複製到 backup 上**,把使用者僅存的良好版本蓋掉。

## 已確認的產品決策

1. **不乾淨關閉以 session document 內的一個布林欄位偵測,不另開檔案。** `%LOCALAPPDATA%\PaneDock` 已經有 primary／backup／temp 三個檔案與一套原子寫入流程;再加一個 marker 檔就要再寫一次原子替換、再處理一次它自己的損壞情況。欄位名為 `clean_shutdown`,依 PD-013 的慣例是**附加式的可選欄位**:舊文件沒有這個欄位時一律視為 `true`(乾淨),schema version 不變。
2. **標記時機:啟動讀取成功後立刻寫一次 `clean_shutdown = false`;正常關閉的那一次寫入寫 `true`。** 執行期的 `save_now()` 一律寫 `false`。這樣「啟動後尚未做任何變更就崩潰」——也就是最重要的那個情境(某個 extension 在第一次 realize 時弄垮 process)——一樣偵測得到。啟動時多一次磁碟寫入是可接受的:NFR-001 的門檻是**閒置**期間零 I/O,啟動不是閒置。
3. **告知方式是啟動時一個 `MessageBoxW`,不是常駐 UI。** 理由:它一年出現不到一次,為它加一條常駐通知列要處理版型、DPI、關閉按鈕與生命週期,不成比例。`MB_OK | MB_ICONWARNING`,owner 為主視窗,在 `ShowWindow` 之後、訊息迴圈之前顯示。
4. **兩種情況兩則訊息,可以在同一次啟動同時出現(先損壞、後不乾淨關閉)。** 英文,見 `AGENTS.md`:
   - 退回備份:`PaneDock could not read its saved session and restored the previous good version. Some recent changes may be missing.`
   - 退回預設:`PaneDock could not read its saved session or its backup and started with a default Group. Your previous Groups could not be recovered.`
   - 不乾淨關閉:`PaneDock did not shut down cleanly last time. If this keeps happening, start it with --diagnostic to run without third-party shell extensions.`
5. **不自動重新啟動、不自動進入診斷模式、不產生第二個 process。** 訊息只告訴使用者旗標怎麼下(PD-024 已交付 `--diagnostic`)。自動重啟需要 `CreateProcess`,那會把單一 process 的架構打開一個口子,而 `docs/design-spec.md` §3.2／§14 對多 process 的立場是明確保留待日後。
6. **不做崩潰計數、不做「連續崩潰 N 次就自動安全模式」。** 沒有實際崩潰資料前那是憑空調參數。若使用者回報崩潰迴圈,再開 ticket,屆時有真實紀錄可依。
7. **不做 minidump、不做崩潰回報、不寫 log 檔。** `AGENTS.md`:no network, no telemetry。診斷事件維持 `OutputDebugStringW`。
8. **`write_session()` 的備份保護寫在共用函式裡,不寫在呼叫端。** `AGENTS.md`:「A guard in the shared function is a smaller diff than a guard in every caller」。`write_session` 只有一個 app_shell 呼叫點加測試,但保護的是資料安全,放在函式內才對每個未來呼叫端都成立。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` FR-013:
> 崩潰後重新啟動復原到可用狀態。session document 無法解析時退回上一個良好版本,並在 UI 告知已退回。

`docs/design-spec.md` §NFR-006:
> 第三方 shell extension 是 in-process 的外部程式碼。PaneDock 保證自身程式碼正確,不保證寫得差的 extension 不會 hang 或 crash 本 process。因此需要一個可抑制第三方 shell extension 的診斷模式,且崩潰復原路徑不是選配。

`docs/design-spec.md` §11:
> - session document 損壞:退回備份並告知(FR-013)
> - COM 失敗:記錄診斷事件,不得靜默忽略,不得使整個視窗不可用
> - 第三方 extension 崩潰:不可預防,但復原路徑必須存在(NFR-006)

`docs/design-spec.md` §10:
> - 寫入:原子替換(temp 檔加 rename),保留上一版為備份
> - **不得寫入 PIDL 或 COM 指標。**

`AGENTS.md`:
> **Every persisted config/setting file must be designed for forward extensibility.** It carries an explicit schema version from its first version. A read that encounters a field it does not recognize preserves that field rather than silently dropping it on the next write-back. A schema change is additive (new optional fields, new migration step) rather than a destructive reinterpretation of an existing field's meaning.

> All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.

> Read the relevant spec section and trace every caller before touching shared code. A guard in the shared function is a smaller diff than a guard in every caller, and patching only the path the ticket names leaves sibling callers broken.

> **Keep `src/core` free of HWND, COM and `windows.h`.** It is the only automated test seam in this project.

> **App UI text must be English.** No Chinese strings ship in the binary.

> Event-driven idle path only. No busy loops, no polling timers.

## Files to read and trace first

- `src/core/session.h` — `SessionDocument`(`application` ＋ `preserved_json`)、`SessionSource`、`SessionReadResult`(含 `recovered_from_corruption`)。新欄位放在 `SessionDocument` 上,**不要**放進 `ApplicationState`:它不是使用者的工作狀態,是持久化層的中繼資料,放進 model 會污染 `is_valid()` 與所有既有 model 測試。
- `src/core/session.cpp` 的 `encode()` / `decode()` / `serialize_session()` / `deserialize_session()` — 注意既有的 `preserved_object()` 與 `preserved_json` 機制:未知欄位是**保留**而非丟棄(PD-013 的慣例),新欄位要沿用同一個路徑寫回。
- `src/core/session.cpp` 的 `write_session()` 第 528–534 行 — `had_primary` 時無條件 `copy_file(primary, backup, overwrite_existing)`。**這就是 Scope 3 要修的地方**:損壞的 primary 會覆蓋良好的 backup。
- `src/core/session.cpp` 的 `read_session()` — 三段退回與 `recovered_from_corruption` 的設定方式(`primary_exists || backup_exists`)。
- `src/app_shell/main.cpp` 的 `wWinMain`(約 1341–1366 行)— `read_session` 之後那段含 `TODO: surface session recovery in application chrome` 的程式碼,以及 `session_source_name()` helper(約 959 行,已存在,直接重用)。
- `src/app_shell/main.cpp` 的 `save_now()`(約 416 行)與它的 13 個呼叫點(`rg -n "save_now" src/app_shell/main.cpp`)— 決策 2 的「執行期一律寫 false」要在 `save_now` 內一次做到,不是在 13 個呼叫端各做一次。
- `src/app_shell/main.cpp` 的 `WM_CLOSE`(約 1288 行)— 正常關閉的最後一次寫入在這裡,決策 2 的 `true` 只能在這一條路徑上發生。
- `tests/unit/core_session_test.cpp` — 既有的往返、遷移、損壞測試寫法;本 ticket 的 self-check 直接加在這裡。
- `docs/tickets/PD-013-config-file-extensibility-convention.md` — 附加式欄位的既有慣例,決策 1 依據它。

## Scope

1. **`core`:`SessionDocument` 新增 `bool clean_shutdown{true};`**,序列化為 top-level `"clean_shutdown"`,反序列化時缺欄位視為 `true`,型別不符也視為 `true`(保守:寧可漏報一次崩潰,不要對著一份好文件亂報)。`kSessionSchemaVersion` **不變**(附加式可選欄位,不是破壞性變更)。未知欄位保留機制不得因此退化。
2. **`app_shell`:接上標記與告知。**
   - `read_session()` 成功後,把 `clean_shutdown = false` 寫回一次(決策 2)。
   - `save_now()` 寫入前一律設 `false`。
   - `WM_CLOSE` 的那一次寫入設 `true`(在 `save_now` 之外處理,或給 `save_now` 一個預設為 `false` 的參數——**選較小的那個做法**)。
   - 依 `loaded.recovered_from_corruption` 與 `loaded.source`,以及載入文件裡的 `clean_shutdown`,在 `ShowWindow` 之後顯示決策 4 的對應訊息;刪掉那行 TODO。既有的 `OutputDebugStringW` 診斷事件保留。
3. **`core`:`write_session()` 不得用不可解析的 primary 覆寫 backup。** 在 `copy_file(primary, backup, ...)` 之前先讀 primary 並以既有的 `deserialize_session()` 驗證;無法解析就**跳過備份複製**(保留既有 backup),其餘流程不變,回傳值語意不變。這是資料安全修正,不是最佳化。
4. **Self-check(全部落在 `core` seam,加進 `tests/unit/core_session_test.cpp`)**:
   - `clean_shutdown` 的 true／false 往返。
   - 舊文件(無此欄位)讀進來是 `true`,且寫回後其他未知欄位仍被保留。
   - 寫入一份良好文件 → 手動把 primary 弄成損壞內容 → 再 `write_session()` 一次 → 讀回 backup,**backup 仍是那份良好文件**(Scope 3 的迴歸測試)。
   - 既有的往返／遷移／損壞測試全部維持通過。

## Non-goals

- 不自動重啟、不自動進入診斷模式、不 `CreateProcess`(決策 5)。
- 不做崩潰次數統計、安全模式階梯、或任何自動降級(決策 6)。
- 不做 minidump／崩潰回報／檔案 log(決策 7)。
- 不加常駐通知列或 UI chrome(決策 3)。
- 不改 `ApplicationState`、不改 `is_valid()`、不改 `kSessionSchemaVersion`、不加 migration step。
- 不改 `read_session()` 的三段退回順序——它已經是 FR-013 要的行為。
- 不處理「不可解析 location」的錯誤 UI(那是 PD-022,已完成)。
- 不加任何 timer 或背景檢查。

## Acceptance

1. 正常啟動並正常關閉一次後,`%LOCALAPPDATA%\PaneDock\session.json` 內 `"clean_shutdown"` 為 `true`;應用程式執行中(尚未關閉)時該欄位為 `false`。
2. 在應用程式執行中直接 `Stop-Process -Force` 模擬崩潰,再啟動:出現決策 4 的「did not shut down cleanly」訊息,且**全部 Group、pane、tab 與 location 都還在**(最後一次 `save_now` 之後的狀態)。
3. 把 `session.json` 內容換成無法解析的垃圾(backup 保持良好)再啟動:出現「restored the previous good version」訊息,且工作狀態來自 backup。
4. 承 3,該次啟動後再做一次會觸發 `save_now` 的操作(例如新增一個 tab),然後檢查 `session.json.bak`:**它不是那份垃圾內容**,而是仍可解析的良好文件(Scope 3)。
5. `session.json` 與 `session.json.bak` 都損壞時啟動:出現「started with a default Group」訊息,應用程式以預設 Group 正常運作,不崩潰。
6. 舊版(不含 `clean_shutdown` 欄位)的 `session.json` 可以直接讀取,不報「不乾淨關閉」,寫回後檔案內先前存在的未知欄位仍在。
7. `docs/testing.md` 的 MVP 驗收清單「FR-013 recovery from a corrupt session document」有一個可照著執行的具體步驟(可直接引用 Acceptance 3–5 的步驟)。
8. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過;新增的 `core` self-check 通過。
9. `rg -n "windows\.h|HWND|IUnknown" src/core` 無輸出;`git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "windows\.h|HWND|IUnknown" src/core        # 預期:無輸出
rg -n "CreateProcess|ShellExecute|SetTimer" src  # 預期:無命中(決策 5、event-driven idle path)
rg -n "TODO" src                                  # 預期:不再命中 session recovery 那一行
rg -n "clean_shutdown" src tests
git diff --check
git status
```

```powershell
# 不乾淨關閉偵測的端到端檢查(不需要人類操作滑鼠鍵盤,可由 agent 執行)
$session = Join-Path $env:LOCALAPPDATA 'PaneDock\session.json'
$p = Start-Process .\build\PaneDock.exe -PassThru
Start-Sleep -Seconds 5
(Get-Content $session -Raw) -match '"clean_shutdown"\s*:\s*false'   # 預期:True
Stop-Process -Id $p.Id -Force                                        # 模擬崩潰
Start-Sleep -Seconds 2
(Get-Content $session -Raw) -match '"clean_shutdown"\s*:\s*false'   # 預期:仍為 True
# 之後手動啟動一次確認 MessageBox 內容(Acceptance 2 的訊息本身需真實桌面)
```

```powershell
# 備份保護的檔案層檢查(Acceptance 4;先備份自己的真實 session)
$dir = Join-Path $env:LOCALAPPDATA 'PaneDock'
Copy-Item (Join-Path $dir 'session.json') "$env:TEMP\panedock-session-safe.json" -Force
Set-Content (Join-Path $dir 'session.json') 'not json at all' -Encoding utf8
# 啟動、做一次會觸發 save_now 的操作、關閉,然後:
Get-Content (Join-Path $dir 'session.json.bak') -Raw | Select-Object -First 1
# 預期:仍是可解析的 JSON,不是 'not json at all'
```

## Handoff requirements

- `clean_shutdown` 最終的序列化位置(top-level 還是巢狀)與缺欄位／型別不符時的實際行為。
- `WM_CLOSE` 那一次 `true` 寫入最終採用的做法(參數還是獨立呼叫),以及為什麼比另一個小。
- Scope 3 的驗證證據:修正前後 `session.json.bak` 的內容差異,以及新增的 `core` 測試名稱。
- Acceptance 2／3／5 三個 `MessageBoxW` 的**實際截圖或實際觀察紀錄**;沒有互動桌面時逐項標「未驗證,需真實桌面」,並寫清楚使用者該怎麼重現。**不得猜測或編造訊息是否出現。**
- 啟動時多出來的那一次寫入對冷啟動時間是否可感知(有讀數就寫讀數,沒有就寫「未量測」)。
- 若發現 `save_now()` 有任何呼叫點在關閉序列之後才發生(那會把 `true` 蓋回 `false`),明確寫出來並說明怎麼處理的。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-24 實作交接

#### Persistence 與 schema

- `SessionDocument` 最終欄位為 `ApplicationState application`、`std::string preserved_json`、`bool clean_shutdown{true}`；沒有把中繼欄位放進 `ApplicationState`，`kSessionSchemaVersion` 仍為 `1`，沒有 migration step。
- `clean_shutdown` 序列化在 JSON **top-level**，固定 key 為 `"clean_shutdown"`。`deserialize_session()` 讀到真正的 JSON boolean 就採用它；欄位缺少或型別不是 boolean 時都保守採用 `true`。`preserved_json` 仍保留整份原文件，`encode()` 先從它建立 root 再覆寫已知欄位，因此未知 root／巢狀欄位仍會寫回。
- `write_session()` 在複製 primary 到 backup 前呼叫既有 `read_file()`（內部走 `deserialize_session()`）驗證 primary；primary 不可解析時跳過複製，直接以新 temp 取代 primary，保留現有 backup。primary 可解析時維持原本 backup 複製與 atomic rename 流程。

#### App shell wiring

- `save_now(AppState& state, bool clean_shutdown = false) noexcept` 是最終簽章。它在一次共用路徑內寫入 `state.session_document.clean_shutdown`，所以所有執行期 save 呼叫預設為 `false`；`WM_CLOSE` 唯一的最後寫入使用 `save_now(*state, true)`。`WM_CLOSE` 之後只 destroy views、DestroyWindow，沒有任何 save 呼叫會把 `true` 蓋回 `false`。
- `wWinMain` 在 `read_session()` 成功取得文件、搬入 `state` 後立刻呼叫 `save_now(state)`，因此程式執行中會是 `false`；這次寫入發生在 `CreateWindowExW` 前，啟動不是 idle window。記錄原始 `loaded.source`、`loaded.recovered_from_corruption` 與 `clean_shutdown` 後，於 `ShowWindow`／`UpdateWindow` 之後、訊息迴圈之前，以 owner 為主視窗的 `MessageBoxW(..., MB_OK | MB_ICONWARNING)` 顯示訊息。原有 `OutputDebugStringW` recovery source 診斷事件保留，`TODO` 已移除。
- backup recovery 與 default recovery 會先後於 unclean warning 顯示，允許同一次啟動同時告知兩種狀況。三則英文訊息採用 ticket 決策 4 的逐字內容；未自動重啟、未開第二 process、未加 timer／crash counter／log／minidump。

#### Self-check 與驗證

- 新增／擴充 `tests/unit/core_session_test.cpp`：`test_round_trip_and_plain_json()` 驗證 `true`／`false` round-trip；`test_unknown_fields_survive_write_back()` 移除 `clean_shutdown` 模擬舊文件並驗證缺欄位為 `true`、root 與巢狀 unknown fields 保留；`test_clean_shutdown_type_mismatch_defaults_true()` 驗證錯誤型別回到 `true`；新增 `test_corrupt_primary_does_not_replace_good_backup()`，先建立兩版良好文件，再把 primary 改成 `not json at all`，確認下一次 `write_session()` 後 backup bytes 不變且仍可解析。`panedock_core_session` 已通過。
- LLVM-MinGW/Ninja configure、`cmake --build build` 與完整 CTest 均成功，CTest 為 4/4。
- 以提升權限暫時備份並在 finally 還原實際 `%LOCALAPPDATA%\PaneDock\session.json`／`.bak` 後，無人操作的端到端欄位檢查結果為 `RUNNING_FALSE=True AFTER_FORCE_FALSE=True RESTART_FALSE=True`：啟動後為 false，`Stop-Process -Force` 後仍為 false，重啟讀到不乾淨狀態。另以 `CloseMainWindow()` 檢查正常關閉：`RUNNING_FALSE=True CLOSE_REQUESTED=True EXITED=True CLEAN_AFTER_CLOSE=True`。這些檔案已還原為檢查前內容。
- `docs/testing.md` 新增 Phase 5 FR-013 具體手動 protocol，涵蓋正常 clean marker、強制終止、primary 損壞退回 backup、save 後 backup 保護、兩檔皆損壞退回 default 與檔案還原步驟。
- Agent boundary checks：`rg -n "windows\.h|HWND|IUnknown" src/core` 無輸出；`rg -n "CreateProcess|ShellExecute|SetTimer" src` 無命中；`rg -n "TODO" src` 不再命中 recovery TODO；`rg -n "clean_shutdown" src tests` 命中預期 persistence／app／test 位置；`git diff --check` 通過。

#### Acceptance 狀態與桌面限制

- Acceptance 1：欄位生命週期以無人操作端到端檢查確認（啟動 false、正常 CloseMainWindow 後 true）；實際雙擊情境未做。
- Acceptance 2（強制終止後的實際 `MessageBoxW` 內容與出現）：**未驗證,需真實桌面**。重現：啟動 PaneDock，等待 `session.json` 為 `clean_shutdown:false`，以 `Stop-Process -Force` 終止，再啟動並記錄 warning 是否為 `PaneDock did not shut down cleanly last time...`，確認所有 Group／pane／tab／location 都在。
- Acceptance 3（primary 垃圾、backup warning 與實際復原 UI）：**未驗證,需真實桌面**。重現：關閉 app，將 `session.json` 寫成 `not json at all`、保留可解析 `.bak`，啟動並記錄 `restored the previous good version` MessageBox，確認工作狀態來自 backup。
- Acceptance 4：core regression `test_corrupt_primary_does_not_replace_good_backup()` 與可解析 backup 檔案層結果 PASS；實際在 UI 做新增 tab／再關閉的操作未於本環境執行。
- Acceptance 5（primary／backup 同時損壞的實際 default warning 與 UI）：**未驗證,需真實桌面**。重現：關閉 app，將兩個 session 檔都換成非法文字，啟動並記錄 `started with a default Group` MessageBox，確認 default Group 可操作且不崩潰。
- Acceptance 6：PASS（缺欄位與未知欄位保留 self-check）。Acceptance 7：PASS（`docs/testing.md` protocol）。Acceptance 8／9：PASS（build、4/4 CTest、core boundary、禁止 API grep、diff check）。
- 啟動多出的 false marker 寫入與 MessageBox 前的啟動時間差沒有量測，記為「未量測」；本票沒有加 timing instrumentation。
- 本輪沒有修改 `docs/tickets.md` 狀態或 `docs/roadmap.md`，沒有 commit；沒有改動 PD-003 或任何既有 ticket 歷史文字。

### 2026-08-24 交接補充：MessageBox 未驗證

以下是程式中傳給 `MessageBoxW` 的預期英文文字與重現方式，**不是本環境的實際觀察或截圖**；三項均維持「未驗證,需真實桌面」：

- 不乾淨關閉：`PaneDock did not shut down cleanly last time. If this keeps happening, start it with --diagnostic to run without third-party shell extensions.`
- primary 退回 backup：`PaneDock could not read its saved session and restored the previous good version. Some recent changes may be missing.`
- primary／backup 都損壞：`PaneDock could not read its saved session or its backup and started with a default Group. Your previous Groups could not be recovered.`

三者都使用 owner 為主視窗、title `PaneDock`、`MB_OK | MB_ICONWARNING`。使用者可依 `docs/testing.md` 的 Phase 5 FR-013 protocol 逐項準備 session 檔、啟動程式並實際記錄 MessageBox 是否出現及內容；在 backup 情境也要確認 restored Group，兩檔皆壞情境要確認 default Group 可運作。
