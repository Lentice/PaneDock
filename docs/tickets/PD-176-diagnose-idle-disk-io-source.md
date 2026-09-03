# PD-176 — 追查閒置 10 分鐘仍有 307294 bytes 磁碟 I/O 的來源（NFR-001 磁碟門檻 FAIL）

Phase 7 · 量測／診斷 · Depends on: PD-003, PD-026

## 來源

本票是 `docs/tickets.md`「候選（尚未開 ticket）」表格中「診斷閒置磁碟 I/O 的來源」一項的正式開票——該候選的觸發條件早已成立（PD-003 已量出實測數字）。2026-09-03 三方稽核（Claude / Codex / OpenCode）在原始碼層面**排除**了 app 自身的 busy-spin 與輪詢計時器，使本票的範圍從「找出我們寫錯的迴圈」收斂為「歸因到 app 之外的來源」。

## 背景與現況

`docs/release-evidence.md:28` 記錄的實測結果：

```
| Idle disk I/O, 10 min | zero bytes | GetProcessIoCounters transfer-byte delta | True | 307294 bytes | FAIL |
```

同檔 `:167` 的判定：

> **FAIL** — the idle disk I/O blocking threshold failed (307294 bytes observed against a zero-byte gate). Idle CPU passed (0.004948% against < 0.1%).

也就是**閒置 CPU 通過（0.004948%）、閒置磁碟 I/O 失敗**。`PD-003`（閒置資源量測基準，已完成）是量測專用票，明確沒有診斷來源。

2026-09-03 稽核在原始碼層面確認的排除結果（三個 agent 一致）：

- 訊息迴圈是阻塞式 `GetMessageW`（`src/app_shell/main.cpp:6469`），不是 `PeekMessage` 空轉。
- 全專案只有兩個 `SetTimer`：session save debounce（`main.cpp:2396`，一次性、到期即 `KillTimer`）與拖曳懸停（`main.cpp:400`，只在拖曳期間存在）。兩者都不是常駐輪詢。
- 唯一的 `Sleep`（`main.cpp:6241` 一帶）只存在於啟動期間有界的 single-instance handshake，不在閒置路徑上。

因此 `AGENTS.md`「Event-driven idle path only. No busy loops, no polling timers.」在**我們自己的程式碼**層面沒有被違反，但 `GetProcessIoCounters` 量到的是**整個 process**的 I/O，包含 process 內載入的原生 Shell 元件與第三方 shell extension 的活動。

## 為什麼這是真的問題

`docs/design-spec.md AC-006`（閒置資源）與 NFR-001 的磁碟門檻是**零 bytes** 才算通過，這是一個 blocking 的發佈閘門：只要這一列是 FAIL，`docs/release-evidence.md` 的整體判定就是 FAIL，發佈閘門依定義關閉。目前的狀態不是「不知道有沒有問題」，而是「已知失敗但不知道原因」——無法判斷這是必須修的缺陷，還是門檻本身需要依實際 Windows Shell 行為調整。兩種結論都需要先歸因。

## Fix 方向

**本票是診斷票，產出是「歸因結論 + 後續決策建議」，不預設一定要改產品程式碼。**

建議的歸因順序（實作者可調整，但需在交接區記錄實際採用的順序與理由）：

1. **用既有的診斷模式二分**：`--diagnostic`（`PD-024` 建立的 `MicrosoftSignedOnly` 政策，會擋掉第三方 shell extension）啟動後重跑同一份閒置量測。若數字大幅下降或歸零，來源就是第三方 extension；若不變，來源在 Windows 自身的 Shell 元件。
2. **用 realize 數量二分**：分別量測「0 個 Group／0 個 live view」、「1 個 pane 1 個 tab」、「4 個 pane」三種狀態的閒置 I/O。若與 live `IExplorerBrowser` 數量正相關，來源就在 Shell view（候選：縮圖快取、`IShellFolder` 變更通知、USN journal 相關監看）。
3. **用資料夾類型二分**：純文字資料夾 vs. 含大量圖片（會觸發縮圖）vs. OneDrive 佔位符資料夾，分別量測。
4. 若前三步指向 Windows Shell 自身的變更通知/快取行為，查證是否有支援的方式抑制（例如 `IExplorerBrowser` 的 `FOLDERSETTINGS` flag、關閉縮圖），並評估抑制的代價是否值得——若不值得或無法抑制，本票的結論就是「提議調整 NFR-001 磁碟門檻的定義」，並必須在 `docs/tickets.md` 依既有規則記錄這個覆寫與其證據。

## 綁定限制（引用）

- `AGENTS.md`：「Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.」
- `AGENTS.md`：「Anything a later session needs must live in the repository, not in a scratchpad handoff...measured numbers go in `docs/performance-baseline.md` or the ticket's 交接區.」——本票的每一組量測數字都必須寫進 repo。
- `AGENTS.md`：「No network, no telemetry, no third-party runtime, no services, no drivers, no admin elevation.」——診斷手段不得引入這些（例如不要為了追 I/O 裝 ETW driver；若必須用 OS 內建工具（如 `Get-Counter`、Resource Monitor）請只在人工診斷階段使用，不要進產品程式碼）。
- `AGENTS.md`：「Do not bundle schema migrations or destructive cleanup into an unrelated change.」

## 檔案與範圍

- `docs/release-evidence.md`（:28、:133、:167）：既有的 FAIL 紀錄與原始 `io_bytes`。
- `docs/performance-baseline.md`：歸因後的數字要填進對應列。
- `docs/tickets/PD-003-idle-resource-baseline.md`：既有量測腳本與方法（重跑要沿用同一套方法，數字才可比較）。
- `docs/tickets/PD-024-diagnostic-mode-suppressing-shell-extensions.md`：`--diagnostic` 的能力邊界。
- `docs/tickets/PD-026-release-evidence-covers-shipped-app.md`：release evidence 的量測對象定義。
- `src/app_shell/main.cpp`：`GetMessageW` 迴圈（:6469 一帶）、兩個 `SetTimer`（:400、:2396）——僅供確認排除結論，預期不需修改。
- `docs/tickets.md`「候選（尚未開 ticket）」表格中對應的那一列（開票後需依既有慣例標記為已開票）。

## Scope

1. 依上述二分法歸因閒置磁碟 I/O 的來源，每一組量測都用 `PD-003` 既有的同一套方法，數字寫進交接區。
2. 給出明確結論：是 (a) 第三方 extension、(b) Windows Shell 自身行為、(c) 我們自己的程式碼（若是，另開修正票或在本票直接修）三者之一。
3. 依結論提出後續建議：修正、抑制、或提議調整門檻定義（若是最後一種，必須說明為什麼零 bytes 門檻對一個宿主原生 Shell view 的應用程式不可達，並附證據）。
4. 更新 `docs/performance-baseline.md` 與（若結論改變判定）`docs/release-evidence.md` 的相關敘述。

## Non-goals

- 不預先假設一定要修產品程式碼。
- 不為了讓數字通過而修改量測方法或放寬門檻——除非第 3 點的結論明確論證且記錄為覆寫。
- 不在本票內處理閒置 CPU（已通過）或記憶體（不同門檻，不同票）。
- 不引入常駐的產品內 I/O 儀器（`docs/tickets.md` 的候選表已就「產品內的計時儀器」記錄過刻意排除的理由；本票的量測應在外部進行）。

## Acceptance Criteria

1. 交接區含至少三組可比較的閒置量測數字（一般模式 vs `--diagnostic`、不同 live view 數量），使用 `PD-003` 的同一套量測方法。
2. 對來源給出明確歸因結論與支撐證據，而非「原因不明」。
3. 依結論明確寫出後續動作建議；若建議另開票，把該候選寫進 `docs/tickets.md`。
4. `docs/performance-baseline.md` 的對應列不再是純估計，含本票量到的數字。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "SetTimer|KillTimer|Sleep\(|PeekMessage|GetMessageW" src/app_shell/main.cpp
```

> **驗證政策提醒：** 閒置量測需要程式維持閒置十分鐘，屬於 Agent 可用非互動方式（啟動 process、等待、讀取 `GetProcessIoCounters`）完成的檢查，沿用 `PD-003` 既有腳本即可；不需要也不應該用 computer-use 工具在量測期間操作 UI（那會直接破壞「閒置」前提）。

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 2026-09-03 量測與歸因

- **結論： (a) 第三方 Shell extension 活動最符合證據。** 這不是對單一 CLSID 的識別；`GetProcessIoCounters` 只提供整個 process 的 transfer-byte delta。受控分界是既有 `--diagnostic`：它在同一 process 套用 `MicrosoftSignedOnly`，阻擋第三方 Shell extension。一般模式的 Three-pane repeat 有 48 bytes，而配對的 diagnostic repeat 是 0；Single 的兩模式都是 0；Four 的兩模式本次都是 0，但 PD-003 先前同方法的正常 Four-pane run 曾量到 307294 bytes。三組 diagnostic 都是 0，與「間歇性的第三方元件活動」一致；沒有證據指向 PaneDock 自身的 idle loop，也沒有證據支持 Windows Shell 自身必然產生這些 bytes。
- **機器與量測環境：** 2026-09-03，Windows build `26200.9278`（registry product string `Windows 10 Pro`, 25H2；既有 evidence 將同一 build 標示為 Windows 11 Pro），12th Gen Intel Core i7-12700K、20 logical processors；evidence script 與六個 PaneDock process 都是未附加 debugger。registry 中可見的第三方 approved Shell extension 為 DropboxExt、7-Zip Shell Extension、TortoiseGit／TortoiseOverlays、UnLockerMenu、AMD Catalyst Context Menu extension、NVIDIA desktop／Play On My TV context-menu extensions；這是安裝清單，不代表每一項都在每次 Shell view 中載入。OneDrive 也存在於既有工作環境，但本輪為 pane-count 對照，不把它當成獨立因果證據。
- **共同方法：** Release `build\\PaneDock.exe`，每次啟動後以 PID 找到 `PaneDockMainWindow`，在 600 秒窗口前先用現有 layout button 選 Single／Three／Four，等待 30 秒讓 Shell 導覽與一次性 session-save settle，再取 `GetProcessIoCounters`、`Process.TotalProcessorTime`、WorkingSet64 與 HandleCount baseline；600 秒後取相同 snapshot，CPU 依 PD-003 的 elapsed／logical-processors 計算，I/O 為 read/write/other transfer-byte delta。關閉用同一個 `WM_CLOSE` 路徑；環境的 session save failure prompt 以 `No` 關閉，未強制終止、未跳過 `IExplorerBrowser::Destroy`。

| Mode | Layout / live views | Elapsed | CPU average | I/O bytes | Working set | Handles | Close | Exit |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| normal | Single / 1 | 600.005 s | 0.003515596% | 0 | 61083648 | 745 | 1.018 s | 0 |
| `--diagnostic` | Single / 1 | 599.996 s | 0.004036483% | 0 | 58544128 | 698 | 1.523 s | 0 |
| normal | Three / 3 | 600.001 s | 0.002343748% | 48 | 61050880 | 773 | 1.210 s | 0 |
| `--diagnostic` | Three / 3 | 600.028 s | 0.004036272% | 0 | 58646528 | 729 | 1.106 s | 0 |
| normal | Four / 4 | 600.013 s | 0.004166577% | 0 | 61644800 | 793 | 1.088 s | 0 |
| `--diagnostic` | Four / 4 | 599.992 s | 0.005338614% | 0 | 58937344 | 749 | 1.267 s | 0 |

- Diagnostic stdout live-view samples were collected for the three diagnostic runs. Single: `0,1,3,1,0,0`; Three: `0,1,3,3,0,0`; Four: `0,1,3,4,0,0`. The final sample was `0` in every case after view destruction. Normal mode intentionally emits no live-view stdout. The startup `0/1/3` samples are startup/layout events; configured pane counts are the post-settle layout selections above.
- Source trace: `src/app_shell/main.cpp` uses blocking `GetMessageW` at the message-loop path, has only the session-save debounce timer and drag-hover timer, and the only `Sleep` is the bounded single-instance handshake. The ticket check `rg -n "SetTimer|KillTimer|Sleep\\(|PeekMessage|GetMessageW" src/app_shell/main.cpp` confirmed no idle busy-spin or polling timer. This rules out (c) PaneDock code as the source of the observed idle I/O at source level; it does not claim that process-total counters can identify an exact external DLL.
- **Follow-up recommendation:** no product-code change and no NFR-001 threshold relaxation in PD-176. Keep the zero-byte gate and retain the existing `--diagnostic` mode as the controlled owner-side comparison. If the exact vendor/CLSID is needed later, use an external OS trace (for example Process Monitor) in a separate investigation; do not add permanent instrumentation, telemetry, timers, or a background thread to PaneDock.
- `docs/performance-baseline.md` now records the PD-176 six-run comparison beside the historic PD-003 307294-byte FAIL. `docs/release-evidence.md` keeps the blocking result FAIL, adds the controlled comparison, and records the external attribution without overwriting the original gate evidence.
