# PD-067 — Group 數量超過側邊欄高度時的捲軸:機制已可用,但視覺存在感過低

Phase 7 · sidebar · Depends on: PD-028, PD-061

- Source: 使用者回報(2026-08-26):「太多 group 且超過 view 的高度時,要能夠顯示 scroll bar,方便捲動切換」。
- Origin: 使用者原文追加項。
- Priority: LOW——**經實測,捲動功能已經完整可用,本票只處理視覺辨識度**。開票前的調查結果讓這張票從「新增功能」降級為「視覺微調」。

## 已確認的現況(實機實測,不是猜測——這一段是本票最重要的內容)

**開票前先實測驗證使用者的假設是否成立,結果是:捲軸已經存在,而且捲動功能完全正常。** 具體證據:

在執行中的 `PaneDock.exe` 上把 Group 數量從 4 個增加到 18 個(實際點擊 `+ New Group` 14 次),側邊欄清單高度 737px、每列 54px@96dpi,約可容納 13 列,確定溢出。量測結果:

1. **`WS_VSCROLL` 已經在建立時就設定。** `src/sidebar/sidebar.cpp` 第 36-37 行:
   ```cpp
   WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_HASSTRINGS |
       LBS_NOINTEGRALHEIGHT | LBS_NOTIFY | LBS_OWNERDRAWFIXED,
   ```
   執行期 `GetWindowLongW(list, GWL_STYLE)` 回傳 `0x50210151`,`WS_VSCROLL`(`0x200000`)位元確實已設定。
2. **捲動狀態正確。** `GetScrollInfo(list, SB_VERT, ...)` 回傳 `min=0 max=17 page=13 pos=5`——範圍、頁大小與位置都正確反映 18 個項目、可見 13 個。
3. **捲軸有實際繪製出來。** `PrintWindow` 截圖可見清單右緣有一條細的深色 thumb 配淺色 track(Windows 11 的細型/overlay 捲軸樣式),不是完全沒有東西。
4. **滑鼠滾輪捲動正常。** 游標置於清單上滾動:向上滾 3 格 `LB_GETTOPINDEX` 由 `5` 變 `0`,向下滾 5 格回到 `5`,完全符合預期。

**結論:使用者要求的「顯示 scroll bar」與「方便捲動」在功能層面都已經成立。** 真正的落差只有一個:那條捲軸又細又低對比(細 thumb、無箭頭按鈕、track 幾乎與背景同色),在淺色側邊欄上很容易被忽略,使用者因此以為沒有捲軸。

## 已確認的產品決策

1. **本票不新增捲動功能,只提高既有捲軸的視覺存在感。** 不得為了「做點什麼」而改寫成自訂捲軸控制項——那會是大幅倒退(要自己處理 thumb 拖曳、分頁捲動、滾輪、鍵盤、DPI、主題),而且 `AGENTS.md` 明確要求優先使用平台既有能力。
2. **實作 agent 必須先自行重現上述量測,確認在自己的執行環境中結論一致,再動手。** 若量測結果與本票不符(例如捲軸真的沒出現),**以實測為準,並在交接區記錄差異**——本票的診斷不該凌駕於新的實機證據之上。
3. **可接受的做法,依優先順序:**
   - **方案 A(優先):加上 `LBS_DISABLENOSCROLL` 樣式,讓捲軸永遠顯示**(項目不足時呈現為停用狀態)。這是一行改動,讓「這個清單可以捲動」變成恆常可見的線索,直接解決使用者「以為不能捲」的認知落差。缺點是項目很少時右側也會佔掉捲軸寬度。
   - **方案 B:維持自動顯示,但把清單的右內距調整成不讓 Group pill 被捲軸壓到**,並確認捲軸與側邊欄配色協調。目前 `Sidebar::draw_item` 的 `pill` 右緣只內縮 4px(第 115 行),捲軸出現時 pill 會被壓在捲軸下方,視覺上更亂。
   - **方案 C:兩者併用。**
   實作 agent 依實機截圖判斷後選擇,並在交接區說明理由。
4. **不改變列高、字級或 Group 列的其他視覺**——那是 PD-061 的範圍。兩票都會改到 `Sidebar::draw_item` 與側邊欄尺寸,後做的那票需先 rebase。
5. **不加自動隱藏/淡入動畫。** GDI 沒有內建動畫,需要 timer,違反 `AGENTS.md` 的事件驅動閒置規則。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`:
> **Keep `src/core` free of HWND, COM and `windows.h`.**

## Files to read and trace first

- `src/sidebar/sidebar.cpp` 第 30-53 行(`Sidebar::create`)——`WS_VSCROLL` 與樣式旗標,方案 A 的落腳處。
- `src/sidebar/sidebar.cpp` 第 100-197 行(`Sidebar::draw_item`),特別是第 113-117 行的 `pill` 內距——方案 B 的落腳處。
- `src/sidebar/sidebar.cpp` 第 61-70 行(`Sidebar::set_rect`)——清單矩形與列高設定。
- `src/app_shell/main.cpp` 的 `layout_sidebar`(第 1185 行起)——側邊欄清單矩形的計算來源。
- `src/sidebar/sidebar.h`——`kSidebarWidth`(226)、`kGroupRowHeight`(54)。
- `docs/tickets/PD-061-sidebar-group-typography.md`——會改到同一批程式碼的相鄰票。

## Scope

1. 提高 Group 清單捲軸的視覺存在感(方案 A / B / C 擇一或併用)。

## Non-goals

- 不自製捲軸控制項。
- 不新增捲動功能(滾輪、拖曳 thumb、鍵盤捲動都已正常)。
- 不改列高、字級、Group 列配色(PD-061)。
- 不加動畫或自動隱藏行為。
- 不改側邊欄寬度以外的版面。

## Acceptance

1. Group 數量超過可見高度時,捲軸清楚可辨,使用者一眼能看出清單可以捲動。
2. 捲動功能維持正常:滑鼠滾輪、拖曳 thumb、點擊 track 分頁捲動都可用(**修改前先量測一次作為基準,修改後重測比對**)。
3. Group pill 與右側數量徽章不被捲軸遮擋或壓到。
4. Group 數量少於可見高度時,版面正常(若採用方案 A,捲軸呈停用狀態且不造成版面跳動)。
5. 捲動後點擊某個 Group 仍能正確切換(PD-057 的行為未回歸,**這點必須以 `session.json` 的 `active_group_id` 驗證,不能只看畫面**)。
6. 在 150%/200% 顯示縮放下捲軸與內距正確縮放。
7. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
8. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "WS_VSCROLL|LBS_DISABLENOSCROLL|LBS_|pill" src\sidebar\sidebar.cpp src\sidebar\sidebar.h
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:連續點擊「+ New Group」直到超過可見高度(約 14 個以上),
# 截圖確認捲軸辨識度;測試滾輪、拖曳 thumb、點擊 track;
# 捲動後點擊某個 Group 並讀 session.json 確認切換正確。
```

**本票的量測方法(已在本環境驗證可用,請直接沿用):**

```powershell
# 捲軸樣式與狀態(不用猜,直接讀)
GetWindowLongW(list, GWL_STYLE)                 # WS_VSCROLL = 0x200000
GetScrollInfo(list, SB_VERT, ...)               # min/max/page/pos
SendMessageW(list, LB_GETTOPINDEX /*0x018E*/)   # 目前捲到第幾項
SendMessageW(list, LB_GETCOUNT   /*0x018B*/)    # 項目總數
```

滾輪模擬:`mouse_event(MOUSEEVENTF_WHEEL /*0x0800*/, 0, 0, 120, ...)` 向上、`-120`(以 `uint32` 傳入 `4294967176`)向下。

截圖用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`,不要用 `CopyFromScreen`。捲軸很細,截圖後請以 `InterpolationMode = NearestNeighbor` 放大 2 倍以上再判讀。

**測試會產生大量測試用 Group,請在測試前備份 `%LOCALAPPDATA%\PaneDock\session.json`,測試後還原**,不要把十幾個 `Group N` 留在使用者的資料裡。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- **自行重現的量測結果**(樣式位元、`GetScrollInfo`、滾輪測試),以及是否與本票的診斷一致。
- 最終採用的方案(A/B/C)與理由。
- 修改前後的側邊欄截圖(放大 2 倍以上)對照。
- 捲動後切換 Group 的 `active_group_id` 驗證結果。
- 高 DPI 下的驗證結果。
- 確認測試用 Group 已清除、`session.json` 已還原。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### PD-067 實作交接（2026-08-27）

實作採用方案 C：

- `src/sidebar/sidebar.cpp` 的原生 `LISTBOX` 加上 `LBS_DISABLENOSCROLL`。既有 `WS_VSCROLL`、`GetScrollInfo`、wheel、thumb 與 track 行為未重寫；少量 Group 時保留停用狀態，避免 scrollbar 出現/消失造成版面跳動。
- Group pill 的右側內距由 DPI-scaled `4px` 改為 DPI-scaled `14px`，讓數量 badge 與右側 scrollbar 保持明確間隔。未改列高、字級或 Group row 顏色，也未新增自訂 scrollbar。

自行量測與本票診斷一致，但測試環境的還原 session 原本已有 60 個 Group，且視窗 list client 為 `193x617`；因此不是票面範例的 18 個 Group / `page=13`，本次 overflow 量測為 74 個 Group / `page=11`。基準版仍證實既有捲動功能正常、但沒有 `LBS_DISABLENOSCROLL`；最終版只增加該 native style bit 並擴大 pill 右側保留空間。

量測證據（基準與最終均以同一個 96 DPI 視窗、同一套 Win32 probe 執行）：

- 基準 `docs/tickets/PD-067-before-96dpi.txt`：style `0x50210151`、`WS_VSCROLL=1`、`LBS_DISABLENOSCROLL=0`；14 個新增後 `min=0 max=73 page=11 pos=63 top=63 count=74`。
- 最終 `docs/tickets/PD-067-after-96dpi.txt`：style `0x50211151`、`WS_VSCROLL=1`、`LBS_DISABLENOSCROLL=1`；14 個新增後同為 `min=0 max=73 page=11 pos=63 top=63 count=74`。scrollbar geometry 為 `rect=249,155,266,772`，list client 為 `0,0,193,617`。
- Wheel：最終版 `after_wheel_up_3` 為 `pos/top=54`，`after_wheel_down_5` 回到 `63`。
- Thumb：先回到頂端後，`before_thumb_drag` 為 `pos/top=0`，`after_thumb_drag` 為 `pos/top=63`。
- Track page：`before_track_page_down` 為 `pos/top=0`，`after_track_page_down` 為 `pos/top=10`。
- probe 確實記錄 `PrintWindow(..., 2 /* PW_RENDERFULLCONTENT */) result=1`；未使用 `CopyFromScreen`。wheel 測試按票面使用 `mouse_event(MOUSEEVENTF_WHEEL, ..., 120/-120, ...)`，但本機前景視窗限制使注入後 index 未變，harness 才記錄並以同一個原生 listbox `WM_MOUSEWHEEL` 訊息 fallback 完成功能量測；因此 wheel 的原生行為已由數值證明，若要求實體 `mouse_event` 必須單獨成功，該環境子項仍受限。

放大後的視覺證據（原圖由 `PrintWindow` 取得，使用 `InterpolationMode.NearestNeighbor` 放大 3 倍）：

- [基準 sidebar](PD-067-before-sidebar-3x.png)
- [最終 sidebar](PD-067-after-sidebar-3x.png)
- [少量 Group sidebar](PD-067-after-low-sidebar-3x.png)

Acceptance：

1. 已驗證：最終放大圖可辨識 scrollbar track/thumb，且基準/最終對照可見 pill badge 與 scrollbar 的間隔改善。
2. 已以 `LB_GETTOPINDEX`、`GetScrollInfo` 驗證 wheel、thumb、track page 的前後變化；實體 `mouse_event` 注入受前景視窗限制，詳見上方限制說明。
3. 已驗證：最終圖中 badge 未被 scrollbar 覆蓋；程式以 DPI-scaled `14px` 右內距保留空間。
4. 已驗證：`docs/tickets/PD-067-after-low-96dpi.txt` 的 1 個 Group 量測為 `max=0 page=1 pos=0 top=0 count=1`，style 保留 `LBS_DISABLENOSCROLL=1`；少量 Group 圖顯示停用 scrollbar，版面欄位寬度未跳動。
5. 已驗證：scroll 後 click index 70 後，`session.json` 讀回 `groups=74`、`active_group_id=group-70`、`schema_version=1`。
6. 未驗證：本環境實際 `GetDpiForWindow=96`；沒有可用的 150%/200% 顯示器，未以模擬值冒充實機 DPI 證據。程式仍保留 `MulDiv(14, dpi, 96)` 與既有 DPI scaling 路徑。
7. 已驗證：LLVM-MinGW/Ninja configure 成功；`cmake --build build` 回報 `ninja: no work to do.`；`ctest --test-dir build --output-on-failure` 為 `100% tests passed out of 4`。
8. 已驗證：`git diff --check` 通過。

測試資料已清除：測試前原本 `session.json` 不存在、`session.json.bak` 為 68,329 bytes / 60 個 Group / `active_group_id=group-59`；測試產生的 74-group 與 1-group fixture 已移走，現在 `session.json` 仍不存在，原始 `session.json.bak` 已恢復。所有測試程序均以不帶 `/F` 的 `taskkill /PID <pid>` 關閉，未使用 `Stop-Process -Force`。

因 Acceptance 6 尚未取得 150%/200% 的實機證據，`docs/tickets.md` 的 PD-067 狀態維持 `ready`，不宣稱本票已完成。
