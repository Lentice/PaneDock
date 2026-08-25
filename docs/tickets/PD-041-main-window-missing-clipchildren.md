# PD-041 — 主視窗缺少 `WS_CLIPCHILDREN`,任何全視窗重繪會蓋掉 pane 內容

Phase 6 · app_shell · Depends on: PD-038

- Source: 使用者實機操作 `.\build\PaneDock.exe` 後回報,附截圖(2026-08-25)。
- Origin: 「hover 的 file item 才會顯示,不會自動刷新」「切換 active pane 會清除 pane 內的所有 item,變成一片空白,又要 mouse hover 的 item 才會刷新」。
- Priority: HIGH——核心可用性缺陷,而且是 PD-038 之後**仍然存在**的同類症狀,代表 PD-038 只修到一個觸發點,根因還有別的來源。

## 已確認的根因(有程式碼證據,不是猜測)

1. **主視窗(`kWindowClassName`)建立時的 style 只有 `WS_OVERLAPPEDWINDOW`(`src/app_shell/main.cpp` 第 3010 行 `CreateWindowExW(0, kWindowClassName, title, WS_OVERLAPPEDWINDOW, ...)`),完全沒有 `WS_CLIPCHILDREN`。**
2. **`paint_client_background`(第 1295 行起)在主視窗的 `WM_PAINT` 裡,一開始就用 `FillRect(dc, &client, canvas)` 對整個 client rect 上色(第 1298-1302 行),之後還會疊上 sidebar、header、divider、每個 pane 卡片的背景/邊框。** Win32 的 `WS_CLIPCHILDREN` 是「子視窗矩形要不要從父視窗的繪圖裁切區排除」的開關:**沒有**這個 style 時,父視窗的 `WM_PAINT` 繪圖操作會直接畫在子視窗(tab strip、PD-040 新增的 explorer container、以及它裡面真正的 Shell view)目前顯示在螢幕上的像素之上,把子視窗的畫面內容實際覆蓋掉——而且這個動作**不會**觸發子視窗自己重新繪製(子視窗的 update region 並未被標記為髒),所以覆蓋之後畫面就一直是空的,直到某個非本視窗來源的事件(例如滑鼠移動造成 Shell ListView 內部的 hit-test/hot-track 邏輯自行重繪)才會讓內容重新出現。
3. **這解釋了兩個回報症狀,而且是同一個根因、不同觸發點:**
   - 「切換 active pane 會清空」:`set_active_pane`(第 1691 行)結尾呼叫 `InvalidateRect(window, nullptr, TRUE)`(第 1699 行),`TRUE` 代表連背景一起擦除,觸發整個主視窗的 `WM_PAINT`,`paint_client_background` 的整片 `FillRect` 就把所有 pane 的 Shell view 內容蓋掉。
   - 「不會自動刷新,需要 hover」:任何其他會呼叫 `InvalidateRect(window, nullptr, TRUE)` 的既有路徑(第 1400 行,`apply_layout` 結尾;以及其他呼叫點,需以 `rg -n "InvalidateRect(window"` 重新確認完整清單)都會觸發一樣的覆蓋。PD-038 修的是 `ExplorerHost::navigation_complete`/`set_visible` 這兩個「導覽/顯示狀態改變」時機沒有強制重繪的問題,但**沒有處理**「主視窗自己重繪時反過來把子視窗蓋掉」這個完全不同的根因,所以症狀在別的觸發點(切換 active pane,不涉及導覽或顯示狀態改變)依然存在。

## 已確認的產品決策

1. **修法是在主視窗建立時的 style 加上 `WS_CLIPCHILDREN`(`WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN`),不是在每個呼叫 `InvalidateRect` 的地方個別補救。** 這是 `AGENTS.md`「Read the relevant spec section and trace every caller before touching shared code. A guard in the shared function is a smaller diff than a guard in every caller」的直接應用:根因在「主視窗會覆蓋子視窗」這一件事,修在主視窗的 style 上,一次解決所有現有與未來呼叫 `InvalidateRect(window, ...)` 的路徑,不需要逐一稽核每個呼叫點並個別加 `RDW_ALLCHILDREN`。
2. **加上 `WS_CLIPCHILDREN` 後,`paint_client_background` 的行為預期不變(它本來就不應該畫到子視窗佔用的區域),但因為過去沒有這個 style,不能排除程式裡有任何地方「故意」依賴父視窗畫到子視窗底下再讓子視窗蓋回來」這種非預期的繪製順序技巧。實作 agent 需要在改動後完整跑一輪視覺檢查(見 Acceptance),確認沒有任何既有視覺元素(pane 卡片圓角、陰影、tab strip、導覽列背景圓角、側邊欄)因為新增的裁切而消失或錯位。**
3. **不额外處理個別呼叫點的 `RDW_ALLCHILDREN` 或改用 `RedrawWindow`。** `WS_CLIPCHILDREN` 從根本上避免「父視窗畫到子視窗」這個問題發生,不需要疊加額外的重繪範圍擴大手段;若加上 `WS_CLIPCHILDREN` 之後仍觀察到任何殘留的空白症狀,那代表還有其他根因,需要在交接區誠實記錄,不要為了讓症狀「看起來消失」而疊加不必要的 `InvalidateRect(..., RDW_ALLCHILDREN)`。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Read the relevant spec section and trace every caller before touching shared code. A guard in the shared function is a smaller diff than a guard in every caller, and patching only the path the ticket names leaves sibling callers broken.

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers.

`docs/tickets/PD-038-pane-blank-until-hover.md`(前一次修正,本票是它遺漏的另一個觸發點,不是重複票——PD-038 的根因是 `ExplorerHost` 內部「導覽完成時 view 仍隱藏」,本票的根因是「主視窗重繪覆蓋子視窗畫面」,兩者程式碼位置、觸發時機都不同):
> 根因確實是「`BrowseToObject` 完成的當下,view 沒有收到讓它真正繪出內容的強制重繪」,但更精確地說——不是「非同步、之後才完成」,而是「同步完成、但完成時視窗是隱藏的,而且完成後沒有人再強制它重繪」。

## Files to read and trace first

- `src/app_shell/main.cpp` 第 3010 行 `CreateWindowExW` 建立主視窗的呼叫——本票要修改的確切位置。
- `src/app_shell/main.cpp` 的 `paint_client_background`(第 1295 行起)——確認它目前對整個 client rect 做全面 `FillRect`/繪製,且沒有排除子視窗佔用的矩形。
- `src/app_shell/main.cpp` 全部 `InvalidateRect(window` 呼叫點(用 `rg -n "InvalidateRect\\(window"` 重新確認完整清單,含 `apply_layout` 第 1400 行、`set_active_pane` 第 1699 行,以及可能還有的其他呼叫點如 `WM_DPICHANGED`)——確認加上 `WS_CLIPCHILDREN` 後,這些呼叫點的行為都變成「只重繪主視窗自己畫的裝飾,不覆蓋任何子視窗」。
- `docs/tickets/PD-040-pane-card-full-corner-rounding.md` 交接區——PD-040 新增的 explorer container 是主視窗的直接子視窗,`WS_CLIPCHILDREN` 也必須正確涵蓋它(而不只是舊有的 tab strip/address bar 等)。

## Scope

1. 主視窗 `CreateWindowExW` 的 style 從 `WS_OVERLAPPEDWINDOW` 改為 `WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN`。
2. 完整跑一輪視覺回歸檢查(見 Acceptance),確認既有的側邊欄、header、pane 卡片背景/陰影/圓角、導覽列背景等自繪視覺元素在加上裁切後沒有消失、錯位或出現新的空白縫隙。

## Non-goals

- 不修改 `ExplorerHost`(PD-038 已經處理過的部分,本票不重複)。
- 不個別修改任何 `InvalidateRect` 呼叫點的旗標或改用 `RedrawWindow`(已確認的產品決策 3)。
- 不處理 PD-042(active pane 外框轉角視覺瑕疵)——那是不同根因,另開票處理,不要在本票內順手修。

## Acceptance

1. 切換 active pane(點擊不同 pane、`F6`/`Shift+F6`)後,所有 pane(包含被切換前後的兩個)的檔案清單內容立即維持可見,沒有任何一格瞬間變空白。
2. 不需要任何滑鼠移動,程式啟動、切換 Group、切換 layout、切換 active pane 後,所有應該顯示內容的 pane 都立刻有內容(涵蓋 PD-038 原本的驗收項,確認本票沒有讓那些情境退步)。
3. 側邊欄、header、pane 卡片圓角背景/陰影、導覽列圓角背景等既有自繪視覺,在加上 `WS_CLIPCHILDREN` 後外觀不變(視覺回歸檢查)。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "WS_CLIPCHILDREN" src\app_shell\main.cpp
# 預期:主視窗 CreateWindowExW 呼叫點命中(新增),PD-040 的 explorer container STATIC 呼叫點原本就有(不受影響)
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動(關鍵驗收,必須完成,不能只跑 build/test 就宣稱完成):
# 開啟後不移動滑鼠,確認全部 pane 立刻有內容;
# 點擊切換 active pane(不同 pane、F6/Shift+F6),確認畫面內容全程不消失;
# 切換 Group、切換 layout,重複確認同樣結果;
# 目視比對側邊欄/header/pane 卡片圓角陰影等既有視覺沒有跑掉
```

## Handoff requirements

- 加上 `WS_CLIPCHILDREN` 後是否觀察到任何既有視覺元素受影響(消失、錯位、新的空白縫隙),若有,記錄下來並說明因應方式。
- 是否仍觀察到任何殘留的「pane 內容短暫消失需要 hover」症狀;若有,記錄復現步驟與初步判斷(可能代表 PD-038/041 都沒涵蓋到的第三個根因)。
- 若真實桌面測試（或本專案一貫的環境限制導致無法互動測試）無法完成,誠實記錄採用的替代驗證方式。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-25 實作交接

**修正內容。** `src/app_shell/main.cpp` 的主視窗 `CreateWindowExW` style
已由 `WS_OVERLAPPEDWINDOW` 改為 `WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN`,
讓主視窗的背景繪製不會覆蓋 tab strip、explorer container 與 Shell view。
未修改 `ExplorerHost`、任何 `InvalidateRect` 呼叫點或 PD-042 範圍。

**驗證狀態。** 指定的 CMake configure/build 成功,`ctest --test-dir build
--output-on-failure` 為 4/4 通過;`rg -n "WS_CLIPCHILDREN"
src\app_shell\main.cpp` 命中 PD-040 container 與本票主視窗建立點;`git diff
--check` 通過。

**桌面與視覺驗證限制。** 嘗試以 PowerShell 啟動 `build\PaneDock.exe` 做非
互動式煙霧測試,程序回報 `Responding=True`,但在 1.5 秒後仍沒有
`MainWindowHandle`/`MainWindowTitle`,因此 `CloseMainWindow()` 無法完成正常
關閉。已清理該次測試啟動的 PID,沒有殘留程序。此環境沒有桌面滑鼠/畫面觀察
通道,所以無法判定既有視覺元素是否消失、錯位或出現空白縫隙,也無法判定
「需要 hover 才恢復」症狀是否仍存在;未將非互動式結果宣稱為視覺驗收通過。
