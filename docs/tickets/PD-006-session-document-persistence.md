# PD-006 — 實作 session document 的序列化、原子寫入、備份退回與 schema 遷移

Phase 1 · core · Depends on: PD-004

- Source: `AGENTS.md`、`docs/design-spec.md` §FR-011／§FR-013／§10、`docs/development.md`
- Origin: 2026-08-20 專案建立。排在 PD-005 之後:本 ticket 會定下 schema,而版型欄位是 schema 的一部分,順序顛倒會造成一次無謂的遷移。
- Priority: **MEDIUM**——使用者資料的正確性。寫壞一次就是使用者全部 Group 消失。

## Goal

把 `ApplicationState` 存成版本化 JSON 並讀回,做到:寫入絕不留下半個檔案、損壞時能退回上一個良好版本、schema 變更時能遷移。

本 ticket 的難點不在 JSON,而在**失敗路徑**。正常存讀很容易寫對;真正會傷到使用者的是斷電時的半寫入、磁碟滿、檔案被防毒鎖住、以及舊版本的檔案遇到新版本的程式。這些是本 ticket 的主要範圍。

## 已確認的產品決策

1. 從第一個版本就帶 schema version 並支援遷移。理由:事後補遷移機制需要處理「沒有版本號的檔案」,那比一開始就有更麻煩。
2. 寫入方式為原子替換:寫到同目錄的暫存檔、`flush`、然後 rename 覆蓋。舊檔在 rename 前先複製為備份。**絕不原地覆寫。**
3. 保留**一個**備份版本,不是多個。理由:多版本備份需要清理策略與命名規則,而使用者要的是「不要弄丟我的 Group」,一個備份就達成了。要改成多版本需先有實際的資料遺失案例。
4. 讀取失敗時的順序:主檔 → 備份 → 預設狀態。退回備份時必須讓上層有辦法知道發生了退回(§FR-013 要求 UI 告知),因此讀取結果要能表達「成功但用的是備份」。
5. JSON 程式庫:**先評估手寫**。本 schema 是封閉的、欄位固定、無需通用解析。若手寫的讀寫加測試明顯超過引入程式庫的成本,才引入,並在交接區寫出比較依據。這是 `AGENTS.md`「Reach for the standard library and Win32 before adding a dependency」的直接套用。
6. **不得序列化 PIDL 或任何 COM 型別**(§10)。`ShellLocation` 是三個字串欄位,這是它一開始就設計成純值型別的原因。
7. 檔案 I/O 在 `core` 內以標準程式庫完成(`std::filesystem`、`std::ofstream`),不呼叫 Win32 檔案 API——那會破壞 `core` 的 COM-free／`windows.h`-free 邊界。原子 rename 由 `std::filesystem::rename` 提供。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §10:
> - 位置:`%LOCALAPPDATA%\PaneDock`
> - 格式:版本化 JSON,含 schema version,自首個版本即支援遷移
> - 寫入:原子替換(temp 檔加 rename),保留上一版為備份
> - **不得寫入 PIDL 或 COM 指標。** 持久化的 identity 為 parsing name ＋ known-folder identity ＋ fallback path。display name 永不作為 identity。

`docs/design-spec.md` §FR-013:
> 崩潰後重新啟動復原到可用狀態。session document 無法解析時退回上一個良好版本,並在 UI 告知已退回。

`AGENTS.md`:
> All user data lives under `%LOCALAPPDATA%\PaneDock`. Write by atomic replace (temp file plus rename) with the previous version retained; never overwrite in place.

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`AGENTS.md`:
> **Keep `src/core` free of HWND, COM and `windows.h`.**

`docs/testing.md`:
> **Session serialization** — round-trip fidelity, schema migration from an older version, graceful handling of a truncated or corrupt document.

## Files to read and trace first

- PD-004 的 `## 交接區` — 六個型別的最終欄位形狀,即 schema 的內容
- PD-005 的 `## 交接區` — 分隔比例的表示方式,它是 schema 的一部分
- `src/core/` — 既有型別
- `docs/design-spec.md` §10、§FR-011、§FR-013
- `tests/CMakeLists.txt` — table-driven 測試註冊方式

## Scope

1. 定義 schema version 常數與 JSON 的欄位名稱。欄位名稱一律使用 `CONTEXT.md` 的詞彙(`groups`、`panes`、`tabs`、`layout_template`、`shell_location`)。
2. 序列化:`ApplicationState` → JSON 字串。
3. 反序列化:JSON 字串 → `ApplicationState`,並在完成後驗證 PD-004 的全部不變式。**不變式驗證是反序列化的一部分**:一個通過 JSON 解析但違反不變式的檔案(例如四宮格版型只有三個 pane)必須被視為損壞,而不是載入一個壞掉的狀態。
4. 原子寫入:
   - 若主檔存在,先複製為備份
   - 寫到同目錄的暫存檔
   - flush 並關閉
   - `std::filesystem::rename` 覆蓋主檔
   - 任何一步失敗:清掉暫存檔,不動主檔,回報失敗
5. 讀取:主檔 → 備份 → 預設狀態。回傳結果須能區分這三種來源,以滿足 §FR-013 的「UI 告知」。
6. 遷移:一個以 schema version 為輸入的遷移路徑。本版本只有 version 1,因此遷移函式的內容是「version 1 直接通過,未知版本視為損壞」。**框架必須在位**,即使目前無事可做。
7. 路徑解析:`%LOCALAPPDATA%\PaneDock` 的取得。此處有一個邊界問題——取得 known folder 路徑需要 Win32(`SHGetKnownFolderPath`)。解法:`core` 的 API 接收一個已解析的目錄路徑作為參數,由呼叫端(`app_shell`)負責取得。`core` 不查詢 known folder。
8. 測試涵蓋:
   - 往返保真:完整的 `ApplicationState` 存後讀回相等
   - 空狀態(零個 Group)的往返
   - 截斷的檔案(JSON 中途被切斷)→ 視為損壞
   - 格式正確但違反不變式的檔案 → 視為損壞
   - 未知 schema version → 視為損壞
   - 主檔損壞、備份良好 → 讀到備份,且結果標示來源為備份
   - 主檔與備份皆損壞 → 讀到預設狀態
   - 主檔不存在(首次啟動)→ 讀到預設狀態,且**不**標示為損壞退回
   - 寫入時暫存檔無法建立 → 主檔保持原狀
   - 完全不含 PIDL 或二進位 blob 的序列化輸出

## Non-goals

- 不實作設定(settings)的持久化。Spec 未定義設定,側邊欄寬度等全域值待有 ticket 再處理。
- 不保留多個備份版本(見已確認的產品決策 3)。
- 不做加密、壓縮或混淆。
- 不做檔案鎖或多 process 併發存取保護——本產品是單一使用者單一實例。
- 不在 `core` 內呼叫 `SHGetKnownFolderPath` 或任何 Win32 API。
- 不實作 UI 的「已退回備份」提示——本 ticket 只讓結果能表達這件事。
- 不序列化 best-effort 狀態中無法以純值表示的部分。選取項目若最終保留(視 PD-002 判定),以 parsing name 字串陣列表示。
- 不為將來的 schema 版本預先寫遷移程式碼。

## Acceptance

1. 完整 `ApplicationState` 的往返保真有測試。
2. 反序列化會驗證不變式,且違反不變式的合法 JSON 被視為損壞,有測試。
3. 原子寫入:暫存檔建立失敗時主檔保持原狀,有測試。
4. 備份在每次成功寫入前產生,有測試。
5. 讀取的三段退回(主檔 → 備份 → 預設)各有測試,且回傳結果可區分來源。
6. 首次啟動(主檔不存在)不被誤判為損壞退回,有測試。
7. 截斷檔案與未知 schema version 各有測試。
8. 序列化輸出為純文字 JSON,不含二進位資料,有測試確認。
9. `src/core` 內無 `windows.h`、無 Win32 呼叫。
10. 若引入了 JSON 程式庫,交接區有與手寫方案的成本比較。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R "session|persist" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "windows\.h|SHGetKnownFolderPath|CreateFileW|ITEMIDLIST|PIDL" src\core
# 預期:無命中
rg -n "ofstream|filesystem::rename" src\core
# 預期:命中在持久化實作內,確認用的是標準程式庫而非 Win32
git diff --check
```

## Handoff requirements

交接時記錄:

- schema version 1 的完整欄位清單與 JSON 欄位名稱,供後續遷移對照。
- 是否引入 JSON 程式庫。若有,寫出與手寫方案的成本比較;若無,寫出手寫實作的行數與測試數,作為將來重新評估的基準。
- 原子寫入的確切步驟順序,以及每一步失敗時的狀態。
- 讀取結果表達「來源為主檔／備份／預設」的方式,供 `app_shell` 實作 §FR-013 的 UI 告知。
- 反序列化時不變式驗證的實際行為:是全部驗證後才回報,還是遇到第一個違反就停。
- `%LOCALAPPDATA%` 路徑由呼叫端傳入的 API 形狀。
- 新增的測試名稱與註冊位置。
- 依 `AGENTS.md`「Every persisted config/setting file must be designed for forward extensibility」:目前封閉、固定欄位的 schema 是否需要為「反序列化時遇到不認識的欄位,寫回時原樣保留」預留設計(例如把未知 JSON key 存進一個 side-map)。若判斷不需要,寫出理由。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-24 — PD-006 實作

- 新增 `src/core/session.h`／`session.cpp`。schema version 常數為 `1`；v1 根物件欄位為 `schema_version`、`groups`、`active_group_id`、`window_placement`。`window_placement` 為 `x`、`y`、`width`、`height`、`maximized`；每個 Group 為 `id`、`name`、`layout_template`、`divider_ratios`、`panes`、`active_pane_id`；每個 pane 為 `id`、`tabs`、`active_tab_id`；每個 tab 為 `id`、`shell_location`、`view_mode`、`sort_column`、`sort_ascending`；`shell_location` 為 `parsing_name`、`known_folder_identity`、`fallback_path`。五個 `layout_template` 字串與列舉同名：`single`、`left_right`、`top_bottom`、`three_pane`、`four_pane_grid`。
- 未引入 JSON 程式庫。手寫實作為 `session.cpp` 約 530 行（含完整 JSON value/parser/writer、UTF-8/UTF-16 轉換、schema 映射、遷移入口與檔案 I/O），聚焦測試約 131 行、5 個 test function 覆蓋 scope 指定的 10 個情境及未知欄位保留。相較之下，引入通用 JSON 套件會新增並維護第三方原始碼／授權／更新面；目前封閉 schema 只需要這一個 translation unit，故維持零相依。若 schema 成長到第二個持久化文件或 parser 維護成本實際超過套件治理成本，再重評。
- 原子寫入順序：建立呼叫端提供的目錄、清除同名 stale temp、以 `std::ofstream` binary/truncate 寫入 `session.json.tmp`、`flush` 並關閉；若 `session.json` 已存在，以 `copy_file(...overwrite_existing)` 產生單一 `session.json.bak`；最後以 `std::filesystem::rename` 將同目錄 temp 原子替換主檔。LLVM-MinGW 的實測可直接替換既有目標。建立目錄、開啟／寫入／flush、查詢主檔、備份複製或 rename 任一步失敗皆回傳 `false` 並清除 temp；rename 失敗時主檔未被事先刪除，仍保持原狀。備份可能已更新為該主檔內容，這正是下一次讀取可用的上一個良好版本。
- `SessionReadResult` 以 `SessionSource::{primary, backup, default_state}` 表達資料來源，另以 `recovered_from_corruption` 區分「因既有損壞檔退回」與「首次啟動、檔案不存在」。`app_shell` 只需在來源為 backup 或該旗標為真時決定 FR-013 的英文 UI 提示；本票未加入 UI。
- 反序列化先完成 JSON 型別與必填欄位檢查、version 1 migration step，再組成六個 core 型別，最後呼叫 PD-004 的 `is_valid(ApplicationState)`；遇到第一個解析、型別、版本或模型不變式錯誤即停止並回傳 `std::nullopt`，不會暴露部分狀態。未知 schema version 由 migration `switch` 拒絕；version 1 是明確 no-op 分支，未預寫未來 migration。
- `%LOCALAPPDATA%` 不在 core 解析。`write_session(const std::filesystem::path& directory, ...)` 與 `read_session(const std::filesystem::path& directory, ...)` 均由呼叫端傳入已解析的 PaneDock 目錄；檔名由 `kSessionFileName`、`kSessionBackupFileName`、`kSessionTemporaryFileName` 固定。
- 依 PD-013，未知 JSON **欄位需要且已實作保留**。`SessionDocument::preserved_json` 保存成功載入的原文件；寫回時先解析它，再覆寫所有 v1 已知欄位。根物件、window placement、Group、pane、tab、Shell location 的未知 key 均原樣保留；集合成員以既有 `id` 配對，重新排序不會把未知欄位移到別的 Group/pane/tab。新建或已刪除的成員不繼承／保留舊成員欄位。此設計沒有修改 PD-004 的六個產品型別，也沒有把 JSON DOM 暴露到公開 API。
- 新測試為 `tests/unit/core_session_test.cpp`，在 `tests/CMakeLists.txt` 的 `PANEDOCK_TESTS` 表註冊為 `panedock_core_session`。涵蓋完整與空狀態往返、截斷 JSON、合法 JSON 違反 pane 數不變式、未知 version、primary 損壞轉 backup、兩份皆損壞轉 default、首次啟動不誤報 corruption、temp 無法建立時主檔不變、純文字且無 PIDL/blob、每次成功覆寫前的單一備份，以及未知欄位寫回保留。
- Agent checks（Release，LLVM-MinGW/Ninja）：`cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release` 成功；`cmake --build build` 成功；`ctest --test-dir build -R 'session|persist' --output-on-failure` 1/1 通過；完整 `ctest --test-dir build --output-on-failure` 3/3 通過；Win32/PIDL leakage `rg` 無命中；`ofstream|filesystem::rename` `rg` 僅命中 `src/core/session.cpp` 的標準函式庫 I/O；`git diff --check` 通過。

### 2026-08-24 驗證

獨立重跑 build 與完整 `ctest`(3/3 通過)、兩個邊界 `rg`、`git diff --check`,結果一致。已讀 `src/core/session.h`／`session.cpp`／`tests/unit/core_session_test.cpp` 全文:手寫 JSON parser/writer 正確處理跳脫字元、UTF-16 surrogate pair、UTF-8/UTF-16 互轉;原子寫入的溫、備、主檔順序與失敗清理正確;`read_session` 的三段退回與 `recovered_from_corruption` 語意經逐案核對與 §FR-013 一致(首次啟動不誤判、單一檔損壞轉移、兩者皆損壞轉預設均正確標示來源)。`test_atomic_write_and_backup` 用「在 temp 路徑預先建立同名目錄」這個手法逼 `std::ofstream` 開檔失敗,是驗證 AC3(暫存檔建立失敗時主檔不變)的乾淨做法。未知欄位保留(PD-013 慣例)以 `preserved_json` 往返並用 `id` 配對集合成員,測試 `test_unknown_fields_survive_write_back` 直接證明根層與巢狀 `shell_location` 的未知欄位都能存活寫回。判定為完成。
