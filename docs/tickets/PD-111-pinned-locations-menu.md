# PD-111 — 每個 tab 的導覽列新增 Pinned Locations 下拉選單(Desktop / My Computer + 自訂路徑)

Phase 7 · app_shell · Depends on: 無

- Source: 使用者與 assistant 的 grilling session(2026-08-28)。
- Origin: 使用者要在導覽列上加按鈕可以快速到 Desktop 或 My Computer,且要能自由加入更多路徑,選單裡固定項目要用系統語言顯示。
- Priority: MEDIUM——新功能,不影響既有行為,但涉及 session document schema 新增欄位與導覽列版面重排,範圍略大,故拆成本票(核心能力)與 PD-112(移除/排序管理對話框)兩張。

## 已確認的產品決策(grilling session 逐項紀錄)

1. **掛在 tab 的導覽列上,不是掛在 pane 上。** `docs/design-spec.md` §4.7:「每個 tab 有自己的網址欄與獨立的導覽歷史」——導覽控制的擁有者是 tab,不是 pane(pane 目前沒有任何 header/toolbar 概念)。因此每個 pane 現有的 5 個導覽按鈕(Back/Forward/Up/Refresh/View,見 `src/app_shell/main.cpp` 第 3515-3526 行)旁邊新增第 6 個按鈕,行為與這 5 個一致:同一個 tab 的導覽列上一顆按鈕,點擊只影響「這個 pane 目前的 active tab」。
2. **點擊清單項目導覽該按鈕所在 pane 的 active tab**,不開新 tab。與 Up/Refresh 等按鈕的既有行為一致。
3. **清單資料是 App 全域一份,不屬於任何 Group。** 跟 Group 的「還原完整排列」語意無關,是使用者個人常用位置的捷徑,四個 pane、所有 Group 共用同一份,不重複維護。
4. **命名為 "Pinned Locations"**,對應動作 "Pin"/"Unpin"。刻意不用 "Quick Locations" 或 "Quick access"——Windows Explorer 本身已有一個叫 "Quick access" 的功能,撞名會讓使用者誤以為是同一個東西。已寫入 `CONTEXT.md` 的 **Pinned Location** 詞條。
5. **儲存的資料是 `core::ShellLocation`,不是路徑字串。** `AGENTS.md`:「Never persist a PIDL or a COM pointer. Persisted identity is parsing name plus known-folder identity plus a fallback path.」UNC 網路路徑(例如 `\\vianextfs06\Timesheet_SW`)沒有 known-folder identity,靠 `fallback_path`/`parsing_name` 帶身分即可,跟本機路徑走同一個型別、同一套「unresolvable location」復原錯誤處理,不必另外處理。
6. **兩個固定項目永遠在最上面,下面一條分隔線,再下面才是使用者自訂的 Pinned Location,新加入的固定附加在清單最後。**
7. **固定項目的顯示文字跟隨系統語言,不寫死字串**——直接重用 PD-100 已經做出來的 `display_text_for_parsing_name(std::wstring_view)`(`src/app_shell/main.cpp` 第 524-550 行:對 `::{CLSID}` 開頭的 parsing name 用 `SHCreateItemFromParsingName` + `IShellItem::GetDisplayName(SIGDN_NORMALDISPLAY,...)` 取得本地化顯示名稱,失敗則退回 parsing name 本身)。兩個固定項目的 parsing name 是眾所皆知的 Shell 命名空間 CLSID:
   - Desktop:`::{B4BFCC3A-DB2C-424C-B029-7FE99A87C641}`
   - This PC / My Computer:`::{20D04FE0-3AEA-1069-A2D8-08002B30309D}`
8. **顯示名稱在 App 啟動時查一次並快取,不要每次開下拉選單都查。** `SHCreateItemFromParsingName` 是 COM 呼叫,下拉選單是高頻互動路徑,沒必要每次都重新解析。**執行期間的系統語言切換不在本票範圍**(需要監聽 `WM_SETTINGCHANGE`/`WM_WININICHANGE` 並重新查快取,這是一個很少發生的邊角情境,獨立評估是否值得另開票,見 Non-goals)。
9. **自訂 Pinned Location 的顯示名稱一律用系統顯示名稱**(同樣透過 `display_text_for_parsing_name` 或直接用 Shell 取得的顯示名稱),不開放使用者自訂標籤文字——避免資料夾改名/搬移後標籤跟實際路徑脫節,Manage 對話框(PD-112)也因此不用做重新命名 UI。
10. **新增進入點是選單最下方的固定項目 "Add Current Folder"**,把該 pane 目前 active tab 的 `TabState::location` 附加到清單最後(分隔線下方)。
11. **重複加入時直接忽略,不彈提示。** 判斷「相同」用 `ShellLocation` 的 `parsing_name`(目前程式碼路徑下 `known_folder_identity`/`fallback_path` 皆未被填入,見下方「與現況的落差」,故現階段用 `parsing_name` 逐一比對即為完整比對)。
12. **移除與排序管理是另一張票(PD-112)**,本票的選單只提供 "Manage Pinned Locations..." 這個入口項目,點了目前先不做事(留一個 `TODO`/直接呼叫 PD-112 要新增的函式——由實作 agent視 PD-112 是否同批實作決定,若 PD-112 尚未實作,選單項目建立但先 `EnableWindow`/`grey out` 或忽略點擊皆可,由交接區記錄選擇)。

## 與現況的落差(讀程式碼時發現,先說明避免誤判為新錯誤)

`src/app_shell/main.cpp` 第 520-522 行的 `location(std::wstring parsing_name)` 建構 `ShellLocation` 時只填 `parsing_name`,`known_folder_identity` 與 `fallback_path` 全部留空;`handle_navigation_complete`(第 1958-1976 行)呼叫的也是這個函式。也就是說**目前整個程式碼庫還沒有任何地方真的把 known-folder identity 或 fallback path 填進去**,包括既有的 tab location。這是既存狀態,不是本票要修的缺陷——本票的 Pinned Location 沿用同一個不完整但一致的填法即可(`Add Current Folder` 直接複製 `TabState::location`,固定兩項直接用 `location(CLSID 字串)`),不在本票裡回頭補齊 known-folder identity 的完整填入(那是一個獨立、影響範圍更廣的既有缺口,不屬於「新增 Pinned Locations 功能」的範圍)。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Never persist a PIDL or a COM pointer. Persisted identity is parsing name plus known-folder identity plus a fallback path. Display names are never identifiers.

`AGENTS.md`:
> Every persisted config/setting file must be designed for forward extensibility. It carries an explicit schema version from its first version. A read that encounters a field it does not recognize preserves that field rather than silently dropping it on the next write-back. A schema change is additive...

`AGENTS.md`:
> App UI text must be English. No Chinese strings ship in the binary.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/design-spec.md` §4.7:
> 每個 tab 有自己的網址欄與獨立的導覽歷史。提供上一頁、下一頁、上層。

`CONTEXT.md`(本次 grilling session 新增的詞條,直接引用):
> **Pinned Location**: A Shell location the user has attached to the address bar's quick-jump menu for one-click navigation from any tab. App-wide: shared across all Groups and panes, not part of any Group's state... A Pinned Location has no known-folder identity when it points to a UNC network path... or an ordinary local folder — the fallback path carries the identity in that case... it becomes an **unresolvable location** if the server or share is unreachable, following the same recoverable-error handling as any other tab.

## Files to read and trace first

- `docs/tickets/PD-059-view-mode-dropdown-menu.md`——**本票要照抄的 `TrackPopupMenu` 下拉選單模式範本**(建立/定位/`TPM_RETURNCMD`/銷毀/送回 `WM_COMMAND` 的完整寫法與交接記錄)。
- `src/app_shell/main.cpp` 第 3024-3048 行——Group 清單右鍵選單,`TrackPopupMenu` 最初的範本。
- `src/app_shell/main.cpp` 第 524-550 行:`display_text_for_parsing_name`——PD-100 已建立的本地化顯示名稱查詢,直接重用,不要重寫。
- `src/app_shell/main.cpp` 第 116-137 行:導覽列相關常數(`kNavigationButtonWidth`、`kNavigationGlyphSize`、`kNavigationGlyphs`、`kBackButtonIdBase`...`kViewModeButtonIdBase`)——新按鈕與新選單都要在這裡加常數,並選一段沒被佔用的 ID 區段(先 `rg -n "constexpr int k.*Id"` 盤點)。
- `src/app_shell/main.cpp` 第 647-673 行:`navigation_geometry`——**目前寫死 5 個按鈕**(`button_width * 5`、`pane_width / 6`),新增第 6 個按鈕後必須把這兩個數字改成 6 / 7,否則地址欄背景會跟新按鈕重疊。
- `src/app_shell/main.cpp` 第 3515-3541 行:5 個導覽按鈕的建立迴圈(`labels`/`ids`/`destinations` 三個 `std::array`)——新按鈕比照同一個迴圈加進去,`BS_OWNERDRAW` + `SetWindowSubclass(..., owner_draw_button_proc, ...)`,hover 高亮會自動透過既有的 `owner_draw_hovered_button` 比對機制生效,不需要額外接線。
- `src/app_shell/main.cpp` 第 2056-2073 行:`apply_layout` 裡 5 個按鈕的 `SetWindowPos` 排版迴圈——`std::array<HWND, 5>` 要擴成 6。
- `src/app_shell/main.cpp` 第 3679-3723 行:`WM_DRAWITEM` 對五種按鈕 ID 範圍呼叫 `draw_navigation_icon_button(..., glyph_kind, ...)` 的五個 if 區塊——新按鈕比照加一個區塊,`glyph_kind = 5`。
- `src/app_shell/main.cpp` 第 122-123 行:`kNavigationGlyphs`(`std::array<wchar_t, 5>`,Segoe MDL2 Assets 字元碼)與第 845-889 行 `draw_navigation_fallback_glyph` 的 GDI 手繪備援——擴成 6 個元素,新增 `case 5` 的手繪備援圖案(字型失敗時的最後防線,PD-075 已統一走字型優先)。**圖示本身用系統字型字元即可,不需要另外設計/生成點陣圖示素材**(這與 App icon 這類需要 Codex 產圖的情境不同——PD-075 已把整條導覽列圖示統一到 `Segoe MDL2 Assets` 字型繪製,本票延續同一慣例)。建議字元:``(Pinned)或 ``(Pin),由實作 agent 挑一個視覺上跟其餘 5 個圖示風格一致的,並在交接區記錄選用的碼位與理由。
- `src/app_shell/main.cpp` 第 2509-2513 行:`navigate_up`——點擊清單項目後導覽該 pane 的既有寫法範本(`state.explorers[pane_index].navigate(parsing_name)`,不必手動更新 `tab.location`,`handle_navigation_complete` 回呼會處理)。
- `src/core/model.h`——`ApplicationState`(第 58-76 行,新增 `std::vector<ShellLocation> pinned_locations;` 欄位的位置)、`ShellLocation`(第 11-17 行)。
- `src/core/session.h`/`session.cpp`——JSON 序列化/反序列化,新增欄位要能讀寫且不影響 `preserved_json` 的既有未知欄位保留機制。
- `docs/tickets/PD-013-config-file-extensibility-convention.md`——確認「新增可選欄位不需要遞增 schema version」的既有共識;`ApplicationState::schema_version` 維持 1。

## Scope

1. `core::ApplicationState` 新增 `std::vector<ShellLocation> pinned_locations;`(App 全域,不屬於任何 `GroupState`),連同 JSON 序列化/反序列化與既有測試一併更新。schema_version 不變(附加式欄位)。
2. `core` 新增最小必要的操作函式(例如 `add_pinned_location`/`remove_pinned_location` 之一或兩者都要——`remove` 若 PD-112 才需要可留給 PD-112 新增,由實作 agent決定並在交接區說明取捨),邏輯包含「已存在則忽略」的去重規則(比對 `parsing_name`)。
3. 每個 pane 的導覽列新增第 6 顆按鈕(圖示化,`Segoe MDL2 Assets`,比照現有 5 顆的建立/排版/繪製/hover 路徑)。
4. 點擊該按鈕彈出 `TrackPopupMenu`,結構由上到下:Desktop、My Computer、分隔線、`pinned_locations` 內容(依序)、分隔線、"Add Current Folder"、"Manage Pinned Locations...";Desktop/My Computer 的顯示文字用 `display_text_for_parsing_name` 在啟動時查好並快取。
5. 選單 ID 配置比照 PD-059 決策(每 pane 固定區段,不用共用的來源 pane 狀態):`kPinnedMenuIdBase` 起、每個 pane 保留足夠的 slot(固定項目 + 一段上限,例如 64 個)覆蓋 Desktop/My Computer/Add/Manage 加上動態的自訂項目數量,超過上限的自訂項目本票先不處理捲動或分頁(見 Non-goals)。
6. 點選 Desktop/My Computer/自訂項目 → 導覽該 pane 的 active tab。點選 "Add Current Folder" → 把該 pane active tab 的 `TabState::location` 加入 `pinned_locations`(去重後),`save_now`,下次開選單會看到新項目。
7. `navigation_geometry` 的按鈕數與地址欄位置計算從 5 改為 6。

## Non-goals

- 不做 Manage 對話框的移除/排序 UI——那是 PD-112。"Manage Pinned Locations..." 選單項目本票只需要存在(可以先無動作或呼叫一個之後由 PD-112 補實作的函式,由實作 agent 決定並記錄)。
- 不開放自訂顯示標籤(決策 9)。
- 不做 Windows 顯示語言在執行期間切換時的即時刷新(決策 8)——固定項目文字只在啟動時查一次。若之後真的需要,獨立開票(監聽 `WM_SETTINGCHANGE`)。
- 不回頭補齊既有程式碼裡 `known_folder_identity`/`fallback_path` 從未被填入的既存缺口(見「與現況的落差」)。
- 不限制 `pinned_locations` 的數量上限之外的 UX(例如捲動選單、分類、資料夾圖示縮圖)。
- 不改變 Up/Refresh/View 等既有 5 顆按鈕的行為或外觀,除了因新增第 6 顆而必要的排版數字調整。

## Acceptance

1. 每個 pane 的導覽列新增第 6 顆圖示按鈕,視覺風格與既有 5 顆一致(含 hover)。
2. 點擊該按鈕彈出下拉選單:Desktop、My Computer(文字隨系統語言顯示,例如切到英文系統顯示 "Desktop"/"This PC"、切到繁中系統顯示對應的本地化名稱)、分隔線、目前的 Pinned Location(初始為空)、分隔線、"Add Current Folder"、"Manage Pinned Locations..."。
3. 點擊 Desktop/My Computer 導覽該 pane 的 active tab 到對應位置。
4. 在某個 pane 導覽到一個資料夾後點 "Add Current Folder",該路徑出現在分隔線下方,四個 pane 的選單都能看到(App 全域共用)。
5. 對已經在清單裡的資料夾再按一次 "Add Current Folder",清單不重複新增。
6. 對一個 UNC 網路路徑執行 Add Current Folder 並重新點擊該項目,能正常導覽;斷線情境下比照既有 unresolvable location 錯誤處理,不會讓程式崩潰或遺失這筆 Pinned Location。
7. 重啟程式後,自訂的 Pinned Location 清單原樣還原。
8. 四個 pane 各自的地址欄與其餘 5 顆按鈕版面沒有因新按鈕而重疊或跑版。
9. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
10. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "pinned_locations|kPinnedMenuIdBase|kPinnedButtonIdBase|display_text_for_parsing_name" src\core src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:對每個 pane 開 Pinned Locations 選單,確認 Desktop/My Computer 文字正確、
# 點擊可導覽;Add Current Folder 後跨 pane 確認清單同步;重啟程式確認還原。
# 本環境已具備 PrintWindow 截圖與 SetCursorPos/mouse_event 點擊模擬能力。
# TrackPopupMenu 的選單是獨立 top-level 視窗(class name #32768),PrintWindow 截主視窗
# 截不到它,驗證選單外觀需另外找到該 HWND 單獨截圖,見 PD-059 交接區的既有記錄。
```

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 新增的 ID 常數區段(`kPinnedButtonIdBase`、`kPinnedMenuIdBase` 及其 slot 配置)與選用理由。
- Segoe MDL2 Assets 選用的字元碼位與理由;字型失敗備援圖案的手繪設計。
- `pinned_locations` 的去重比對欄位與理由(目前程式碼下 `parsing_name` 是否已足夠,或發現需要更多欄位)。
- "Manage Pinned Locations..." 選單項目在本票的具體行為(disabled/no-op/呼叫佔位函式),供 PD-112 銜接。
- 是否與 PD-112 同批實作;若分開,PD-112 開工前需要重讀哪些本票新增的符號。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 實作交接（2026-08-28）

- `core::ApplicationState` 新增全域 `pinned_locations`，`add_pinned_location` 以 `ShellLocation::parsing_name` 去重；目前整個程式仍未填入 `known_folder_identity`／`fallback_path`，因此 parsing name 是現況下完整且一致的 identity 比對欄位，UNC 路徑也沿用同一型別。
- session JSON 新增可選的 `pinned_locations` 陣列，`schema_version` 維持 1。讀取舊檔時缺欄位即視為空清單；每個 pinned location 的未知欄位會依 parsing name 從 `preserved_json` 保留並在寫回時帶出。現有原子替換與備份流程未改動。
- `kPinnedButtonIdBase = 392`，使用未佔用的 392–395（位於既有 View popup 360–391 與 layout 400 之間）；`kPinnedMenuIdBase = 500`，每個 pane 保留 68 個 slot：Desktop、This PC、64 個自訂項目、Add、Manage，四個 pane 共 500–771。超過 64 筆時 UI 只顯示前 64 筆，本票的 Add 也在 64 筆時停止。
- 導覽列使用 Segoe MDL2 Assets `U+E718`（Pin），因為它是現有五個 glyph 同一字型系統中最直接的固定位置圖示。字型失敗時備援圖案以橢圓 pin head、垂直 shaft 與橫向 pin bar 手繪，沿用既有 GDI fallback 路徑。
- 固定 Desktop／This PC 顯示名稱在 COM 初始化後的啟動流程查詢一次並快取；自訂位置在建立選單時用既有 `display_text_for_parsing_name` 取得系統顯示名稱。所有項目只以 parsing name 導覽，不以顯示文字作 identity。
- `Manage Pinned Locations...` 目前保持 enabled 但 command no-op（選單返回後直接結束），未建立佔位對話框；PD-112 可在 `WM_COMMAND` 的 pinned menu 分支接上 `kPinnedMenuManageOffset`。本票未與 PD-112 同批實作；PD-112 開工前請重讀 `ApplicationState::pinned_locations`、`add_pinned_location`、`kPinnedButtonIdBase`／`kPinnedMenu*` 常數及 `show_pinned_locations_menu`／`WM_COMMAND` 分支。
- 已新增 core model 去重測試、session round-trip、舊檔缺少新欄位及 pinned unknown-field preservation 測試。`cmake --build build` PASS；`ctest --test-dir build --output-on-failure` PASS（5/5）；`git diff --check` PASS。

#### 使用者手動驗證（本 agent 未執行）

因本次要求所有 UI 互動留給使用者，本 agent 沒有啟動程式、點擊、送鍵盤輸入、使用 Computer Use、`SetCursorPos`、`mouse_event` 或互動後截圖。請使用者手動執行：

1. 執行 `build\\PaneDock.exe`，在每個可見 pane 開啟 Pinned Locations；確認 Desktop／This PC（或系統語言對應名稱）、兩條分隔線、Add Current Folder、Manage Pinned Locations... 的順序與第 6 顆圖示按鈕位置。
2. 在各 pane 分別點 Desktop、This PC 與一筆自訂 pinned location，確認只導覽該 pane 的 active tab；確認自訂位置的清單在所有 pane 共用。
3. 導覽到資料夾後選 Add Current Folder，確認它出現在清單；再次加入同一路徑，確認不重複且會持久化。
4. 加入可用 UNC 路徑後暫時讓 share 不可達，再點該 pinned 項目；確認顯示既有可復原錯誤、程式不崩潰且 pinned 設定仍保留，恢復連線後可重試。
5. 關閉並重新啟動程式，確認自訂 pinned 清單原樣還原；在 1–4 pane 版型與縮放視窗下確認地址欄及原有五顆按鈕沒有重疊。
