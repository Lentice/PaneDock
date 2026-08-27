# PD-078 — 確保電腦當機/斷電時 session.json 不會毀損

Phase 7 · core · Depends on: PD-006

- Source: 使用者要求(2026-08-27):「要保證電腦突然當機或斷電時,不會造成設定檔/記憶檔毀損」。
- Origin: 使用者原文第 1 項。
- Priority: HIGH——這是使用者資料完整性問題。PD-006 的 Goal 本身就寫著「寫壞一次就是使用者全部 Group 消失」,本票要補的正是這句話還沒完全兌現的部分。

## 已確認的根因(有程式碼證據,不是猜測)

`src/core/session.cpp` 的 `write_session`(第 520-561 行)已經實作了 PD-006 決策 2 要求的「temp 檔 + flush + rename,舊檔先複製為備份」:

```cpp
std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
if (!stream) return false;
stream << serialize_session(document);
stream.flush();
if (!stream) { stream.close(); std::filesystem::remove(temporary, error); return false; }
```

**問題在於 `stream.flush()` 只把 C++ 標準函式庫的使用者空間緩衝區推進作業系統的檔案快取(page cache),不代表資料已經寫進實體磁碟。** 如果電腦在這之後、資料真正落盤之前失去電源:

1. 緊接著的 `std::filesystem::rename(temporary, primary, error)`(第 551 行)本身也沒有要求同步落盤——在 Windows 上,`std::filesystem::rename` 底層對應 `MoveFileExW`,沒有帶 `MOVEFILE_WRITE_THROUGH` 旗標時,rename 這個 metadata 操作本身也可能只存在快取裡。
2. NTFS 的日誌(journal)保證的是**檔案系統結構**在斷電後維持一致(不會出現半個目錄項或損毀的 volume),但**不保證**一個尚未真正落盤的檔案內容,在 rename 指向它之後,斷電還能讀回正確資料——你可能開機後看到 `session.json` 存在、rename 成功,但內容是舊資料、部分資料或零。
3. 第 542-544 行把舊的 `primary` 複製成 `backup` 的 `copy_file` 呼叫同樣沒有要求落盤,備份本身在極端時序下也可能不可靠。

**結論:目前的原子寫入在「rename 是原子的」這一層是對的,但在「rename 指向的資料本身已經真正落盤」這一層沒有保證,兩者都要成立才是真正的斷電安全。**

## 已確認的架構限制(必須先讀,決定本票怎麼落地)

`AGENTS.md`:
> **Keep `src/core` free of HWND, COM and `windows.h`.**

真正讓資料落盤的呼叫是 Win32 的 `FlushFileBuffers`(或等效的 `_commit`/`fsync` 概念),但這是 `windows.h` 底下的 API,**不能直接寫進 `src/core/session.cpp`**——那會違反上面這條硬規則,也會讓 `core` 失去它作為本專案唯一自動測試 seam 的價值(`docs/testing.md`)。

**這是本票的核心技術決策,必須由實作 agent 解決,不是繞過:** `write_session` 的「temp 寫入 → 落盤 → 備份 → rename → (失敗時)復原」這個順序邏輯必須留在 `core`(可測試、平台無關),但「真正讓位元組落盤」這一步需要一個平台相依的落盤呼叫。建議方向(不是強制,實作 agent 決定並在交接區寫理由):

- 讓 `write_session` 接受一個可注入的「落盤」callback/函式指標(例如 `std::function<bool(const std::filesystem::path&)>` 或等效簽章),預設值在測試環境下是 no-op 或直接回傳 `true`(維持現有測試可跑),由 `app_shell`(本來就允許碰 `windows.h`)在啟動時注入一個真正呼叫 `FlushFileBuffers`(對 temp 檔案的 handle)的實作。
- 或者:`core` 只負責產生「要寫哪些檔案、用什麼內容、什麼順序」的**計畫**(一個資料結構或一組回呼點),實際 I/O 呼叫序列由 `app_shell` 執行,`core` 保留邏輯測試用的抽象檔案系統介面。

兩種都要滿足:**core 的邏輯順序(先落盤 temp、再落盤/複製 backup、最後 rename,任何一步失敗都不得讓 primary 進入半寫入或不一致狀態)必須能在不碰 `windows.h` 的前提下被單元測試涵蓋**;**真正落盤的動作必須確實被呼叫且回傳值被檢查,不能靜默忽略失敗**。

## 已確認的產品決策

1. **落盤時機:temp 檔內容寫完、呼叫落盤成功之後,才能進行 rename。** rename 前,「這份資料已經在磁碟上」必須是真的,不能只是「已經流進 OS 快取」。
2. **backup 複製也要落盤,不只是 `copy_file` 回傳成功。** 現有第 542-548 行的 backup 複製邏輯同樣要接上落盤步驟,否則備份本身在斷電情境下也不可靠,違背 PD-006 決策 4「退回備份」的前提假設。
3. **落盤呼叫失敗必須視為寫入失敗**,走現有的 `false` 回傳路徑並清掉 temp 檔,不得吞掉錯誤繼續 rename——那樣等於沒修。
4. **不追求「絕對不可能遺失最後一次尚未完成的寫入」**(那是不可能的物理極限,斷電永遠可能發生在寫入這一刻本身),**本票的目標是「rename 完成後,primary 檔案的內容必須是完整、可解析、且通過 PD-004 不變式驗證的最後一次成功寫入結果,不會是半寫入或損毀的資料」。** 也就是說:PD-006 決策 4 的「主檔 → 備份 → 預設狀態」讀取失敗順序必須永遠有一個可用的結果,不會出現主檔和備份同時損毀的狀況(除非兩次寫入之間都遇到斷電,那是本票明確排除的極端情境)。
5. **不改變 `write_session` 的對外簽章語意**(呼叫端仍然只關心成功/失敗),落盤機制是內部實作細節。若最終方案需要改簽章(例如新增注入參數),必須是向後相容的新增,不是破壞性修改。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.

`AGENTS.md`:
> **Keep `src/core` free of HWND, COM and `windows.h`.** It is the only automated test seam in this project (`docs/testing.md`); every type that leaks into it costs that seam.

`AGENTS.md`:
> **Every persisted config/setting file must be designed for forward extensibility.** ……這條規則適用於本票要改的同一個檔案,本票不得破壞既有的 schema version/未知欄位保留設計(若 PD-013/PD-006 已經有相關決策,不重新討論)。

`docs/tickets/PD-006-session-document-persistence.md` 已確認的產品決策 #2:
> 寫入方式為原子替換:寫到同目錄的暫存檔、`flush`、然後 rename 覆蓋。舊檔在 rename 前先複製為備份。**絕不原地覆寫。**

(本票是這條決策的延伸與補強,不是推翻——「flush」在 PD-006 開票當時指的是 C++ streaming 語意,本票要把它補強到「真正落盤」的語意。)

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

## Files to read and trace first

- `src/core/session.cpp` 第 520-561 行(`write_session`)——本票要修改的核心函式。
- `src/core/session.h`——`kSessionFileName`/`kSessionBackupFileName`/`kSessionTemporaryFileName` 常數,以及 `write_session` 的簽章。
- `src/core/model.h` 第 67 行——`schema_version` 欄位,確認本票不動它的語意。
- `docs/tickets/PD-006-session-document-persistence.md`——完整交接區,原始決策脈絡。
- `docs/tickets/PD-013-config-file-extensibility-convention.md`——設定檔可擴充性慣例,確認本票不違反。
- `docs/tickets/PD-025-crash-recovery-path.md`——「不乾淨關閉」偵測與退回備份的 UI 告知邏輯,確認本票不重複也不破壞它(PD-025 處理的是「偵測到不乾淨關閉後怎麼辦」,本票處理的是「讓寫入這個動作本身在斷電當下不會產生半毀損的檔案」,兩者互補但範圍不同)。
- 呼叫 `write_session` 的所有地方(`src/app_shell/main.cpp` 的 `save_now` 等)——確認注入點/呼叫端不需要改變行為。

## Scope

1. 修改 `src/core/session.cpp` 的 `write_session`,在 temp 檔寫入完成與 backup 複製完成之後、rename 之前,確保資料已經真正落盤(不只是 stream flush)。
2. 若需要注入平台相依的落盤呼叫,設計一個不引入 `windows.h` 到 `core` 的介面(函式指標/`std::function`/呼叫端提供的介面等),並在 `app_shell` 提供使用 `FlushFileBuffers` 的真實實作。
3. 落盤失敗時,`write_session` 必須回傳 `false` 並清掉暫存檔,不得讓 rename 在落盤失敗後仍然執行。

## Non-goals

- 不處理「寫入這個動作本身正在進行時斷電」這種物理上無法避免的極端情境(見產品決策 4)。
- 不新增多份備份版本(PD-006 決策 3 已定案,一份備份)。
- 不改變 schema version 或欄位語意。
- 不改 PD-025 的不乾淨關閉偵測與 UI 告知邏輯。
- 不引入 SQLite 或任何交易式資料庫——這是單一小型 JSON 檔案,`AGENTS.md` 明確要求先用標準函式庫/平台既有能力。
- 不新增 crash counter、log 檔或 minidump(PD-025 已明確排除,本票沿用該排除)。

## Acceptance

1. `write_session` 在 rename 之前,對 temp 檔案內容呼叫一個會實際落盤的機制(非僅 `std::ofstream::flush()`),且該呼叫的失敗會被檢查並導致寫入失敗。
2. backup 複製同樣接上落盤機制。
3. `core` 內新增/修改的邏輯(順序、失敗路徑、注入點)可以在不含 `windows.h` 的環境下被單元測試涵蓋——即使無法用自動測試「證明」實體磁碟已寫入(那需要真的斷電或核外部工具),也必須能測試「落盤呼叫是否真的在 rename 之前被呼叫」「呼叫失敗時是否真的中止並回傳 false」這兩件事(例如用可注入的 fake 落盤函式,驗證呼叫順序與失敗傳播)。
4. `app_shell` 提供的真實落盤實作使用 `FlushFileBuffers`(或功能等效的 Win32 呼叫),回傳值被檢查。
5. 既有 `write_session`/`read_session` 的呼叫端(`save_now` 等)行為不變,不需要修改呼叫方式(除非注入點設計需要,若需要必須向後相容)。
6. 既有 CTest 全數通過,且新增至少一個 focused test 涵蓋本票新增的落盤呼叫順序/失敗路徑邏輯。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
8. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "FlushFileBuffers|write_session|stream.flush" src\core\session.cpp src\core\session.h src\app_shell\main.cpp
git diff --check
```

## Handoff requirements

- 最終選擇的注入/分層方案(callback 簽章、或 core 產生寫入計畫由 app_shell 執行等),以及為何選它而不是另一個候選方案。
- `core` 端新增的 focused test 內容與涵蓋範圍,以及為什麼這樣的測試足以驗證「落盤呼叫順序正確、失敗會傳播」,即使無法自動化驗證實體磁碟落盤本身。
- `app_shell` 端 `FlushFileBuffers` 呼叫的實際位置與回傳值處理方式。
- 若發現除了 `session.json` 之外還有其他跨啟動存活的持久化檔案(目前判斷只有 session.json/.bak/.tmp 這一組,若交接時發現有遺漏必須記錄)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
