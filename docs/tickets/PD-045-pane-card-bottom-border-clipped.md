# PD-045 — Active pane 下緣外框線被容器裁切,粗細與上緣不一致

Phase 6 · app_shell · Depends on: PD-041, PD-042

- Source: 使用者實機操作 `.\build\PaneDock.exe` 後回報,附截圖(2026-08-25)。
- Origin: 「active pane 的下緣框線粗細與上緣框線不一致」。
- Priority: MEDIUM——視覺一致性問題,PD-041 修正 `WS_CLIPCHILDREN` 之後才顯現出來的既有幾何缺陷。

## 已確認的根因(有程式碼證據,不是猜測)

1. **`draw_pane_card`(`src/app_shell/main.cpp` 第 1210 行起)的卡片矩形 `card` 在左/上/右三邊往外多留了 `outset`(96-DPI 基準 2px,經 DPI 縮放)的空隙,但下緣完全沒有這個空隙:**
   ```cpp
   const RECT card{pane_rect.left - outset, pane_rect.top - outset,
                   pane_rect.right + outset, pane_rect.bottom};
   ```
   `card.bottom` 直接等於 `pane_rect.bottom`,跟 PD-040 新增的 explorer container 的 `rect.bottom`(見 `apply_layout` 第 1466-1467 行附近,container 的 rect 就是這個 `pane_rect`)完全同一條線。
2. **外框是用單一 `RoundRect` 呼叫、單一 `HPEN`(`border_width` 寬)畫出來的,GDI 的筆畫寬度是「以路徑為中心,內外各一半」。** 在左/上/右三邊,因為 `card` 邊界比 `pane_rect`(=container 邊界)多了 `outset` 的距離,筆畫的內外兩半都落在 container 矩形**外面**,完全不會被裁切,肉眼看到的是完整寬度。但在下緣,筆畫路徑正好跟 container 的邊界重合,筆畫寬度有一半落在 container 矩形**內部**;PD-041 修正 `WS_CLIPCHILDREN` 之後,主視窗(父視窗)已經不能畫到子視窗(container)佔用的區域,所以下緣筆畫「落在 container 內部」的那一半直接被裁掉不會顯示,肉眼看到的下緣框線只剩大約一半寬度——這正是使用者說的「下緣框線粗細與上緣不一致」。**這是 PD-041 修正 `WS_CLIPCHILDREN` 之後才會顯現的既有幾何缺陷:修 PD-041 之前,父視窗會直接畫過 container 的畫面覆蓋掉它(PD-041 本身要修正的 bug),連帶「意外地」讓下緣筆畫的內側那一半被父視窗畫出來、看起來完整——PD-041 修好覆蓋問題後,這個裁切不對稱才浮現出來。**

## 已確認的產品決策

1. **`card.bottom` 也比照左/上/右三邊,往外延伸 `outset` 的距離,即改為 `pane_rect.bottom + outset`。** 這讓下緣的外框筆畫路徑也整條落在 container 矩形之外,跟其他三邊處理方式一致,不需要引入不同的裁切或繪製手法,是最小改動。
2. **陰影矩形(`shadow_rect`)與白色卡片背景(`card_brush` 的 `RoundRect`)都沿用同一個 `card` 矩形,不需要個別調整**——它們本來就跟著 `card.bottom` 一起變動,改了 `card.bottom` 之後自動保持一致,不需要額外程式碼。
3. **PD-042 新增的 container 圓角裁切(`apply_pane_container_region`)不需要跟著調整。** container 的 `rect`/`width`/`height` 仍然對齊 `pane_rect`(即 Shell 內容實際佔用的範圍),裁切半徑不變;本票只調整 `draw_pane_card` 畫外框用的視覺矩形往下延伸 `outset`,兩者是不同矩形(卡片視覺 vs. 內容裁切),分開處理,不要為了「讓兩個矩形完全一樣」而反過來改動 container 的尺寸。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/tickets/PD-041-main-window-missing-clipchildren.md`(本票要修正的是它修正完 `WS_CLIPCHILDREN` 後才顯現的既有幾何缺陷,不是要撤銷 PD-041):
> 沒有這個 style 時,父視窗的 `WM_PAINT` 繪圖操作會直接畫在子視窗……目前顯示在螢幕上的像素之上,把子視窗的畫面內容實際覆蓋掉。

`docs/tickets/PD-042-pane-container-top-corner-seam.md`(container 的 rect/裁切半徑,本票不改動):
> `apply_pane_container_region` 改為只圓化 container 的下緣兩角,頂端兩角維持直角。

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `draw_pane_card`(第 1210-1272 行)——本票要修改的 `card.bottom` 計算式所在。
- `src/app_shell/main.cpp` 的 `apply_layout` 裡 container 的 rect 計算(第 1466-1489 行附近)——確認 container 的 `rect.bottom` 就是 `pane_rect.bottom`,理解為什麼下緣筆畫路徑跟 container 邊界重合。
- `docs/tickets/PD-040-pane-card-full-corner-rounding.md`/`PD-042-pane-container-top-corner-seam.md` 交接區——確認 card 的視覺矩形與 container 的裁切矩形本來就是兩個獨立概念(卡片背景/外框 vs. Shell 內容裁切邊界),本票延續這個既有的分離設計。

## Scope

1. `draw_pane_card` 的 `card` 矩形計算,把 `card.bottom` 從 `pane_rect.bottom` 改為 `pane_rect.bottom + outset`。

## Non-goals

- 不修改 PD-042 的 container 裁切半徑或矩形範圍(已確認的產品決策 3)。
- 不修改陰影/背景的繪製邏輯本身(它們沿用同一個 `card` 矩形,自動受益於本票的改動,不需要個別修改)。
- 不修改左/上/右三邊既有的 `outset` 數值或計算方式。

## Acceptance

1. Active pane(藍色 2px 外框)與 inactive pane(灰色 1px 外框)的下緣框線粗細與上緣/左右框線視覺上一致,沒有下緣明顯變細的現象。
2. Pane 卡片下緣的圓角、陰影視覺不因本票改動而跑位或變形(卡片整體只是往下多延伸 `outset` 的距離,圓角比例維持不變)。
3. 真實 Shell 檔案列表內容顯示範圍不受影響(container 裁切矩形本身沒有改變)。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "card\.bottom|pane_rect\.bottom \+ outset" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:目視比對 active/inactive pane 四邊框線粗細是否一致,
# 確認下緣圓角/陰影沒有跑位、Shell 檔案列表內容顯示範圍正常
```

## Handoff requirements

- 改動後 `card.bottom` 的最終計算式與是否有任何連帶需要調整的地方(例如與相鄰 pane 的間距是否因為卡片往下多延伸而出現視覺重疊)。
- 若真實桌面測試發現框線粗細仍有些微差異(次像素取整等因素),記錄下來並說明是否在可接受範圍內。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-25 實作交接

`draw_pane_card` 的 `card.bottom` 已改為 `pane_rect.bottom + outset`，讓下緣
外框路徑與左/上/右邊使用相同的外擴距離。陰影與白色卡片背景仍沿用同一個
`card`，不需要連帶修改；`apply_layout` 的 Shell container rect、裁切區域、
相鄰 pane 間距與其他三邊計算均未改動，因此沒有新增視覺重疊或影響檔案列表
範圍。編譯階段成功，但 Release link 因既存的 `build\\PaneDock.exe` 程序
（PID 15904）持有輸出檔而回報 `Permission denied`；`ctest` 4/4 通過，
`rg` 及 `git diff --check` 也通過。

本環境未具備可觀察桌面的互動能力，未完成真實桌面目視驗證；因此 active/
inactive 下緣框線、圓角與陰影的最終視覺一致性仍需在可互動的 Windows 桌面
補測。非互動式 smoke check 亦未宣稱為視覺驗收。
