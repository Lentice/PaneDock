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
