# PD-033 — Active pane 指示改為扁平彩色外框,移除舊式立體邊框

Phase 6 · app_shell, explorer_host · Depends on: PD-030

- Source: `docs/panedock-ui-prototype.html?refined=1&variant=1&solo=1`(Quiet Header 變體)
- Origin: 2026-08-25,PD-028~031 視覺改版過程中盤點既有功能與設計稿落差時發現:`ExplorerHost::set_active` 目前用 `WS_EX_CLIENTEDGE`(Windows 95 年代的立體凹陷邊框)標示 active pane,這個視覺語言與 PD-030 剛做完的扁平白底圓角卡片風格衝突——凹陷邊框在圓角卡片旁邊會顯得突兀。
- Priority: MEDIUM——功能上「有區分 active pane」這件事本來就存在且正確(`docs/tickets/PD-009-active-pane-and-layout-toggle.md` 已驗收),本票純粹是視覺風格追上 PD-030 之後的一致性問題,不是修 bug。

## 已確認的產品決策

1. **`ExplorerHost::set_active` 整個刪除,不保留 no-op 版本。** 目前這個函式(`src/explorer_host/explorer_host.cpp:367-390`)唯一做的事就是切換 `GWL_EXSTYLE` 的 `WS_EX_CLIENTEDGE` 並強制重繪框線;拿掉這個視覺效果後,函式內容就是空的。留一個永遠什麼都不做的函式比直接刪除更容易誤導之後的讀者以為它還有作用,`AGENTS.md`「Deletion over addition」與「Don't add features, refactor, or introduce abstractions beyond what the task requires」都指向直接刪除。`src/explorer_host/explorer_host.h` 對應的宣告(第 39 行)一併刪除。
2. **`main.cpp` 目前呼叫 `.set_active(true/false)` 的 8 個既有呼叫點全部直接刪除該行,不替換成其他呼叫。** 這些呼叫點(`activate_group`、`add_group`、`delete_group`、`set_active_pane`、`toggle_layout`、`WM_PARENTNOTIFY` 分支——實際行號在改動當下重新 `rg` 確認,不要憑本文件的行號)本身只是純粹轉發「哪個 pane 現在是 active」,拿掉視覺效果後這個轉發沒有其他作用。**這些呼叫點旁邊呼叫的 `focus()`(實際鍵盤焦點)完全不受影響、不刪除**——`set_active`(視覺)與 `focus()`(鍵盤焦點)是兩個獨立方法,只刪前者。
3. **視覺指示改由 `draw_pane_card`(PD-030 新增的函式)畫一個扁平彩色外框,取代原本刪掉的立體邊框。** `draw_pane_card` 新增一個 `bool is_active` 參數:`true` 時外框顏色改用 accent 藍(`RGB(37,99,235)`,沿用側邊欄既有 `kSidebarActiveText`/選取 pill 用的同一個藍色,全應用配色一致),線寬比非 active 狀態粗一階(例如非 active 用 1px、active 用 2px,依 DPI 縮放);`false` 時維持 PD-030 既有的淺灰邊框與線寬。不新增動畫或漸層效果。
4. **是否為 active pane 的判斷,直接用既有的 `active_pane_index(active_group(state))` 比對迴圈索引,不新增任何新的狀態欄位。** `paint_client_background` 本來就在迴圈裡逐一畫每個可見 pane 的卡片,呼叫 `draw_pane_card` 時多算一個 `index == active_pane_index(active_group(state))` 的布林值傳進去即可;`has_active_group(state)` 為 `false`(空狀態)時不會進到這個迴圈,不需要額外判斷。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Don't add features, refactor, or introduce abstractions beyond what the task requires... Don't design for hypothetical future requirements.

`CONTEXT.md`:
> **active pane**: The pane that receives keyboard commands, clipboard operations and shortcuts. Exactly one pane is active at any time, and it is visually indicated.
> _Avoid_: focused pane, current pane, selected pane

`docs/tickets/PD-009-active-pane-and-layout-toggle.md`(既有驗收,本票不重新開放這個決策,只換視覺呈現方式):
> AC1:程式碼證據為 `WM_PARENTNOTIFY` → `set_active_pane` → `ExplorerHost::set_active`／`focus`;active border 使用 `WS_EX_CLIENTEDGE`。

## Files to read and trace first

- `src/explorer_host/explorer_host.h`(第 39 行 `set_active` 宣告)、`src/explorer_host/explorer_host.cpp`(第 367–390 行 `set_active` 定義,含 `focus()` 在它下面,確認兩者互不依賴)。
- `src/app_shell/main.cpp` 全部 `.set_active(` 呼叫點(目前有 8 處,包含 `activate_group`、`add_group`、`delete_group`、`set_active_pane`、`toggle_layout`、`WM_PARENTNOTIFY`/`WM_LBUTTONDOWN` 分支)——改動前用 `rg -n "\.set_active\("` 重新確認完整清單與行號,不要憑本文件枚舉的位置。
- `docs/tickets/PD-030-pane-card-chrome-and-tab-header-restyle.md` 的交接區——`draw_pane_card` 目前的簽章、邊框顏色/線寬的既有數值,與它在 `paint_client_background` 迴圈裡的確切呼叫方式。
- `docs/tickets/PD-009-active-pane-and-layout-toggle.md`——active pane 概念的既有驗收記錄,確認本票沒有改變「哪個 pane 是 active」的判斷邏輯,只改「怎麼畫出來」。

## Scope

1. 刪除 `src/explorer_host/explorer_host.h` 的 `set_active` 宣告與 `src/explorer_host/explorer_host.cpp` 的定義。
2. 刪除 `src/app_shell/main.cpp` 全部呼叫 `.set_active(true)`/`.set_active(false)` 的既有行(逐一確認,不要用全域取代誤刪其他同名符號——`core::set_active_pane`/`core::set_active_tab` 是不同函式,不在此範圍)。
3. `draw_pane_card` 簽章新增 `bool is_active` 參數,依決策 3 改變邊框顏色與線寬。
4. `paint_client_background` 對每個可見 pane 呼叫 `draw_pane_card` 時,多傳入 `index == active_pane_index(active_group(state))` 的比對結果。

## Non-goals

- 不改變「哪個 pane 是 active」的判斷邏輯或 `core::set_active_pane`/`GroupState::active_pane_id` 語意。
- 不改變鍵盤焦點(`ExplorerHost::focus()`)行為。
- 不做外框顏色的漸層、動畫或陰影效果。
- 不處理側邊欄的 Group 選取視覺(PD-028 已完成,與本票無關)。

## Acceptance

1. 切換 active pane(點擊另一個 pane、Group 切換、layout 切換、`F6`/`Shift+F6`)時,新的 active pane 顯示藍色扁平外框,舊的 active pane 恢復淺灰外框,沒有殘留的立體凹陷邊框視覺。
2. 鍵盤輸入、剪貼簿操作、快速鍵仍然正確送往原本判斷為 active 的 pane(行為與改版前一致,只是視覺呈現不同)。
3. `rg -n "WS_EX_CLIENTEDGE" src` 只命中既有的 `kErrorWindowClassName` 那一處(不可解析 location 的錯誤視窗樣式,PD-022 既有功能,與 active pane 指示無關,不在本票範圍內移除)。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "set_active\(" src\app_shell\main.cpp src\explorer_host
# 預期:無命中(函式與全部呼叫點都已刪除)
rg -n "WS_EX_CLIENTEDGE" src
# 預期:只剩 kErrorWindowClassName 錯誤視窗那一處
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:點擊不同 pane、切換 Group、切換版型、F6/Shift+F6,確認 active pane 顯示藍色扁平外框
# 且鍵盤輸入/剪貼簿操作正確送往該 pane
```

## Handoff requirements

- `draw_pane_card` 最終簽章與 active/非 active 邊框顏色、線寬的具體數值。
- 8 個(或實際確認後的正確數量)`.set_active(` 呼叫點刪除後的最終清單,供之後若要重新加回某種 host 層級視覺提示時參考「當初刪在哪裡」。
- 若真實桌面測試發現藍色外框與 PD-030 卡片陰影疊在一起時對比度不足或視覺衝突,記錄下來並說明採用的因應(例如加深顏色或加寬線寬)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-25 實作交接

- `draw_pane_card` 最終簽章為 `void draw_pane_card(HDC dc, RECT pane_rect, UINT dpi, bool is_active) noexcept`。active 外框使用 `RGB(37,99,235)`、96-DPI 基準 2px；非 active 外框維持 `RGB(223,229,236)`、96-DPI 基準 1px；兩者都以 `MulDiv(value, dpi, 96)` 做 DPI 縮放。PD-030 既有卡片底色 `RGB(255,255,255)`、圓角半徑 10px、outset 2px、陰影 `RGB(205,211,219)` 未改動。
- 已刪除 `ExplorerHost::set_active` 宣告、定義與全部實際呼叫，共 10 個呼叫點：`activate_group` 舊 active／新 active 各 1 處、`add_group` 空狀態初始化 1 處、`delete_group` 舊 active／新 active 各 1 處、`set_active_pane` 前後各 1 處、`set_layout` 前後各 1 處、`WM_CREATE` 初始 active 1 處。所有 `focus()` 呼叫保留。active 狀態切換後由 `set_active_pane`，以及共用的 `apply_layout` 路徑呼叫 `InvalidateRect(window, nullptr, TRUE)`，讓卡片背景依最新 `active_pane_index(group)` 重畫。
- Agent checks：指定 LLVM-MinGW/Ninja configure、`cmake --build build` 成功；`ctest --test-dir build --output-on-failure` 為 4/4 通過；`rg -n "set_active\\(" src\\app_shell\\main.cpp src\\explorer_host` 無命中；`git diff --check` 通過。
- `rg -n "WS_EX_CLIENTEDGE" src` 仍命中兩處：`src/explorer_host/explorer_host.cpp` 的 PD-022 `kErrorWindowClassName` 錯誤視窗，以及 PD-028 既有的 `src/sidebar/sidebar.cpp` Group rename `EDIT` 控制項。後者不是 active pane 指示，且不在本票 Scope、Non-goals 明確排除 sidebar 視覺，因此沒有為了 literal grep 改動相鄰功能；本票的該 Agent check 依文字預期「只剩錯誤視窗」未完全滿足。
- 已嘗試以 Computer Use 啟動並操作真實桌面；初始化與一次重試都回報 `Computer Use native pipe is unavailable: failed to connect native pipe: 系統找不到指定的檔案。 (os error 2)`，與 PD-030 交接記錄的限制相同，因此沒有完成真實滑鼠點擊不同 pane、也沒有宣稱藍色外框視覺驗證通過。另以非互動方式啟動 `build\\PaneDock.exe`，觀察到程序 `Responding=True` 且取得有效主視窗 handle，這只算啟動 smoke check。
