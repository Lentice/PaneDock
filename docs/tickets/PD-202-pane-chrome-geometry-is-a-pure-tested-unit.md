# PD-202 — pane chrome 幾何抽為純函式並補測試

## 背景

`apply_layout`（`src/app_shell/main.cpp`）在 visible-pane 分支裡把兩件事寫在同一段程式碼：

1. **算**一個 pane 的 chrome 幾何——tab strip、六顆導覽按鈕、位址列背景與 EDIT、status bar、footer action、explorer container 的矩形。
2. **擺**這些子視窗——`position_window` 與 parent-scoped 的 `DeferWindowPos` 批次。

這段算式全部是 `pane_rect` 與單一 DPI 的整數運算，其中約十二個 `std::min`／`std::max` 夾制只在 pane 很小時才生效：

- `button_width = min(scaled(kNavigationButtonWidth), pane_width / 7)`
- `actual_strip_height = min(scaled(kTabStripHeight), pane 高度)`
- `navigation_height`、`status_height` 的 `max(0, …)`
- `footer_vertical_inset`／`footer_horizontal_inset`／`footer_button_width`／`footer_button_left`

因為與 `DeferWindowPos` 融在一起，**沒有任何自動化測試碰得到它**，而 PD-107／PD-152／PD-154 三次實機回報都屬於這一類 hit-test／版面缺陷。

## 覆寫的先前決定

`docs/tickets.md`（2026-09-06 PD-200 交接、2026-09-07 複驗）把「版面幾何」列為評估後不拆，理由是與 `DeferWindowPos` 的 parent-scoped 批次契約糾纏、且無法自動化驗證。

**本票不推翻該理由，而是換一個當時未評估過的形狀**：

- 當時評估的是把**擺放**搬進 `Pane`，讓 `apply_layout` 不再透過 8 個裸 `HWND` accessor 擺放 pane 自己的子視窗。**那個仍然不做**，`DeferWindowPos` 的批次契約整份留在 `apply_layout`。
- 本票只抽出**純矩形計算**。

新證據一項：原理由第三條腿「無法自動化驗證」在 2026-09-07 已失效——`docs/testing.md:17` 已把測試規則改寫為「能不經由活的 Shell view、透過公開介面驅動的單元就要測」，並明列 `app_shell` 的純 header（`tab_overflow.h`／`window_placement.h`／`window_helpers.h`）為適用範圍。當初擋住這一刀的是那份文件失真的 core-only 宣告，與 F3 一開始被誤判為「需先釐清政策」是同一個原因。

## 綁定約束

- AGENTS.md：「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」——本票不新增 interface、adapter 或 factory，只有一個純函式與一個 struct。
- AGENTS.md：「Keep `src/core` free of HWND, COM and `windows.h`.」——新 header 使用 `RECT`，因此留在 `app_shell`，**不進 `core`**。
- AGENTS.md：「Per-Monitor-V2 DPI awareness…Mixed-DPI multi-monitor is a normal case」——新函式以 `UINT dpi` 為參數而非讀 HWND，DPI 是輸入不是隱含狀態。
- AGENTS.md：「New non-trivial logic needs one focused runnable test or self-check.」
- `docs/testing.md`：能不經活的 Shell view 透過公開介面驅動的單元就要測；來源字串掃描不是測試。
- 模組契約 (2)（`docs/tickets.md:769`）：`Pane` 只擁有 HWND／幾何／hover／捲動，`core::PaneState` 擁有會存檔的真相。本票的矩形永不存檔，符合此分界。

## Scope

1. 新增 `src/app_shell/pane_chrome_geometry.h`（純 header，只依賴 `windows.h`／`<algorithm>`／`<array>`）：
   - `scale_for_dpi(int value, UINT dpi)`——與 `app_shell::scaled_value` 同義，**包含其 `max(1, …)` 下限**（`kNavigationButtonOffsetX = 0` 會被縮放成 1 而非 0，既有版面一直是照這個排的）。
   - `pane_card_outset`／`pane_card_radius`／`pane_card_shadow_offset` 與 `kPaneCardOutset`／`kPaneCardShadowOffset` 由 `pane.h` 移入。
   - `inset_rect` 由 `main.cpp` 移入。
   - pane chrome 的 8 個尺寸常數由 `main.cpp` 移入；`kTabAddButtonVerticalInset`／`kSpaceTight` 在 footer 的用途更名為 `kPaneFooterVerticalInset`／`kPaneFooterHorizontalInset`。
   - `struct PaneChromeRects` 與 `pane_chrome_rects(RECT pane_rect, UINT dpi)`。全部矩形以主視窗座標回報。
2. `pane.h` 改為 include 新 header，刪掉搬走的 pane card helper。
3. `pane.cpp` 的 status bar 文字保留區改用新 header 的常數，刪掉三個本地重複常數（`kStatusBarHeight`／`kTabAddButtonVerticalInset`／`kSpaceTight`）——那段保留寬度必須跟著 footer action 幾何走，先前是兩份各自維護的知識。
4. `main.cpp`：刪除 `NavigationGeometry`／`navigation_geometry`／`inset_rect` 與搬走的常數；`apply_layout` 改為呼叫一次 `pane_chrome_rects` 並讀取結果。
5. 新增 `tests/unit/pane_chrome_geometry_test.cpp`，在 `tests/CMakeLists.txt` 表格加一列 `pane_chrome_geometry`。

## Non-goals

- **不**把擺放、`position_window`、`DeferWindowPos` 批次或 `pane_local()` 搬進 `Pane`。`apply_layout` 仍持有全部 parent-scoped 批次與 commit 順序。
- **不**改任何尺寸數值、夾制邏輯或視覺結果。
- **不**動 `layout_rects`／`splitters`／`layout_sidebar`／`layout_header`（那些不是單一 pane 的 chrome）。
- **不**動 `kAddressBarBackgroundRadius`（繪製用，留在 `main.cpp`）與 `pane_tab_strip.cpp` 自己的 `kTabAddButtonVerticalInset`（tab add 按鈕，另一個用途）。
- **不**改 `src/core`，**不**新增 interface 或 adapter。

## Acceptance criteria

1. `apply_layout` 內不再出現 pane chrome 的尺寸算式；矩形全部來自 `pane_chrome_rects`。
2. `pane_chrome_rects` 為純函式：輸入只有 `RECT` 與 `UINT dpi`，不讀 HWND、不呼叫 Win32 以外的東西，可在無視窗環境呼叫。
3. `panedock_pane_chrome_geometry` 涵蓋：寬鬆 pane 的堆疊、pane 視窗外框、導覽列打包、pane 比 tab strip 還矮、導覽列被截斷、pane 比七顆按鈕還窄、1×1 退化 pane、footer action 落在 status bar 內、192 dpi。
4. 該測試經反證：至少三個變造（拿掉 strip 高度夾制、`/7` 改 `/6`、footer 垂直內縮改寫）各自使測試失敗，還原後通過。
5. 零行為、零視覺變更。
6. Release build 無新警告，完整 CTest 全綠。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 交接區

**結果**：`main.cpp` 4,452 → 4,329（淨減 123）；新 header 182 行、新測試 205 行。`apply_layout` 的 visible-pane 分支由約 150 行降為約 70 行，剩下的全是擺放與 Shell 呼叫。

**逐項核對過的等價性**（轉錄風險最高的部分）：

- `button_offset_x = scale_for_dpi(0, dpi)` 仍為 **1**，不是 0。新 header 的 `scale_for_dpi` 保留 `max(1, …)`，註解說明原因。
- `navigation_button_width` 用未夾制的 `pane_rect.right - pane_rect.left` 除 7（可為負），與原 `navigation_geometry` 一致；footer 的 `pane_width` 另外取 `max(0, …)`，兩者刻意不同，照原樣保留。
- `set_paint_geometry` 仍在 tab strip 擺放之後、按鈕之前無條件呼叫。
- 立即呼叫的 `SetWindowPos(folder_context_button, HWND_TOP, …)` 仍在批次 commit 之前，z-order 序列未變。
- `container_rects[index]` 仍只在 `pane_geometry_changed` 時寫入。

**驗證**：LLVM-MinGW Release build 乾淨（修掉一個因刪常數而生的 `-Wunused-const-variable`），完整 CTest **27/27 通過**（含 `panedock_launch_smoke`、`panedock_explorer_host_lifetime`）。Acceptance 4 的三個變造反證已執行，記錄於上。

**未驗**：實機逐像素視覺比對、跨 DPI 拖移視窗、live resize 期間的視覺。本票為零視覺變更改動，但這三項無法自動化，仍待人工確認。不推進 Phase 5 release gate。

**留在原地且不重開的部分**：`layout_rects`／`splitters`／`apply_layout` 的擺放與批次仍在協調層，理由與 PD-200 交接區相同（`DeferWindowPos` parent-scoped 契約、PD-155 atomic live resize、PD-187 風險、無法自動化驗證）。本票未取得推翻該理由的證據，只是把**不依賴這些理由**的那一半抽了出來。
