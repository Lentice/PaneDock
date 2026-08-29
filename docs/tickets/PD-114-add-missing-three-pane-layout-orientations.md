# PD-114 — 補齊三窗格固定版型：1 up / 2 bottom 與 2 left / 1 right

Phase 7 · core, app_shell · Depends on: PD-005, PD-016, PD-046, PD-099

- Source: 使用者需求（2026-08-29）。
- Origin: 使用者原文：「add more layout for 3 panes, 1 up 2 bottom, 2 left 1 right」。
- Priority: MEDIUM——使用者直接提出的固定版型擴充。

## Outcome

新增兩個具名、可持久化的三窗格 `layout template`：

1. **1 up / 2 bottom**：上方一個 pane 橫跨全寬，下方兩個 pane 左右並排。
2. **2 left / 1 right**：左側兩個 pane 上下排列，右側一個 pane 直通全高。

完成後 PaneDock 共有八種固定版型；既有六種版型的幾何、序列化識別字串與已儲存 Group 行為不得改變。

## 這不是重開「任意遞迴 pane 分割」

`docs/tickets.md` 的「已否決的方向」仍禁止任意遞迴 pane 分割。這張票只比照 `LayoutTemplate::three_pane` 與 `LayoutTemplate::two_over_one`，增加兩個固定 enum 值及其窮舉 `case`，不提供樹狀分割模型、使用者任意新增分割或巢狀比例。

使用者已具體指出現有六種版型無法表達的兩個工作情境，因此滿足該否決方向所列的重開證據；但採用的仍是既有固定版型策略，不重開任意分割本身。

## 覆寫既有產品決策

`docs/design-spec.md` §2.4、§3.1、§4.3 與 FR-003 現稱「六種版型」。本票明確覆寫為：**支援且僅支援八種固定版型**，新增上述兩種三窗格方向。新證據是本票 Source 的直接使用者需求。

`CONTEXT.md` 的 `layout template` 定義也須同步列出八種固定排列；「任意遞迴 pane 分割」仍維持不支援。

## 必讀與 caller trace

實作前完整閱讀並追蹤：

- `docs/design-spec.md` §2.4、§3.1、§3.2、§4.3、FR-003、FR-004
- `docs/development.md` 的 Architecture rules、UI language、Change workflow
- `docs/tickets/PD-005-layout-rect-computation.md`
- `docs/tickets/PD-016-splitters-five-layouts-and-dpi-scaling.md`
- `docs/tickets/PD-099-add-two-over-one-layout-template.md`
- `src/core/model.h` 的 `LayoutTemplate`
- `src/core/model.cpp` 的 `pane_count`、`divider_ratio_count`、`default_divider_ratios`、`switch_layout` 及所有 caller
- `src/core/layout.cpp` 的 `compute_layout_rects` 及所有 caller
- `src/core/session.cpp` 的雙向 layout 字串映射及 session read/write caller
- `src/app_shell/main.cpp` 的 `kLayoutButtonIds`、`kLayoutButtonLabels`、`kLayoutTemplates`、`draw_layout_glyph`、`splitters`、`set_layout` 及各 caller
- `tests/unit/core_layout_test.cpp`、`tests/unit/core_model_test.cpp`、`tests/unit/core_session_test.cpp`

## 實作範圍

### Core model 與 persistence

- `LayoutTemplate` 新增兩個語意清楚的值。建議 persisted identity：
  - `one_over_two`：1 up / 2 bottom。
  - `two_beside_one`：2 left / 1 right。
- `pane_count` 對兩者回傳 3；`divider_ratio_count` 回傳 2；沿用 `default_divider_ratios` 的 `0.5` 填值，不新增 helper。
- `session.cpp` 的讀寫映射加入兩個新 identity。不得改名或重新解釋既有 `three_pane`、`two_over_one` identity，也不得順帶修改未知 layout 值的既有 fallback 行為。

### 純矩形計算

- `one_over_two`：`ratios[0]` 控制全寬上下分隔，`ratios[1]` 控制下半部左右分隔；pane 順序為上方、左下、右下。
- `two_beside_one`：`ratios[0]` 控制全高左右分隔，`ratios[1]` 控制左半部上下分隔；pane 順序為左上、左下、右側。
- 沿用既有 `split()`、minimum pane size、divider thickness 與退化尺寸行為，不建立通用遞迴 layout engine。

### App shell

- 版型控制的三個平行 array 擴為八項，新增兩個 English tooltip 與兩個 layout template 對應。
- `draw_layout_glyph` 新增兩個圖示：
  - `one_over_two`：全寬水平線，加只存在於下半部的垂直線。
  - `two_beside_one`：全高垂直線，加只存在於左半部的水平線。
- `splitters` 新增兩個窮舉分支；第一條 splitter 對應 `ratios[0]`，第二條對應 `ratios[1]`，拖曳方向及作用範圍須與矩形定義一致。
- 此票先把新按鈕接入現有 array；最終 1→4 panes 的顯示次序由 PD-115 統一整理。

### 文件與測試

- 更新 `docs/design-spec.md` 四個「六種」位置及版型清單為八種。
- 更新 `CONTEXT.md` 的 `layout template` glossary。
- `core_layout_test` 對兩種版型各加入非 0.5 比例的精確矩形案例，並至少以一個退化尺寸案例覆蓋 minimum/divider 行為。
- `core_model_test` 窮舉兩者的 pane/ratio count。
- `core_session_test` 驗證兩個新 identity 各自可 round-trip。

## Non-goals

- 任意遞迴 pane 分割、layout tree 或可自訂分割方向。
- 增加 pane 上限；仍為 1–4 panes。
- 改變版型切換時合併／新增 pane 的既有規則。
- session schema version migration；本票只新增合法 enum 字串值。
- 修改 Shell view 建立、保活、導覽或銷毀生命週期。
- 重構平行 array 或抽象化 glyph/splitter 系統。
- 決定版型按鈕最終顯示順序；見 PD-115。

## 綁定限制（原文引用）

`docs/design-spec.md` §2.4：
> 固定版型優於任意分割。六種版型涵蓋實際需求,遞迴分割只增加模型與實作複雜度。

本票只把固定數量由六更新為八，不改後半句的架構決策。

`docs/development.md`：
> `core` | Group/pane/tab model, layout rectangle computation, session serialization and migration | **Any HWND, COM type, or `windows.h` include**

`AGENTS.md`：
> Keep `src/core` free of HWND, COM and `windows.h`.

> App UI text must be English. No Chinese strings ship in the binary.

> Every persisted config/setting file must be designed for forward extensibility. It carries an explicit schema version from its first version.

> New non-trivial logic needs one focused runnable test or self-check.

## Acceptance criteria

- 兩個新 layout template 都顯示三個 panes，矩形方向與本票 Outcome 完全一致。
- 兩條 splitter 可分別拖曳，且只改變對應 ratio；雙擊重設沿用既有 PD-105 行為。
- 切換至新 layout 後，active button、pane focus cycling、session save/reload 均使用正確 template。
- 既有六個 persisted identity 與 layout geometry 不變。
- 版型清單、tooltip 和圖示皆為 English UI；文件可用繁體中文。
- `src/core` 沒有新增 Win32/COM include 或型別。
- `docs/design-spec.md` 與 `CONTEXT.md` 不再聲稱只有五或六種 layout templates。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "LayoutTemplate|layout_template|pane_count\(|divider_ratio_count\(|compute_layout_rects|splitters\(" src tests
rg -n "五種版型|六種版型|exactly five|exactly six" CONTEXT.md docs/design-spec.md
git diff --check
```

實機單步檢查：逐一點選兩個新按鈕並截圖，確認 panes、glyph、active highlight 與 splitter 位置。需要連續拖曳的 splitter 驗證交由使用者執行並把結果記入交接區。

## 交接區

完成時記錄：最終 enum/persisted identity、pane index 順序、修改檔案、上述 checks 結果，以及兩種版型的實機驗證狀態。

### 2026-08-29 — 實作完成

- 最終 identity：`LayoutTemplate::one_over_two` / `"one_over_two"` 與 `LayoutTemplate::two_beside_one` / `"two_beside_one"`。既有六個 identity 未修改，session schema version 未變更。
- Pane 順序與 ratio：`one_over_two` 為上方、左下、右下，`ratios[0]` 控制上下、`ratios[1]` 控制下方左右；`two_beside_one` 為左上、左下、右側，`ratios[0]` 控制左右、`ratios[1]` 控制左側上下。
- 修改：`src/core/model.h`、`model.cpp`、`layout.cpp`、`session.cpp`；`src/app_shell/main.cpp`；三個 core unit test；`docs/design-spec.md`。沿用既有 enum/switch/array、`split()` 與 splitter 模式，沒有新增 abstraction、dependency、schema migration 或 Shell view 生命週期改動。
- 自動驗證：`cmake --build build` 成功（只有既有的 missing-field-initializers warnings）；`ctest --test-dir build --output-on-failure` 5/5 通過；ticket 的兩組 `rg` caller/spec checks 已執行且 spec/glossary 不再命中五種或六種；`git diff --check` 通過。
- 依使用者指示，Agent 未啟動 PaneDock、未使用 Computer Use、滑鼠 API、`SetCursorPos`、`mouse_event` 或互動後截圖進行 UI 驗證。

#### 需要使用者手動驗證

1. 啟動 PaneDock，分別 hover 並點選 `One over Two` 與 `Two beside One`；確認 tooltip、glyph、active highlight 與三個 pane 的方向一致。
2. 在 `One over Two` 依序拖曳全寬水平 splitter 與下方垂直 splitter；確認前者只改上下比例，後者只改下方左右比例。雙擊各 splitter，確認各自回到 50%。
3. 在 `Two beside One` 依序拖曳全高垂直 splitter 與左側水平 splitter；確認前者只改左右比例，後者只改左側上下比例。雙擊各 splitter，確認各自回到 50%。
4. 將兩種 layout 的 splitter 調成非 50%，關閉並重新啟動 PaneDock；確認 layout identity、兩個比例、active pane 與各 pane tabs/locations 精確還原。
5. 在窄視窗及混合 DPI 螢幕各切換一次兩種 layout，確認八個 layout buttons 未重疊，pane 無零尺寸，splitter 與 glyph 沒有錯位。
