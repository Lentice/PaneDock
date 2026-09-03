# PD-167 — 所有新 tab 的預設 Shell location 統一為 My Computer

Phase 7 · app_shell · Depends on: PD-019, PD-154, PD-160

- Source: 使用者需求與 grilling session（2026-09-03）。
- Origin: 使用者先要求 `+` add tab button 與 tab 條空白區雙擊新增的 tab 預設到 My Computer，後確認 `Ctrl+T` 與所有新增 tab 行為都要統一，並要求 code 透過共用 function、預設 My Computer、允許 caller 指定其他 location。
- Priority: LOW——既有新增 tab 流程已可用，本票只統一初始 Shell location 與 default/fallback policy。

## Outcome

所有新建立的 `TabState` 預設使用 My Computer 的 Shell location，而不是依 pane index 使用不同的本機路徑。所有入口都沿用同一個新增／建立流程；需要特殊目的地的 caller 可以明確傳入另一個 `core::ShellLocation`。

My Computer 的 persisted identity 使用既有固定項目的 parsing name：

`::{20D04FE0-3AEA-1069-A2D8-08002B30309D}`

這是 Shell location identity，不是顯示文字。UI 顯示名稱仍由既有 Shell display-name 路徑依系統語言決定。

## 已確認的產品決策

1. `+`、tab 條空白區雙擊與 `Ctrl+T` 都呼叫同一個 `add_tab_to_pane` function，不各自複製 `TabState`、active tab、navigate、refresh 或 save 流程。
2. `add_tab_to_pane` 的 initial location 參數預設為 My Computer；signature 必須保留明確傳入另一個 `core::ShellLocation` 的能力，避免未來特殊 caller 再複製一套新增流程。
3. 其他會建立新 `TabState` 的路徑也採用同一個 default-location function：初始 application state、新 Group、版型增加 pane，以及任何其他目前使用 `kDefaultLocations` 作為新 tab 初始值的路徑。
4. 關閉 pane 最後一個 tab、跨 pane 搬走來源 pane 最後一個 tab 時的 fallback 也統一為 My Computer；這些不是新增 tab，但同樣是產品定義的 default location。
5. 已存在於 session 的 tab location 不回溯修改；使用者已保存的 `C:\`、`C:\Windows` 或其他 location 必須照原值還原。
6. 參數使用 `core::ShellLocation` value，不使用 display name、PIDL、COM pointer 或只傳一個未來可能遺失 identity 的裸路徑字串。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.6：

> 每個 pane 有一個以上的 tab,可新增、關閉、切換。tab 條在 pane 上緣；在 tab 條未被 tab、`+` 或捲動按鈕占用的空白區雙擊會新增 tab。

`docs/design-spec.md` §FR-005：

> 每個 pane 至少一個 tab。可新增、關閉、切換 tab；在 tab 條未被 tab、`+` 或捲動按鈕占用的空白區雙擊會新增 tab。關閉 pane 的最後一個 tab 時,該 tab 導覽至預設 location 而非留下空 pane。

`docs/design-spec.md` §FR-002：

> 選取 Group 時還原:版型、分隔比例、每個 pane 的 tab 集合、每個 tab 的 Shell location、view mode、排序欄位與方向、active pane、每個 pane 的 active tab。

`docs/design-spec.md` §NFR-005：

> 必要狀態(版型、location、tab、view mode、排序)必須精確還原,否則視為缺陷。

`docs/design-spec.md` §10：

> 不得寫入 PIDL 或 COM 指標。持久化的 identity 為 parsing name ＋ known-folder identity ＋ fallback path。display name 永不作為 identity。

`docs/development.md` §Change workflow：

> Read and trace the files the ticket lists. Grep every caller of any shared function you intend to change.

> Make the smallest change that satisfies the acceptance criteria. Reuse before adding.

`AGENTS.md`：

> Never persist a PIDL or a COM pointer. Persisted identity is parsing name plus known-folder identity plus a fallback path. Display names are never identifiers.

> Keep `src/core` free of HWND, COM and `windows.h`.

## Files to read and trace first

- `CONTEXT.md`——`Shell location`、`Group`、`pane`、`tab` 與 `Pinned Location` 的既有語意；不要把 My Computer 的顯示名稱當 identity。
- `docs/design-spec.md` §4.6、§FR-002、§FR-005、§NFR-005、§9.1、§10——新增 tab、預設 location、還原與模組邊界。
- `docs/development.md`——Change workflow、core boundary 與 Shell identity 規則。
- `docs/tickets/PD-019-tab-strip-and-realize-on-activation.md`——原始 `+` 新增 tab 與 `add_tab_to_pane` 契約。
- `docs/tickets/PD-153-default-view-mode-details.md`——新 tab 初始值只影響新 tab、不得改已保存 tab 的先例。
- `docs/tickets/PD-154-double-click-empty-tab-strip-adds-tab.md`——空白區雙擊已經接入共用新增流程的契約。
- `docs/tickets/PD-160-shell-location-identity-capture-and-resolution.md`——`ShellLocation` 三段 identity 與 resolve/capture 邊界。
- `docs/tickets/PD-111-pinned-locations-menu.md`——既有 My Computer 固定 parsing name `kPinnedFixedParsingNames[1]`。
- `src/app_shell/main.cpp`：
  - `kPinnedFixedParsingNames`、目前的 `kDefaultLocations` 與 `location` helper；確認 My Computer identity 只取既有固定項目。
  - `default_application_state`——初始 Group 的 tab 建立。
  - `new_group_state`——新 Group 與版型差異處理。
  - `add_tab_to_pane` 及其全部 caller——`kTabStripSelectionMessage` 的 `+`／空白雙擊路徑與 message loop 的 `Ctrl+T`。
  - `close_tab_in_pane`——最後一個 tab 的 fallback location。
  - `set_layout`——`core::switch_layout` 新增 pane/tab 的 default location。
  - `finish_tab_drag`——跨 pane 搬走最後 tab 時的 source fallback location。
- `src/core/model.h`／`src/core/model.cpp`——`TabState`、`ShellLocation`、`switch_layout`、`close_tab`、`move_tab` 的 value 參數；不把 My Computer 或 HWND/COM 下放 core。
- `src/shell_core/shell_core.h`／`shell_core.cpp`——現有 parsing-name location、identity capture/resolve；不得新增另一套 default identity parser。
- `tests/unit` 與 `tests/release`——既有 core/session、shell-core boundary、re-entry、launch smoke checks；確認 focused check 放在正確 seam。

## Scope

1. 在 `src/app_shell/main.cpp` 建立唯一的 My Computer default Shell location function，重用既有 `kPinnedFixedParsingNames[1]`；不要重複硬編碼第二份 GUID。
2. 將 `add_tab_to_pane` 改為接收可省略的 initial `core::ShellLocation`，預設呼叫上述 My Computer default function；把傳入的 value 放入新 `TabState`。
3. 確認 `+`、空白區雙擊、`Ctrl+T` 的 caller 都不傳自製 per-pane default，並繼續走同一個 `add_tab_to_pane` 流程。
4. 將初始 application state、新 Group、`set_layout` 新 pane 及其他建立新 `TabState` 的路徑改用同一個 default function。
5. 將 `close_tab_in_pane` 與 `finish_tab_drag` 傳給 `core::close_tab`／`core::move_tab` 的 fallback location 改用同一個 default function。
6. 保留 `core::switch_layout`、`core::close_tab`、`core::move_tab` 的既有 `ShellLocation` explicit parameter，讓需要特殊 default/fallback 的 caller 仍可明確傳值；不在 `core` 新增產品預設。
7. 更新 `docs/design-spec.md`，明確記錄本專案的 default location 是 My Computer，且 explicit location 可以覆寫；補充新 tab、初始建立與最後 tab fallback 的語意。
8. 更新 `docs/tickets.md` Ticket 總覽與計畫決策紀錄；ticket 文件本身不寫 status。

## Non-goals

- 不修改已存在於 session 的 tab location，不做資料 migration 或批次導覽。
- 不修改 `core::ShellLocation`、session schema、unknown-field preservation 或 atomic replace。
- 不把 My Computer display name 寫入 persisted identity；不新增 UI 字串，系統語言顯示仍沿用既有 Shell lookup。
- 不新增 `shell_core` API、COM abstraction、PIDL storage、filesystem probe、thread、timer、polling 或 dependency。
- 不改 tab 的 view mode、sort state、history、active-tab semantics、tab strip hit-test、drag 行為或 `IExplorerBrowser` lifetime。
- 不讓 caller 以裸 display name 或未經既有 Shell location value 的 path string 取代 `core::ShellLocation`。
- 不順手修改其他與 default location 無關的 startup、Group、layout 或 Shell error handling。

## Acceptance criteria

1. 點擊任一 pane 的 `+` 新增 tab，該 tab 的 initial Shell location 是 My Computer，且仍立即成為 active tab。
2. 在 tab 條空白區雙擊新增 tab，結果與 `+` 完全相同：同一新增 function、My Computer location、active、realize/navigation、refresh 與 persistence semantics。
3. 按 `Ctrl+T` 新增 tab，結果與 `+` 及空白區雙擊完全相同。
4. 初始 application state、新 Group 與版型增加 pane 所建立的 tab 都使用 My Computer；不再依 pane index 產生不同預設本機路徑。
5. 關閉 pane 最後一個 tab，或跨 pane 搬走來源 pane 最後一個 tab，留下的 source tab fallback location 是 My Computer。
6. 至少一個 focused self-check 能確認：共用 default function 來源是既有 My Computer parsing name、add function 具備 default/explicit location 參數、所有 default-location caller 已切換，且不再使用舊的 per-pane `kDefaultLocations`。
7. 對 add function 傳入明確的其他 `core::ShellLocation` 時，新增 tab 使用該 location，不被 My Computer default 覆寫。
8. 既有 session 中已保存的非 My Computer tab 在讀取、Group 切換與重啟後仍維持原 location。
9. 新增 tab 的 active、view mode、sort、history、Shell realize-on-activation、session write 與既有重入／shutdown guard 行為沒有回歸。
10. Release configure/build、完整 CTest、ExplorerHost lifetime self-check、focused check 與 `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
.\build\panedock_explorer_host_lifetime_check.exe
git diff --check
```

```powershell
rg -n -C 4 "kPinnedFixedParsingNames|kDefaultLocations|default_shell_location|add_tab_to_pane|core::switch_layout|core::close_tab|core::move_tab" src\app_shell\main.cpp
```

Focused source self-check（實作 agent 應將檢查命令的實際輸出記入交接區）：

```powershell
$source = Get-Content -Raw 'src\app_shell\main.cpp'
if ($source -notmatch 'default_shell_location') { throw 'missing unified default Shell location function' }
if ($source -notmatch '20D04FE0-3AEA-1069-A2D8-08002B30309D') { throw 'missing My Computer parsing identity' }
if ($source -match 'kDefaultLocations') { throw 'old per-pane default locations remain' }
if ($source -notmatch 'add_tab_to_pane[\s\S]*ShellLocation') { throw 'add_tab_to_pane lacks explicit ShellLocation override' }
```

真實桌面人工檢查：

1. 在可見 pane 以 `+`、tab 條空白區雙擊、`Ctrl+T` 各新增一次，確認三者都導覽到 My Computer，且只新增一個 tab。
2. 建立新 Group、切換到會增加 pane 的 layout，確認新建立的 tab 也在 My Computer。
3. 在有兩個 tab 的 pane 關閉其中一個，並測試跨 pane 搬走最後 tab；確認留下的 tab 都回到 My Computer。
4. 先保存一個位於普通資料夾的既有 tab，重啟後確認它沒有被改成 My Computer。
5. 以實作中保留的 explicit location caller/self-check 確認指定其他 location 時不被 default 覆蓋。

## Handoff requirements

- 記錄 unified default function 的名稱、實際重用的既有 parsing-name 常數，以及 `add_tab_to_pane` 的最終 signature。
- 列出所有 `TabState` creation 與 default/fallback caller 的 trace，確認沒有遺漏 startup、new Group、layout、close-last 或 cross-pane last-tab 路徑。
- 記錄 explicit `ShellLocation` override 的 caller／測試方式；沒有實際產品 caller 時，說明它由 focused source self-check 覆蓋。
- 記錄既有 session location 未修改的驗證結果，以及 My Computer UI 顯示名稱沿用既有系統語言 lookup 的結果。
- 記錄 Release build、CTest、lifetime self-check、focused self-check、人工檢查與 `git diff --check` 結果；無法執行者逐項標記「未驗證，需真實桌面」。

## 交接區

<!-- 實作 agent 填寫，append-only -->

### 2026-09-03 實作交接

- 統一 helper 為 `default_shell_location()`，重用既有 `kPinnedFixedParsingNames[1]`（`::{20D04FE0-3AEA-1069-A2D8-08002B30309D}`）；沒有新增第二份 GUID 或 display-name identity。
- `add_tab_to_pane` 最終 signature 為 `void add_tab_to_pane(HWND, AppState&, std::size_t, panedock::core::ShellLocation initial_location = default_shell_location())`，以 `std::move(initial_location)` 建立新 `TabState`，因此省略參數使用 My Computer，明確傳入其他 `ShellLocation` 則保留 caller 的值。
- Caller trace：startup 的 `default_application_state`、new Group 的 `new_group_state`、layout growth 的 `set_layout`、`+` 與空白 tab strip 雙擊共用的 `kTabStripSelectionMessage`、`Ctrl+T`、close-last 的 `close_tab_in_pane`、cross-pane last-tab 的 `finish_tab_drag` 全部改用 unified helper；`core::switch_layout`／`core::close_tab`／`core::move_tab` 的 explicit value 參數未改動。所有舊 `kDefaultLocations` source references 已移除。
- 沒有實際產品 caller 需要特殊 initial location；explicit override 由 `add_tab_to_pane` signature 與 source self-check 覆蓋，`core` 既有 explicit default/fallback tests 仍通過。
- 既有 session location 未被修改：本票只改 default/fallback 建構點，`read_session` 的已保存 `TabState::location` 路徑未動；My Computer UI 顯示名稱仍由既有 `refresh_startup_chrome` → `display_text_for_parsing_name` 系統 Shell lookup 提供。
- Deterministic checks：unified default source self-check PASS；Release configure PASS；`cmake --build build` PASS；elevated 完整 CTest 17/17 PASS；`panedock_explorer_host_lifetime_check.exe` PASS；`git diff --check` PASS。受限環境第一次完整 CTest 的 smoke 因 `%LOCALAPPDATA%` 儲存限制未能正常關閉，之後以可寫 elevated session 重跑完整 CTest PASS。
- 真實桌面人工互動矩陣（`+`、空白雙擊、`Ctrl+T`、new Group/layout growth、close-last/cross-pane fallback、既有 session restart、explicit override UI）未執行，需真實桌面驗證；未修改任何真實 session 檔案。
