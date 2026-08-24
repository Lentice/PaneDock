# PD-020 — 每個 pane 的網址列與上一頁／下一頁／上層按鈕

Phase 3 · app_shell · Depends on: PD-018, PD-019

- Source: `AGENTS.md`、`docs/design-spec.md` §4.7／FR-010／FR-012／NFR-003、`docs/tickets.md` §候選(「每個 pane 的導覽列」)
- Origin: 2026-08-24,`docs/roadmap.md` Phase 3「Per-tab address field, back, forward, parent」。同時是 `docs/tickets.md` §候選 中「每個 pane 的導覽列(上一頁／下一頁／上層按鈕 ＋ editable path bar)」候選項的正式落地——該候選項當初就預期拆成「導覽按鈕」與「path bar」兩張,本 ticket 判斷兩者共用同一段 UI chrome 與同一組事件路徑(都要接 `ExplorerHost` 的導覽完成通知),合併成一張比較省事,不強行拆成兩張徒增交接成本。
- Priority: HIGH——是 Phase 3 剩餘功能(FR-010)裡對使用者可見度最高的一塊,也是 PD-018 的資料模型第一次被真正使用。

## Goal

在 PD-019 建好的 tab 條下面,每個 pane 再加一段導覽列:上一頁、下一頁、上層三顆按鈕,加一個可輸入/貼上路徑的網址列。三顆按鈕與 PD-018 的 `can_navigate_tab_back`/`navigate_tab_back` 等函式對接;網址列與「使用者在 Shell view 內雙擊資料夾」這兩種導覽方式都要正確地把新位置記錄進該 tab 的歷史(呼叫 PD-018 的 `record_navigation`)。

## 已確認的產品決策

1. **`ExplorerHost` 新增一個「導覽完成」回呼,讓 app_shell 能區分『是使用者在 Shell view 內部雙擊資料夾造成的導覽』與『是我方呼叫 `navigate()` 造成的導覽』。** 目前 `ExplorerHost::navigation_complete(PCIDLIST_ABSOLUTE)` 只更新內部 `location_`,app_shell 只在存檔時被動 `poll` 這個值(`capture_locations`)。這對「每次導覽都要記一筆歷史」不夠——使用者在 Shell view 內連續雙擊兩層資料夾,存檔前完全看不到中間那一層,無法上一頁回去。新增签章:
   ```cpp
   void set_navigation_callback(std::function<void(std::wstring_view)> callback);
   ```
   在 `navigation_complete()` 內、更新完 `location_` 之後呼叫它(若已設定)。app_shell 在 `initialize()` 之后為每個 `ExplorerHost` 設定各自的回呼,回呼內容是「幫這個 pane 目前的 active tab 呼叫 `core::record_navigation`,除非這次導覽是我方剛剛呼叫上一頁/下一頁造成的」(見決策 2)。
2. **「這次導覽是不是上一頁/下一頁造成的」用每個 pane 一個 `bool` 旗標抑制,不靠比較 location 字串猜測。** `AppState` 新增 `std::array<bool, kExplorerCount> suppress_history_record{}`。呼叫上一頁/下一頁按鈕的處理函式時,流程固定為:先呼叫 `core::navigate_tab_back`/`navigate_tab_forward` 更新 `TabState.history_index`,把該 pane 的旗標設 `true`,再對 `ExplorerHost` 呼叫 `navigate()`;導覽完成回呼看到旗標為 `true` 就跳過 `record_navigation`(旗標已經反映了正確的歷史位置,不需要、也不能再 push 一筆新的),並把旗標清回 `false`。網址列輸入與上層導覽**不**設這個旗標,一律照常呼叫 `record_navigation`——這正是它們「新導覽會清掉 forward 分支」的預期行為(PD-018 決策 3)。
3. **上層導覽(parent)用 `IExplorerBrowser::BrowseToObject(nullptr, SBSP_PARENT)`**,不在 `core` 或字串層面計算父路徑。這是公開文件記載的旗標組合(`SBSP_PARENT`:以「使用者按上層按鈕」的語意瀏覽到父資料夾),對虛擬命名空間(例如「本機」底下的裝置節點)也能正確運作,不像對 `parsing_name` 做字串截斷那樣在虛擬節點上會出錯。`ExplorerHost` 新增 `HRESULT navigate_up() noexcept`,內部呼叫這個 API;成功後一样會觸發既有的 `OnNavigationComplete`,一路帶出決策 1 的回呼、記入歷史,不需要另外特殊處理。
4. **網址列用一般 `EDIT` 控制項,Enter 提交,不做即時自動完成或下拉建議清單。** Windows Explorer 的網址列自動完成是 Shell 的 `IAutoComplete2` 整合,額外接一個 COM 物件、處理下拉 UI 的複雜度,對 FR-010「網址輸入(可鍵入或貼上)」這條最低要求來說不成比例。**這是一個判斷,已在交接時請你確認**——若使用者對自動完成有強需求,留給後續 UI 打磨 ticket。
5. **網址列提交失敗(`ExplorerHost::navigate()` 回傳失敗或 `navigation_failed()` 被觸發)時,不清空使用者剛輸入的文字,也不呼叫 `record_navigation`。** 對應 `docs/design-spec.md` FR-012「無法解析的 location 在 tab 內顯示可復原錯誤,保留設定」——`ExplorerHost` 既有的 `navigation_failed()`/錯誤視窗機制(PD-009/PD-014 已經建立)已經處理了「pane 內顯示錯誤」,本 ticket 只需要確保「網址列文字保留、不誤植入歷史」這兩件事不要因為新增網址列而破壞既有錯誤處理路徑。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §4.7:
> 每個 tab 有自己的網址欄與獨立的導覽歷史。提供上一頁、下一頁、上層。

`docs/design-spec.md` FR-010:
> 每個 tab 提供網址輸入(可鍵入或貼上)、上一頁、下一頁、上層。導覽歷史為每個 tab 各自獨立。

`docs/design-spec.md` FR-012:
> 無法解析的 Shell location 在 tab 內顯示可復原錯誤,保留設定,並可重試。不得因此刪除任何已儲存的設定。

`docs/design-spec.md` NFR-003:
> Group 切換、版型切換、tab 切換不得因為某個 Shell location 緩慢或無法連線而凍結 UI。
（`navigate()`/`navigate_up()` 都是既有的非同步 `IExplorerBrowser` 呼叫路徑,本 ticket不引入任何同步等待或阻塞呼叫。）

`AGENTS.md`:
> Never persist a PIDL or a COM pointer. Persisted identity is parsing name plus known-folder identity plus a fallback path.
（網址列提交的文字直接當作新的 `parsing_name` 傳給 `navigate()`,不嘗試自己解析成 PIDL 再存。）

## Files to read and trace first

- `docs/tickets/PD-018-tab-navigation-history.md`——`record_navigation`/`navigate_tab_back`/`navigate_tab_forward`/`can_navigate_tab_back`/`can_navigate_tab_forward` 的最終簽章(交接區記錄)。
- `docs/tickets/PD-019-tab-strip-and-realize-on-activation.md`——tab 條的最終 chrome 高度、`switch_active_tab`/`refresh_tab_strip` 等函式,本 ticket 的導覽列要接在同一段 pane chrome 佈局邏輯裡。
- `src/explorer_host/explorer_host.h`/`.cpp`——`navigate()`、`navigation_complete()`、`navigation_failed()`、`translate_accelerator()`(網址列的 `EDIT` 控制項若搶走鍵盤焦點,要確認不影响既有 `translate_accelerator` 轉發路徑)。
- `src/app_shell/main.cpp`——`active_tab()`、`capture_locations()`、`apply_layout()`、`layout_metrics()`、`scaled_value()`。

## Scope

1. `ExplorerHost` 新增 `set_navigation_callback(std::function<void(std::wstring_view)>)` 與 `navigate_up() noexcept`(決策 1、3),`navigation_complete()` 尾端呼叫已設定的回呼。
2. `AppState` 新增 `std::array<HWND, kExplorerCount> address_bars{}`、`back_buttons{}`、`forward_buttons{}`、`up_buttons{}`,以及 `suppress_history_record{}`(決策 2)。`WM_CREATE` 建立四組(每個 pane 一組):一個 `EDIT`、三個 `BUTTON`。
3. pane chrome 佈局(延伸 PD-019 的 `layout_pane_chrome`):在 tab 條下方再切出一段固定高度的導覽列,由左到右排列上一頁/下一頁/上層三顆按鈕與網址列(網址列填滿剩餘寬度),`ExplorerHost` 的 rect 再扣掉這段高度。
4. 為每個 pane 的 `ExplorerHost` 呼叫 `set_navigation_callback`,回呼實作(決策 1、2):
   ```cpp
   if (state.suppress_history_record[pane_index]) {
       state.suppress_history_record[pane_index] = false;
   } else {
       core::record_navigation(active_tab(group.panes[pane_index]),
                                location(std::wstring(new_location)));
   }
   refresh_navigation_buttons(state, pane_index);  // 依 can_navigate_tab_back/forward 更新 EnableWindow
   SetWindowTextW(state.address_bars[pane_index], active_tab(...).location.parsing_name.c_str());
   ```
5. 上一頁/下一頁按鈕的 `BN_CLICKED` 處理:呼叫 `core::navigate_tab_back`/`navigate_tab_forward`,若回傳 `true`,設 `suppress_history_record[pane_index] = true`,再對該 pane 的 `ExplorerHost` 呼叫 `navigate(active_tab(...).location.parsing_name)`。
6. 上層按鈕的 `BN_CLICKED` 處理:直接呼叫該 pane `ExplorerHost::navigate_up()`(不設抑制旗標——上層導覽是新的一步,要記入歷史,見決策 2)。
7. 網址列:`EDIT` 控制項的 subclass 或 `WM_COMMAND`/`EN_*` 攔截 Enter 鍵(比照 `sidebar.cpp` 的 `edit_proc` subclass 模式),提交時取出文字、對該 pane 的 `ExplorerHost` 呼叫 `navigate(text)`。不清除文字,失敗與否都交給既有 `navigation_failed()` 顯示錯誤視窗(決策 5)。
8. `refresh_navigation_buttons(AppState&, std::size_t pane_index)`:用 `EnableWindow` 依 `core::can_navigate_tab_back`/`can_navigate_tab_forward` 啟用/停用對應按鈕。tab 切換(PD-019 的 `switch_active_tab`)之後也要呼叫這個函式與更新網址列文字——換了 active tab,導覽按鈕的可用狀態與網址列內容都要跟著換。

## Non-goals

- 不做網址自動完成/下拉建議(決策 4)。
- 不做「上一頁/下一頁」的歷史清單下拉選單(像瀏覽器按住上一頁按鈕看清單那種)——只做單步。
- 不修改 `core::model.h` 的簽章(PD-018 已經定案)。
- 不做鍵盤快速鍵(`Alt+Left`/`Alt+Right`/`Backspace` 等)——PD-021。
- 不處理網址列貼上/輸入時的路徑驗證或自動修正——原樣傳給 `ExplorerHost::navigate()`,由 Shell 自己判斷能不能解析。

## Acceptance

1. 每個 pane 的導覽列顯示網址列與三顆按鈕,初始狀態:剛啟動時上一頁/下一頁按鈕停用(歷史為空),上層按鈕永遠啟用。
2. 在網址列輸入一個有效路徑並按 Enter,對應 pane 正確導覽過去,網址列文字與 `ExplorerHost` 顯示內容一致;上一頁按鈕變為啟用。
3. 在 Shell view 內雙擊資料夾導覽後,上一頁按鈕啟用;按上一頁能正確回到雙擊前的位置,網址列文字同步更新;此時下一頁按鈕啟用,按下一頁能回到雙擊後的位置。
4. 按上層按鈕能導覽到父資料夾(在一般檔案系統路徑與至少一個虛擬命名空間節點,例如「本機」下的磁碟機圖示,都要驗證)。
5. 在網址列輸入一個無法解析的路徑,pane 內顯示既有的可復原錯誤狀態,網址列文字不被清空,该 tab 的其他設定不受影響(FR-012)。
6. 切換 PD-019 建立的 tab 條到另一個 tab 後,導覽列的按鈕啟用狀態與網址列文字正確反映新 active tab 的歷史與位置。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
8. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "windows\.h|SBSP_PARENT|std::function" src\core
# 預期:src\core 內無命中(std::function 回呼與 SBSP_PARENT 屬於 explorer_host/app_shell)
git diff --check
```

```powershell
Start-Process .\build\PaneDock.exe
Start-Sleep -Seconds 2
Get-Process PaneDock | Select-Object Responding
# 手動(不涉及鍵盤/滑鼠自動化,留給使用者):網址列輸入、雙擊資料夾後按上一頁/下一頁、按上層、輸入錯誤路徑
```

## Handoff requirements

- `ExplorerHost::set_navigation_callback` 的最終簽章與 `navigate_up()` 使用的確切旗標組合(是否只用 `SBSP_PARENT`,還是搭配了其他 `SBSP_*` 旗標),供之後任何需要理解導覽事件流的 ticket 參考。
- `suppress_history_record` 旗標在「使用者快速連續點兩下上一頁,第二次點擊發生在第一次 `navigate()` 尚未觸發 `OnNavigationComplete` 之前」這種競態下是否正確——記錄下實際觀察或程式碼層面的論證(理論上旗標在 `navigate()` 呼叫前設 `true`、在對應的完成回呼才清除,只要 `OnNavigationComplete` 保證與 `navigate()` 呼叫順序一致就不會有問題,但值得明確記錄推理過程)。
- 決策 4(不做自動完成)、決策 5(失敗不清空網址列)這兩項若與使用者實際使用體感不符,記錄下來作為後續 UI 打磨 ticket 的輸入。

## 交接區

<!-- 實作 agent 填寫,append-only -->
