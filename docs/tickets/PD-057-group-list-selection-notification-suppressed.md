# PD-057 — Group 拖曳排序的 `ReleaseCapture` 時機錯誤,吃掉 `LBN_SELCHANGE`,點擊 Group 無法切換

Phase 7 · app_shell · Depends on: PD-036

- Source: 使用者實機操作後回報(2026-08-26):「create new group 會自動切換到 new group,但無法切換回去舊的 group」。
- Origin: 使用者原文追加項。
- Priority: **CRITICAL**——Group 是本產品的核心抽象(`AGENTS.md`:「a **Group** is a named, saved working context that restores an entire pane arrangement in one click」),無法切換 Group 等於核心功能失效。

## 已確認的根因(經決定性實驗證實,不是猜測)

### 實驗證據

在執行中的 `PaneDock.exe`(側邊欄有 Group 1 / Group 2 / Group 3,active 為 `group-2`)上:

1. **用 `SetCursorPos` + `mouse_event` 真實點擊 Group 2 那一列:**
   - `LB_GETCURSEL` 由 `0` 變成 `1` — **LISTBOX 確實處理了點擊,選取狀態有移動,畫面也重繪成選取態。**
   - `session.json` 的 `active_group_id` **仍然是 `"group-2"`** — `activate_group` 沒有提交。
2. **改用 `SendMessageW(main_hwnd, WM_COMMAND, MAKEWPARAM(kGroupListId, LBN_SELCHANGE), listbox_hwnd)` 手動補送通知(`LB_GETCURSEL` 維持 `1` 不變):**
   - `active_group_id` **立刻變成 `"group-1"`(索引 1 那個 Group 的 id),Group 成功切換。**

**結論:`activate_group`、`WM_COMMAND` 處理、`Sidebar::selected_index()` 全部正常。唯一的缺口是「真實滑鼠點擊時,LISTBOX 沒有送出 `LBN_SELCHANGE`」。**

(附帶排除:曾有假設認為是 `activate_group` 第 1790-1792 行的 `target_id == active_group_id` 提前返回,因為 Group 順序被拖曳重排過。此假設**已被上述實驗推翻**——索引 1 對應的 id 是 `"group-1"`,與當時的 active `"group-2"` 不同,提前返回不會觸發;而且同一個 `cursel` 值在手動補送通知時可以正常切換。)

### 程式碼層級的根因

`src/app_shell/main.cpp` 的 `group_list_proc`(第 2535-2574 行)是 PD-036 為了 Group 拖曳排序而加上的 subclass。它的 `WM_LBUTTONUP` 分支:

```cpp
} else if (message == WM_LBUTTONUP && state != nullptr) {
    const bool dragging = state->group_drag.has_value() &&
                          state->group_drag->list == window &&
                          state->group_drag->dragging;
    finish_group_drag(*state, window);        // ← 這裡面會 ReleaseCapture()
    if (dragging) return 0;
}
...
return DefSubclassProc(window, message, wparam, lparam);  // ← LISTBOX 才在這裡收到 WM_LBUTTONUP
```

而 `finish_group_drag`(第 2485-2500 行)第 2491 行:

```cpp
if (GetCapture() == list) ReleaseCapture();
```

**問題在順序。** LISTBOX 的滑鼠選取是一個「按下開始追蹤 → 放開才提交並送出 `LBN_SELCHANGE`」的兩段式流程,追蹤期間它自己持有滑鼠 capture。上面的程式碼在把 `WM_LBUTTONUP` 交給 `DefSubclassProc`(也就是 LISTBOX 本人)**之前**就先呼叫了 `ReleaseCapture()`。`ReleaseCapture` 會立即送出 `WM_CAPTURECHANGED` 給 LISTBOX,而控制項收到 `WM_CAPTURECHANGED` 的標準反應是**放棄進行中的點擊追蹤**。等 `DefSubclassProc` 終於把 `WM_LBUTTONUP` 送到 LISTBOX 時,它已經沒有進行中的點擊了,於是不送 `LBN_SELCHANGE`。

選取外觀之所以還是會變,是因為那是在 `WM_LBUTTONDOWN` 階段就設定好的(第 2548 行的 `DefSubclassProc` 已經讓 LISTBOX 處理過按下)。**於是產生「看起來選到了、實際上沒切換」這個特別容易誤導的症狀。**

同一個 subclass 的 `WM_LBUTTONDOWN` 分支(第 2539-2553 行)反而是**正確**的示範:它先呼叫 `DefSubclassProc` 讓 LISTBOX 完成自己的處理,才動自己的狀態。`WM_LBUTTONUP` 分支沒有遵守同一個順序。

## 已確認的產品決策

1. **修法是把 `WM_LBUTTONUP` 分支調整成與 `WM_LBUTTONDOWN` 分支一致的順序:先讓 LISTBOX 完成它的處理(`DefSubclassProc`),再做我們自己的 `finish_group_drag`。** 這是最小且對稱的修改,不需要新增旗標或狀態機。
   - 注意仍需保留「真的在拖曳時吃掉這個 `WM_LBUTTONUP`」的行為(`if (dragging) return 0;`),否則拖曳結束會順帶觸發一次選取切換。實作 agent 要處理好「拖曳中」與「單純點擊」兩條路徑:拖曳中不可以讓 LISTBOX 處理 button-up;單純點擊必須讓 LISTBOX 處理。
2. **不要用「延後 `ReleaseCapture`」之類的權宜做法**(例如 `PostMessage` 一個自訂訊息稍後再放開 capture)。那會讓 capture 生命週期跨越訊息邊界,是 `AGENTS.md` 明文警告的重入風險來源。
3. **不改 `activate_group`、`refresh_sidebar`、`Sidebar::selected_index` 或 `WM_COMMAND` 的 `LBN_SELCHANGE` 處理。** 上述實驗已證明這四者都正確。
4. **必須同時驗證 Group 拖曳排序(PD-036)沒有回歸。** 本票動的正是拖曳排序的程式碼路徑,這是唯一的高風險點。
5. **`cancel_group_drag`(第 2477-2483 行)裡同樣的 `ReleaseCapture` 不在本票範圍**——它只在拖曳被中止時呼叫(滑鼠離開 client 區、按鍵放開、`WM_CAPTURECHANGED`),那些情境下 LISTBOX 本來就不該提交選取。實作 agent 若發現它也造成問題,記錄在交接區,不要順手改。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Read the relevant spec section and trace every caller before touching shared code.

`AGENTS.md`:
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`(產品核心,說明本 bug 的嚴重性):
> a **Group** is a named, saved working context that restores an entire pane arrangement in one click.

`docs/design-spec.md` FR-002:
> 選取 Group 時還原:版型、分隔比例、每個 pane 的 tab 集合、每個 tab 的 Shell location、view mode、排序欄位與方向、active pane、每個 pane 的 active tab。

## Files to read and trace first

- `src/app_shell/main.cpp` 第 2535-2574 行(`group_list_proc`)——**本票要修改的地方,重點是第 2561-2566 行的 `WM_LBUTTONUP` 分支。**
- `src/app_shell/main.cpp` 第 2477-2500 行(`cancel_group_drag`/`finish_group_drag`)——`ReleaseCapture` 的實際位置。
- `src/app_shell/main.cpp` 第 2502-2533 行(`update_group_drag`)——拖曳門檻判定,理解 `dragging` 旗標何時為真。
- `src/app_shell/main.cpp` 第 3051-3057 行(`WM_COMMAND` 的 `LBN_SELCHANGE` 處理)——確認不需要改。
- `src/app_shell/main.cpp` 第 1787-1810 行(`activate_group`)——確認不需要改。
- `src/sidebar/sidebar.cpp` 第 30-53 行(`Sidebar::create`,含 `LBS_NOTIFY` 與 `RegisterDragDrop`)——確認控制項樣式正確。
- `docs/tickets/PD-036-group-drag-reorder.md`——本回歸的來源票,理解拖曳排序原本的設計意圖。

## Scope

1. `group_list_proc` 的 `WM_LBUTTONUP` 分支修正訊息處理順序,讓單純點擊時 LISTBOX 能完成點擊並送出 `LBN_SELCHANGE`。

## Non-goals

- 不改 `activate_group` / `refresh_sidebar` / `Sidebar` 的任何邏輯。
- 不改 `cancel_group_drag`。
- 不改 Group 的右鍵選單、New Group 按鈕、拖曳懸停自動切換(`make_sidebar_drag_hover_target`)。
- 不改 Group 列的視覺(PD-058 hover、PD-061 字級)。

## Acceptance

1. **點擊側邊欄任一個 Group,該 Group 立即成為 active:** 右側 pane 版型與內容切換成該 Group 的內容,`session.json` 的 `active_group_id` 更新為該 Group 的 id。
2. 在三個以上的 Group 之間來回點擊切換,每一次都正確(特別是「新建 Group 後點回第一個 Group」這個使用者原始回報的情境)。
3. 點擊目前已經 active 的那個 Group 不會造成異常(既有的提前返回路徑仍正確)。
4. **Group 拖曳排序(PD-036)未回歸:** 按住某個 Group 拖到另一個位置放開,順序改變,且**不會**順帶把某個 Group 切成 active。
5. 拖曳過程中的插入指示線(`draw_group_insertion_indicator`)仍然可見。
6. 拖曳到一半按 Esc 或把滑鼠拖出清單範圍,拖曳正確取消且不觸發切換。
7. Group 的右鍵選單(Duplicate/Rename/Delete/Move Up/Move Down)未回歸。
8. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
9. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "group_list_proc|finish_group_drag|ReleaseCapture|LBN_SELCHANGE" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:建立三個 Group,在它們之間來回點擊切換,每次確認 pane 內容改變;
# 再拖曳其中一個 Group 改順序,確認順序改變且 active Group 不變。
```

**本票的自動化驗證方法(已在本環境驗證可用,請直接沿用):** 這個 bug 的關鍵在於「畫面看起來對、資料沒變」,純看截圖會被騙。請用資料層級驗證:

```powershell
# 點擊後直接讀 session.json 的 active_group_id,不要只看畫面
(Select-String -Path "$env:LOCALAPPDATA\PaneDock\session.json" `
  -Pattern '"active_group_id":"[^"]*"' -AllMatches).Matches[0].Value
# 同時用 LB_GETCURSEL (0x0188) 讀 LISTBOX 的選取索引,兩者必須一致
```

用 `LB_GETITEMRECT`(`0x0198`)取得每一列的實際矩形再換算螢幕座標來點擊,不要用截圖目測的座標——本環境已證實目測座標會點錯列。

**截圖驗證方法:** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 而不是 `Graphics.CopyFromScreen`。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- `WM_LBUTTONUP` 分支最終的訊息處理順序與程式碼。
- 「拖曳中」與「單純點擊」兩條路徑各自如何處理 `DefSubclassProc` 與 `finish_group_drag`。
- Group 切換與 Group 拖曳排序兩者的實機驗證結果(切換要附 `active_group_id` 的前後值,不能只附截圖)。
- 若發現 `cancel_group_drag` 的 `ReleaseCapture` 也有類似問題,記錄症狀但不要在本票修改。

## 交接區

<!-- 實作 agent 填寫,append-only -->
