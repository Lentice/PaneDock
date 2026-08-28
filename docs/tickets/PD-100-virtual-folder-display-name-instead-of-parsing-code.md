# PD-100 — 虛擬資料夾(如 This PC)的位址列/分頁標題顯示原始 parsing code,應改為友善顯示名稱

Phase 7 · app_shell, explorer_host · Depends on: none

- Source: 使用者需求(2026-08-28),附截圖:導覽到「This PC」後,位址列與該分頁標題皆顯示原始字串 `::{20D04FE0-3AEA-1069-A2D8-08002B30309D}`,而非「This PC」/「本機」。
- Origin: 使用者原文:「for pane address and tab name, do not display code. display proper name instead.」
- Priority: MEDIUM——不影響功能,但任何導覽到虛擬資料夾(This PC、控制台、資源回收筒、網路等)的分頁,位址列與標題都會顯示無法辨識的 GUID 字串。

## 已確認的根因(有程式碼證據,不是猜測)

整個程式碼庫只有一處呼叫 `GetDisplayName`/`SIGDN`:`src/explorer_host/explorer_host.cpp:595-655`(`navigation_complete` 附近),只解析 `SIGDN_DESKTOPABSOLUTEPARSING`:

```cpp
PWSTR parsing_name = nullptr;
if (FAILED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsing_name))) {
    return;
}
...
location_.assign(parsing_name);
...
if (navigation_callback_) { navigation_callback_(location_); ... }
```

`SIGDN_DESKTOPABSOLUTEPARSING` 回傳的是**識別用途的 parsing name**——對一般檔案系統資料夾,它剛好就是可讀的絕對路徑(例如 `C:\Users\foo\Documents`),所以問題被掩蓋;對虛擬/特殊資料夾(用 CLSID 識別,例如 This PC),parsing name 是 `::{GUID}` 形式,對識別是正確的,但拿來顯示就是原始代碼。

這個 `location_`(即 `core::ShellLocation::parsing_name`)未經任何處理,直接餵給兩個顯示用途:

1. **位址列**:`refresh_navigation_chrome`(`main.cpp:1071-1081`)第 1080 行 `SetWindowTextW(state.address_bars[pane_index], text)`,其中 `text = active_tab(...).location.parsing_name.c_str()`——完全沒有轉換,直接顯示 parsing name 本身。對一般路徑這是對的(顯示完整路徑),對虛擬資料夾就是顯示 GUID。
2. **分頁標題**:`tab_display_text`(`main.cpp:1177-1183`)取 `parsing_name` 最後一個 `\`/`/` 之後的子字串。一般路徑會正確取出葉節點資料夾名(例如 `Documents`);虛擬資料夾的 parsing name 完全沒有 `\`/`/`,函式的既有分支(找不到分隔符時)直接回傳整個原始字串——這正是截圖裡分頁標題顯示完整 GUID 的原因。

## 已確認的產品決策

1. **只修正虛擬/非檔案系統項目的顯示,一般檔案系統路徑的既有行為完全不動。** 原因:目前一般路徑的位址列顯示完整路徑、分頁標題顯示葉節點名稱,兩者都已經是正確、使用者沒有抱怨的行為。若把兩個顯示位置一律改成呼叫 `SIGDN_NORMALDISPLAY`,一般檔案系統項目的 `SIGDN_NORMALDISPLAY` 通常只回傳葉節點名稱(不含路徑)——套用到位址列會讓完整路徑退化成只剩資料夾名稱,是不必要的退步。因此本票的判斷邏輯必須先分辨「這是不是虛擬/非檔案系統項目」,只在該情況下才走友善名稱解析,其餘維持原樣。
2. **判斷「是否為虛擬項目」的方式,由實作 agent 在下列兩種既有 Shell 慣例間擇一,並在交接區記錄理由:**
   - (a) 檢查 `parsing_name` 是否以 `::` 開頭(Windows 對非檔案系統 parsing name 的通用慣例前綴,簡單、不需要額外 COM 呼叫);
   - (b) 用 `IShellItem::GetAttributes` 查詢 `SFGAO_FILESYSTEM` 旗標,未設定時視為虛擬項目(官方定義的語意更精確,但需要多一次 COM 呼叫)。
   
   兩者皆為可接受方案,實作 agent 依複雜度/正確性取捨自行決定,不需要回頭問使用者。
3. **友善名稱的解析時機與方式:** 用 `SHCreateItemFromParsingName` 從既有的 `parsing_name` 字串建立 `IShellItem`,再呼叫 `GetDisplayName(SIGDN_NORMALDISPLAY, ...)`。這是獨立、一次性的 Shell API 呼叫,不需要建立或存在任何 `IExplorerBrowser` view,因此對**尚未 realize 的 inactive tab 一樣可行**,不違反 realize-on-activation 規則。呼叫時機比照現有兩個顯示點各自的既有更新時機(位址列在 `refresh_navigation_chrome`、分頁標題在建立/更新 `TabVisual` 時),不是每次重繪都重新解析。
4. **解析失敗時的 fallback:** 直接維持目前行為,顯示原始 `parsing_name`(即修正前的結果)。不得因為解析失敗而顯示空字串或造成例外——這是「不會比現狀更差」的安全網,不需要額外錯誤 UI。
5. **不擴大到磁碟機根目錄(例如 `C:\`)的顯示名稱升級(原生 Explorer 會顯示「Local Disk (C:)」)。** 磁碟機根目錄的 parsing name(`C:\`)本身仍是路徑形式(有 `\`),不落入本票判斷邏輯的「虛擬項目」分支,維持顯示 `C:\` 不變。使用者本次回報的是虛擬資料夾(This PC)的 GUID 顯示問題,不是磁碟機根目錄的命名風格;若要一併處理,列為候選,需另開票。
6. **不在 `core::ShellLocation`/`TabState` 新增任何欄位或持久化任何顯示名稱。** 友善名稱是可隨時從 `parsing_name` 重新推導的衍生值,不是識別身分,持久化它會違反 `AGENTS.md`「Display names are never identifiers」以及「Never persist a PIDL or a COM pointer」的精神(即使不是 PIDL,快取一個可能過期的顯示字串到 session 檔案也沒有必要)。此外 `SHCreateItemFromParsingName`/`GetDisplayName` 屬於 Shell COM API,必須留在 `app_shell`/`explorer_host` 層,`src/core` 依 `AGENTS.md` 規則本來就不得出現 HWND/COM/`windows.h`。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Never persist a PIDL or a COM pointer. Persisted identity is parsing name plus known-folder identity plus a fallback path. Display names are never identifiers.

本票新增的是**顯示層**的友善名稱解析,完全不觸碰持久化的識別身分(`parsing_name`/`known_folder`/`fallback_path` 三者不變),與此規則相容而非衝突。

`AGENTS.md`:
> Keep `src/core` free of HWND, COM and `windows.h`. It is the only automated test seam in this project.

友善名稱解析(`SHCreateItemFromParsingName`/`IShellItem::GetDisplayName`)是 Shell COM API,必須放在 `explorer_host` 或 `app_shell`,不得下放到 `core`。

`AGENTS.md`:
> Only the visible pane's active tab holds a live `IExplorerBrowser`. Inactive tabs persist as data and are realized on activation.

友善名稱解析不建立、不需要 `IExplorerBrowser`,因此可以安全地套用在尚未 realize 的 inactive tab 上,不違反此規則。

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

只在「偵測到虛擬項目」這個新分支裡插入一次 Shell API 呼叫,其餘既有的位址列/分頁標題邏輯不變動。

## Files to read and trace first

- `src/explorer_host/explorer_host.cpp:595-655`——`navigation_complete` 現有的 `SIGDN_DESKTOPABSOLUTEPARSING` 解析與 `location_`/`navigation_callback_` 賦值處,新增友善名稱解析的可能落點之一。
- `src/app_shell/main.cpp:1071-1081`(`refresh_navigation_chrome`)——位址列文字來源,修改處之一。
- `src/app_shell/main.cpp:1177-1183`(`tab_display_text`)——分頁標題文字來源,修改處之一。
- `src/core/model.h`——確認 `ShellLocation`/`TabState` 現有欄位,本票不得新增持久化欄位(僅供核對決策 6)。

## Scope

1. 新增一個判斷函式(位置由實作 agent決定,建議 `main.cpp` 或 `explorer_host.cpp`,不進 `core`):給定 `parsing_name`,判斷是否為虛擬/非檔案系統項目;是的話嘗試用 `SHCreateItemFromParsingName` + `SIGDN_NORMALDISPLAY` 解析出友善名稱,失敗則回傳原始 `parsing_name`。
2. `refresh_navigation_chrome` 的位址列文字改用上述函式的結果(一般路徑行為不變,虛擬項目改顯示友善名稱)。
3. `tab_display_text` 對虛擬項目(現有分支:找不到 `\`/`/` 分隔符時)改用上述函式的結果,不再直接回傳整個原始 parsing name。

## Non-goals

- 不改變一般檔案系統路徑的位址列(完整路徑)或分頁標題(葉節點名稱)既有顯示邏輯。
- 不處理磁碟機根目錄(`C:\`)的原生風格命名(如「Local Disk (C:)」)——候選,需另開票。
- 不在 `core::ShellLocation`/`TabState` 新增欄位或持久化友善名稱。
- 不改變位址列/分頁標題以外的任何 UI(檔案列表本身是原生 Shell view,顯示邏輯由 Windows 決定,不在本票範圍)。
- 不改變導覽/session 還原時識別身分(`parsing_name`/`known_folder`/`fallback_path`)的解析或比對邏輯。

## Acceptance Criteria

1. 導覽到 This PC 後,位址列顯示「This PC」(或系統當前語系的等效字串,如「本機」),不再顯示 `::{GUID}`。
2. 導覽到 This PC 後,該分頁標題顯示同樣的友善名稱,不再顯示 `::{GUID}`。
3. 導覽到一般檔案系統資料夾(例如 `C:\Users\<user>\Documents`)後,位址列與分頁標題的顯示與修改前完全相同(無退步)。
4. 對尚未被啟用(inactive、未 realize)的虛擬資料夾分頁,分頁標題一樣正確顯示友善名稱,不需要等到該分頁被啟用才更新。
5. 友善名稱解析失敗時(例如極端情境下的無效 parsing name),位址列/分頁標題退回顯示原始 parsing name,不當機、不顯示空白。
6. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
7. `git diff --check` 通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "SIGDN_DESKTOPABSOLUTEPARSING|SIGDN_NORMALDISPLAY|SHCreateItemFromParsingName|tab_display_text|refresh_navigation_chrome" src\explorer_host\explorer_host.cpp src\app_shell\main.cpp
git diff --check
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,單次啟動 + 導覽到 This PC + 單次截圖即可完成驗證,不需要連續互動。

**驗證原則(本專案共同約定):只做單次動作(啟動、導覽、截圖)的驗證由 Agent 或本人執行;需要連續、多步驟操控滑鼠鍵盤的測試交給使用者本人執行。**

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 「是否為虛擬項目」的最終判斷方式(`::` 前綴 vs. `SFGAO_FILESYSTEM`)與理由。
- 友善名稱解析函式的最終放置位置(`explorer_host.cpp` 或 `main.cpp`)與理由。
- This PC 導覽後位址列/分頁標題的實際截圖結果。
- 一般檔案系統路徑(修改前/修改後對照)確認無退步的驗證結果。
- 未驗證項目與原因(若有)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 實作交接（2026-08-28）

- 「是否為虛擬項目」採 `parsing_name` 以 `::` 開頭的既有 Shell 慣例判斷。這能直接涵蓋 This PC 等 CLSID/非檔案系統 parsing name，不必為每次顯示再做 `SFGAO_FILESYSTEM` 查詢；一般路徑（包含 `C:\`）不會進入此分支。
- 友善名稱解析函式放在 `src/app_shell/main.cpp`，因為位址列與 tab 標題都是 app-shell 顯示層，且 `SHCreateItemFromParsingName`/`IShellItem::GetDisplayName` 屬 Shell COM；這樣不會把 COM 帶進 `src/core`，也能在重建 tab visuals 時替 inactive tab 解析名稱。
- 實機截圖：Release `PaneDock.exe` PID 41492，主視窗 `0x3F18DC`；以 `GetDlgItem`/`EnumChildWindows` 定位第一 pane 的 tab strip（ID 200，`242,83-1069,114`）與 address bar（ID 330，`409,120-1063,136`），`PrintWindow(hwnd, hdc, 2)` 成功。以 4x `InterpolationMode.NearestNeighbor` 放大後，active tab 與位址列皆顯示系統語系的「本機」，未再顯示 `::{20D04FE0-3AEA-1069-A2D8-08002B30309D}`。完整截圖：`C:\Users\lenticetsai\AppData\Local\Temp\panedock-pd100-printwindow-20260828-112120.png`；chrome 放大圖：`C:\Users\lenticetsai\AppData\Local\Temp\panedock-pd100-chrome-4x-nearestneighbor-20260828-112120.png`。
- 一般檔案系統路徑無退步：修改後非 `::` parsing name 直接回傳原字串，tab 的分隔符取葉節點邏輯未改；同一張實機截圖中其餘 pane 仍分別顯示 `C:\Windows`/`Windows`、`C:\Users`/`Users`、`C:\Program Files`/`Program ...`，與修改前既有行為一致。未對 drive root 做友善命名升級。
- 未驗證：未另外建立 inactive virtual tab，也未故意導覽無效 parsing name 觸發 fallback；前者會改動使用者 session，後者需額外多步驟互動。本次以 `refresh_tab_strip` 對所有持久化 tab 呼叫同一解析函式的程式碼路徑，以及失敗時回傳原字串的分支完成核對。
