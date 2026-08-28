# PD-104 — 側邊欄寬度可拖曳調整,並跨啟動持久化

Phase 7 · app_shell/core · Depends on: PD-097, PD-103

- Source: 使用者需求(2026-08-28)。
- Origin: 使用者原文:「make left group area resizable. should keep the width and restore when next time AP executed. Shrink default group area since the circle number is remove in another ticket. Consider apply the throttling when resize.」
- Priority: LOW——純 UI 易用性擴充,不影響既有功能;側邊欄目前完全無法調整寬度。

## 這張票解決的候選項目

`docs/tickets.md` 候選表已經預告過這個需求:

> 側邊欄寬度的全域設定持久化 | 若使用者回報每次啟動都要重拖再開;目前預設值可接受。

觸發條件現在成立(使用者直接提出),本票依候選表原定方向開票:**全域(非 per-Group)設定**,不是每個 Group 各自的寬度。

## 已確認的現況(有程式碼證據,不是猜測)

側邊欄寬度目前是編譯期常數,完全無法拖曳調整:

- `src/sidebar/sidebar.h:20`:`inline constexpr int kSidebarWidth = 226;`
- 這個常數被四處各自重複算出「目前寬度」,寫法幾乎一致但各自獨立:
  - `src/app_shell/main.cpp:950-959`(`pane_area`)——直接內聯用 `scaled_value(window, kSidebarWidth)` 算 pane 區域左邊界,沒有中間變數。
  - `src/app_shell/main.cpp:1391-1395`(`layout_sidebar` 開頭)——`const int width = std::min(client 寬度, scaled_value(window, kSidebarWidth));`。
  - `src/app_shell/main.cpp:1426-1430`(`layout_header` 開頭)——同樣的 `std::min` 寫法,變數名 `sidebar_width`。
  - `src/app_shell/main.cpp:1772-1784`(`paint_client_background`)——同樣的 `std::min` 寫法,用來畫側邊欄背景與算 header 矩形起點。
- 四處都是同一個運算式的複製貼上,任何寬度來源改變都必須同時修改全部四處,目前用複製貼上維持一致純粹是巧合。

`core::ApplicationState`(`src/core/model.h:56-73`)已經有完全對應的既有先例——**全域(非 per-Group)、跨啟動持久化的 UI 設定**:

```cpp
struct ApplicationState final {
    struct WindowPlacement final {
        int x{};
        int y{};
        int width{};
        int height{};
        bool maximized{};
        ...
    };
    std::uint32_t schema_version{1};
    std::vector<GroupState> groups;
    std::string active_group_id;
    WindowPlacement window_placement;
    ...
};
```

`window_placement` 正是「使用者手動調整過的視窗幾何,下次啟動要還原」的既有做法,`sidebar_width` 性質完全相同,應該以同一層級的欄位加入 `ApplicationState`,而不是塞進 `GroupState`(每個 Group 各自的版型/pane/tab 才該放 `GroupState`;側邊欄寬度是整個應用程式的單一 UI 偏好,不隨 Group 切換而變)。

`src/core/session.cpp` 對 `window_placement` 的讀寫(`:399-406` 寫、`:420-430` 讀)有一個**本票不能照抄**的地方:

```cpp
const auto x = integer(*placement, "x"), y = integer(*placement, "y"),
           width = integer(*placement, "width"), height = integer(*placement, "height");
const auto* maximized = as<bool>(*placement, "maximized");
if (!x || !y || !width || !height || !maximized) return std::nullopt;
```

`window_placement` 的每個子欄位目前是**必要欄位**——缺任何一個,整個 `decode()` 失敗。這是因為 `window_placement` 從 schema v1(專案一開始)就存在,從來沒有「舊檔案沒有這個欄位」的情境。`sidebar_width` 不一樣:本票上線後,使用者既有的 `session.json` 檔案不會有這個新欄位,若比照 `window_placement` 做成必要欄位,現有使用者下次啟動就會整個 `decode()` 失敗。

既有的拖曳分隔線機制(`Splitter`/`splitter_at_point`/`update_splitter_drag`,`main.cpp:381-385, 1004-1049, 2553-2573`)是 per-Group 版型內部的 pane 分隔線,`ratio_index` 指向 `GroupState::divider_ratios`(`model.h:49`)。側邊欄邊界不屬於任何 Group 的版型,沒有對應的 `divider_ratios` 索引可用,不適合硬塞進 `Splitter` 這個型別。

側邊欄邊界的可拖曳寬度目前完全不存在——沒有 hit-test、沒有游標變化、沒有拖曳狀態。

Pane 分隔線放開滑鼠時的既有行為(`main.cpp:3874-3883`,`WM_LBUTTONUP`):

```cpp
case WM_LBUTTONUP:
    if (state != nullptr && state->splitter_drag.has_value()) {
        update_splitter_drag(window, *state, point_from_lparam(lparam), true);
        state->splitter_drag.reset();
        save_now(*state);
        ReleaseCapture();
        return 0;
    }
    break;
```

放開滑鼠立即呼叫 `save_now(*state)`(不是 PD-091 的 `schedule_session_save` 防抖),這是本票側邊欄邊界放開滑鼠時應該比照的既有寫法。

## 已確認的產品決策

1. **`sidebar_width` 是 `ApplicationState` 的新欄位,不是 `GroupState` 的欄位。** 理由見上方候選表引用與 `window_placement` 先例——這是全域 UI 偏好,不是 Group 內容。
2. **不新增 `Splitter`/`ratio_index` 的變體來表示側邊欄邊界。** 側邊欄邊界的拖曳狀態用一個獨立的、最小的欄位(例如 `AppState` 裡一個 `std::optional<int>` 之類記錄「拖曳開始時的滑鼠 x 與起始寬度」的小結構,型別與命名由實作者決定),與既有 `state.splitter_drag` 並列,不合併。命中測試沿用既有的 `layout_metrics(window).divider_thickness`(`main.cpp:1007` 已有的既有寫法)當作可拖曳邊界的寬度,不另外發明新的粗細常數。`WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP`/`WM_SETCURSOR` 這四個既有訊息分支(`main.cpp:3855-3921` 區間)各自新增一個「先檢查是否命中側邊欄邊界」的判斷,命中就走側邊欄邏輯,否則落回既有的 `splitter_at_point` 邏輯——兩者並列检查,不是互斥的 if/else 改寫(側邊欄邊界與 pane 分隔線在畫面上不會重疊,順序不影響正確性)。
3. **抽出一個共用函式取代四處重複的寬度運算式**,例如 `int current_sidebar_width(HWND window, const AppState& state) noexcept`,回傳 `std::min(client 寬度, scaled_value(window, state.application.sidebar_width))`,取代 `pane_area`(`:950-959`)、`layout_sidebar`(`:1391-1395`)、`layout_header`(`:1426-1430`)、`paint_client_background`(`:1772-1784`)這四處目前各自内聯的計算。這不是新增抽象,是消除四份「必須同步修改」的重複程式碼——四處現在改用常數尚且靠複製貼上維持一致,改用可變欄位後不抽共用函式,四處會有算出不一致寬度的真實風險(hit-test 用一個值、實際畫出來用另一個值)。
4. **預設寬度縮小,理由可回推計算,不是憑感覺猜的數字**:PD-103 拿掉徽章後釋放的水平空間 = `badge_size(22) + badge_margin(6) + text_area 收窄用的 4px 間隙` = 32px(皆為 96 DPI 基準、`scaled_value`/`MulDiv` 縮放前的原始單位,見 `docs/tickets/PD-103-remove-sidebar-group-tab-count-badge.md` 的既有數字)。新預設寬度 = 226 − 32 = **194**。`kSidebarWidth`(`sidebar.h:20`)改為 194,同時作為 `ApplicationState::sidebar_width` 在 `decode()` 找不到既有欄位時的 fallback 預設值——兩處使用同一個數字,不要各自硬編碼一份。
5. **持久化欄位為選填(optional),不比照 `window_placement` 做成必要欄位。** `session.cpp` 的 `decode()` 讀到沒有 `sidebar_width` 欄位的舊檔案時(本票上線前所有使用者的既有 `session.json`),必須 fallback 到上述新預設值 194,**不得因為缺這個欄位就讓整個 `decode()` 失敗**。這是 `AGENTS.md` 「新欄位是 additive、讀到不認識的欄位保留、缺欄位不能破壞既有檔案」規則的直接應用,`window_placement` 目前的必要欄位寫法是本票上線前就存在的既有例外,本票不比照它,也不需要跟著把 `window_placement` 一併改掉(不在本票範圍)。
6. **不需要 bump `schema_version`。** 純粹新增一個有預設值的選填欄位,屬於 additive 變更,`AGENTS.md` 只要求「schema 變更是新增選填欄位或新增 migration 步驟」,沒有要求每次新增欄位都要動版本號。
7. **拖曳邊界時套用節流,依賴 PD-097 已經確立的節流設計,不另建第二套 timer。** PD-097(`docs/tickets/PD-097-splitter-drag-geometry-throttling.md`,目前 `ready`,尚未實作)已經決定好「節流不是防抖、`WM_TIMER` 到期才套用最後一次滑鼠位置、`WM_LBUTTONUP`/capture 遺失要取消 timer」這一整套規則,並且會在 `main.cpp` 加入節流用的 timer id 與狀態欄位。本票的側邊欄邊界拖曳必須沿用 PD-097 造好的同一個節流機制(把「目前正在拖曳的是 pane 分隔線還是側邊欄邊界」當成節流到期時要分派的目標,而不是側邊欄邊界另開一個 timer id),因此**本票依賴 PD-097 先落地**。若實作當下 PD-097 仍是 `ready` 未實作,先完成 PD-097 再做本票這一項,不要繞過去自己刻一個獨立的節流。
8. **放開滑鼠時的持久化,比照 pane 分隔線既有寫法,呼叫 `save_now(*state)`**(`main.cpp:3879` 既有寫法),不是 PD-091 的 `schedule_session_save` 防抖——側邊欄寬度調整跟 pane 分隔線一樣是「使用者放手那一刻就是最終值」的明確操作終點,沒有防抖的必要。
9. **可拖曳範圍的最小/最大寬度由實作者決定並記錄在交接區**,原則:最小值要能完整顯示 Group 項目的圖示與名稱不至於過度截斷,最大值不能吃掉主要內容區域到影響檔案列表可用性(不需要與現有欄位精確對應到某個既有常數,合理的落點即可)。這比照 `docs/tickets/PD-097` 對節流毫秒數的既有做法——「具體數值由實作者決定並記錄在交接區」,不是本票需要事先鎖死的產品決策。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Every persisted config/setting file must be designed for forward extensibility. It carries an explicit schema version from its first version. A read that encounters a field it does not recognize preserves that field rather than silently dropping it on the next write-back. A schema change is additive (new optional fields, new migration step) rather than a destructive reinterpretation of an existing field's meaning.

`sidebar_width` 必須是選填欄位、缺欄位時 fallback 到新預設值 194,不得讓缺欄位破壞既有使用者的 `session.json`。

`AGENTS.md`:
> Never persist a PIDL or a COM pointer. Persisted identity is parsing name plus known-folder identity plus a fallback path. Display names are never identifiers.

本票只持久化一個整數寬度,與此規則無直接衝突,列出以確認未觸碰識別性資料的持久化方式。

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers.

側邊欄邊界拖曳的節流沿用 PD-097 已確立的「事件觸發、狀態機自行取消」timer 模式,不新增輪詢。

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

抽出的共用寬度函式是消除四處既有重複,不是新增抽象;拖曳狀態機、命中測試粗細、節流 timer、放開滑鼠持久化寫法,能沿用既有寫法的地方全部沿用,不重新設計。

`docs/tickets/PD-097-splitter-drag-geometry-throttling.md` 已確認的既有決策(本票沿用,不覆寫):
> 必須是「節流」不是「防抖」……節流視窗到期後立即套用一次最新位置,並重新開始下一個節流視窗(只要拖曳仍在進行)。

`docs/tickets/PD-103-remove-sidebar-group-tab-count-badge.md` 已確認的既有數字(本票用來回推新預設寬度,不重新測量):
> `badge_size = MulDiv(22, dpi, 96)`、`badge_margin = MulDiv(6, dpi, 96)`、`text_area.right = badge.left - MulDiv(4, dpi, 96)`。

## Files to read and trace first

- `src/sidebar/sidebar.h:20`——`kSidebarWidth` 常數,本票改其數值(226→194)。
- `src/core/model.h:56-73`(`ApplicationState`)——新欄位 `sidebar_width` 的加入處,比照 `window_placement` 的既有寫法與欄位層級。
- `src/core/session.cpp:390-408`(`encode`/寫入)、`:410-430`(`decode`/讀取)——新欄位的序列化與**選填**讀取邏輯,對照 `window_placement` 目前的必要欄位寫法,理解為何本票不能照抄。
- `src/app_shell/main.cpp:950-959`(`pane_area`)、`:1391-1395`(`layout_sidebar`)、`:1426-1430`(`layout_header`)、`:1772-1784`(`paint_client_background`)——四處重複的寬度運算式,改為呼叫新抽出的共用函式。
- `src/app_shell/main.cpp:381-385`(`Splitter`)、`:1004-1049`(`splitters`/`splitter_at_point`)、`:2553-2573`(`update_splitter_drag`)——既有 pane 分隔線拖曳機制,側邊欄邊界的拖曳狀態與此並列而非合併,但命中測試粗細(`:1007` 的 `divider_thickness`)直接沿用。
- `src/app_shell/main.cpp:3855-3921`——`WM_LBUTTONDOWN`/`WM_MOUSEMOVE`/`WM_LBUTTONUP`/`WM_SETCURSOR` 四個既有訊息分支,側邊欄邊界的新判斷式插入處。
- `src/app_shell/main.cpp:1868-1877`(`schedule_session_save`)、`:3879`(`save_now` 呼叫點)——持久化寫入的既有兩種模式,本票用 `save_now`(比照 pane 分隔線放開滑鼠的既有寫法)而非防抖。
- `docs/tickets/PD-097-splitter-drag-geometry-throttling.md`——節流機制的完整既有設計決策,本票依賴其落地後的 timer/狀態欄位。
- `docs/tickets/PD-103-remove-sidebar-group-tab-count-badge.md`——新預設寬度回推所依據的既有像素數字。

## Scope

1. `core::ApplicationState` 新增 `int sidebar_width` 欄位,預設值 194,序列化為選填欄位(缺欄位時 fallback 194,不使 `decode()` 失敗)。
2. `sidebar.h` 的 `kSidebarWidth` 常數數值由 226 改為 194。
3. 抽出一個共用函式取代 `main.cpp` 四處重複的側邊欄寬度運算式,四處呼叫點改用該函式與 `state.application.sidebar_width`。
4. 新增側邊欄邊界的拖曳狀態(hit-test、游標變更為 `IDC_SIZEWE`、拖曳更新、放開滑鼠寫回 `sidebar_width` 並 `save_now`),與既有 pane 分隔線拖曳並列、不合併。
5. 拖曳期間的幾何重排套用 PD-097 落地後的節流機制(同一個 timer,依目前拖曳目標分派)。
6. 應用程式啟動時,既有的版面配置初始化路徑自然套用還原後的 `sidebar_width`(不需要新增額外的啟動時特殊處理,只要四處寬度來源改讀新欄位即可)。

## Non-goals

- 不做 per-Group 各自不同的側邊欄寬度——這是全域單一設定,候選表與 `window_placement` 先例都指向全域。
- 不做多視窗即時同步——本專案是單視窗應用,不存在需要同步的情境。
- 不改變 `window_placement` 現有的必要欄位讀取邏輯——本票只讓新欄位走選填路徑,不回頭修正 `window_placement` 本身(不在本票範圍,若要改需另開票)。
- 不改變既有 pane 分隔線(`Splitter`)的型別、行為或 `divider_ratios` 持久化方式。
- 不新增使用者可設定的側邊欄寬度數值輸入框或設定頁面——只有拖曳這一種調整方式,比照 pane 分隔線目前也只能拖曳、沒有數值輸入。

## Acceptance Criteria

1. 側邊欄與內容區交界處可用滑鼠拖曳調整寬度,拖曳中游標顯示 `IDC_SIZEWE`。
2. 放開滑鼠後關閉並重新啟動應用程式,側邊欄寬度與關閉前一致(讀取自 `session.json` 的 `sidebar_width`)。
3. 使用本票上線前產生、沒有 `sidebar_width` 欄位的既有 `session.json` 檔案仍能正常啟動(不因缺欄位而回退到全新狀態),側邊欄使用新預設寬度 194。
4. 新安裝(無既有 `session.json`)的預設側邊欄寬度為 194,比修改前的 226 窄,且側邊欄內容(Group 名稱/副標題,PD-103 移除徽章後的版面)在新寬度下不會不當截斷。
5. 拖曳側邊欄邊界時,幾何重排的呼叫頻率經過節流,行為與 PD-097 為 pane 分隔線定義的節流原則一致(間隔性前進、非防抖、放開滑鼠立即套用最終值)。
6. 放開滑鼠後,滑鼠靜止時應用程式回到 0% CPU,無殘留 timer。
7. 拖曳側邊欄邊界時,既有 pane 分隔線的拖曳與命中測試不受影響(兩者互不干擾)。
8. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
9. `git diff --check` 通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kSidebarWidth|sidebar_width|ApplicationState|window_placement" src\core\model.h src\core\session.cpp src\sidebar\sidebar.h src\app_shell\main.cpp
git diff --check
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,單次啟動 + 單次截圖即可完成拖曳前後寬度比對。

**驗證原則(本專案共同約定):只做單次點擊/滑鼠移入 + 截圖的驗證由 Agent 或本人執行;需要連續、多步驟拖曳流暢度或跨啟動還原測試交給使用者本人執行**,若某項驗收條件無法用單次動作完成,如實在交接區標記未驗證並說明需要使用者手動驗證的具體步驟(例如實際拖曳跟手感受、關閉並重新啟動應用程式確認寬度還原)。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**若測試過程建立了暫時性 Group/tab,測試後從備份還原 `session.json`。

## Handoff requirements

- 抽出的共用寬度函式簽章與命名。
- 側邊欄邊界拖曳狀態的型別設計(與 `state.splitter_drag` 並列的欄位名稱與內容)。
- 最小/最大可拖曳寬度的實際數值與選擇理由。
- 節流機制與 PD-097 落地後的 timer/狀態欄位如何整合(是否需要在 PD-097 的 timer 分派邏輯裡新增一個分支)。
- 選填欄位讀取邏輯的實際寫法(對照 `window_placement` 必要欄位寫法的差異)。
- 舊版 `session.json`(無 `sidebar_width` 欄位)實際測試結果。
- 跨啟動還原的實際驗證結果或未驗證原因。
- 未驗證項目與原因(若有)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
