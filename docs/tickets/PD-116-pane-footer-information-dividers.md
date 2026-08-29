# PD-116 — Pane footer 資訊段落加入 Windows Explorer 風格垂直分隔線

Phase 7 · app_shell · Depends on: PD-060, PD-069

- Source: 使用者實機回報(2026-08-29)，指出 pane footer 的 file count、selected item info 距離太近，並提供 Windows 檔案總管 footer 截圖作為目標。
- Origin: 使用者原文：「selected item info 與 file count 之間沒有分隔線，距離太近 UX 不好。兩個中間應該要有分隔線，分隔線左右兩邊要保留足夠的 margin。」後續以 Windows 檔案總管截圖確認：三段資訊之間各有一條短垂直 divider。
- Priority: MEDIUM——資訊正確但分組不清楚，屬於可見的 footer UX 缺陷。

## 覆寫的既有決策

PD-060 的「已確認的產品決策」第 1 點與交接區曾明確決定三段資訊使用固定 3 個空白，且「不用 `|` 等符號」。本票以使用者最新的實機 UX 回饋與 Windows 檔案總管參考截圖為新證據，**只覆寫段落分隔方式**：改成三個獨立文字段，於相鄰且存在的段落之間繪製短垂直 divider。

PD-060 的其餘決策全部維持：item count／selected count／selected bytes 的資料來源、1000 項上限、`PKEY_Size`、`StrFormatByteSizeW`、footer 淺灰底色與上緣水平分隔線都不變。不得編輯已完成的 PD-060 ticket 文件。

## 已確認的現況與根因

1. `refresh_status_bar` (`src/app_shell/main.cpp:1314-1342`) 把所有資訊組成單一字串；相鄰段落只以 `L"   "` 隔開。
2. `draw_status_bar` (`src/app_shell/main.cpp:1039-1075`) 以一次 `DrawTextW` 繪製整段文字。它只有 footer 上緣的水平分隔線，沒有資訊段落的幾何或垂直 divider。
3. PD-069 已建立 `kSpaceSnug = 8` 與 `kSpaceBase = 12` 的 DPI-aware 4px spacing scale；footer 外側文字 inset 已使用 `kSpaceBase`。本票直接重用 `kSpaceSnug`，不再發明新的 margin 數值。
4. Status bar 已是 `SS_OWNERDRAW`，建立位置在 `main.cpp:4098-4103`，並由 `WM_DRAWITEM` (`main.cpp:4197-4205`) 路由至 `draw_status_bar`。不需要新增 child control、subclass 或 UI framework。

## 已確認的產品決策

1. Footer 資訊依 Windows 檔案總管分成三段：
   - file count：`137 items`
   - selected item count：`1 selected`
   - selected size：`670 bytes`（實際字面仍由既有 `StrFormatByteSizeW` 產生）
2. 每兩個**相鄰且存在**的段落之間畫一條 divider：
   - 無選取：`137 items`，不畫 divider。
   - 有選取且沒有可顯示大小：`137 items  │  1 selected`，畫 1 條 divider。
   - 有選取且有可顯示大小：`137 items  │  1 selected  │  670 bytes`，畫 2 條 divider。
   `│` 只用來說明版面；不得把 pipe／Unicode bar 當文字塞進 UI，divider 必須由 GDI 畫線，才能精確控制高度、顏色與 DPI。
3. Divider 使用 1 logical px 寬、12 logical px 高，垂直置中於 `kStatusBarHeight = 24` 的 footer；寬度與高度都經 DPI 換算，並夾到實際 client rect 內。
4. Divider 左右各保留 `kSpaceSnug = 8` logical px。Footer 最左／最右既有 `kSpaceBase = 12` inset 不變。
5. Divider 顏色沿用 footer 上緣水平分隔線的 `RGB(232, 237, 242)`，不新增色票。
6. 保留單一 owner-draw `STATIC`。最小資料傳遞方式是在 `refresh_status_bar` 以 `\t` 區隔存在的段落；`draw_status_bar` 逐段量測與繪製，遇到下一段時先加入 8px margin、畫 divider、再加入 8px margin。不得增加三個 child `STATIC`、新類別或一般化 layout abstraction。
7. 窄 pane 仍以 status bar client rect 裁切；不得為了塞下 footer 文字提高 minimum pane width、改 footer 高度或讓文字畫到 client rect 外。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`:
> **Keep `src/core` free of HWND, COM and `windows.h`.**

`AGENTS.md`:
> **App UI text must be English.** No Chinese strings ship in the binary.

`docs/design-spec.md` §7:
> C++20,原生 Win32,無 UI 框架層。

`docs/design-spec.md` NFR-007:
> 無網路、無遙測、無第三方 runtime、無服務、無 driver、無管理員權限。

`docs/development.md` Change workflow:
> Make the smallest change that satisfies the acceptance criteria. Reuse before adding.
>
> Add one focused runnable test or self-check for new non-trivial logic. If the logic is not in `core`, say in the ticket's 交接區 why it could not be, and what manual check replaces it.

`docs/testing.md`:
> **All automated tests target `core`, and `core` alone.**
>
> **End-to-end UI automation (WinAppDriver / UIAutomation).** Rejected: against live Shell views it is severely flaky.

## Files to read and trace first

- `src/app_shell/main.cpp:72-76`——既有 `kSpaceSnug`／`kSpaceBase`，不得另建重複 spacing 常數。
- `src/app_shell/main.cpp:1039-1075`——`draw_status_bar`，本票的主要繪製落點。
- `src/app_shell/main.cpp:1314-1342`——`refresh_status_bar`，把固定空白改為結構化段落 delimiter 的落點。
- `src/app_shell/main.cpp:2491-2504`——status bar layout 與既有高度／client rect 邊界；只讀確認，不改排版。
- `src/app_shell/main.cpp:4098-4103`、`4197-4205`——owner-draw control 建立與 `WM_DRAWITEM` caller；確認所有 status bar 都走同一個 shared draw function。
- `docs/tickets/PD-051-pane-status-bar.md`——item／selection count 與 selection change callback 的來源。
- `docs/tickets/PD-060-pane-status-bar-selection-size-and-separator.md`——被本票局部覆寫的三段格式、大小統計與既有 footer 配色。
- `docs/tickets/PD-069-chrome-spacing-scale.md`——4px spacing scale 與 footer 外側 inset 的既有決策。
- `docs/testing.md`——為何這個純 Win32 visual behavior 不新增假的 `core` unit test。

## Scope

1. `refresh_status_bar` 不再用 3 個空白表達段落邊界，改以明確 delimiter 傳遞 1–3 個既有文字段。
2. `draw_status_bar` 依實際存在的段落逐段繪製，並在相鄰段落間畫 DPI-aware divider 與左右 margin。
3. 保留既有 footer 背景、上緣水平線、外側 inset、文字顏色、字型、資料取得與更新時機。

## Non-goals

- 不改 item count、selected count 或 selected bytes 的計算。
- 不改 `ExplorerHost::ItemCounts`、Shell callback、1000 項上限或 `StrFormatByteSizeW`。
- 不增加 free-space、view mode 或其他 footer 資訊。
- 不改 footer 高度、pane minimum size、pane layout、背景或上緣水平線。
- 不新增 child controls、dependency、UI framework、timer、polling 或 persistence。
- 不在 `src/core` 放 Win32 繪製邏輯，也不為視覺行為新增假的 core test／E2E UI automation。

## Acceptance

1. 無選取時只顯示 `N items`，沒有垂直 divider。
2. 有選取但大小省略時顯示兩段資訊，兩段間只有 1 條 divider。
3. 有選取且大小存在時顯示三段資訊，兩個段落邊界各有 1 條 divider。
4. 每條 divider 左右各有 8 logical px margin；divider 為 1×12 logical px、垂直置中，顏色與 footer 上緣線一致。
5. Divider 是 GDI 線條，不是 UI 字串中的 `|`／`│` 字符；binary 不新增非英文 UI 字面。
6. 100%、150%、200% DPI 下，divider 與 margin 按比例縮放且保持置中，沒有重疊或裁出 footer。
7. Pane 縮到最窄與切換所有版型時，繪製被 client rect 正確裁切，不溢出相鄰 pane。
8. 選取／取消選取時 footer 仍即時更新，PD-051／PD-060 行為沒有回歸。
9. `cmake --build build`、`ctest --test-dir build --output-on-failure` 與 `git diff --check` 全數通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kSpaceSnug|kSpaceBase|draw_status_bar|refresh_status_bar|SS_OWNERDRAW" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 真人／PrintWindow 驗證：無選取、只有 selected count、selected count + size、
# 最窄 pane，以及 100%／150%／200% DPI。不得改用 WinAppDriver／UIAutomation。
```

## Handoff requirements

- 最終段落 delimiter 與 `refresh_status_bar` 產生的三種字串形狀。
- Divider 的最終 logical width／height、左右 margin、顏色與 DPI 換算方式。
- 無選取、大小省略、大小存在、最窄 pane 的視覺驗證結果。
- 100%、150%、200% DPI 的結果；若環境無法實測，記錄限制並提供所有縮放使用點的程式碼證據。
- 說明未新增 automated UI test 的原因，以及採用的真人／`PrintWindow` 替代檢查。

## 交接區

<!-- 實作 agent 填寫，append-only -->

### 2026-08-29 實作交接

- `refresh_status_bar` 以 `\t` 傳遞 1–3 個既有資訊段：無選取為 `N items`；有選取且無大小為 `N items\tM selected`；有選取且大小有效為 `N items\tM selected\t<formatted size>`。tab 僅是 owner-draw 內部 delimiter，不會顯示在 UI。
- `draw_status_bar` 仍由單一 `SS_OWNERDRAW` `STATIC` 負責繪製；逐段以 `GetTextExtentPoint32W` 量測、`DrawTextW` 繪製，在相鄰段落間用既有 footer divider 色 `RGB(232,237,242)` 畫 GDI 垂直線。
- Divider 最終為 1 logical px 寬、12 logical px 高，垂直置中於去除上緣水平線後的內容區；左右各為 `kSpaceSnug` 的 8 logical px。寬度、高度與 margin 都依目前 `dpi` 經 `MulDiv(..., dpi, 96)` 換算，並在 status bar client rect 內裁切。
- 未新增 child control、helper class、dependency、timer、polling 或 `src/core` 變更；未改 item count／selected bytes 計算、大小上限、`StrFormatByteSizeW`、footer 高度、layout 或外側 inset。
- Agent checks：`cmake --build build` 成功；`ctest --test-dir build --output-on-failure` 成功，5/5 tests passed。程式碼路徑檢查使用 ticket 指定的 `rg` 命令；`git diff --check` 於 commit 前另行執行。
- 依使用者明確限制，未啟動 PaneDock、未使用滑鼠、Computer Use、`SetCursorPos`、`mouse_event`、`PrintWindow-after-interaction` 或任何 UI automation；因此以下視覺項目沒有虛構的通過證據，請使用者在真實桌面自行驗證：
  1. 無選取：只見 `N items`，沒有垂直 divider。
  2. 有選取但大小省略：`N items` 與 `M selected` 之間有一條 divider。
  3. 有選取且大小存在：三段資訊之間有兩條 divider，兩側 margin 足夠且一致。
  4. 切換最窄 pane、所有 layout，以及 100%／150%／200% DPI；確認 divider 維持短線、置中、不重疊、不溢出。
