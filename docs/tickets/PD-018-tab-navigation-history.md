# PD-018 — `core` 每個 tab 獨立的導覽歷史(上一頁／下一頁）

Phase 3 · core · Depends on: PD-006

- Source: `AGENTS.md`、`docs/design-spec.md` §4.7／FR-010／FR-014、`docs/testing.md`、`docs/tickets.md` §已否決的方向
- Origin: 2026-08-24,`docs/roadmap.md` Phase 3「Per-tab navigation history」。Phase 3 其餘三項(tab UI、網址列/上一頁/下一頁/上層按鈕、鍵盤快速鍵)在 PD-019／PD-020／PD-021 处理,本 ticket 只交付它们共同依赖的資料模型。
- Priority: HIGH——PD-020(網址列與導覽按鈕)直接依賴本 ticket 的函式簽章,越早定案,後面的 UI ticket 才有東西可以呼叫。

## Goal

在 `src/core/model.h`/`.cpp` 既有的 `TabState` 上,新增「這個 tab 自己的導覽歷史」,並提供上一頁/下一頁的純資料操作函式,供 PD-020 的 UI 呼叫。本 ticket 全程留在 `core`——不碰 HWND、COM 或 `ExplorerHost`。

## 已確認的產品決策

1. **歷史紀錄不參與 session 持久化,只存在於執行期的 `ApplicationState`。** `docs/design-spec.md` §205 明確列出「必要狀態」只有版型、location、tab、view mode、排序;導覽歷史不在清單內,屬於既非必要也未列入 best-effort 的第三類。重新啟動後每個 tab 的歷史從空陣列開始(只有目前 location,無法上一頁),這是刻意的最小實作,不觸碰 `core/session.cpp` 的 schema、不需要 PD-013 的相容性遷移。**這是一個判斷,已在交接時請你確認**——若你希望歷史像瀏覽器分頁一樣跨重啟保留,需要另開 ticket 擴充 session schema。
2. **歷史陣列允許為空,不強制「至少一筆」。** 一個剛建立(`add_tab`)或剛從舊版 session 讀入(沒有 `history` 欄位)的 `TabState`,其 `history` 為空陣列是合法狀態,語意是「這個 tab 還沒有被記錄過任何導覽」,此時上一頁/下一頁皆不可用,但 `location` 欄位仍是目前顯示位置的唯一真相來源(既有欄位,行為不變)。**理由**:讓既有呼叫端(`switch_layout`、`add_tab`、`default_application_state` 裡的 aggregate-init `TabState{...}`)完全不用修改——C++20 aggregate 初始化在成員少於欄位數時,尾端新欄位自動套用 default member initializer(空 vector／`0`),舊的 5 欄位呼叫點原樣可編譯,是最小 diff。
3. **`record_navigation` 是唯一的「寫入新歷史項」入口,語意等同瀏覽器:清掉目前位置之後的 forward 分支,再把新位置接在後面。** 呼叫端(PD-020)自己判斷什麼情境算「新的導覽」(使用者在網址列輸入、在 Shell view 內雙擊資料夾、按上層)—— 一律呼叫 `record_navigation`。上一頁/下一頁按鈕呼叫的是 `navigate_tab_back`/`navigate_tab_forward`,兩者只移動游標、不寫入新項、也不清 forward 分支。這條分工是 PD-020 串接的基礎,必須先在這裡把語意鎖死。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.7:
> 每個 tab 有自己的網址欄與獨立的導覽歷史。提供上一頁、下一頁、上層。

`docs/design-spec.md` FR-010:
> 每個 tab 提供網址輸入(可鍵入或貼上)、上一頁、下一頁、上層。導覽歷史為每個 tab 各自獨立。

`docs/design-spec.md` §205:
> **必要狀態**(版型、location、tab、view mode、排序)必須精確還原,否則視為缺陷。**best-effort 狀態**(選取項目、捲動位置、欄寬)不保證。

`AGENTS.md`:
> Keep `src/core` free of HWND, COM and `windows.h`. It is the only automated test seam in this project.

`AGENTS.md`:
> Every persisted config/setting file must be designed for forward extensibility... This applies to anything meant to survive a restart; it does not apply to a file a ticket has explicitly designed as disposable.
（本 ticket 的「已確認的產品決策 1」引用此條的反向情形:歷史刻意不設計為跨重啟存活,所以不落入 schema 擴充性的約束範圍。）

## Files to read and trace first

- `src/core/model.h`/`.cpp`——`TabState`、`ShellLocation`、`PaneState`、`is_valid(GroupState)`、`add_tab`、`close_tab`、`switch_layout`(注意 `switch_layout` 內部用 5 欄位 aggregate-init 建立新 tab 的那一行)。
- `tests/unit/core_model_test.cpp`——既有測試風格與 fixture 建構方式,新測試要照同一種寫法加。
- `docs/tickets/PD-004-core-model-invariants.md` 交接區——`is_valid` 的既有不變式設計理由。

## Scope

1. `TabState` 新增兩個欄位(加在既有欄位之後,不打亂既有欄位順序):
   ```cpp
   std::vector<ShellLocation> history;
   std::size_t history_index{0};
   ```
   `operator==` 用 `= default` 已涵蓋新欄位,不需手動改。
2. 新增純函式(`model.h` 宣告、`model.cpp` 實作):
   ```cpp
   void record_navigation(TabState& tab, ShellLocation location);
   bool can_navigate_tab_back(const TabState& tab) noexcept;
   bool can_navigate_tab_forward(const TabState& tab) noexcept;
   bool navigate_tab_back(TabState& tab) noexcept;
   bool navigate_tab_forward(TabState& tab) noexcept;
   ```
   - `record_navigation`:若 `location == tab.location` 直接 return(同一位置,不留重複紀錄)。否則清掉 `history` 中 index 之後的元素(forward 分支),把 `location` push 進去,`history_index` 指向新的最後一項,並把 `tab.location = std::move(location)`。若 `history` 原本是空的(尚未記錄過),先把目前 `tab.location` 補進去當第 0 項,再照上面流程接上新項——這樣「上一頁」第一次呼叫時才能回到使用者一開始看到的位置,而不是憑空消失。
   - `can_navigate_tab_back`:`history` 非空且 `history_index > 0`。
   - `can_navigate_tab_forward`:`history` 非空且 `history_index + 1 < history.size()`。
   - `navigate_tab_back`/`navigate_tab_forward`:對應方向不可用時回傳 `false` 且不改動狀態;可用時 `--history_index`/`++history_index`,把 `tab.location = history[history_index]`,回傳 `true`。
3. `is_valid(const GroupState&)` 的既有 pane/tab 檢查(`model.cpp` 內的 lambda)追加一條:
   ```cpp
   (tab.history.empty() ||
    (tab.history_index < tab.history.size() &&
     tab.location == tab.history[tab.history_index]))
   ```
   維持既有 `history` 為空即合法的語意(決策 2)。

## Non-goals

- 不做「上層」(parent folder)導覽——那需要真實 Shell PIDL 才能正確算出父資料夾(虛擬命名空間不是單純字串截斷),留給 PD-020 用 `IExplorerBrowser::BrowseToObject(nullptr, SBSP_PARENT)` 處理,`core` 不需要知道父資料夾怎麼算。
- 不修改 `core/session.cpp` 的序列化——歷史不落地(見決策 1)。
- 不修改 `switch_layout`/`add_tab`/`close_tab` 既有行為,新 tab 一律以空歷史開始。
- 不建立任何 `ExplorerHost`、`main.cpp` 的呼叫端接線——那是 PD-020 的範圍。

## Acceptance

1. 新增 `panedock_core_model_test.cpp`(或擴充既有 `core_model_test.cpp`)涵蓋:空歷史時上一頁/下一頁皆回傳 `false`;連續 `record_navigation` 三次後可上一頁兩次、下一頁兩次回到最後;上一頁後再 `record_navigation` 會截斷原本的 forward 分支;對同一 location 連續呼叫 `record_navigation` 不新增項目。
2. `is_valid(GroupState)` 對空歷史與非空但 `history_index`/`location` 一致的 tab 都回傳 `true`;對 `history_index` 越界或 `location` 與 `history[history_index]` 不一致的 tab 回傳 `false`(新增對應的 invalid-case 測試)。
3. 既有 `panedock_core_model`/`panedock_core_layout`/`panedock_core_session` 三個 CTest 不修改即繼續通過。
4. `rg -n "windows\.h" src/core` 無命中。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "windows\.h" src\core
# 預期:無命中
git diff --check
```

## Handoff requirements

- `record_navigation`/`navigate_tab_back`/`navigate_tab_forward`/`can_navigate_tab_back`/`can_navigate_tab_forward` 的最終簽章與檔案位置(供 PD-020 直接引用)。
- 若實作中發現「同一 location 判斷相等」用 `ShellLocation::operator==`(逐欄位比較 parsing_name/known_folder_identity/fallback_path)在某些情境會誤判不同(例如同一路徑但 known_folder_identity 一個有填一個沒填),記錄下來給 PD-020 參考。
- 決策 1(歷史不落地)是否需要在 `docs/design-spec.md` 補一筆澄清,或维持只記在本 ticket——由你判斷,若你認為應該補進 spec,留言在此交接區,不要自行改 spec。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-24 實作交接

- `TabState` 已在 `src/core/model.h` 的既有五個欄位之後追加：
  ```cpp
  std::vector<ShellLocation> history;
  std::size_t history_index{0};
  ```
  空歷史維持合法，既有 `switch_layout`、`add_tab`、`close_tab` 與 session schema/serialization 均未改動；session read 建出的 tab 因尾端 default initializer 而從空歷史開始。
- PD-020 可直接引用的最終介面均位於 `src/core/model.h`，實作位於 `src/core/model.cpp`：
  ```cpp
  void record_navigation(TabState& tab, ShellLocation location);
  bool can_navigate_tab_back(const TabState& tab) noexcept;
  bool can_navigate_tab_forward(const TabState& tab) noexcept;
  bool navigate_tab_back(TabState& tab) noexcept;
  bool navigate_tab_forward(TabState& tab) noexcept;
  ```
- `record_navigation` 對與目前 location 完全相等的輸入直接返回；第一次真正導覽先補入原 location，再追加目的地；已有歷史時從目前 index 後方 erase 掉 forward branch，再追加目的地並同步 `history_index`/`location`。back/forward 僅在相應 `can_*` 成立時移動 index 並同步 location，不新增紀錄。
- `is_valid(const GroupState&)` 已逐 tab 接上 ticket 指定的不變式：空 history 合法；非空 history 必須 index 在界內且 `tab.location == tab.history[tab.history_index]`。`tests/unit/core_model_test.cpp` 新增 `test_tab_navigation_history`，並在既有 deliberate-breakage 測試加入 index 越界與 current location 不一致兩個 invalid case。測試涵蓋空歷史雙向皆 false、三次記錄後 back/back/forward/forward、回上一頁後建立 branch、相同 location 不重複，以及有效的非空歷史 Group。
- location 相等沿用 `ShellLocation::operator== = default`，會逐欄位比較 `parsing_name`、`known_folder_identity`、`fallback_path`。因此同一 Shell 位置若一份 identity 欄位完整、另一份只有 parsing name，會被視為不同並留下兩筆；core 無 Shell canonicalization 能力，PD-020 呼叫 `record_navigation` 前應盡量沿用 ExplorerHost 回報的標準化 `ShellLocation` 表示，不應在 core 改成只比較路徑字串。
- 歷史不落地的決策目前不需要修改 `docs/design-spec.md`：§205 的必要／best-effort 持久化清單沒有 history，而本 ticket 已明訂 runtime-only 語意。若產品之後要求瀏覽器式跨重啟歷史，再以獨立 schema migration ticket 補 spec 與 session 格式，避免在 PD-018 擴張範圍。
- Ponytail 原則的實際影響：直接在既有 model/test seam 加最小欄位、函式與單一 focused test，未新增 history 類別、介面、session migration 或 UI glue。

#### Agent checks

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：成功。
- `cmake --build build`：成功。LLVM-MinGW 對 ticket 決策 2 刻意保留的五欄 `TabState{...}` aggregate call sites 報 `-Wmissing-field-initializers`（`model.cpp`、`session.cpp`、`main.cpp` 與兩個 test fixture），但沒有 error；依 Non-goals 未改寫這些既有呼叫端，新欄位仍正確採 empty vector／index 0 預設值。Reviewer 若要求 warning-free build，需要決定是否允許機械式補上尾端 `{}`/`0`，這不影響行為但會偏離「舊呼叫點原樣」的明文取捨。
- `ctest --test-dir build --output-on-failure`：3/3 通過（`panedock_core_model`、`panedock_core_layout`、`panedock_core_session`）。
- `rg -n "windows\.h" src\core`：無命中。
- `git diff --check`：通過。
- 未修改 `docs/tickets.md`，未 commit；工作開始前既有未追蹤 `.claude/` 未觸碰。
