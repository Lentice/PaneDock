# PD-060 — Pane 狀態列補上選取檔案的總大小,並加上分隔線/底色

Phase 7 · app_shell · explorer_host · Depends on: PD-051

- Source: 使用者實機操作後回報(2026-08-26),附上 Windows 檔案總管狀態列的參考截圖。
- Origin: 使用者原文第 5 項「pane footer 在使用者選擇檔案後應該是變成像這樣(`18 個項目  已選取 3 個項目  347 KB`)」、第 14 項「pane footer 應該要有顏色或細細的分隔線,UX 會比較好」。
- Priority: MEDIUM——狀態列已存在但資訊量不足,且與上方檔案區沒有視覺分界。

## 已確認的根因(有程式碼證據,不是猜測)

### 缺口一:選取時遺失「總項目數」與「總大小」

PD-051 已建立狀態列,但 `refresh_status_bar`(`src/app_shell/main.cpp` 第 1047-1061 行)的格式是**二選一**:

```cpp
const std::wstring text = counts.selected == 0
                              ? std::to_wstring(counts.total) + L" items"
                              : std::to_wstring(counts.selected) + L" of " +
                                    std::to_wstring(counts.total) +
                                    L" selected";
```

也就是有選取時顯示 `"3 of 18 selected"`,沒選取時顯示 `"18 items"`。使用者提供的參考截圖(Windows 檔案總管)是**三段並列**:總項目數、選取項目數、**選取項目的總大小**。目前完全沒有大小資訊,`ExplorerHost::ItemCounts`(`src/explorer_host/explorer_host.h` 第 25-28 行)也只有 `total` 與 `selected` 兩個 `int`,沒有任何大小欄位。

### 缺口二:狀態列與上方檔案區沒有視覺分界

狀態列是 `STATIC` 控制項(第 2763-2769 行建立),`WM_CTLCOLORSTATIC`(第 2964-2966 行)把它畫成白底、文字 `RGB(100,116,139)`。**白底 + 上方 Shell view 也是白底 = 兩者之間沒有任何分界線**,狀態列看起來像是浮在檔案清單裡的一行文字,而不是一條 footer。

## 已確認的產品決策

1. **狀態列格式改為三段式,UI 文字用英文**(`AGENTS.md`:App UI text must be English):
   - 未選取時:`18 items`
   - 有選取時:`18 items    3 selected    347 KB`
   三段之間的分隔用足夠寬的空白(或 tab stop),不用「|」等符號,比照使用者提供的檔案總管截圖。
2. **大小只在「有選取」時顯示,且只統計選取項目。** 不顯示整個資料夾的總大小——那需要遞迴列舉子資料夾,是昂貴且會產生磁碟 I/O 的操作,直接違反 `AGENTS.md` 的閒置規則。
3. **大小的取得方式:`IFolderView2::Items(SVGIO_SELECTION, IID_PPV_ARGS(&items))` 取得 `IShellItemArray`,逐項 `GetItemAt` → `QueryInterface<IShellItem2>` → `GetUInt64(PKEY_Size, &size)` 累加。** 這是 Shell 官方公開介面路徑,符合「Reach for the standard library and Win32 before adding a dependency」,而且不碰檔案系統(大小來自 Shell 的屬性系統)。
   - **資料夾沒有 `PKEY_Size`,`GetUInt64` 會失敗——這是正常情況,不是錯誤。** 該項目直接跳過(不累加、不中止整個統計)。
   - **選取項目數很多時必須設上限。** 使用者可能 Ctrl+A 選取數萬個項目,逐項 COM 呼叫會讓 UI 卡住。實作 agent 必須設一個明確上限(建議 1000 項),超過時**不顯示大小**(只顯示項目數與選取數),而不是顯示一個算到一半的錯誤數字。上限值與超過時的行為要寫進交接區。
4. **大小的格式化沿用 Shell 的慣例:`StrFormatByteSizeW`**(`shlwapi.h`)。不要自己寫 KB/MB/GB 的換算——`StrFormatByteSizeW` 是 Windows 內建、會跟隨系統慣例,而且免去一個容易寫錯的邊界條件集合。
5. **視覺分界採用「狀態列上緣一條 1px 淺灰分隔線 + 狀態列淺灰底」兩者並用。** 底色建議 `RGB(249,250,251)` 或與 pane 卡片體系一致的淺灰,分隔線建議 `RGB(232,237,242)`(與 `draw_pane_card` 的 inactive 邊框同色,保持整體色票一致)。具體色值由實作 agent 微調,但必須滿足:在白色檔案清單下方可清楚辨識為獨立的 footer 區塊,且不會搶眼到干擾檔案清單。
6. **分隔線的畫法由實作 agent 決定**——可以是狀態列自己 owner-draw(需把 `STATIC` 改成 `SS_OWNERDRAW` 並處理 `WM_DRAWITEM`),也可以在 pane 卡片的繪製路徑(`draw_pane_card` 附近)畫一條線。**若選擇改成 owner-draw,必須確認 `WM_CTLCOLORSTATIC` 的既有處理不再適用並一併清理,不要留下兩套互相打架的著色路徑。**
7. **`ItemCounts` 結構擴充為 `{ int total; int selected; unsigned long long selected_bytes; bool selected_bytes_valid; }` 或等效形狀。** `selected_bytes_valid` 用來表達「超過上限所以沒算」與「算出來剛好是 0」的差別——這兩者必須可區分,否則會顯示誤導性的 `0 KB`。精確簽章由實作 agent 決定。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> **App UI text must be English.** No Chinese strings ship in the binary.

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers. The application must be measurably at 0% CPU and doing no disk I/O when the user is not interacting with it.

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`AGENTS.md`:
> File operations go through Shell `IDataObject` and `IFileOperation`. Never assemble a path string and call the filesystem directly — that loses virtual items, progress UI and conflict handling.
>
> (相關:取得大小同樣不得繞過 Shell 直接呼叫 `GetFileSizeEx`/`std::filesystem`,必須走 `IShellItem2::GetUInt64(PKEY_Size)`。)

`AGENTS.md`:
> **Keep `src/core` free of HWND, COM and `windows.h`.**

`AGENTS.md`:
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

## Files to read and trace first

- `src/app_shell/main.cpp` 第 1047-1061 行(`refresh_status_bar`)——文字格式化處。
- `src/explorer_host/explorer_host.h` 第 25-28 行(`ItemCounts`)、第 47 行(`item_counts`)——要擴充的結構與方法。
- `src/explorer_host/explorer_host.cpp` 的 `item_counts` 實作——既有的 `IFolderView2::ItemCount(SVGIO_ALLVIEW/SVGIO_SELECTION)` 呼叫,大小統計要接在同一處。
- `src/explorer_host/explorer_host.cpp` 的 `ViewCallback` 與 `SFVM_SELECTIONCHANGED` 處理——確認選取變化時狀態列會被觸發更新(PD-051 已建立,本票不改)。
- `src/app_shell/main.cpp` 第 2763-2769 行(狀態列建立)、第 2964-2966 行(`WM_CTLCOLORSTATIC`)、第 1663-1671 行(狀態列排版)、`kStatusBarHeight`(第 61 行)——視覺調整的落腳處。
- `src/app_shell/main.cpp` `draw_pane_card`(第 1367-1429 行)——色票參考,分隔線顏色要與這裡一致。
- `docs/tickets/PD-051-pane-status-bar.md`——狀態列的來源票與其交接區。

## Scope

1. `ExplorerHost::ItemCounts` 擴充出選取總大小欄位;`item_counts` 以 `IShellItem2::GetUInt64(PKEY_Size)` 累加選取項目大小,含項目數上限保護。
2. `refresh_status_bar` 改為三段式格式,大小用 `StrFormatByteSizeW` 格式化。
3. 狀態列加上上緣分隔線與淺灰底色。

## Non-goals

- 不統計未選取時整個資料夾的總大小(昂貴且需遞迴)。
- 不遞迴統計選取資料夾內部的大小(同上;資料夾直接跳過)。
- 不在狀態列顯示檢視模式、可用磁碟空間或其他額外欄位。
- 不改狀態列的高度以外的 pane 排版。
- 不改 `SFVM_SELECTIONCHANGED` 的偵測機制。

## Acceptance

1. 未選取任何項目時,狀態列顯示 `N items`。
2. 選取若干檔案後,狀態列顯示總項目數、選取項目數與選取項目的總大小三段資訊。
3. 選取的項目全部是資料夾時,大小段落不顯示誤導性數字(顯示 `0 KB` 或直接省略大小段,由實作 agent 決定並記錄)。
4. 選取數量超過設定上限時,大小段落不顯示,狀態列不卡頓。
5. 用 Ctrl+A 全選一個大資料夾(例如 `C:\Windows`),UI **不得**出現可察覺的卡頓或無回應。
6. 狀態列與上方檔案清單之間有清楚可見的分界(分隔線或底色差異)。
7. 選取變化時狀態列即時更新(PD-051 行為未回歸)。
8. 滑鼠與鍵盤都靜止時,程式回到 0% CPU、無磁碟 I/O。
9. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
10. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "ItemCounts|item_counts|StrFormatByteSize|PKEY_Size|GetUInt64|refresh_status_bar" src\explorer_host\explorer_host.h src\explorer_host\explorer_host.cpp src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:在一個含檔案的資料夾選取 1 個、3 個檔案,確認三段式狀態列;
# 選取資料夾確認不顯示誤導大小;在 C:\Windows 按 Ctrl+A 確認不卡頓;
# 靜止時用工作管理員確認 0% CPU。
```

**截圖驗證方法(本環境已驗證可用,請直接沿用):** 用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)` 而不是 `Graphics.CopyFromScreen`。

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- `ItemCounts` 的最終形狀與 `item_counts` 的最終簽章。
- 選取項目數的上限值,以及超過上限時的實際顯示行為。
- 資料夾(無 `PKEY_Size`)的處理方式。
- 三段式文字的實際格式(含分隔用的空白寬度)。
- 分隔線的實作方式(owner-draw 或在卡片繪製路徑),以及最終色值。
- Ctrl+A 全選 `C:\Windows` 的實測反應時間。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 實作交接

2026-08-27

- `ItemCounts` 最終形狀為 `{ int total; int selected; unsigned long long selected_bytes; bool selected_bytes_valid; }`；`item_counts` 簽章為 `HRESULT item_counts(ItemCounts& counts) const noexcept`。
- 大小統計上限為 1000 個選取項目。`selected > 1000` 時不呼叫 `IFolderView2::Items`，回傳 `S_OK`、保留項目數、設定 `selected_bytes_valid=false`；狀態列顯示 `N items   M selected`，省略大小。
- 上限內使用 `IFolderView2::Items(SVGIO_SELECTION, IID_PPV_ARGS(&items))`，逐項取得 `IShellItem2` 並呼叫 `GetUInt64(PKEY_Size)`；資料夾或其他沒有 `PKEY_Size` 的項目在 `GetUInt64` 失敗時跳過，不視為錯誤。成功計算但總和為 0 時為 `selected_bytes_valid=true`，UI 省略大小段；因此它與 cap 的 `false` 在資料層可區分。
- 文字格式為 `N items   M selected   <StrFormatByteSizeW result>`，段落間固定 3 個空白；無選取仍為 `N items`。大小只傳入 `StrFormatByteSizeW`，沒有手算單位或遞迴資料夾大小。
- 狀態列改為 `STATIC` + `SS_OWNERDRAW`，由主視窗 `WM_DRAWITEM` 直接繪製。footer 背景為 `RGB(249,250,251)`，上緣分隔線為按 DPI 縮放的 1px、`RGB(232,237,242)`，文字維持 `RGB(100,116,139)`；舊的狀態列 `WM_CTLCOLORSTATIC` 路徑已移除。
- 選取通知沿用 PD-051 已驗證的 `IShellFolderView::SetCallback` + `IShellFolderViewCB` `SFVM_SELECTIONCHANGED`，本票未增加輪詢；`navigation_complete` 與既有 callback 仍會更新狀態列。
- 可執行檢查：指定 LLVM-MinGW/Ninja Release configure 成功；`cmake --build build` 成功；`ctest --test-dir build --output-on-failure` 為 4/4 passed；`build\\pd062-output\\panedock_explorer_host_lifetime_check.exe` 輸出 `PASSED: explorer_host_lifetime_check`；API `rg` 檢查命中 `Items`、`PKEY_Size`、`GetUInt64`、`StrFormatByteSizeW` 與 owner-draw 路徑。
- 實機 UI 證據：未完成。`computer-use` 原生通道在初始化、一次重試及重置 JS session 後均回報 `Computer Use native pipe is unavailable: failed to connect native pipe: 系統找不到指定的檔案。`；依本票限制未改用 PowerShell/其他長序列輸入，也未使用 `CopyFromScreen`，因此沒有可附的 `PrintWindow(PW_RENDERFULLCONTENT)` 截圖。
- `Ctrl+A` 全選 `C:\Windows` 反應時間：未量測；需人類在解鎖桌面以單次 Ctrl+A 檢查。idle 0% CPU／無磁碟 I/O 同樣未量測，因票券要求的 10 分鐘 soak 不屬快速互動驗證。
- `git diff --check` 全域檢查目前被工作樹中先於本票的 `docs/tickets.md` PD-078/PD-079 未提交變更之 trailing whitespace 阻塞（行 117–119、376–386）；本票修改檔案的 scoped `git diff --check` 通過，未改動或清理該使用者變更。

#### Acceptance evidence

| # | 結果 | 證據 |
|---|---|---|
| 1 | 未驗證 | 未取得 GUI 截圖；程式碼已保留 `N items` 無選取分支。 |
| 2 | 未驗證 | 未執行檔案選取或取得 `PrintWindow` 截圖；三段格式已建置。 |
| 3 | 未驗證 | 未能在 Shell view 選取資料夾；程式碼對缺少 `PKEY_Size` 的項目跳過並對 0 總和省略大小。 |
| 4 | 未驗證 | 未能在 GUI 執行超過 1000 項選取；上限分支與省略大小行為已編譯。 |
| 5 | 未驗證 | 未執行 `C:\Windows` 的 Ctrl+A；未取得可負責任的反應時間。 |
| 6 | 未驗證 | 未取得 `PrintWindow(..., 2)` 實機截圖；owner-draw 路徑已建置。 |
| 7 | 未驗證 | 未執行選取/取消選取互動；既有 `SFVM_SELECTIONCHANGED` callback 未改動。 |
| 8 | 未驗證 | 未執行 10 分鐘 idle 量測；不可用短暫命令輸出代替。 |
| 9 | PASS | `cmake --build build` 成功；`ctest --test-dir build --output-on-failure` 4/4 passed。 |
| 10 | 未驗證 | 全域 `git diff --check` 被既有 `docs/tickets.md` 變更的 trailing whitespace 阻塞；本票檔案 scoped check 通過。 |

### 2026-08-27 補充:使用者手動驗證 Ctrl+A

使用者本人在真實桌面對 `C:\Windows` 執行 `Ctrl+A` 全選,回報「ok」——UI 沒有出現可察覺的卡頓或無回應。

**Acceptance 項目 5 更新為已驗證**(1000 項上限保護生效,大量選取時不卡 UI)。其餘項目(1-4、6-8、10)仍維持未驗證,tracker 狀態維持 `ready`。
