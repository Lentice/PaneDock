# PD-103 — 移除 Group 列表項右側的 tab 數量圓形徽章(與副標題重複)

Phase 7 · sidebar · Depends on: PD-028

- Source: 使用者需求(2026-08-28),附截圖:每個 Group 項目右側有一個灰底圓形徽章顯示 tab 數字,同一列的副標題文字裡已經有相同數字("4 panes · 4 tabs")。
- Origin: 使用者原文:「for group items remove the circle at right (tabs count). It should duplcate as subtitle of the group」
- Priority: LOW——純視覺精簡,不影響功能,使用者判斷這是重複資訊。

## 需要覆寫的既有決策

`docs/tickets/PD-028-sidebar-brand-row-two-line-group-summary-and-footer-restyle.md` 當初**刻意**同時設計了副標題與徽章,兩者顯示同一個數字:

> 右側徽章數字 = tab 總數(不是 pane 數),與副標題文字裡的第二個數字相同,呈現為一個圓形淺灰底徽章……

這是依照 `docs/panedock-ui-prototype.html` refined variant 1(Quiet Header)的設計稿原樣照做的重複顯示,不是實作疏漏。本票**明確覆寫**這個決策:使用者實機看到後判斷這是不必要的重複資訊,徽章拿掉,只保留副標題文字裡已經有的同一個數字。新證據為使用者本次直接提出的具體回饋(附截圖)。

## 已確認的現況(有程式碼證據,不是猜測)

`src/sidebar/sidebar.cpp`:

- `format_subtitle`(`:22-28`)已經產生 `"N panes · M tabs"`,`tab_count` 已經在副標題裡——使用者要保留的內容已經存在,不需要新增。
- `:188-193` 計算徽章的 `RECT`(直徑 22px@96DPI,置中於列高)。
- `:195-197` 因為要替徽章留空間,把 `text_area.right` 往左收窄到 `badge.left - 4px`。
- `:242-255` 畫徽章本體(`Ellipse` + `kBadgeBackground` 底色)與徽章文字(`std::to_wstring(group.tab_count)`,`kBadgeText` 文字色)。

## Fix 方向

1. 刪除徽章矩形計算(`:188-193`)與徽章繪製區塊(`:242-255`,含 `Ellipse` 呼叫與 `badge_text` 的 `DrawTextW`)。
2. `text_area.right` 改為延伸到 `pill.right` 扣掉一個邊界留白(建議沿用 `text_area.left` 已經在用的同一個 `MulDiv(6, dpi_, 96)` 內縮值,取得左右對稱的留白),不再受 `badge.left` 限制。
3. `format_subtitle`、`name_rect`/`subtitle_rect` 的既有計算方式、字型、顏色邏輯完全不變——副標題本來就已經包含使用者要保留的數字,不需要修改其內容或格式。
4. `kBadgeBackground`/`kBadgeText` 兩個常數(`:17-18`)若刪除徽章後在檔案裡不再被任何地方使用,一併移除;若後續發現其他地方仍引用,保留並在交接區說明。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

副標題文字本來就有 tab 數字,本票不需要新增任何格式化邏輯,純粹刪除徽章繪製並調整既有矩形寬度。

## Files to read and trace first

- `src/sidebar/sidebar.cpp:185-260`(`draw_item` 或等效繪製函式)——徽章計算與繪製的完整範圍,主要修改處。
- `src/sidebar/sidebar.cpp:22-28`(`format_subtitle`)——確認副標題已含使用者要保留的數字,不需修改。
- `src/sidebar/sidebar.cpp:17-18`(`kBadgeBackground`/`kBadgeText`)——刪除徽章後檢查是否仍被使用。
- `docs/tickets/PD-028-sidebar-brand-row-two-line-group-summary-and-footer-restyle.md`——徽章的原始設計決策與設計稿依據,本票覆寫其中「徽章 = 重複顯示 tab 數」的部分,其餘決策(品牌列、副標題本身、footer 按鈕)不受影響。

## Scope

1. 移除 Group 列表項右側的圓形 tab 數量徽章(視覺元素與其繪製邏輯)。
2. 名稱/副標題文字區的可用寬度隨徽章移除而擴大,填滿原本讓給徽章的空間。
3. 副標題文字內容、格式、字型、顏色完全不變。

## Non-goals

- 不改變副標題文字的格式或內容(`format_subtitle` 不變)。
- 不改變 Group 名稱行的字型、顏色、省略號邏輯。
- 不改變 PD-028 其餘決策:品牌列、footer 按鈕、選取/hover 底色。
- 不改變 `GroupSummary`(`sidebar.h`)的欄位——`pane_count`/`tab_count` 仍需保留供副標題使用,不刪除資料欄位,只刪除徽章這個視覺呈現。

## Acceptance Criteria

1. 每個 Group 列表項右側不再顯示圓形徽章。
2. 副標題("N panes · M tabs")與修改前顯示相同,數字正確對應該 Group 實際的 pane/tab 內容。
3. 移除徽章後,名稱/副標題文字區確實延伸使用原本讓給徽章的空間(不是留白/不是維持原窄度)。
4. 選取/hover/placeholder 三種既有視覺狀態不受影響。
5. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
6. `git diff --check` 通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "kBadgeBackground|kBadgeText|Ellipse|badge" src\sidebar\sidebar.cpp
git diff --check
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,單次啟動 + 單次截圖即可完成驗證,不需要連續互動。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 移除徽章後 `text_area.right` 最終採用的留白值與理由。
- 修改前後的側邊欄放大截圖比對結果。
- `kBadgeBackground`/`kBadgeText` 是否已無其他使用者、是否一併移除的確認結果。
- 未驗證項目與原因(若有)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

- `text_area.right` 最終改為 `pill.right - MulDiv(6, dpi_, 96)`（實作上以 `-=` 形式套用），與左側既有的 6px 內縮對稱；徽章移除後文字區使用原本保留給徽章的寬度，同時維持 pill 邊界留白。
- 截圖比對：修改前以既有 `docs/tickets/PD-067-before-sidebar-3x.png`（可見每列右側灰底圓形 `4` 徽章）作基準；修改後以單次啟動的 `PrintWindow(hwnd, hdc, 2)` 擷取，再以 4x `InterpolationMode.NearestNeighbor` 放大（暫存檔 `panedock-pd103-sidebar-4x-588.png`）。放大圖中 Group 3、Group 4、Group 2（`4 panes · 32 tabs`）等每列均保留正確副標題，右側不再有圓形徽章，文字區延伸至 pill 右側內縮處。主視窗幾何由 `GetWindowRect` 查得 `1936x1048`，Group 列表由 `GetDlgItem(hwnd, 100)` 取得，未以截圖估算座標。
- `kBadgeBackground`、`kBadgeText` 已一併移除；`rg -n "kBadgeBackground|kBadgeText|Ellipse|badge" src/sidebar/sidebar.cpp` 無任何結果，確認沒有其他使用者。
- 建置與測試：既有 `build/` 已配置，`cmake --build build` 最終成功，`ctest --test-dir build --output-on-failure` 為 5/5 PASS，`git diff --check` 通過。首次連結因舊測試行程鎖住 `PaneDock.exe`，以不帶 `/F` 的 `taskkill /PID` 關閉後重跑成功。
- 未驗證：未逐一對選取、hover、placeholder 三種狀態各自做互動截圖；本票 Agent Check 限定單次啟動＋單次截圖，且這些狀態的繪製分支未修改，故僅完成程式碼範圍核對。
