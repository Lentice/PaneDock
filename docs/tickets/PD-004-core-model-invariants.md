# PD-004 — 建立 `core` 的 Group／pane／tab 資料模型與其不變式

Phase 1 · core · Depends on: PD-001

- Source: `AGENTS.md`、`docs/design-spec.md` §FR-001／§FR-002／§FR-003／§FR-005／§9.1、`docs/development.md`、`docs/testing.md`
- Origin: 2026-08-20 專案建立。`core` 是本專案唯一的自動測試 seam(`docs/testing.md`),PD-005 與 PD-006 都依賴本 ticket 定下的型別。
- Priority: **HIGH**——Phase 1 的根。型別一旦被其他模組引用,改動成本急升。

## Goal

在 `src/core/` 建立 Group／pane／tab 的資料模型與變更操作,並以測試釘住它的不變式。

本 ticket 的關鍵約束不是功能,而是**邊界**:`core` 不得含任何 HWND、COM 型別或 `windows.h`。這條規則是承載結構的——它是全案唯一能自動測試的地方,一個洩漏進來的 COM 型別就毀掉這個 seam,且事後很難便宜地拆掉。

## 已確認的產品決策

1. `core` 只描述狀態,不執行任何 Shell 動作。「導覽到某個 location」在 `core` 裡就只是改一個欄位;真正的 `BrowseToObject` 由 `explorer_host` 負責。
2. Shell location 在 `core` 內是**值**,不是 COM 物件。以 parsing name ＋ known-folder identity ＋ fallback path 三者組成的純資料型別表示(§10)。`core` 不解析、不驗證這個值能不能對應到真實位置——那是 `shell_core` 的事。
3. 版型是封閉的列舉,恰五個值(§FR-003)。不設「自訂」或「其他」成員,不預留擴充點。
4. 序列化**不在**本 ticket 範圍(歸 PD-006),但型別設計必須讓序列化不需要反射或侵入式修改。
5. 版型矩形計算**不在**本 ticket 範圍(歸 PD-005),但本 ticket 定下版型列舉與分隔比例的表示方式。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> **Keep `src/core` free of HWND, COM and `windows.h`.** It is the only automated test seam in this project (`docs/testing.md`); every type that leaks into it costs that seam.

`docs/development.md`:
> The `core` boundary is load-bearing, not stylistic. It is the only automated test seam in the project (`docs/testing.md` §Single seam). A COM type that leaks into `core` costs the seam and cannot be undone cheaply.

`docs/design-spec.md` §FR-003:
> 支援且僅支援五種版型:單一、左右、上下、三分割、四宮格。切換版型時,若新版型的 pane 數較少,超出的 pane 之 tab 依序併入保留的 pane;若較多,新增的 pane 以預設 location 開啟一個 tab。

`docs/design-spec.md` §FR-005:
> 每個 pane 至少一個 tab。可新增、關閉、切換 tab。關閉 pane 的最後一個 tab 時,該 tab 導覽至預設 location 而非留下空 pane。

`docs/design-spec.md` §10:
> **不得寫入 PIDL 或 COM 指標。** 持久化的 identity 為 parsing name ＋ known-folder identity ＋ fallback path。display name 永不作為 identity。

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`CONTEXT.md` 的詞彙為強制:型別與函式命名一律使用 Group／pane／tab／layout template／Shell location,不得使用 workspace、window、split 等 _Avoid_ 詞。

## Files to read and trace first

- `CONTEXT.md` — 全部術語。命名不得偏離。
- `docs/design-spec.md` §FR-001～§FR-005、§9.1 模組責任表、§10 資料儲存
- `docs/testing.md` §Single seam — 本 ticket 要測什麼、為何只測 `core`
- PD-001 的 `## 交接區` — 原型實際遇到的 view 生命週期時序,可能影響 realize 狀態該不該進模型

## Scope

1. 建立 `src/core/` 與其 CMake 目標。目標的 include 路徑不得含任何需要 `windows.h` 的目錄。
2. 定義型別:
   - `ShellLocation` — parsing name、known-folder identity、fallback path 三個欄位的純值型別
   - `LayoutTemplate` — 恰五個值的列舉
   - `TabState` — id、`ShellLocation`、view mode、排序欄位與方向
   - `PaneState` — id、tab 集合、active tab id
   - `GroupState` — id、名稱、`LayoutTemplate`、分隔比例、pane 集合、active pane id
   - `ApplicationState` — schema version、Group 集合、active group id、視窗位置
3. 不變式,以測試釘住:
   - `GroupState` 的 pane 數恰等於其 `LayoutTemplate` 所要求的數量
   - 恰有一個 active pane,且該 id 存在於 pane 集合中
   - 每個 pane 恰有一個 active tab,且該 id 存在於其 tab 集合中
   - 每個 pane 至少一個 tab
   - 分隔比例的數量與 `LayoutTemplate` 相符,每個值落在 0.0–1.0
   - id 在其作用範圍內唯一
4. 變更操作,每個都維持全部不變式:
   - Group:新增、重新命名、複製(複製完整狀態但產生新 id)、刪除、重新排序
   - 版型切換:依 §FR-003 的規則搬移 tab——pane 數減少時超出的 pane 之 tab 依序併入保留的 pane;增加時新 pane 以預設 location 開一個 tab
   - tab:新增、關閉、設為 active。關閉 pane 最後一個 tab 時導覽至預設 location 而非移除 tab
   - pane:設為 active
5. 刪除操作後的參照完整性:刪除 Group 後 active group id 必須指向仍存在的 Group,或在集合為空時有明確定義的狀態。
6. 測試以 `tests/unit/` 下的獨立可執行檔實作,依 `tests/CMakeLists.txt` 的 table-driven 註冊方式加入。

## Non-goals

- 不實作序列化、schema 遷移或檔案 I/O(歸 PD-006)。
- 不實作版型矩形計算(歸 PD-005)。
- 不實作任何 UI、任何 HWND、任何 Shell 呼叫。
- 不驗證 `ShellLocation` 是否指向真實存在的位置——`core` 不做解析。
- 不為將來可能的版型預留擴充點;列舉是封閉的五個值。
- 不引入任何第三方程式庫,含 JSON 程式庫(PD-006 再評估)。
- 不為 `core` 加介面或抽象層以「方便替換實作」——只有一個實作。
- 不實作 undo/redo。Spec 未要求。

## Acceptance

1. `src/core/` 編譯通過,且不含 `windows.h`、HWND 或任何 COM 型別。
2. 六個型別皆已定義,命名符合 `CONTEXT.md`。
3. Scope 3 的六類不變式各有測試,且每個測試在不變式被破壞時會失敗(以刻意破壞驗證過一次)。
4. Scope 4 的全部變更操作有測試,斷言的是結果狀態而非內部呼叫序列。
5. 版型切換的 tab 搬移行為在「pane 數減少」與「pane 數增加」兩個方向都有測試,含四宮格→單一(三個 pane 的 tab 併入一個)這種最極端的情形。
6. 刪除最後一個 Group 後的狀態有明確定義且有測試。
7. `ctest` 全數通過,且新測試已依 table-driven 方式註冊。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build -R "core" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "windows\.h|HWND|IUnknown|ComPtr|CComPtr|_COM_|IShell" src\core
# 預期:無命中。任何命中都是 seam 洩漏,必須修掉而不是註解掉。
rg -n "workspace|split configuration|file list" src\core
# 預期:無命中。CONTEXT.md 的 _Avoid_ 詞彙不得出現在識別字中。
git diff --check
```

## Handoff requirements

交接時記錄:

- 六個型別的最終形狀(欄位與型別),供 PD-005／PD-006 直接引用。
- 分隔比例的表示方式與 `LayoutTemplate` 的對應關係——PD-005 依賴這個決定。
- 版型切換時 tab 搬移的確切規則,含四宮格→單一的行為。
- 刪除最後一個 Group 後的定義狀態。
- 新增的測試名稱與其在 `tests/CMakeLists.txt` 的註冊位置。
- 任何為了維持 `core` 的 COM-free 邊界而做的妥協,以及該妥協把什麼責任推給了哪個模組。

## 交接區

<!-- 實作 agent 填寫,append-only -->
