# PD-128 — `panedock_launch_smoke`／關閉時間歇性 `STATUS_STACK_BUFFER_OVERRUN`(0xC0000409)的調查

Phase 7 · investigation(門檻:可重現再修正)· Depends on: 可重現的 runtime repro(非 ticket)

- Source: 既有候選(見 `docs/tickets.md` §候選),2026-08-30 `PD-124`~`PD-127` 稽核迴圈期間**再次出現一次**。偵察性 ticket,**不承諾**在取得可重現步驟前改產品碼。
- Origin: 已知在「關閉一個含 4 個 Group、其中一個 42 個 tab 的真實 `%LOCALAPPDATA%\PaneDock\session.json` 之後」間歇觸發;本次在完整 `ctest --test-dir build --output-on-failure` 執行 `panedock_launch_smoke` 時偶發 `0xC0000409`(STATUS_STACK_BUFFER_OVERRUN),立即單獨重跑該測試與完整 suite 皆通過,無法穩定重現。兩次皆與本次 `PD-124`~`PD-127` 的改動無關(該崩潰在 `PD-003` 時期即被記錄,早於本批次)。
- Priority: 條件式 HIGH——它是崩潰(非風格),但 0xC0000409 無重現即無法歸因;未歸因就修是猜測,違反本專案「未取真實 runtime evidence 前不預設現行行為有 bug」與 PD-123/PD-106 的證據紀律。

## 已確認的事實

1. 崩潰 exit code 為 `-1073740791`(0xC0000409,`STATUS_STACK_BUFFER_OVERRUN`)。在 LLVM-MinGW 上這對應 `-fstack-protector` 的 `__stack_chk_fail`(或 MSVC `/GS` 的 `__report_gsfailure`)──偵測到某個 stack 區域寫越界／stack cookie 被破壞。也可能由系統 DLL 的 failfast 於 teardown 觸發。
2. 兩次發生都緊隨「關閉一個大 session(4 Group,其一 42 tab)的 `panedock_launch_smoke`」;立即重跑同一測試與完整 suite 均通過。
3. 非 `PD-124`~`PD-127` 所致;非 JSON 解析遞迴深度(該 parser 遞迴深度上限約 6 層,`session.cpp:24-58`,深層嵌套才會 `0xC00000FD`,非 `0xC0000409`)。

## 調查假說候選(先列出,不先修)

- **H1 — 固定大小 stack buffer 被 tab/項數量驅動寫越界**:在 close／save 路徑，`refresh_status_bar`/`item_counts`(`explorer_host.cpp` `kSelectionSizeItemLimit = 1000`)、`draw_status_bar`(`main.cpp:1089` `std::array<wchar_t,256>`)、或 session serialize 中任何以 tab 數／項數為索引的區域性陣列。目前掃描未見明顯越界(`GetWindowTextW` 兩處 `main.cpp:3223`／`sidebar.cpp:296` 的 buffer size 都正確 = length+1)。
- **H2 — teardown 期間 Shell/系統 DLL 的 failfast**:`IExplorerBrowser::Destroy`/相關 shell32 回呼在大量 view/選取的 teardown 時命中其內部的 stack cookie 檢查。此類與 app 程式碼無關,只與 session 規模／Shell 狀態有關,最符合「大 session 才偶發」。
- **H3 — `--diagnostic` 對照**:若 H2,則 `--diagnostic`(抑制第三方 shell extension)應為其隔離變數;若 H2 成立,延續 PD-024 的歸因管道。
- **H4 — 與 `panedock_launch_smoke.ps1` harness 本身**:測試腳本以 `taskkill`/`Stop-Process` 關閉的時機與 my harness 退出競態,而非產品缺陷。需檢視 `tests/release/launch_smoke.ps1` 的關閉方式。

## 門檻(在此前不接受任何產品修正)

必須先在**已知重現條件**下穩定復現,並以 CDB/debugger 抓出 `0xC0000409` 的 raise stack,才可開修正:

1. 用與 `PD-003` 相同的真實 4-Group/42-tab `session.json`(先備份再測,測後還原),`panedock_launch_smoke` 連續 N 次直到觸發 ≥1 次;每次記錄 `--diagnostic` vs 一般模式。
2. 觸發時以 CDB `!analyze -v` 抓 `STATUS_STACK_BUFFER_OVERRUN` 的 faulting stack;若 fault 在我們 `src/`(`main.cpp`／`explorer_host`／`sidebar`／`core/session`)──依 H1 修正該 buffer;若在 shell32／第三方 extension──依 H2 記錄並轉 `--diagnostic` 對照,不攻殼。
3. 修正後以同一 session 檔案重跑同次數,確認不再觸發。

## Files to read and trace first(調查用)

- `tests/release/launch_smoke.ps1`——harness 如何啟動/關閉 PaneDock。
- `src/app_shell/main.cpp:1089`(`draw_status_bar`,`std::array<wchar_t,256>`)、`:1408`(`size_text`)、`src/explorer_host/explorer_host.cpp`(item counts，`kSelectionSizeItemLimit`)。
- `src/core/session.cpp`——serialize/deserialize 的路徑與任何彙總變數。
- `docs/tickets/PD-003`、`docs/tickets.md` §候選(該候選原始紀錄)、`docs/performance-baseline.md`(量測時曾附的 session.json 特徵)。

## Non-goals(調查期間)

- 不在未取得 repro stack 前修改 `src/*`／`tests/*`／`CMakeLists.txt`。
- 不把「單次重跑通過」當作非問題;也不把「間歇」當作「無法修」的藉口——門檻是拿到 raise 點。
- 不為此新增正式 end-to-end UI 自動化 suite(與 `docs/testing.md` 已否決方向一致)。

## Acceptance(調查完成條件)

1. 取得一次 `0xC0000409` 的 CDB/winver faulting stack(或等價),並標出 fault 落在 `src/` 哪個函式或系統 DLL。
2. 對 H1-H4 各方假說」逐項給出：支持／不支持／未決,附證據。
3. 若 fault 在 `src/`：依其修正並以同一 session 檔案回歸;若在系統 DLL：紀錄為 H2 並轉 `--diagnostic` 對照,不推定產品缺陷。

## 交接區

<!-- 調查 agent 填寫,append-only -->

### 2026-08-30 建票時已補的證據

- 先前證據:PD-003 量測期間(2026-08-29)偶發一次,內容見 `docs/tickets.md` §候選 與 `docs/release-evidence.md`。
- 本次(2026-08-30)在 `PD-124`~`PD-127` 改動後,完整 `ctest` 執行 `panedock_launch_smoke` 時再偶發一次 `0xC0000409`;隨後單獨 `ctest -R panedock_launch_smoke` 與完整 `ctest` 重跑均 6/6 通過。確認與本次改動無關(該崩潰早於本次)。
- 已先掃 `src/` 的固定 stack buffer:`GetWindowTextW` 兩處 buffer=length+1 正確;`std::array<wchar_t,256>`(status bar)與 JSON parser 遞迴深度上限皆無明顯越界。尚未取得 faulting stack,門檻未達成,不修改產品碼。

### 2026-08-30 — close/startup audit 後的重現嘗試

- 使用者明確允許 PaneDock 測試正常寫入 `%LOCALAPPDATA%\PaneDock` 後，先在 elevated context 跑完整 CTest 6/6 PASS，再連續執行 `panedock_launch_smoke` 30 次。
- 結果 30/30 PASS，每輪約 0.81–0.89 秒，沒有 process crash、非零 exit、close timeout 或 `0xC0000409`；因此仍無法取得 CDB faulting stack，Acceptance 1 未達成。
- 依本票既有門檻與 audit evidence contract，不在沒有 raise stack 時猜修固定 buffer，也不把歷史兩次偶發事件抹除。狀態改為 `deferred`；若 launch smoke 再次回傳 `0xC0000409`，立即以本票步驟在同一 session/repro 上掛 CDB，重新轉為 active investigation。
