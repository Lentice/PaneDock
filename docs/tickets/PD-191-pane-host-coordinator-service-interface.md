# PD-191 — `PaneHost`：把協調層服務抽成介面，讓 `Pane` 能持有 pane 自己的行為

Phase 7 · architecture · Depends on: PD-190

- Source: 2026-09-05 使用者原話「我希望 main 中不要有屬於 pane 中的獨立功能，所有 pane 的獨立功能都應該搬到 pane 之中，或是另外的物件歸屬在 pane 的掌控之下。main 控制程式的初始化、layout、groups，pane 控制個別的檔案瀏覽功能。」
- Priority: HIGH——PD-192～PD-197 全部依賴本票，單獨完成沒有可見價值。

## 為什麼需要這張票（本票覆寫 PD-190 的一項判定）

PD-190 交接區的判定是：「六支單一 pane 的刷新函式中，**需要協調層服務的維持自由函式，改吃 `Pane&`**」。同樣的判定也擋住了其餘約 2,900 行 pane-scoped 程式碼。

2026-09-05 的全檔盤點顯示：**這些函式留在 `main.cpp` 的理由完全一致，而且只有四項**——

| 服務 | 出現形式 | 為什麼 `Pane` 現在拿不到 |
|---|---|---|
| 關閉閘 | `if (state.closing_ || state.shutdown_deferred) return;` | `closing_`／`shutdown_deferred` 是 `AppState` 欄位 |
| Shell 呼叫重入計數 | `ShellCallScope shell_call(state);` | `ShellCallScope` 建構子吃 `AppState&` |
| session 存檔排程 | `schedule_session_save(state);` | 讀 `state.main_window` 與 timer |
| 目前 Group | `active_group(state)` / `unique_tab_id(group, ...)` | Group 是協調層的域 |

外加 tab strip 群另外需要三項：`state.chrome_font`、`state.layout_tooltip`、以及**跨 pane 的** `state.tab_drag`。

新證據：使用者 2026-09-05 明示界線是「main 管初始化／layout／groups，pane 管個別檔案瀏覽功能」。這條界線把「pane 的行為」放在 `Pane`，而不是放在一群吃 `Pane&` 的自由函式。PD-190 的判定在當時是正確的（當時沒有介面可用），本票補上那個介面。

**本票不覆寫** 2026-09-03 模組契約 (1) 的精神：`Pane` 仍然看不到 `AppState`、看不到別的 `Pane`、看不到 Group 域模型。它只看到一個窄介面。

## Outcome

1. 新增 `src/app_shell/pane_host.h`：純虛介面 `PaneHost`，只暴露上表七項服務。
2. `AppState` 實作 `PaneHost`。
3. `Pane` 新增 private 成員 `PaneHost* host_{nullptr}` 與 `void set_host(PaneHost*)`，在 `create()` 之前由協調層設定。
4. **零函式搬移、零行為變更。** 本票只建介面並接線；`main.cpp` 的自由函式一支都不動（唯一例外見 Scope 2 最後一項的純提取）。搬移由 PD-192～PD-197 逐群進行。

之所以刻意不在本票搬任何函式：介面形狀若錯，錯誤只會停在一個新檔案裡，而不是散進 2,900 行的搬移 diff。

## Scope

### 1. `src/app_shell/pane_host.h`

介面內容（`namespace panedock::app_shell`）：

- `class Pane;` 前置宣告。
- `class ShellCall final` — 建構呼叫 `host->shell_call_entered()`，解構呼叫 `host->shell_call_left()`；不可複製。等同既有 `ShellCallScope` 的 RAII 形狀，但不暴露 `AppState`。
- `class PaneHost`，恰好七支純虛方法：
  - `virtual bool is_shutting_down() const noexcept = 0;` — 即 `state.closing_ || state.shutdown_deferred`，今天約 20 支 pane 函式的第一行守衛。
  - `virtual void shell_call_entered() noexcept = 0;`
  - `virtual void shell_call_left() noexcept = 0;`（兩者只透過 `ShellCall` 呼叫，不直接用）
  - `virtual void schedule_session_save() noexcept = 0;`
  - `virtual const std::string &active_group_id() const noexcept = 0;` — 供 `NavigationRequest` 身分（PD-170）使用；無 active Group 時回傳空字串。
  - `virtual std::string make_unique_tab_id() const = 0;` — 唯一性範圍是 Group，那是協調層的域而非 pane 的。
  - `virtual std::optional<TabStripDragLayout> tab_drag_layout(const Pane &pane, HWND strip, int min_width, int max_width, int text_reserve) const = 0;` — 跨 pane tab 拖曳依 2026-09-03 契約 (3) 留在協調層；由協調層回答「這個 pane 的 strip 現在要不要排一個佔位、多寬」，包含被拖的 tab 屬於**別的** pane 的情形（那個 pane 的標籤，提問的 pane 永遠不得直接讀）。

需 `#include <windows.h>`、`<optional>`、`<string>`、`"app_shell/tab_overflow.h"`（`TabStripDragLayout`）。

`ShellCall` 的建構／解構定義放 header inline 或 `pane_host.cpp`，實作者自選並記在交接區。

### 2. `AppState` 實作 `PaneHost`

`AppState`（`main.cpp:452-568`）加上 `: public panedock::app_shell::PaneHost`，並實作七支：

- `is_shutting_down()` → `return closing_ || shutdown_deferred;`
- `shell_call_entered()` / `shell_call_left()` → 現有 `ShellCallScope` 建構／解構的函式體（後者呼叫既有的 `finish_shell_call(*this)`，`main.cpp:573`）。
- `schedule_session_save()` → 轉呼叫既有自由函式 `::schedule_session_save(*this)`（`main.cpp:2302`）。
- `active_group_id()` → `has_active_group(*this) ? active_group(*this).id : kEmptyGroupId`，`kEmptyGroupId` 為函式內 `static const std::string`，避免回傳暫存物件的懸置參考。
- `make_unique_tab_id()` → `std::size_t c = 0; return unique_tab_id(active_group(*this), c);`（`unique_tab_id` 於 `main.cpp:3010`）
- `chrome_font()` → `chrome_font`；`tooltip()` → `layout_tooltip`。
- `tab_drag_layout(...)` → 從 `apply_tab_item_size`（`main.cpp:1757-1795`）**原封剪下**那段 `foreign_placeholder` 邏輯，回傳 `std::optional<TabStripDragLayout>`；`apply_tab_item_size` 改為呼叫它。這是本票唯一動到既有函式體的地方，且是純提取。

**已知的宣告順序問題**：`AppState` 定義在 `main.cpp:452`，但 `schedule_session_save`／`active_group`／`unique_tab_id`／`finish_shell_call` 都定義在它之後。因此七支 override 一律在 `AppState` 內只**宣告**，在對應自由函式全部定義完之後（`unique_tab_id` 於 `:3024` 是最後一支）再寫 out-of-line 定義。不要把函式體寫進類別內。

`ShellCallScope` **保留不刪**——`main.cpp` 內約 30 處協調層自身的用法（`apply_layout`、group 切換、shutdown、clipboard paste）與 pane 無關，不該改成 `ShellCall`。兩者共用同一組 `shutdown_sequence` 事件，語意一致。

### 3. `Pane` 接線

`pane.h` 新增：

- `void set_host(PaneHost *host) noexcept { host_ = host; }`
- `PaneHost *pane_host() const noexcept { return host_; }`
- private `PaneHost *host_{nullptr};`

注意 `Pane::host()` 這個名字**已被佔用**（回傳 `ExplorerHost&`，PD-183）。不要重載它、不要改名它——改名會動到 PD-183 交接區列出的全部呼叫點，超出本票 scope。新 accessor 用不同名字（`pane_host()`）。

接線點：`create_main_window_children`（`main.cpp:4476`）在 `pane.create(...)` 之前對四個 pane 各呼叫一次 `pane.set_host(&state)`。**必須在任何 `Pane` 方法可能用到 host 之前完成**；本票沒有任何方法會用到，但 PD-192 起會。

### 4. 建置

`pane_host.h`（若有 `.cpp` 則一併）加進 `CMakeLists.txt` 的 `app_shell` 來源清單。

## Non-goals

- **不搬任何函式。** `main.cpp` 行數在本票後大致不變。
- **不改 `ShellCallScope` 的既有約 30 處用法。**
- **不讓 `PaneHost` 長出「取得第 i 個 pane」「取得 `AppState&`」「取得 `GroupState&`」之類的成員。** 那等於把 `AppState*` 用另一個名字塞給 `Pane`，直接廢掉本票的價值。往後任何一張票要加成員，必須在票內說明為什麼那個服務無法由 `Pane` 自己完成。
- 不改 `core`（`src/core` 依 `AGENTS.md` 必須無 HWND／COM／`windows.h`，`PaneHost` 有 HWND 與 HFONT，因此**只能**放 `app_shell`）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> **Keep `src/core` free of HWND, COM and `windows.h`.** It is the only automated test seam in this project (`docs/testing.md`); every type that leaks into it costs that seam.

> **A `pane_index` parameter is a signal that the function belongs to that pane.** Before adding one, ask whether the function can simply be a member of `app_shell::Pane`. It can when all three hold: it touches exactly one pane; it needs no coordinator service (`ShellCallScope`, session save scheduling, shutdown gates, cross-pane layout); and it is not shared state that is a singleton by nature.

（本票的作用正是消掉第二項條件——把「需要協調層服務」從「不能成為成員」的理由降級成「透過 `PaneHost` 取得」。）

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`docs/tickets.md` 2026-09-03 模組契約：

> (1) **單向依賴**——協調層 → `Pane`，`Pane` 不得持有 `AppState*`、回呼介面或 `std::function` 成員。
> (3) **跨 pane 的拖曳狀態留在協調層**，因為它天生跨越來源與目標兩個 pane。

本票對 (1) 的處理：`Pane` 仍不持有 `AppState*` 與 `std::function`。它持有一個**七支方法的純虛介面指標**，字面上屬於 (1) 說的「回呼介面」，因此本票**明確覆寫 (1) 的這半句**，新證據為使用者 2026-09-05 的界線宣告。(1) 的另外兩半（不得持有 `AppState*`、不得持有 `std::function`）維持有效並由本票的 Non-goals 守住。(3) 完全不動——`tab_drag_layout` 的計算留在協調層。

## Acceptance criteria

1. `pane_host.h` 存在，`PaneHost` 恰好七支純虛方法，無其他成員。
2. `AppState` 繼承 `PaneHost` 並實作全部七支，實作全為 out-of-line。
3. 四個 `Pane` 在 `create()` 前都已 `set_host(&state)`。
4. `Pane` 的 `host_` 在本票結束時**尚無任何使用者**（除了 `set_host`／accessor 本身）——這是刻意的。
5. `apply_tab_item_size` 的 `foreign_placeholder` 段落已改為呼叫 `tab_drag_layout(...)`，且 PD-110 跨 pane 拖曳的佔位視覺零變更。
6. `main.cpp` 行數變化在 ±60 行以內。
7. 零行為變更、零視覺變更。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

原始碼守門（`grep`）：

- `src/app_shell/pane.h` 不得出現 `AppState`、`std::function`、`GroupState`。
- `src/app_shell/pane_host.h` 不得出現 `AppState`、`GroupState`、`PaneState`。
- `PaneHost` 的 `virtual` 數量 == 7（加解構子共 8 個 `virtual`）。

新增 `tests/unit/pane_test.cpp` 一例：`test_pane_without_host_is_constructible` — 未 `set_host` 的 `Pane` 可正常建構與解構（`pane_host()` 為 `nullptr`），確保 PD-192 起的 null 檢查有依據。

## 使用者實機檢查

本票零行為變更，僅需煙霧測試：

1. 啟動 → 四 pane 正常顯示、可導覽。
2. 跨 pane 拖曳一個 tab → 目標 pane 的佔位寬度與拖曳前一致（`tab_drag_layout` 提取正確）。
3. 關閉視窗 → 無殘留行程。

## 交接區

- `ShellCall` 放在 `pane_host.h` header inline；介面維持驗收條件指定的 7 支純虛方法（加虛解構子共 8 個 `virtual`）。Scope 2 中另列的 `chrome_font()`／`tooltip()` 未加入，因為它們與 Outcome、介面清單、Acceptance criteria 及 `docs/tickets.md` 的 7 支成員變動紀錄衝突；本票唯一需要字型的提取邏輯由 `AppState::tab_drag_layout` 直接讀協調層既有欄位。
- `set_host` 接線位於 `src/app_shell/main.cpp:4611`，緊接在四 pane 建立迴圈內、`Pane::create()` 之前。
- `apply_tab_item_size` 現位於 `src/app_shell/main.cpp:1739`，並於 `:1767` 呼叫 `AppState::tab_drag_layout`；提取出的 override 位於 `:3026`。`main.cpp` 由 5,975 行變為 6,021 行，淨增 46 行，符合 ±60 行限制。
- PD-192 起可直接使用：`if (pane_host() == nullptr || pane_host()->is_shutting_down()) return;`，需要 Shell 重入保護時再於 guard 後建立 `ShellCall shell_call(pane_host());`。
- 新增 `test_pane_without_host_is_constructible`，確認未接 host 的 `Pane` 可建構、解構且 `pane_host() == nullptr`。`tab_drag_layout` 依賴 HWND/GDI 與跨 pane 協調狀態，不能放進無 Windows 型別的 `core`；自動檢查由既有 tab layout 測試、`panedock_launch_smoke` 與本票的原始碼守門涵蓋，佔位寬度視覺仍依「使用者實機檢查」第 2 項人工確認。
- 2026-09-05 Agent checks：指定 LLVM-MinGW configure 通過；build 通過且本票變更無警告；sandbox 外搭配可寫 session 目錄執行完整 CTest，23/23 通過（含 `panedock_launch_smoke` 1.95 秒）。sandbox 內 smoke 會在關閉後逾時，未修改的 HEAD 基準版同樣重現，故判定為受限執行環境效應而非本票回歸。
