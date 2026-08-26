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
