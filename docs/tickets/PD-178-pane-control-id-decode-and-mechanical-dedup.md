# PD-178 — 以 `decode_pane_control` 純函式取代 15 處 control ID 算術，並清掉五組逐字複製

Phase 7 · architecture · Depends on: PD-161, PD-163

- Source: 2026-09-03 使用者需求「refactor code，用物件導向的概念來設計重複的內容，例如: panes, groups, tabs。pane 用 CreateWindowW，讓 win message 也能切割乾淨處理」，經 `/grill-with-docs` 多輪問答與 opencode 獨立審查收斂。使用者的目標經確認為「簡化、好維護、每個模組管理自己的內容、與其他模組不要有太複雜的耦合關係」。
- Priority: HIGH——本系列的第一張，風險最低，且它產出的 `decode_pane_control` 是後續 `Pane` 型別能「用起來像自己的控制項」的前提。

## Outcome

新增一個**不含 `windows.h`、可單元測試的純函式** `decode_pane_control(int id)`，把「control ID → (pane index, 控制項種類)」的算術收斂到唯一一處，取代目前散在 `WM_COMMAND` 與 `WM_DRAWITEM` 的 **15 個** 逐字複製的 range-test 區塊。同時清掉四組同樣是逐字複製的樣板（tooltip 註冊、COM refcount、window proc prologue、GDI 圓角矩形），並把兩台同構的 hover 狀態機合而為一。

視覺輸出、幾何數值與行為完全不變。

## 已確認的現況（2026-09-03 工作樹）

- `src/app_shell/main.cpp` 為 6748 行。
- `WM_DRAWITEM` 內 `main.cpp:5601-5662` 有 **8 個** 8–9 行的區塊，除了 ID base 常數與 glyph 索引外逐字相同：

  ```cpp
  if (item != nullptr && item->CtlType == ODT_BUTTON &&
      item->CtlID >= kBackButtonIdBase &&
      item->CtlID < kBackButtonIdBase + static_cast<int>(kExplorerCount)) {
      draw_navigation_icon_button(*item, 0,
          state->owner_draw_hovered_button == item->hwndItem);
      return TRUE;
  }
  ```

- `WM_COMMAND` 內 `main.cpp:5964-6005` 有 **7 個** 同樣形狀的區塊，只有一行呼叫體不同。
- 兩處合計，`id >= kXButtonIdBase && ... + static_cast<int>(kExplorerCount)` 這段文字共出現 **15 次**。
- Pane 控制項的 ID base（每個 `+ pane_index`，`kExplorerCount == 4`）：`kTabStripIdBase = 200`、`kBackButtonIdBase = 300`、`kForwardButtonIdBase = 310`、`kUpButtonIdBase = 320`、`kAddressBarIdBase = 330`、`kRefreshButtonIdBase = 340`、`kViewModeButtonIdBase = 350`、`kPinnedButtonIdBase = 392`（宣告於 `src/app_shell/pane_chrome.h:16-23`），以及 `kFolderContextButtonIdBase = 790`（`main.cpp:193`）。
- 非 pane 的 ID：`kViewModeMenuIdBase = 360`、`kLayoutButtonIdBase = 400`、`kPinnedMenuIdBase = 500`、`kCloseTabId = 780`、`kCloseOtherTabsId = 781`。**與上列 pane 區間無碰撞**，已核對。
- COM `AddRef`/`Release` 逐字複製四份：`main.cpp:308-317`（`DragHoverTarget`）、`src/explorer_host/explorer_host.cpp:81-90`（`ViewCallback`）、`explorer_host.cpp:145-155`（`Site`）、`src/file_operations/file_operations.cpp:56-65`。全部是 `std::atomic<ULONG> references_{1}` + `fetch_add` / `fetch_sub(acq_rel)` + `delete this`。
- `TOOLINFOW` 註冊逐字複製四份：`main.cpp:1803-1812`、`5337-5346`、`5389-5398`、`5423-5432`。
- Window proc 的 `WM_NCCREATE` prologue（取 `CREATESTRUCTW`、`SetWindowLongPtrW(GWLP_USERDATA)`）複製五份：`main.cpp:2588-2597`、`4761-4771`、`4889-4896`、`5206-5214`、`explorer_host.cpp:546-551`。
- GDI「建 brush + 建 pen + 兩次 Select + `RoundRect` + 還原 + 兩次 `DeleteObject`」14 行舞蹈複製五份：`main.cpp:1131-1148`、`1298-1315`、`1356-1373`、`2290+`、`4103+`。另有五份 `HBRUSH x = CreateSolidBrush(); if (x) { FillRect(); DeleteObject(x); }`。
- `owner_draw_button_proc`（`main.cpp:4449-4491`）與 `layout_button_proc`（`main.cpp:4494-4524`）是同一台 hover 狀態機寫兩次：同樣的 `WM_MOUSEMOVE` → `TRACKMOUSEEVENT{sizeof, TME_LEAVE, window, 0}` + 使前一個失效 + 使自己失效；`WM_MOUSELEAVE` → 清除 + 失效；`WM_NCDESTROY` → 清除 + `RemoveWindowSubclass`。唯一差別是 hover 身分存在 `AppState::owner_draw_hovered_button`（HWND）還是 `AppState::layout_hover_index`（索引）。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> **Keep `src/core` free of HWND, COM and `windows.h`.** It is the only automated test seam in this project (`docs/testing.md`); every type that leaks into it costs that seam.

`AGENTS.md`：

> New non-trivial logic needs one focused runnable test or self-check.

`docs/development.md`：

> Do not add a dependency, background loop, framework, or abstraction without a measured need.

`docs/testing.md` §Single seam：

> A good test here exercises externally observable behavior through a public boundary and says nothing about how that behavior is implemented. It states an input and an expected output. **It does not assert on internal call sequences, private structure, or the identity of collaborating objects.**

`docs/tickets.md` Agent 交付規則：

> 預設每個 ticket 的實作範圍為半天至兩天;若超過,先拆 ticket。

`docs/tickets.md` §已否決的方向：

> 端到端 UI 自動化(WinAppDriver／UIAutomation)…對 live Shell view 極易 flaky。

## Files to read and trace first

- `src/app_shell/pane_chrome.h:16-23`：全部 pane 控制項的 ID base 宣告。
- `src/app_shell/main.cpp:176-225`：`main.cpp` 端的 ID 常數（含非 pane 的 360／400／500／780／781）。
- `src/app_shell/main.cpp:5571-5699`：`WM_DRAWITEM` 完整區塊。
- `src/app_shell/main.cpp:5860-6069`：`WM_COMMAND` 完整區塊，含 `6044` 起的巢狀 switch。
- `src/app_shell/main.cpp:4449-4524`：兩個 hover subclass proc。
- `src/app_shell/main.cpp:1025-1400`：全部 owner-draw 繪製函式，確認 GDI helper 的正確簽章。
- `src/explorer_host/explorer_host.cpp:62-220`、`src/file_operations/file_operations.cpp:40-80`：另外三份 COM refcount。
- `tests/unit/pane_chrome_test.cpp`、`tests/unit/test_util.h`：既有的無視窗 self-check 寫法。
- `tests/CMakeLists.txt:11-24`：table-driven 測試註冊方式。

## Scope

1. 新增 `src/app_shell/pane_control_id.h`（header-only，**不得 include `windows.h`**）：

   ```cpp
   enum class PaneControl {
       tab_strip, back, forward, up, address_bar,
       refresh, view_mode, pinned, folder_context,
   };

   struct PaneControlId final {
       std::size_t pane;
       PaneControl control;
       bool operator==(const PaneControlId&) const = default;
   };

   std::optional<PaneControlId> decode_pane_control(int id) noexcept;
   int encode_pane_control(PaneControl control, std::size_t pane) noexcept;
   ```

   實作以一張 `{PaneControl, base}` 常數表驅動，**唯一一處**做 `id - base` 與範圍檢查。`encode_pane_control` 供建立控制項時使用，取代目前散在 `pane_chrome.cpp:39-61` 的手寫 `base + pane_index`。

2. `WM_DRAWITEM` 的 8 個區塊改為一次 `decode_pane_control` + 一張 `{PaneControl → glyph 索引, 是否 blend}` 表。`kFolderContextButtonIdBase` 那個尾端多帶 `true` 的差異用表格欄位表達，不用特例分支。

3. `WM_COMMAND` 的 7 個區塊改為一次 `decode_pane_control` + 一個對 `PaneControl` 的 switch。

4. 新增以下四個 helper，各自放在最貼近使用者的既有檔案，**不新增新的公用 util 模組**：
   - `add_tooltip(HWND tooltip, HWND owner, UINT_PTR id, const wchar_t* text)`（`main.cpp` 匿名 namespace 內即可）。
   - `WM_NCCREATE` prologue：`template <typename T> T* window_state_from_create(HWND, LPARAM)`（`main.cpp` 匿名 namespace；`explorer_host.cpp` 那一份是不同 TU，**本票不動它**，理由寫進交接區）。
   - `fill_rounded_rect(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF border)`——`border` 傳 `CLR_NONE` 時不畫框。
   - COM refcount：以一個 CRTP base（`template <typename T, typename... Interfaces> class ComRefCounted`）或直接改用已在專案內使用的 `Microsoft::WRL::RuntimeClass`；**擇一即可，選 diff 較小的那個並在交接區說明**。

5. 合併兩台 hover 狀態機為單一 `hover_tracking_proc`，hover 身分改用單一 `AppState` 欄位（`std::optional<HWND>` 或既有的 `owner_draw_hovered_button`），刪除 `layout_hover_index`。版型按鈕的繪製需要索引時，由 HWND 反查 `layout_buttons` 陣列取得。

6. 新增 `tests/unit/pane_control_id_test.cpp` 並註冊進 `tests/CMakeLists.txt`。至少涵蓋：每個 `PaneControl` 的 pane 0 與 pane 3 都能來回 encode/decode；`base - 1` 與 `base + kExplorerCount` 回傳 `nullopt`；非 pane 的 360／400／500／780／781 全部回傳 `nullopt`。

## Non-goals

- 不搬任何 `AppState` 欄位進 `PaneChrome`（PD-182／PD-183 處理）。
- 不抽 `window_proc` 的巨型 case 成具名函式（PD-179 處理）。
- 不搬 dialog（PD-180 處理）。
- 不動 `explorer_host.cpp` 的 `WM_NCCREATE` prologue 與 error window（PD-181 處理）。
- **不統一** `{ ShellCallScope } + if (closing_ || shutdown_deferred) return X;` 這個 30+ 處的慣用法。回傳值分別是 `void`／`E_ABORT`／`0`，且 PD-172／PD-173／PD-177 剛修完這一區；統一時錯一個就破一條 reentrancy 路徑。
- **不移除** `AppState:506-528` 那 14 個 shutdown reference alias。實測 `closing_` 用於 91 處、`shutdown_deferred` 88 處；移除會讓約 200 個呼叫點變成 `state.shutdown_sequence.state().closing_`，更長更難讀。它們是 reference，不可能不同步，沒有兩份真相的風險。
- 不改任何視覺數值、常數值、glyph、圓角半徑或間距。
- 不註冊新的 window class。

## Acceptance Criteria

1. `id >= k` + `+ static_cast<int>(kExplorerCount)` 這個 range-test 樣式在 `main.cpp` 中只剩 **0 次**（算術全部在 `pane_control_id.h`）。
2. `pane_control_id.h` 不 include `windows.h`，且 `pane_control_id_test.cpp` 在 `ctest` 中出現並通過。
3. `AppState::layout_hover_index` 已刪除，`owner_draw_button_proc` 與 `layout_button_proc` 合併為一個 proc。
4. 四組樣板各自只剩一份定義。
5. 視覺與行為零變更（見下方實機清單）。
6. `ctest --test-dir build --output-on-failure` 全綠，含 `panedock_launch_smoke`。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 算術不得殘留在 main.cpp
Select-String -Path src/app_shell/main.cpp -Pattern '\+ static_cast<int>\(kExplorerCount\)'
# 純函式標頭不得碰 windows.h
Select-String -Path src/app_shell/pane_control_id.h -Pattern 'windows\.h|HWND|LRESULT'
# 合併後的 hover 欄位
Select-String -Path src/app_shell/main.cpp -Pattern 'layout_hover_index'
```

前兩條與第三條都必須「無結果」。

```powershell
git diff --check
```

## 交給使用者的實機檢查清單

本專案沒有 UI 自動化（`docs/tickets.md:322` 已否決方向），下列由使用者執行並回報：

1. 四宮格 Group：逐一點擊每個 pane 的 back／forward／up／refresh／view mode／pinned／folder-context 按鈕，確認都作用在**正確的 pane** 上。
2. 滑鼠移過上述按鈕與 header 的版型按鈕，確認 hover 高亮出現與消失的時機、顏色與既有 build 一致（合併 hover 狀態機的回歸點）。
3. Tooltip：停在 tab 的 add 按鈕與左右捲動按鈕上，確認 tooltip 內容與延遲不變。
4. 版型按鈕列：切 1→2→3→4→1，確認按鈕圖示與圓角外觀不變。

## Handoff requirements

在 `## 交接區` 記錄：`main.cpp` 行數變化、15 個 range-test 區塊縮成幾行、COM refcount 選了 CRTP 還是 WRL 及理由、`explorer_host.cpp` 的 prologue 為何留下、以及使用者實機檢查的回報結果。

## 交接區

- `src/app_shell/main.cpp`：6748 行降為 6612 行（-136）。原本 15 個 range-test 區塊改為 `WM_DRAWITEM` 的 32 行 table/decode route 與 `WM_COMMAND` 的 63 行 decode/switch route，共 95 行；control ID 的差值與 pane 範圍檢查只留在 `pane_control_id.h`。
- COM refcount 選 CRTP `ComRefCounted`：只抽出四個類別逐字相同的 `AddRef`／`Release`，保留各類別既有 `QueryInterface`，比改寫為 WRL `RuntimeClass` 的 diff 小。
- `explorer_host.cpp` 的 `WM_NCCREATE` prologue 依 Non-goals 留下；`window_state_from_create` 只合併 `main.cpp` 內四份現存 prologue（工作樹實際找到四份，不是現況描述所寫五份）。
- GDI helper 只取代固定 1 px solid border 與 `NULL_PEN` 的逐字樣板；依 DPI pen、虛線 pen 與後續另畫 border 的段落保留，以維持既有視覺數值。
- Agent checks：configure PASS、build PASS、三條 `Select-String` PASS（皆無結果）、`git diff --check` PASS；以可寫 session storage 的 elevated 權限完整 ctest 20/20 PASS，含 `panedock_launch_smoke`。非 elevated 重跑的表面 FAIL 是實際 `%LOCALAPPDATA%\\PaneDock` ACL 拒絕建立 `session.json.tmp`，觸發預期的 save-failure MessageBox，不是 shutdown hang。
- 使用者實機檢查：尚未執行／尚未回報；需依上方四項清單驗證 pane routing、hover、tooltip 與版型圖示。
