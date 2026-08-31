# PD-152 — Group row inline rename editor 偶發不可見

Phase 7 · sidebar + app_shell · Depends on: PD-017, PD-041, PD-150

- Source: 使用者實機回報與 2026-08-31 handoff。
- Symptom: 對 Group row 開啟 `Rename Group` 時，偶爾只看見閃爍 caret，EDIT 的背景與文字不可見。
- Priority: HIGH——rename 仍可輸入但沒有可見回饋，且由不受保護的 sibling repaint 時序觸發。

## Outcome

從 Group row 右鍵選單或 `F2` 開始 inline rename 時，EDIT 的背景、文字、選取範圍與 caret 每次都可見；後續 sidebar repaint 不得蓋掉 editor。Enter commit、Escape／失焦 cancel、Group 選取與儲存語意不變。

## 診斷結論與待驗證邊界

靜態程式碼與 Win32 clipping 契約支持以下首要根因，但目前沒有可互動桌面，尚未取得會紅的 runtime capture；實作 agent 必須先完成本票的 Phase A，不能把本段當成已通過的實機證明。

1. `Sidebar::begin_rename()` 以 `LB_GETITEMRECT` 取得 listbox client rect，映射到 `parent_` 後，把 EDIT 建成主視窗的 child；因此 EDIT 與 `list_box_` 是重疊 siblings。
2. `Sidebar::create()` 建立 owner-draw LISTBOX 時沒有 `WS_CLIPSIBLINGS`；EDIT 也沒有。Microsoft 的 `WS_CLIPSIBLINGS` 契約明載：重疊 child 未指定此 style 時，一個 child 的繪製可能侵入相鄰 sibling。
3. 右鍵路徑在 `TrackPopupMenu` 前呼叫 `refresh_sidebar()`；它經 `Sidebar::set_groups()` 執行 `LB_RESETCONTENT`／`LB_ADDSTRING`，再恢復 selection。hover 與 selection 路徑也會 invalidation／重繪 owner-draw row。因此 menu 關閉、EDIT 建立後仍可能有 listbox repaint，時序符合「偶發」。
4. listbox repaint 蓋掉 sibling EDIT 的 client pixels，不會移除 EDIT focus 或 system caret，正好解釋「caret 還在，背景與文字消失」。
5. 次要假設依序為：(a) EDIT 未收到初次 `WM_PAINT`；(b) focus 在 popup menu teardown 後被移走；(c) editor rect 映射錯誤。Phase A 若看到 EDIT `WM_PAINT` 且 focus／rect 正確，但 listbox repaint 後 pixels 消失，即排除三者並支持首要根因。

## 首要根因成立時的最小修正方向

1. `Sidebar::begin_rename()` 將 EDIT parent 由 `parent_` 改為 `list_box_`；直接使用 `LB_GETITEMRECT` 的 listbox client 座標，刪除 `MapWindowPoints`。
2. `Sidebar::create()` 的 LISTBOX style 加上 `WS_CLIPCHILDREN`，使 listbox 繪製排除 child EDIT 區域。
3. `close_editor()` commit 時仍向 `parent_` 傳送 `kRenameCommitMessage`；不改 rename 資料流。
4. 若 Phase A 反證首要根因，先在 ticket 交接區記錄 capture，再依實證採最小修法；不得退回 `HWND_TOP`、重複 invalidate、timer 或 polling 掩蓋 race。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-001：
> 使用者可新增、重新命名、複製、刪除、重新排序 Group。

`docs/design-spec.md` §FR-001：
> 當鍵盤焦點在 sidebar 的 Group row 時，按 `F2` 開始該 Group 的 inline rename；鍵盤焦點在 Shell view 時，`F2` 維持 Windows 原生檔案 rename 行為。

`docs/development.md` Architecture rules：
> `sidebar` | Group list rendering, selection input | Authoritative Group state (that is `core`)

`docs/development.md` Change workflow：
> Make the smallest change that satisfies the acceptance criteria. Reuse before adding.

`AGENTS.md`：
> Read the relevant spec section and trace every caller before touching shared code.

`AGENTS.md`：
> Keep `src/core` free of HWND, COM and `windows.h`.

`AGENTS.md`：
> Event-driven idle path only. No busy loops, no polling timers.

`AGENTS.md`：
> New non-trivial logic needs one focused runnable test or self-check.

Microsoft Win32 `Window Styles`：
> `WS_CLIPCHILDREN` excludes the area occupied by child windows when drawing occurs within the parent window.

Microsoft Win32 `Window Styles`：
> Without `WS_CLIPSIBLINGS`, drawing within one overlapping child can draw within a neighboring child window.

## Files to read and trace first

- `docs/design-spec.md` §4.1、§FR-001、§FR-014。
- `docs/development.md` Architecture rules、Change workflow；`docs/testing.md` 的 Win32 UI 驗證邊界。
- `docs/tickets/PD-017-group-sidebar.md`——既有 Group mutation 與 inline rename 決策。
- `docs/tickets/PD-041-main-window-missing-clipchildren.md`——同類 clipping 缺口及不可用重繪掩蓋根因的先例。
- `docs/tickets/PD-150-group-row-f2-rename.md`——新增的第二個 `Sidebar::begin_rename()` caller 與鍵盤路由。
- `src/sidebar/sidebar.cpp`——完整讀取 `create`、`set_groups`、hover invalidation、`begin_rename`、`edit_proc`、`close_editor`。
- `src/sidebar/sidebar.h`——editor、parent 與 commit message ownership。
- `src/app_shell/main.cpp`——完整追蹤 `refresh_sidebar`、`WM_CONTEXTMENU`、`kRenameGroupId`、`kRenameCommitMessage`、message-loop F2 caller。

## Scope

### Phase A — 建立會紅的 feedback loop

1. 在真實桌面的 Release build，以至少 50 次迴圈重複：右鍵不同 Group row → `Rename Group` → 立即觸發 sidebar hover／selection repaint → Escape。記錄失敗次數；loop 必須能捕捉「EDIT 背景或文字消失但 caret／focus 仍存在」的精確症狀。
2. 只加暫時、具唯一 `[DEBUG-PD152]` 前綴的 probe，記錄 EDIT `WM_PAINT`／`WM_KILLFOCUS`、`GetFocus()`、EDIT 與 LISTBOX rect，以及 listbox repaint 的先後；一次只驗證一個假設。
3. 若 50 次仍無法出現症狀，保留相同 loop 並加入 `RedrawWindow(list_box_, ..., RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN)` 作為 deterministic repaint 壓力，只用於診斷，不得提交產品。

### Phase B — 修正與回歸

1. 首要根因成立時，只修改 `src/sidebar/sidebar.cpp`：EDIT 改為 LISTBOX child、刪除座標映射、LISTBOX 加 `WS_CLIPCHILDREN`。
2. 以 Phase A 原始 loop 與 repaint 壓力 loop 各重跑至少 50 次，失敗數皆為 0。
3. 移除所有 `[DEBUG-PD152]` probe 與 throwaway 壓力碼。
4. 驗證右鍵與 F2 兩個 caller，以及 Enter、Escape、失焦三條 close path。

## Non-goals

- 不修改 `src/core`、`core::rename_group`、session schema 或儲存格式。
- 不改 Group row owner-draw 視覺、hover、selection、context menu 或 F2 routing。
- 不用 `SetWindowPos(HWND_TOP)`、額外 `InvalidateRect`、timer、polling 或 sleep 掩蓋重繪順序。
- 不新增 helper、control class、UI framework、dependency 或 UIAutomation／WinAppDriver 測試。
- 不順手改其他重疊 child window；沒有相同實機症狀就不擴 scope。

## Acceptance criteria

1. Phase A 留下可重複執行的精確操作序列與修正前失敗數；若只能由 repaint 壓力 loop 變紅，交接區必須明記自然重現率與壓力重現率。
2. 修正後右鍵 `Rename Group` 與 sidebar-focus `F2` 各連續 50 次，EDIT 背景、原 Group name、全選狀態與 caret 全程可見，失敗數為 0。
3. rename 期間移動滑鼠造成 row hover repaint、切換 listbox selection repaint 或強制 repaint，皆不得蓋掉 EDIT。
4. Enter 仍 commit 並刷新／儲存；Escape 與失焦仍 cancel；完成後沒有殘留 EDIT HWND。
5. 96 DPI 與至少一個非 96 DPI 桌面下，editor rect 對齊選中 Group row，沒有因移除 `MapWindowPoints` 產生偏移。
6. `cmake --build build`、`ctest --test-dir build --output-on-failure` 與 `git diff --check` 全數通過。

這是 `sidebar` 的 Win32 repaint/lifetime 修正，沒有適合放進 `src/core` 的可測邏輯；focused runnable self-check 是 Phase A/B 的重複 repaint loop。若環境沒有可互動桌面，必須在交接區記錄 `未驗證，需真實桌面`，不得宣稱根因或視覺驗收已確認。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "begin_rename|MapWindowPoints|WS_CLIPCHILDREN|WS_CLIPSIBLINGS|kRenameGroupId|kRenameCommitMessage|VK_F2|refresh_sidebar|LB_RESETCONTENT" src\sidebar src\app_shell\main.cpp
rg -n "DEBUG-PD152" src tests
git diff --check
```

```powershell
.\build\PaneDock.exe
# 真實桌面：執行 Phase A/B 的右鍵與 F2 各 50 次 loop，並在 96 DPI 與非 96 DPI 驗證 rect。
# 不使用 UIAutomation／WinAppDriver；測試後以不帶 /F 的 taskkill 優雅關閉。
```

## Handoff requirements

- 記錄 Phase A 的自然重現率、壓力重現率、訊息／focus／rect 順序，以及被排除的假設。
- 記錄最終 HWND parent/style 與座標系；若未採首要修法，附上反證與實際根因。
- 記錄右鍵、F2、Enter、Escape、失焦與兩種 DPI 的結果。
- 確認所有 `[DEBUG-PD152]` 與 throwaway 壓力碼已移除。
- 記錄 build、full CTest、`rg` 與 `git diff --check` 結果。

## 交接區

<!-- 實作 agent 填寫，append-only -->

### 2026-08-31 實作交接

- `src/sidebar/sidebar.cpp` 是唯一產品程式修改：LISTBOX 建立 style 加上 `WS_CLIPCHILDREN`；inline EDIT 的 parent 由主視窗 `parent_` 改為 `list_box_`；沿用 `LB_GETITEMRECT` 的 listbox client 座標並刪除 `MapWindowPoints`。`close_editor()` 仍向主視窗 `parent_` 傳送 `kRenameCommitMessage`，rename commit/cancel 資料流未變。
- 此執行環境沒有 ticket 要求的可互動真實桌面，而且可用的 Windows automation 依賴 UIAutomation／合成輸入，屬本票明確排除項目；因此 Phase A 的自然／強制 repaint 50 次紅燈、修正後右鍵與 F2 各 50 次綠燈、非 96 DPI rect 尚未驗證。不得據此宣稱 runtime 根因已被 capture；需真實桌面補驗後才能把 tracker 改為 `done`。
- Release configure/build PASS。sandbox 內 CTest 為 10/11，只有 `panedock_launch_smoke` 因無法寫入真實 `%LOCALAPPDATA%` 而在 modal warning 後逾時；改在提升權限環境重跑完整 CTest 為 11/11 PASS，launch smoke 0.90 秒。
- 未加入或留下 `[DEBUG-PD152]` probe、repaint 壓力碼、timer、polling、額外 invalidate 或新測試 framework。
