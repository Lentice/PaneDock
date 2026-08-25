# PD-053 — 側邊欄品牌列與 Group 清單之間的分隔線造成視覺割裂

Phase 6 · app_shell · Depends on: PD-028

- Source: 使用者比對 `docs/panedock-ui-demo-01-refined-quiet-header.html` 目標畫面與實機截圖後回報(2026-08-25)。
- Origin: 「左側 group 以及 PaneDock 標題應該是一個整體,不應該有分隔線,背景也是統一的」。
- Priority: LOW——一行程式碼等級的視覺修正。

## 已確認的根因(有程式碼證據,不是猜測)

`src/app_shell/main.cpp` 的 `draw_brand_bar`(第 1172-1229 行)第 1173 行填色 `RGB(251, 252, 254)`,`paint_client_background` 對整個側邊欄的填色(第 1389 行附近)也是同一個 `RGB(251, 252, 254)`——**兩者背景色已經完全一致,使用者說的「背景也是統一的」這部分其實已經滿足**,不需要改動。

真正造成視覺割裂的是 `draw_brand_bar` 自己在第 1178-1184 行額外畫的一條分隔線:

```cpp
RECT divider{rect.left, rect.bottom - scaled_value(window, 1), rect.right,
            rect.bottom};
HBRUSH divider_brush = CreateSolidBrush(RGB(223, 229, 236));
if (divider_brush != nullptr) {
    FillRect(dc, &divider, divider_brush);
    DeleteObject(divider_brush);
}
```

這條 1px 灰藍色(`RGB(223,229,236)`)分隔線,是品牌列與下方 Group 清單之間唯一的視覺區隔——去掉它,兩塊背景色相同的區域就會連成一個整體,符合使用者的要求與目標畫面(`docs/panedock-ui-demo-01-refined-quiet-header.html`,品牌列與 Group 清單之間沒有可見分隔線)。

## 已確認的產品決策

1. **刪除 `draw_brand_bar` 第 1178-1184 行畫分隔線的程式碼區塊。** 這是唯一需要的改動——背景色已經一致(見根因分析),不需要額外統一背景或調整版面配置。
2. **側邊欄下方其他既有分隔線(例如側邊欄與右側 pane 區域之間的分隔線,若有)不受本票影響**,本票只針對品牌列與 Group 清單之間的這一條。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `draw_brand_bar`(第 1172-1229 行)——本票要刪除的分隔線程式碼所在。
- `src/app_shell/main.cpp` 的 `paint_client_background`(第 1389 行附近側邊欄填色)——確認品牌列與 Group 清單背景色數值相同(`RGB(251,252,254)`),不需要額外改動。

## Scope

1. 刪除 `draw_brand_bar` 內畫分隔線的程式碼區塊(第 1178-1184 行)。

## Non-goals

- 不改變品牌列或側邊欄的背景色(已經一致,見根因分析)。
- 不改變品牌列的圖示/標題排版(圖示本身的內容由 PD-054 處理)。
- 不移除側邊欄與右側 pane 區域之間的分隔線(不同的分隔線,不在本票範圍)。

## Acceptance

1. 品牌列(「PaneDock」標題所在區塊)與下方 Group 清單之間沒有可見的分隔線,視覺上是一個連續的區塊。
2. 側邊欄其餘視覺(Group 清單項目、footer 按鈕)不受影響。
3. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
4. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "draw_brand_bar" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:目視確認品牌列與下方 Group 清單之間沒有可見分隔線。
# 本環境已具備螢幕截圖能力,請實際截圖比對。
```

## Handoff requirements

- 確認刪除分隔線後,螢幕截圖比對是否符合預期(無分隔線、背景連續)。

## 交接區

<!-- 實作 agent 填寫,append-only -->
