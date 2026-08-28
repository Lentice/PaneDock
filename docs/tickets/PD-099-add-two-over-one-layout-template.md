# PD-099 — 新增第 6 種版型:上方兩格併排、下方一格全寬(2 up / 1 down)

Phase 7 · core, app_shell · Depends on: PD-005, PD-016, PD-046

- Source: 使用者需求(2026-08-28),附參考截圖:上排並排兩個矩形,下排一個橫跨全寬的矩形。
- Origin: 使用者原文:「Add new layout, 3 panes (2 at up half, 1 at bottom half)」。
- Priority: MEDIUM——新增功能,使用者直接提出的具體版型需求。

## 這不是重開「任意遞迴 pane 分割」

`docs/tickets.md` 的「已否決的方向」表列了「任意遞迴 pane 分割」,依據 `docs/design-spec.md` §3.2,否決理由是「固定五種版型已涵蓋實際需求,遞迴分割使版型狀態、還原與矩形計算複雜度大幅上升」,重開條件是「先有使用者實際回報五種版型不足的情境」。

本票**不是**重開該方向:本票新增的是**第 6 個固定、具名的版型**(與現有 `single`/`left_right`/`top_bottom`/`three_pane`/`four_pane_grid` 同一種模式——`LayoutTemplate` enum 裡多一個具體值,`compute_layout_rects` 多一個 `case`),不是開放任意遞迴分割的機制。使用者提出的「2 up / 1 down」是現有 5 種版型都無法表達的具體形狀(見下方「與既有 `three_pane` 的差異」),這正是重開條件所要求的「使用者實際回報既有版型不足的情境」的實質內容,故本票同時滿足重開條件,雙重確認可以進行。

## 需要覆寫的既有決策(明確覆寫聲明)

`docs/design-spec.md` FR-003:
> 支援且僅支援五種版型:單一、左右、上下、三分割、四宮格。

**覆寫為:支援六種版型,新增「2 up / 1 down」。** 新證據:使用者本次直接提出的具體需求(見上方 Source/Origin)。`docs/design-spec.md` §4.3「在當前 Group 內可切換五種版型」與 §3.2 條列的既有五版型說明需同步更新為六種——這是本票 Scope 的一部分,不是留給未來的 TODO。

## 與既有 `three_pane` 的差異(避免誤判為重複)

現有 `LayoutTemplate::three_pane`(`src/core/layout.cpp:54-64`)的幾何是:**左邊一個直通全高的 pane,右邊上下兩個 pane 疊放**(先左右分割 `ratios[0]`,再對右半邊做上下分割 `ratios[1]`)。

使用者要的是**轉置**:**上排左右兩個 pane 並排,下排一個橫跨全寬的 pane**(先上下分割,再對上半邊做左右分割)。這是完全不同的矩形佈局,不能靠調整 `three_pane` 的比例或參數達成,必須是新的 `case`。

## Fix 方向

比照現有 5 個版型的實作模式(全部是「enum 值 + 每個相關 `switch` 多一個 `case`」,沒有例外處理或特殊路徑),新增第 6 個 `LayoutTemplate` 值,建議命名 `two_over_one`(3 pane:上排 2 個、下排 1 個)——實作者可依專案既有命名慣例微調命名,並在交接區記錄最終選用的識別字串(需同步反映在 `session.cpp` 的字串序列化)。

### 1. `src/core/model.h`

- `LayoutTemplate` enum(`:17-23`)新增一個值(如 `two_over_one`),置於 `three_pane` 之後、`four_pane_grid` 之前或之後皆可,由實作者決定,理由記錄於交接區。

### 2. `src/core/model.cpp`

- `pane_count`(`:32-39`,約)——新增 `case`,回傳 3。
- `divider_ratio_count`(`:43-50`,約)——新增 `case`,回傳 2(比照 `three_pane`/`four_pane_grid`,同樣需要兩個獨立比例:上下分割一個、上排左右分割一個)。
- `default_divider_ratios` 沿用既有邏輯(全部填 0.5),不需要新增程式碼。

### 3. `src/core/layout.cpp`

- `compute_layout_rects`(`:38-77`)新增一個 `case`,幾何邏輯:先用 `ratios[0]` 做上下分割(比照 `top_bottom` 的 `split(client_height, ratios[0], ...)`),再用 `ratios[1]` 對**上半部**做左右分割(比照 `left_right` 的 `split(client_width, ratios[1], ...)`),下半部維持全寬、不分割。矩形順序建議:上排左、上排右、下排(全寬),與現有 `three_pane` 回傳順序「先分割軸、後細分軸」的慣例一致,實際順序需與 UI 端的 pane index 假設對齊,並在交接區說明。

### 4. `src/core/session.cpp`

- `layout(std::string_view)`(`:320-327`,約)與 `layout(LayoutTemplate)`(`:329-338`,約)兩個方向的字串映射各新增一行,字串值需與交接區記錄的識別字串一致。
- **既知的向下相容行為(非本票要修的缺陷,僅記錄以避免誤判)**:`read_application`(`:438`,約)目前的邏輯是,若 `layout_template` 字串無法辨識(`layout(*layout_text)` 回傳 `std::nullopt`),整個 `ApplicationState` 的讀取直接失敗(`return std::nullopt`),交由既有的 PD-025 崩潰復原退回備份機制處理——這代表「用新版 PaneDock 存了新版型的 Group、再用舊版 PaneDock 開啟」會導致舊版把整份 session 當成損毀檔退回備份。這是新增任何 `LayoutTemplate` 值(包含當初新增 `four_pane_grid` 時)就存在的既有行為,不是本票引入的新問題,本票不需要也不應該改變這個既有的失敗處理路徑。

### 5. `src/app_shell/main.cpp`

- `kLayoutButtonIds`(`:150-152`)、`kLayoutButtonLabels`(`:153-154`)、`kLayoutTemplates`(`:155-159`,約)三個 `std::array` 的大小從 5 改為 6,各自新增一個元素(tooltip 文字建議「Two over One」或等效簡短描述,比照 `L"Three"`/`L"Four"` 的簡短風格,PD-039 已把可見文字改成 tooltip,本票沿用不新增可見標籤)。
- `draw_layout_glyph`(`:635-687`)的 `switch (index)`(`:661-684`)新增對應 `index` 的手繪圖示:一條橫跨全寬的水平線(上下分割)+ 一條只在上半部的垂直線(上排左右分割)——是既有 `case 3`(`three_pane`:垂直線+右半部水平線)的座標軸轉置版本。
- 確認 header 版型按鈕列的寬度計算(`:1442-1447` 一帶,`kLayoutButtonIds.size()` 已經是動態算式,理論上會自動適應變成 6 個按鈕,但需要實機/截圖確認第 6 個按鈕不會被裁切或擠出可視範圍,尤其是窄視窗或高 DPI 情境)。

### 6. `docs/design-spec.md`

- FR-003:五種改六種,補上第 6 種的一句話說明(比照既有其他四種的簡短程度)。
- §4.3:「可切換五種版型」改為六種。
- §3.2「明確不包含」的「任意遞迴 pane 分割」條目維持不變、不刪除(本票沒有開放任意分割,只是多一個固定形狀,見上方「這不是重開」段落)。

### 7. 測試

- `tests/unit/core_layout_test.cpp`(`:26-33` 一帶的 `expect_rects` 表格)新增一筆新版型的矩形驗證案例,涵蓋一般情況與退化尺寸(比照既有 `four_pane_grid` 的最小尺寸/極端比例案例,`:44-56`)。
- `tests/unit/core_model_test.cpp`——若該檔案有針對每個 `LayoutTemplate` 窮舉 `pane_count`/`divider_ratio_count` 的測試,新增對應案例。
- `tests/unit/core_session_test.cpp`——新增往返序列化(round-trip)驗證,確認新字串值可正確寫入與讀回。

## 綁定限制(引用)

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

全部改動都是既有 `switch`/`std::array` 模式的機械式擴充,不新增抽象層、不新增設定選項。

`AGENTS.md`:
> Keep `src/core` free of HWND, COM and `windows.h`. It is the only automated test seam in this project.

`model.h`/`model.cpp`/`layout.cpp`/`session.cpp` 的改動維持純 C++ 資料與計算,不得引入任何 Win32 型別。

`AGENTS.md`:
> Every persisted config/setting file must be designed for forward extensibility. ... A schema change is additive (new optional fields, new migration step) rather than a destructive reinterpretation of an existing field's meaning.

新增 `LayoutTemplate` 值本身是加法(新增一個合法值),不是重新解釋既有值的意義,符合此規則;上方「既知的向下相容行為」段落已說明既有的失敗處理路徑不受影響。

## 檔案與範圍

- `src/core/model.h`(enum)、`src/core/model.cpp`(`pane_count`/`divider_ratio_count`)、`src/core/layout.cpp`(`compute_layout_rects`)、`src/core/session.cpp`(字串映射)
- `src/app_shell/main.cpp`(`kLayoutButtonIds`/`kLayoutButtonLabels`/`kLayoutTemplates`/`draw_layout_glyph`)
- `docs/design-spec.md`(FR-003、§4.3)
- `tests/unit/core_layout_test.cpp`、`tests/unit/core_model_test.cpp`、`tests/unit/core_session_test.cpp`

## Scope

1. `core` 新增第 6 個 `LayoutTemplate` 值,含 `pane_count`/`divider_ratio_count`/`compute_layout_rects`/session 字串序列化的完整支援。
2. `app_shell` header 新增第 6 個版型按鈕,含 tooltip 與手繪圖示。
3. `docs/design-spec.md` 的 FR-003/§4.3 同步改為六種版型。
4. 三個既有測試檔各自新增涵蓋新版型的案例。

## Non-goals

- 不改變現有 5 種版型的任何行為、矩形計算或按鈕。
- 不新增任意遞迴分割機制,新版型是與其餘 5 種同等地位的固定形狀。
- 不處理「舊版 PaneDock 開啟含新版型的 session.json」的相容性強化(見上方既知行為說明,維持既有的退回備份機制,不做新的相容處理)。
- 不改變版型切換時 pane 數增減的既有 tab 併入/新增規則(FR-003 第二句既有邏輯不變,只是新版型的 pane 數是 3,套用既有規則即可)。

## Acceptance Criteria

1. 新版型可在 header 版型按鈕列點選,切換後畫面呈現「上排兩個並排 pane、下排一個全寬 pane」,與使用者提供的參考圖一致。
2. 上排兩個 pane 之間的分隔線、上排與下排之間的分隔線均可獨立拖曳調整比例,且能正確持久化與還原(切換 Group 或重啟後比例不變)。
3. 從其他版型切換到新版型、或從新版型切換到其他版型時,既有的 tab 併入/新增規則正確套用(pane 數變化時不遺失任何 tab 的資料)。
4. 視窗縮小到最小尺寸時,三個 pane 都不產生負值或零尺寸矩形(FR-004a 退化尺寸規則)。
5. `docs/design-spec.md` FR-003 與 §4.3 已更新為六種版型的敘述。
6. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過,含三個測試檔新增的案例。
7. `git diff --check` 通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "LayoutTemplate|kLayoutButtonIds|kLayoutTemplates|three_pane|two_over_one" src\core\model.h src\core\model.cpp src\core\layout.cpp src\core\session.cpp src\app_shell\main.cpp
git diff --check
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,放大用 `InterpolationMode.NearestNeighbor`。單次點擊新版型按鈕 + 單次截圖即可完成外觀驗證;**連續拖曳兩條分隔線調整比例的跟手測試留給使用者本人**,不要用 computer-use 連續操作。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 最終選用的 `LayoutTemplate` enum 識別字與 session 字串值。
- `compute_layout_rects` 新 `case` 回傳的矩形順序(上排左/上排右/下排的 index 對應),以及此順序如何與 `app_shell` 的 pane index 假設對齊。
- Header 按鈕列新增第 6 顆按鈕後的寬度/DPI 驗證結果(是否有裁切或溢出)。
- 三個測試檔新增案例的清單與涵蓋範圍。
- 未驗證項目與原因(若有,例如連續拖曳兩條分隔線的跟手感受留給使用者)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
