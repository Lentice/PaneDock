# PD-188 — pane 卡片與導覽列背景改由 pane 自己繪製，移出 `paint_client_background`

Phase 7 · architecture · Depends on: PD-187

- Source: 同 PD-187（2026-09-04 使用者「為了權責分割更乾淨」的重開決定）。本票是 PD-187 那組效益裡**真正兌現價值**的一張：PD-187 只搬視窗階層，本票才把繪製責任還給 pane。
- Priority: MEDIUM——這是 PD-041／PD-045／PD-063 那個 bug 家族的結構性根治。

## Outcome

主視窗的 `paint_client_background` 不再計算與繪製任何 pane 的東西。每個 pane 在自己的 `WM_ERASEBKGND` 裡畫自己的卡片背景、陰影、外框與導覽列圓角底。

視覺逐像素相同。

## 這張票要根治的 bug 家族

| 票 | 症狀 | 共同根因 |
|---|---|---|
| PD-041 | 主視窗缺 `WS_CLIPCHILDREN`，全視窗重繪蓋掉 pane 內容 | 主視窗在自己的畫布上畫別人的區域 |
| PD-045 | active pane 下緣外框線被容器裁切，粗細與上緣不一致 | 卡片外框與 pane 容器分屬兩個座標系 |
| PD-063 | active pane 圓角外框鋸齒 | 同上 |
| PD-042 | pane 容器誤圓角化內部邊界頂端兩角 | 同上 |

四張票都是「誰的畫布、誰的座標」這一件事的不同表現。本票之後，pane 的視覺完全在 pane 自己的 client 座標內完成。

## 已確認的現況（2026-09-04 工作樹）

- `paint_client_background`（`main.cpp:2313-2379`）目前依序畫：
  1. `:2316-2320` 整個 client 的 canvas 底色 `RGB(243,246,249)`
  2. `:2322-2329` sidebar 底色
  3. `:2331-2335` brand bar（`draw_brand_bar`，`:2168`）
  4. `:2337-2340` header 底色
  5. `:2342-2354` 版型分段控制的底（`draw_layout_segment_background`）
  6. `:2356-2362` header 下緣分隔線
  7. **`:2364-2378` 每個 pane 的 `draw_pane_card` ＋ `draw_navigation_bar_background`** ← **只有這一段是本票要搬的**
- `draw_pane_card`（`main.cpp:2240` 起）刻意畫在 `pane_rect` 之外（左／上／右各 outset ≈2px@96dpi），讓圓角與外框落在 pane 之間的間隙裡。**PD-187 已把 pane HWND 的矩形外擴 `kPaneCardOutset` 並定義 pane-local 座標原點**，本票直接站在那個保證上。
- `pane_card_radius`（`main.cpp:2197` 附近）同時被 `draw_pane_card` 與 PD-040 的 explorer container 圓角裁切使用——**兩者必須繼續共用同一個半徑來源**，否則底部會出現接縫（PD-040 交接區已記錄過這個接縫）。
- active pane 指示是 `paint_tab_strip` 畫的直條 accent bar（`main.cpp:3969`，`kActivePaneIndicatorHeight`），畫在 tab strip 控制項自己身上，**不在本票範圍**。
- 主視窗有 `WS_CLIPCHILDREN`（PD-041）。PD-187 之後 pane HWND 是主視窗的子視窗，因此主視窗重繪時 pane 區域**自動被裁掉**——本票搬走那段繪製之後，主視窗不再有任何理由去碰 pane 的像素。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`：

> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`：

> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`：

> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

`docs/design-spec.md` NFR-003 反應性：

> Group 切換、版型切換、tab 切換不得因為某個 Shell location 緩慢或無法連線而凍結 UI。

**本票的責任**：繪製路徑**不得**做任何 Shell 呼叫、location 解析或 item count 查詢——卡片視覺只依賴矩形與 DPI，兩者都已在手上。PD-096 的教訓（`draw_brand_bar` 每次 `WM_ERASEBKGND` 都重新載入圖示與建立字型）同樣適用：pane 的繪製路徑不得在每則訊息重新建立 GDI 物件或重新推導幾何，該快取的要快取。

`docs/tickets.md` §已否決的方向：

> 端到端 UI 自動化(WinAppDriver／UIAutomation)｜對 live Shell view 極易 flaky。

（因此本票的視覺驗收靠逐像素截圖比對加上使用者實機清單，不引入 UI 自動化框架。）

PD-030 的既有產品決策（本票不覆寫）：陰影是 flat offset RoundRect，**不是**真正的模糊；不使用 `AlphaBlend`／`GradientFill`。

PD-040 的既有產品決策（本票不覆寫）：四角全圓角，explorer container 以同一半徑裁切真實 Shell view，底部不得出現接縫。

`docs/tickets.md` Agent 交付規則：

> 預設每個 ticket 的實作範圍為半天至兩天;若超過,先拆 ticket。

## Files to read and trace first

- `src/app_shell/main.cpp:2168-2379`：`draw_brand_bar`、`pane_card_radius`、`draw_pane_card`、`draw_navigation_bar_background`、`paint_client_background`。
- `src/app_shell/main.cpp:2700-2730`：`apply_layout` 中與 `draw_pane_card` 圓角／裁切相關的段落（`:2707` 有對應註解）。
- `src/app_shell/main.cpp:3960-3975`：`paint_tab_strip` 的 active pane accent bar（本票不動，但要確認搬家後仍對齊）。
- `src/app_shell/main.cpp:5370-5385`：`WM_ERASEBKGND` 呼叫 `paint_client_background` 的位置。
- `src/app_shell/pane.h`／`pane.cpp`：PD-187 產出的 pane proc 與 `kPaneCardOutset`。
- `docs/tickets/PD-187-per-pane-window-class-and-hwnd.md`：pane HWND 的矩形外擴與 pane-local 座標定義。
- `docs/tickets/PD-030`、`PD-040`、`PD-041`、`PD-042`、`PD-045`、`PD-048`、`PD-063`：卡片視覺的既有產品決策，**全部不得覆寫**。

## Scope

1. 把 `paint_client_background` 的 `:2364-2378` 整段（含 `has_active_group` 判斷、`layout_rects` 呼叫、`draw_pane_card`、`draw_navigation_bar_background` 迴圈）從主視窗移除。
2. `draw_pane_card` 與 `draw_navigation_bar_background` 移到 pane 的繪製路徑，改吃 **pane-local 座標**：卡片矩形是整個 pane HWND 的 client rect（因為 PD-187 已把 outset 併進 pane 視窗矩形），導覽列底則由 pane 自己的 `navigation_geometry` 換算。
3. 兩個繪製函式若仍被 pane 以外的地方使用，維持共用；若移動後只剩 pane 使用，搬進 `pane.cpp` 的匿名 namespace。**`pane_card_radius` 必須維持單一來源**，供卡片繪製與 PD-040 的 explorer container 裁切共用。
4. pane proc 的 `WM_ERASEBKGND` 實作必須避免閃爍：一次填滿整個 client（不要先填底色再蓋卡片造成兩次可見繪製）。若量到閃爍，允許改用 memory DC 一次 blit，但**不得**引入 `AlphaBlend`／`GradientFill`（PD-030 決策）。
5. 主視窗端確認：搬走之後 `paint_client_background` 不再呼叫 `layout_rects`，也不再需要 `active_group`——順手把它的簽章從 `const AppState&` 收窄。
6. 確認 pane 顯示／隱藏（版型切換）與 pane 矩形變動時的失效區處理正確：pane HWND 移動或改變大小時，其自身與**相鄰間隙**都要正確重繪（間隙屬於主視窗，卡片的 outset 屬於 pane，兩者交界是最容易留殘影的地方，PD-077 是前例）。
7. 加一個 source-level 檢查腳本或斷言，防止未來有人把 pane 的繪製加回主視窗：`paint_client_background` 內不得出現 `draw_pane_card`／`layout_rects`。

## Non-goals

- **不**改任何卡片視覺參數：圓角半徑、陰影偏移、外框顏色、底色、導覽列圓角（PD-030／040／048／063 的既有決策）。
- **不**動 active pane 指示（`paint_tab_strip` 的 accent bar）。
- **不**動 sidebar、brand bar、header、版型分段控制、header 分隔線的繪製——它們留在主視窗。
- **不**把命令／通知處理搬進 pane proc（PD-189）。
- **不**引入 `AlphaBlend`／`GradientFill`／真實模糊陰影。
- **不**引入 UI 自動化框架。
- **不**改 `core`、realize 政策、Shell 呼叫閘門或 session 行為。

## Acceptance Criteria

1. `paint_client_background` 內已無 `draw_pane_card`、`draw_navigation_bar_background`、`layout_rects` 或 `active_group` 的呼叫。
2. pane 卡片與導覽列底由 pane proc 繪製，座標全在 pane-local。
3. `pane_card_radius` 仍是卡片繪製與 explorer container 裁切的**單一**來源。
4. **視覺逐像素相同**（方法見 Agent Checks），涵蓋：1／2／3／4 pane 的每一種版型、active 與非 active pane、96dpi 與 150dpi。
5. 拖曳縮放與拖曳分隔線時，pane 與間隙交界處無殘影（PD-077 的回歸檢查）。
6. 新增的防回歸檢查存在且會在違規時失敗。
7. `ctest --test-dir build --output-on-failure` 全綠。
8. 閒置時 CPU 為 0%、無額外重繪（NFR-001 不得因本票退步）。
9. **NFR-003 不退步**：pane 的繪製路徑內無 Shell 呼叫、location 解析或 item count 查詢；未在每則訊息重新建立 GDI 物件（PD-096 的同類問題），交接區說明快取策略。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 主視窗不得再繪製 pane
Select-String -Path src/app_shell/main.cpp -Pattern 'paint_client_background' -Context 0,70 |
  Select-String -Pattern 'draw_pane_card|layout_rects'
```

必須無結果。

```powershell
# 不得引入被否決的繪製手段
Select-String -Path src/app_shell/pane.cpp,src/app_shell/main.cpp -Pattern 'AlphaBlend|GradientFill'
```

必須無結果。

```powershell
# 視覺逐像素比對：以 PrintWindow 擷取（不是 CopyFromScreen），改動前後同一組條件各一張
# 條件：同一份 session.json、同一視窗大小、96dpi 與 150dpi 各一輪、四種版型各一張
$p = Start-Process build\PaneDock.exe -PassThru; Start-Sleep 4
# （擷取與比對結果貼進交接區）
$p.CloseMainWindow() | Out-Null
if (-not $p.WaitForExit(8000)) { throw 'process survived graceful close' }
```

```powershell
git diff --check
```

## 交給使用者的實機檢查清單

1. 四種版型（1／2／3／4 pane）各看一遍，確認卡片圓角、陰影、外框與現狀**看不出差別**。
2. 切換 active pane 5 次，確認 accent bar 位置與粗細不變。
3. 拖曳主視窗邊框縮放 30 秒、拖曳 pane 分隔線 20 次，確認 pane 與間隙交界處無殘影、無閃爍。
4. 把視窗移到不同 DPI 螢幕來回 5 次，確認圓角與陰影在兩種 DPI 下都正確。
5. 在 pane 內捲動大資料夾、開右鍵選單、切換檢視模式，確認卡片沒有被蓋掉或閃爍。
6. 放著不動 1 分鐘，用工作管理員確認 CPU 為 0%。
7. 工作管理員無殘留 `PaneDock.exe`。

## Handoff requirements

在 `## 交接區` 記錄：搬移後 pane 繪製的座標換算方式、`pane_card_radius` 的共用點、閃爍處理採用的做法（直接繪製或 memory DC）與依據、逐像素比對涵蓋的組合與結果、PD-077 殘影回歸檢查結果、防回歸檢查的實作位置、閒置 CPU 讀數、以及使用者實機檢查 7 項的逐項回報。

## 交接區

### 2026-09-04 實作交接

- `paint_client_background` 現只畫主視窗 canvas、sidebar、brand bar、header、
  layout segment 與 divider；簽章收窄為 sidebar width 與 layout-button
  `span`，不再取得 `AppState`，函式內無 pane layout 或 pane paint。
- `apply_layout` 仍以唯一的 `navigation_geometry` 算出主視窗座標；
  `Pane::set_paint_geometry` 以 `pane_window_rect.left/top` 轉成 pane-local
  座標並快取。`WM_ERASEBKGND` 只讀 pane client rect、快取的導覽列矩形與
  DPI，不做 Shell 呼叫、location 解析、item count 或幾何重算。
- `pane_card_radius(dpi)` 移至 `pane.h`，是 `Pane::paint_background` 卡片
  `RoundRect` 與 `apply_pane_container_region` explorer clip 的單一共用來源。
  視覺參數未改：10px radius、2px outset、2px shadow、既有白底／陰影／
  border／navigation 色票。為讓 pane 真正擁有舊主視窗畫在相鄰 gap 的
  shadow footprint，pane HWND 的 right/bottom 同時納入 shadow offset；card
  rect 扣回該 offset，子控制項仍以舊 `pane_rect` 原點定位。這是 0-pixel
  比對所需的座標修正。
- 閃爍處理採每個 `Pane` 可重用的 memory DC／bitmap：bitmap 只在首次或
  client size 改變時建立，card border pen 只在首次或 DPI 改變時建立；
  brush 使用 `DC_BRUSH`／`DC_PEN` stock object。陰影、卡片、外框與導覽列
  先完整畫到離屏表面，最後單次 `BitBlt` 到 `WM_ERASEBKGND` 的 target DC；
  未使用 `AlphaBlend`／`GradientFill`，也未在每則 paint 重建 GDI 資源。
- PD-077 回歸：四 pane 下連續 30 次 window resize、20 次 splitter drag，
  再以 `PrintWindow(PW_RENDERFULLCONTENT)` 比較固定 1300x900 前後畫面。
  pane/card/gap 邊界差異 0 pixels；全圖僅 56 pixels 位於兩個原生 Shell
  scrollbar thumb（x=717–718/y=697–719、x=1267–1268/y=588–592），不是
  pane/gap stale repaint。changed pane 另以 `RDW_ERASE | RDW_NOCHILDREN`
  invalidate 自身，主視窗既有 invalidation 負責相鄰 gap。
- 防回歸檢查位於 `tests/release/pane_paint_ownership_check.ps1`，並以
  `panedock_pane_paint_ownership` 接入 ctest；它截出
  `paint_client_background` 函式並拒絕 `draw_pane_card`／`layout_rects`。
- 逐像素比較：committed `HEAD` baseline 與本票 build 使用同一份
  `session.json`、同一 desktop session、固定 1300x900，全部以
  `PrintWindow(PW_RENDERFULLCONTENT)`（非 `CopyFromScreen`）擷取。96 DPI
  的 1-pane active、2-pane active/inactive、3-pane active/inactive、4-pane
  active/inactive 共 7 組皆為 0/1,170,000 different pixels。環境有兩個
  monitor，但 `GetDpiForMonitor` 均為 96x96；沒有可達的 150 DPI 環境，
  因此 150 DPI 未執行並留待實機檢查。
- 60 秒完全閒置量測：CPU delta 0.000000 seconds（0.0000%），process I/O
  delta為 0 read ops、0 write ops、0 read bytes、0 write bytes；無 polling
  或額外 idle repaint 路徑。
- Agent Checks：LLVM-MinGW configure/build PASS；
  `ctest --test-dir build --output-on-failure` 23/23 PASS、0 failed、6.59 秒
  （`panedock_launch_smoke` 2.09 秒、`panedock_explorer_host_lifetime`
  0.45 秒）；兩組 `Select-String` 禁止 pattern 均無結果；
  `git diff --check` PASS。

### 使用者實機檢查清單回報

1. 四種 pane-count 卡片視覺：96 DPI PrintWindow 逐像素 PASS；仍建議肉眼
   確認實際互動畫面。
2. active pane 切換：自動切換 active/non-active 並逐像素 PASS；accent bar
   程式碼未動。
3. resize 30 次／splitter drag 20 次：自動壓力檢查 pane/gap 0 pixels
   difference；實際拖曳的主觀閃爍仍建議肉眼確認。
4. mixed DPI：未驗證；目前兩個 monitor 都是 96 DPI，無 150 DPI 環境。
5. Shell view 捲動／context menu／view mode：未做完整人工互動；本票未動
   command/notification route，launch smoke 與全量測試通過。
6. 閒置一分鐘：PASS，CPU 0.0000%，process I/O 無增量。
7. 殘留 process：各 PrintWindow、resize、idle 與 launch-smoke 輪次皆正常
   關閉，最後無殘留 `PaneDock.exe`。
