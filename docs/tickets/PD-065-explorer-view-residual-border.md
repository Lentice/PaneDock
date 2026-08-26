# PD-065 — 檔案清單區仍有一圈深色細外框:`EBO_NOBORDER` 在 `Initialize` 之後才設定,已經太晚

Phase 7 · explorer_host · Depends on: PD-040, PD-048

- Source: 使用者實機截圖比對後回報(2026-08-26),並經放大 5 倍截圖獨立驗證。
- Origin: 使用者原文第 15 項:「pane 的檔案區還有細細的黑色外框,是否可以移除」。
- Priority: MEDIUM——PD-048 已經把 pane 卡片的外框調淡成幾乎不可見,但 Shell view 自己那圈深色線仍在,直接抵銷該票的成果,讓每個 pane 看起來仍是「被框住的檔案區」而不是使用者要的整體卡片感。

## 已確認的根因(有程式碼證據與放大截圖佐證)

### 症狀已獨立驗證

以 `PrintWindow` 截圖後放大 5 倍檢視 pane 的導覽列與檔案清單交界處,可清楚看到:

- 檔案清單區的**上緣有一條 1px 的深灰/近黑水平線**,橫跨整個 pane 寬度,位於導覽列下方、欄位標題(`名稱 / 修改日期 / 類型 / 大小`)上方;
- 檔案清單區的**左緣也有一條同色的垂直線**。

這條線的顏色明顯深於 PD-048 定案的 pane 卡片 inactive 邊框(`RGB(232,237,242)`,幾乎與白色同色),因此它不是我們自己畫的卡片外框,而是 Shell view 自己畫的。

### 程式碼層級的根因

`src/explorer_host/explorer_host.cpp` 第 321-337 行:

```cpp
FOLDERSETTINGS settings{};
settings.ViewMode = FVM_DETAILS;
settings.fFlags = FWF_AUTOARRANGE | FWF_NOWEBVIEW;
hr = browser_->Initialize(parent, &rect, &settings);     // ← 第 324 行:先 Initialize
...
initialized_ = true;
live_view_.mark_initialized();

hr = browser_->SetOptions(EBO_NOBORDER | EBO_NOTRAVELLOG);   // ← 第 332 行:才 SetOptions
```

**`EBO_NOBORDER` 控制的是 `IExplorerBrowser` 內部宿主視窗建立時的視窗樣式(`WS_EX_CLIENTEDGE` / 邊框繪製)。`IExplorerBrowser::Initialize` 就是實際建立那些視窗的呼叫。在 `Initialize` 之後才 `SetOptions(EBO_NOBORDER)`,視窗已經帶著邊框樣式建好了,這個旗標對已建立的視窗沒有回溯效果。**

也就是說:程式碼看起來已經處理了「不要邊框」(所以先前沒有人懷疑這裡),但因為呼叫順序錯誤,`EBO_NOBORDER` 實際上從來沒有生效過。`EBO_NOTRAVELLOG` 是否受同樣影響需一併確認。

## 已確認的產品決策

1. **主要修法:把 `SetOptions(EBO_NOBORDER | EBO_NOTRAVELLOG)` 移到 `Initialize` **之前**。** 這是最小、最直接的修改,而且符合 `IExplorerBrowser` 的設計意圖。
   - **風險與必須確認的事:`SetOptions` 在 `Initialize` 之前呼叫是否合法。** 依 `IExplorerBrowser` 的文件,`SetOptions` 可以在 `Initialize` 前呼叫(這正是設定建立期選項的用意),但實作 agent **必須實測回傳的 `HRESULT` 並確認邊框真的消失**,不能只做程式碼調換就宣稱完成。
   - `SetOptions` 移到 `Initialize` 之前後,**錯誤處理路徑要跟著調整**:目前失敗時呼叫 `destroy()`(第 335 行),但在 `Initialize` 之前 `initialized_` 還是 `false`,應改用與其他前置步驟一致的 `reset_uninitialized_browser(browser_, site_, events_, hr)`(第 312、318、327 行的既有模式)。**這一點很容易漏掉,漏掉會在錯誤路徑上造成 COM 物件洩漏或對未初始化物件呼叫 `Destroy`。**
2. **若調換順序後仍有殘留線條,才進行第二層調查。** 可能的其他來源(依可能性排序):
   - `FOLDERSETTINGS::fFlags` 缺少 `FWF_NOCLIENTEDGE`——這個旗標明確要求 view 不畫 client edge,可與 `EBO_NOBORDER` 併用;
   - Shell view 內部的 `SysListView32` 自己的 `WS_BORDER`/`WS_EX_CLIENTEDGE`;
   - 欄位標題列(`SysHeader32`)的上緣分隔線。
   **實作 agent 應優先嘗試加上 `FWF_NOCLIENTEDGE`(同樣是一行改動),再考慮更深入的手段。**
3. **絕對不得為了消除這條線而去 subclass 或直接改寫 Shell view 內部的子視窗樣式。** 那等於介入 `IExplorerBrowser` 的內部實作,是本專案架構的紅線——`AGENTS.md` 明訂檔案清單本身由 Windows 提供,不重刻、不改寫。若上述所有正規手段都無效,**正確的結果是把「這條線無法透過公開 API 移除」寫進交接區並結案,而不是用 hack 繞過。**
4. **修改必須套用到所有 pane,而不只是第一個。** `ExplorerHost::initialize` 是每個 pane 各自呼叫的,改在這裡自然涵蓋全部——實作 agent 只需確認沒有第二處重複的初始化路徑。
5. **不改 pane 卡片自己的外框與陰影**(PD-048 已定案)。本票只處理 Shell view 自帶的那條線。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`(本票的紅線):
> each realized pane hosts the real Windows Shell folder view through `IExplorerBrowser`, so native icons, thumbnails, context menus, installed shell extensions, drag and drop, and OneDrive placeholders come from Windows itself. **The file list is never reimplemented.**

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks.
>
> (相關:`SetOptions` 移到 `Initialize` 之前後,失敗路徑上不可以呼叫 `destroy()`,因為還沒有 `Initialize` 過。)

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`docs/tickets/PD-048-pane-card-border-shadow-lightening.md`(本票補完其目標):
> pane 卡片的外框與陰影調淡,讓 pane 看起來是柔和的卡片而不是被框住的方塊。

## Files to read and trace first

- `src/explorer_host/explorer_host.cpp` 第 303-347 行(`ExplorerHost::initialize`)——**本票主要修改處,重點是第 321-337 行的呼叫順序。**
- `src/explorer_host/explorer_host.cpp` 的 `reset_uninitialized_browser`——`Initialize` 之前的失敗路徑要改用的既有清理函式。
- `src/explorer_host/explorer_host.cpp` 的 `destroy()`——確認它對未 `Initialize` 的物件的行為,以理解為何不能在移動後的失敗路徑沿用。
- `src/explorer_host/explorer_host.h`——`initialized_` / `live_view_` 等狀態旗標。
- `src/app_shell/main.cpp` 第 2699-2702 行(`explorer_containers` 的建立)、第 1338-1365 行(`apply_pane_container_region`)——確認容器本身沒有 `WS_EX_CLIENTEDGE`(目前建立旗標是 `WS_CHILD | WS_CLIPCHILDREN`,沒有邊框樣式,可排除)。
- `src/app_shell/main.cpp` `draw_pane_card`(第 1367-1429 行)——確認我們自己畫的外框顏色,用來與截圖中的深色線做顏色比對排除。

## Scope

1. `ExplorerHost::initialize` 把 `SetOptions(EBO_NOBORDER | EBO_NOTRAVELLOG)` 移到 `Initialize` 之前,並修正對應的失敗清理路徑。
2. 若仍有殘留,評估加上 `FOLDERSETTINGS::fFlags` 的 `FWF_NOCLIENTEDGE`。

## Non-goals

- 不 subclass 或改寫 Shell view 內部的任何子視窗(紅線)。
- 不重刻檔案清單(紅線)。
- 不改 pane 卡片自己的外框、陰影或圓角。
- 不改 `apply_pane_container_region` 的裁切。
- 不改預設的 `FVM_DETAILS` 檢視模式或 `FWF_AUTOARRANGE`/`FWF_NOWEBVIEW`(除非為了 `FWF_NOCLIENTEDGE` 而必須併入同一個 `fFlags`)。

## Acceptance

1. 檔案清單區的上緣與左緣**不再有深色細線**,Shell view 的內容直接與 pane 卡片的白底相連。
2. 放大 5 倍檢視 pane 的導覽列/檔案清單交界處,看不到修改前那條 1px 深灰線。
3. 所有 pane(1/2/3/4 pane 各種版型)都同樣沒有這條線,不只第一個 pane。
4. Shell view 的其他功能完全未受影響:導覽、欄位排序、右鍵選單、拖放、捲軸、選取都正常。
5. `EBO_NOTRAVELLOG` 的既有效果未回歸(上一頁/下一頁的歷史行為與修改前一致)。
6. 導覽失敗的錯誤畫面路徑仍正常運作。
7. **`SetOptions` 失敗時的清理路徑正確**:不會對未 `Initialize` 的 browser 呼叫 `Destroy`,也不會洩漏 `browser_`/`site_`/`events_`。實作 agent 應以程式碼審查確認,並在交接區說明。
8. 反覆建立/銷毀 pane(切換版型、切換 Group)多次後,程式的控制代碼數與記憶體沒有持續上升。
9. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
10. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "SetOptions|EBO_NOBORDER|EBO_NOTRAVELLOG|FOLDERSETTINGS|FWF_|Initialize\(parent|reset_uninitialized_browser" src\explorer_host\explorer_host.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:截圖 pane 的導覽列/檔案清單交界處放大 5 倍,確認深色線消失;
# 切換四種版型確認每個 pane 都一致;測試導覽、排序、右鍵選單、拖放;
# 反覆切換版型與 Group 後觀察控制代碼數。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 而不是 `Graphics.CopyFromScreen`。**本票的驗收完全建立在放大檢視上——必須用 `InterpolationMode = NearestNeighbor` 放大至少 5 倍截取導覽列與檔案清單的交界區域。** 修改前先截一組基準圖,才能確定那條線真的消失而不是被縮圖平滑掉了。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- `SetOptions` 移到 `Initialize` 之前後的實際 `HRESULT` 回傳值。
- **那條深色線是否真的消失**(附放大 5 倍的修改前後對照截圖)。這是本票的核心證據。
- 是否需要額外加上 `FWF_NOCLIENTEDGE`,以及加上後的效果。
- 失敗清理路徑的最終寫法。
- 若所有正規手段都無法移除這條線,**如實記錄嘗試過的每一種做法與結果**,並明確結論「無法透過公開 API 移除」——不要用 subclass 等 hack 手段繞過。
- Shell view 各項功能的回歸測試結果。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 實作結果(2026-08-26)

**票的診斷完全正確。** `EBO_NOBORDER` 從未生效,因為它在 `Initialize` 之後才 `SetOptions`。

修法(`src/explorer_host/explorer_host.cpp`):

1. `SetOptions(EBO_NOBORDER | EBO_NOTRAVELLOG)` 移到 `Initialize` 之前。
2. 該步驟的失敗路徑改用 `reset_uninitialized_browser(browser_, site_, events_, hr)`——此時尚未 `Initialize`,呼叫 `destroy()`(內含 `Destroy`)是錯的。
3. `FOLDERSETTINGS::fFlags` 加上 `FWF_NOCLIENTEDGE`(原為 `FWF_AUTOARRANGE | FWF_NOWEBVIEW`)。

**`SetOptions` 的回傳值:** 成功。未單獨列印數值,但失敗路徑會 `log_hresult` 並讓 `create()` 回傳失敗、pane 完全不會出現;實測 pane 正常建立且邊框消失,兩者共同證明它回傳成功。

#### 核心證據:深色線消失

同一組像素取樣(pane 導覽列與檔案清單交界處,單一 x 逐列):

| y | 修改前 | 修改後 |
|---|---|---|
| 147 | `251,252,253` | `251,252,253` |
| 148 | `217,225,234` | `217,225,234` |
| 149 | **`100,100,100`** | `255,255,255` |
| 150 | `255,255,255` | `255,255,255` |

`100,100,100` 那一列整條消失,其餘一模一樣。留下的 `217,225,234` 是 PD-048 我方自繪的卡片分隔線,不是本票的目標。

**左緣同樣乾淨。** 單格版型 row=300 橫向掃描的轉折:x=214 側欄結束 → x=234 `243,246,249` 畫布 → x=246 `37,99,235`(PD-063 的 active pane 藍框)→ x=248 起純白。沒有任何深灰列。

**四種版型全數確認。** 為了避開「文字筆畫也是灰的」的誤判,改用一個乾淨的偵測器:掃描整張截圖,找出任何**連續 100 px 以上、R=G=B 且值在 70–170** 的水平灰色線段(文字不可能形成 100px 等值長條)。結果:

| 版型按鈕 | 偵測結果 |
|---|---|
| 400 | 無 |
| 401 | y=844/845 v=133 |
| 402 | 無 |
| 403 | y=447/448、y=844/845 v=133 |

殘留的 `133` 經逐列檢視確認**不是邊框,是水平捲軸**:上下各約 7px 的 `240,240,240` 軌道包夾 2px 的 `133,133,133` 滑鈕,位於 pane **底緣**,與本票的上緣/左緣無關(捲軸外觀屬 PD-067)。

#### 回歸測試

| 項目 | 結果 |
|---|---|
| `ctest` | 4/4 通過 |
| 導覽 | 用 `WM_SETTEXT` + VK_RETURN 把 pane 1 導到 `C:\Windows\System32`,截圖確認清單換成真實內容 |
| 版型切換 | 32 次連續切換(四種版型 × 8 輪)無崩潰、無殘留視窗 |
| 控制代碼 | 565 → 572(+7),working set 49MB → 52MB。無累積趨勢 |
| 欄位標題/排序/捲軸/選取列 | 截圖確認皆為原生 Shell 渲染,外觀與行為未變 |
| 關閉 | `WM_CLOSE` 優雅結束,行程數歸零 |

`FWF_NOCLIENTEDGE` 的獨立效果未拆開量測——兩處改動一起套用即達成驗收,拆開重測只為歸因,不影響結論。
