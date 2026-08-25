# PD-042 — Pane 容器誤圓角化「內部邊界」的頂端兩角,active 外框轉角視覺瑕疵

Phase 6 · app_shell · Depends on: PD-040

- Source: 使用者實機操作 `.\build\PaneDock.exe` 後回報,附截圖(2026-08-25)。
- Origin: 「active pane 的外框轉角處很奇怪」。
- Priority: MEDIUM——純視覺瑕疵,但發生在最顯眼的 active pane 藍色外框上,直接影響 PD-033/040 剛做完的視覺一致性。

## 已確認的根因(有程式碼證據,不是猜測)

1. **PD-040 新增的 explorer container(`state.explorer_containers[index]`)的矩形起點是「導覽列下緣」,不是「pane 的最外層上緣」。** 證據見 `src/app_shell/main.cpp` 第 1466-1467 行:
   ```cpp
   RECT rect = pane_rect;
   rect.top = navigation_top + navigation_height;
   ```
   也就是 container 的 `rect.top` 是 tab strip + 導覽列(address bar 那一整排)之下的位置,是一個**內部邊界**,不是 `draw_pane_card` 畫外框/陰影用的 `pane_rect`/`card` 那個最外層的 pane 邊界。
2. **PD-040 的 `apply_pane_container_region`(第 1181-1189 行)卻無條件把 container 的四個角全部裁成圓角**,呼叫方式是 `CreateRoundRectRgn(0, 0, width, height, radius, radius)` 套用到整個 container,包含它的**頂端兩角**。這代表:在「tab strip/導覽列下緣」與「Shell view 內容」交界的地方,憑空多裁出兩個圓角缺口——那裡在視覺設計上應該是一條**筆直的內部分界線**(導覽列底下緊接著內容區,不是 pane 的外層轉角),不應該有任何圓角。
3. **`draw_pane_card`(第 1191 行起)畫的卡片背景/外框用的是 `pane_rect`/`card`(涵蓋整個 pane,從最上緣到最下緣)的四個角,四角都圓,這才是視覺設計真正要的「pane 卡片圓角」。** container 的圓角裁切**只應該負責 pane 卡片「最下緣」那兩個角**(因為 container 的 `rect.bottom` 就是整個 pane 的下緣,PD-040 的目的正是要讓 Shell view 的下緣也跟著卡片一起圓角);container 的**頂端**兩角不該被裁——那裡上方還有 tab strip/導覽列覆蓋著,裁切頂端角只會在導覽列下緣與內容區之間製造一條多餘的圓角縫隙,和 active pane 藍色外框(沿著 `pane_rect` 整體外緣繪製,經過 tab strip/導覽列旁邊的圓角、再筆直往下到內容區,再到下緣圓角)在視覺上對不上,形成「轉角處很奇怪」的縫隙/鋸齒。

## 已確認的產品決策

1. **`apply_pane_container_region` 改為只圓化 container 的下緣兩角,頂端兩角維持直角。** 具體做法:先用 `CreateRoundRectRgn(0, 0, width, height, radius, radius)` 產生四角都圓的區域,再用 `CreateRectRgn(0, 0, width, radius)` 產生一塊「涵蓋頂端 `radius` 高度、寬度佔滿整個 container」的矩形區域,把兩者用 `CombineRgn(result, rounded, top_strip, RGN_OR)` 聯集——聯集會把頂端兩個圓角區域原本缺角的部分補滿成直角,底部兩角則因為矩形區域沒有覆蓋到那裡,維持原本的圓角。這是 Win32 產生「只有部分角落是圓角」區域的標準技巧,不需要新的第三方繪圖手段。
2. **半徑計算沿用既有共用函式 `pane_card_radius(dpi)`,不新增第二套半徑常數**,只改變「哪些角落套用圓角」,不改變圓角本身的大小或 DPI 縮放方式。
3. **`draw_pane_card` 本身不需要修改。** 它畫的是 pane 最外層卡片的四角圓角(涵蓋 tab strip/導覽列/內容區的完整外框),這部分視覺設計是對的,問題只出在 container 這個「內容區子集」不該重複裁切自己的頂端兩角。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/tickets/PD-040-pane-card-full-corner-rounding.md`(本票修正的是它遺留的邊界案例,不是推翻它的整體方向——四角圓角、容器裁切、`IExplorerBrowser` 生命週期不受影響這些決策都維持不變,只修正「container 的哪些角該圓」這個細節):
> 容器視窗的區域(`HRGN`)在建立與每次 `WM_SIZE`/`WM_DPICHANGED` 造成 rect 改變時,用 `CreateRoundRectRgn` 依 `draw_pane_card` 現有的 `radius`……重新計算並 `SetWindowRgn`。

## Files to read and trace first

- `src/app_shell/main.cpp` 第 1466-1489 行(`apply_layout` 裡 container 的 rect 計算、`apply_pane_container_region` 呼叫)——確認 container 的 `rect.top` 確實是「導覽列下緣」而非 pane 最外層上緣,理解為什麼頂端角是內部邊界。
- `src/app_shell/main.cpp` 第 1170-1189 行(`pane_card_radius`、`apply_pane_container_region`)——本票要修改的函式。
- `src/app_shell/main.cpp` 的 `draw_pane_card`(第 1191 行起)——確認它畫的 card/border 用的是完整 `pane_rect`(含 tab strip/導覽列),四角圓角的視覺意圖沒有問題,不需要改動,只是拿來對照 container 裁切範圍的差異。
- Win32 `CombineRgn`/`RGN_OR` 文件——確認參數順序(`CombineRgn(destination, src1, src2, mode)`)與回傳值處理(失敗時需要 `DeleteObject` 已建立的中繼 `HRGN`,避免資源洩漏)。

## Scope

1. 修改 `apply_pane_container_region`,依決策 1 改為「下緣圓角、頂端直角」的區域組合方式。
2. 確認並清理所有中繼 `HRGN`(`CreateRoundRectRgn`、`CreateRectRgn` 產生的物件在 `CombineRgn` 完成、`SetWindowRgn` 呼叫之後,只有最終傳給 `SetWindowRgn` 且成功的那一個區域所有權轉移給系統,其餘全部 `DeleteObject`,包含 `SetWindowRgn` 失敗時原本就有的清理路徑要延伸涵蓋新增的中繼區域)。

## Non-goals

- 不修改 `draw_pane_card` 的圓角繪製邏輯(已確認的產品決策 3)。
- 不修改 tab strip、導覽列本身的外觀或位置。
- 不處理 PD-041(主視窗缺少 `WS_CLIPCHILDREN` 導致內容被覆蓋)——那是不同根因,不要在本票內順手修,但**若兩票由不同 session 實作,注意兩者都改動 `apply_layout` 附近的程式碼,後做的一方需要重新讀取目前檔案內容再下手,避免互相覆蓋**。

## Acceptance

1. Active pane(藍色外框)與 inactive pane(灰色外框)在 tab strip/導覽列下緣與內容區交界處,不再出現圓角缺口或鋸齒縫隙——那裡應該是一條筆直的分界線。
2. Pane 卡片的下緣兩角(貼著真實 Shell 內容的最下緣)維持圓角,且與 `draw_pane_card` 畫的卡片背景圓角完全貼合(不因本票改動而退步成方角)。
3. 真實 Shell 檔案列表內容在裁切後的 container 內仍正常顯示與互動,沒有新的裁切異常。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "CombineRgn|RGN_OR|apply_pane_container_region" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:切換不同 pane 觀察 active/inactive 外框在導覽列下緣的轉角是否筆直、
# 下緣兩角是否維持圓角、真實檔案清單捲動/選取是否正常
```

## Handoff requirements

- 最終採用的 `HRGN` 組合方式與資源釋放路徑的具體實作。
- 若真實桌面測試發現任何殘留的視覺縫隙或半徑對不齊,記錄下來並說明因應方式,不強行在本票內解決所有邊角案例。
- 若在改動過程中發現 PD-041 已經改過 `apply_layout` 附近的程式碼,記錄合併方式,確保兩張票的改動都保留。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-25 實作交接

**HRGN 組合與釋放。** `apply_pane_container_region` 先以
`CreateRoundRectRgn` 建立四角圓角區域，再以 `CreateRectRgn(0, 0,
width, radius)` 建立頂端矩形，透過 `CombineRgn(region, rounded,
top_strip, RGN_OR)` 補回頂端兩角，最後只保留下緣兩角的圓角。`rounded`、
`top_strip` 與中繼 `region` 在建立失敗或 `CombineRgn` 失敗時均會釋放；組合
成功後先釋放兩個來源區域，`SetWindowRgn` 成功時由系統接管最終區域，失敗
時由程式釋放。

**PD-041 合併狀態。** 修改前重新讀取目前 `main.cpp`；已保留 PD-041 在
`CreateWindowExW` 的 `WS_CLIPCHILDREN`，本票沒有改動 `apply_layout` 的
既有佈局邏輯。

**驗證狀態。** Release 配置、`cmake --build build` 與 `ctest --test-dir
build --output-on-failure` 通過，測試為 4/4；PD-042 指定的 `rg` 與
`git diff --check` 也通過。嘗試使用 Computer Use 時原生 pipe 不可用，
因此未完成真實桌面截圖、滑鼠互動及視覺驗收；改以非互動 smoke check 啟動
`build\PaneDock.exe`，確認 `InputIdle=True`、`Responding=True`、主視窗標題
為 `PaneDock`，送出正常關閉後 5 秒內退出，未觀察到掛起或殘留程序。圓角
縫隙對齊、Shell 清單選取/右鍵/捲軸與拖放的視覺結果仍需具備桌面互動能力
時補測。
