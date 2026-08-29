# PD-117 — Pane 內所有按鈕補上 tooltip

Phase 7 · app_shell · Depends on: PD-083, PD-111

- Source: 使用者要求(2026-08-29)。
- Origin: 使用者原文:「For all buttons in the pane they should have tooltip .」
- Override: PD-039 的 non-goal 原本把導覽列按鈕與 tab `+` 排除在 tooltip 外；本票以新的使用者需求覆寫那個範圍限制，只處理 pane 內的按鈕。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`AGENTS.md`:
> App UI text must be English. No Chinese strings ship in the binary.

`docs/development.md`:
> UI ticket 的 Agent checks 應驗證建置、視窗生命週期、狀態資料、訊息與可測的 Win32 結果；視覺人工驗證不屬於本追蹤表。

## Files to read and trace first

- `src/app_shell/main.cpp`: existing `layout_tooltip`, pane button creation, `apply_layout`, `tab_strip_proc`, `apply_tab_item_size`, and tooltip/control lifetime.
- `docs/tickets/PD-039-layout-label-removal-and-tooltip.md`: existing Common Controls tooltip pattern and its new override boundary.
- `docs/tickets/PD-083-remaining-buttons-missing-or-weak-hover.md`: pane control inventory and existing hover event paths.

## Scope

1. Reuse the existing native Common Controls tooltip window for each pane's six navigation buttons: Back, Forward, Up, Refresh, View, and Pinned Locations.
2. Register tooltip tools for the custom tab strip's `+` button and conditional left/right tab-scroll buttons. Reuse the existing tab hit-test rectangles and update them when tab geometry changes.
3. Keep all tooltip strings in English and do not change button behavior, layout, hover colors, or Shell view behavior.

## Non-goals

- No tooltip for tab labels, the embedded Shell view, the header layout controls, the sidebar, or the manager dialog's text buttons.
- No custom tooltip drawing, dependency, animation, or persistence.
- No change to the existing tooltip lifetime or to the tab hit-test rules.

## Acceptance

1. Each pane navigation button shows the correct tooltip: `Back`, `Forward`, `Up`, `Refresh`, `View`, and `Pinned locations`.
2. The tab `+` affordance shows `New tab`; when visible, the left/right tab-scroll affordances show `Scroll tabs left` and `Scroll tabs right`.
3. Tooltips track pane resize, DPI changes, tab overflow changes, and tab scrolling without stale hit rectangles or crashes.
4. Existing controls still receive their original click, keyboard, hover, and disabled behavior.
5. `cmake --build build`, `ctest --test-dir build --output-on-failure`, and `git diff --check` pass.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "Pinned locations|New tab|Scroll tabs left|Scroll tabs right|TTM_ADDTOOL|TTM_NEWTOOLRECT" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:停留在每個 pane 的導覽按鈕、tab + 與可見的左右捲動按鈕，確認提示文字正確；再縮放視窗與切換 tab overflow。
```

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉，不要 `Stop-Process -Force`。**

## Handoff requirements

- 最終 tooltip 文字與各 tool 的 HWND/矩形來源。
- 說明 custom tab strip tooltip 在 overflow 出現/消失與 DPI/resize 後如何同步。
- 未能做的視覺人工驗證要如實記錄。

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 實作交接（2026-08-29）

- 重用既有 `layout_tooltip` Common Controls tooltip。六顆 pane 導覽按鈕以 `TTF_IDISHWND | TTF_SUBCLASS` 綁定各自 HWND；文字依序為 `Back`、`Forward`、`Up`、`Refresh`、`View`、`Pinned locations`。
- tab strip 的 `+`、左捲、右捲使用 `TTF_SUBCLASS` 與既有 `tab_add_rects`／`tab_scroll_button_rects`。初次建立使用 `TTM_ADDTOOLW`，之後每次 `apply_tab_item_size` 以 `TTM_NEWTOOLRECT` 同步；overflow 消失時矩形為空，下一次出現會恢復。
- 沒有新增 tooltip 視窗、依賴或自繪提示；既有 tab/按鈕訊息路徑不變。未另加測試，因為這是 app_shell HWND 互動，core 測試 seam 無法覆蓋。
- Agent checks：CMake configure PASS；`cmake --build build` PASS；`ctest --test-dir build --output-on-failure` PASS（6/6）；`git diff --check` PASS。
- 未執行滑鼠停留與 DPI/overflow 的人工畫面驗證；需使用者依 Agent checks 的手動步驟確認提示顯示。
