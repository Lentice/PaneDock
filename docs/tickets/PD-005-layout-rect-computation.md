# PD-005 — 實作五種版型的矩形計算,含退化尺寸處理

Phase 1 · core · Depends on: PD-004

- Source: `AGENTS.md`、`docs/design-spec.md` §FR-003／§FR-004／§FR-004a、`docs/development.md`
- Origin: 2026-08-20 專案建立。這是 `core` 內最容易完整測試的一塊純計算,適合在 PD-004 定下型別後立即做。
- Priority: **LOW**——純計算,無外部依賴,風險低。排在 PD-004 之後、PD-006 之前:它不動 schema。

## Goal

給定一個 client area 尺寸、一個 `LayoutTemplate` 與一組分隔比例,算出每個 pane 的矩形。

這是全案唯一完全確定性、無外部依賴的邏輯,因此也是測試覆蓋最該做滿的地方。重點不在演算法難度,而在**退化情形**:視窗被拖到極小時不得產生零或負值的矩形,因為那會傳給 `IExplorerBrowser::SetRect`,而 Shell 對無效矩形的反應不在我們控制範圍內。

## 已確認的產品決策

1. 矩形以整數像素表示,分隔比例以 0.0–1.0 的浮點表示。四捨五入造成的一像素誤差由最後一個 pane 吸收,使矩形總和恰好填滿 client area 而不留縫。
2. 三分割的具體排列已定:左側一個全高 pane,右側上下各一。這是 Q-Dir 的慣例排列,也是使用者提到的參考行為。
3. 退化尺寸的處理方式是**夾住最小值**,不是拒絕計算或回傳錯誤。每個 pane 有一個最小寬度與最小高度常數;client area 小到無法滿足時,維持比例但每個矩形不小於最小值,允許矩形總和超出 client area(超出部分被裁切)。理由:UI 必須在任何視窗尺寸下都能繼續運作,回傳錯誤會把處理責任推給呼叫端而每個呼叫端都得寫一次。
4. 分隔線本身的厚度算在版型計算內,不由呼叫端另外扣除。
5. 本 ticket 不處理拖曳分隔線的互動,只處理「給定比例算矩形」。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` §FR-003:
> 支援且僅支援五種版型:單一、左右、上下、三分割、四宮格。

`docs/design-spec.md` §FR-004:
> pane 分隔線可拖曳,比例以 0.0–1.0 的相對值儲存於該 Group,視窗縮放時維持比例。

`docs/design-spec.md` §FR-004a:
> 視窗過小導致 pane 寬度或高度低於下限時,維持比例但不得產生負值或零尺寸的 pane 矩形。

`AGENTS.md`:
> **Keep `src/core` free of HWND, COM and `windows.h`.**

`docs/design-spec.md` §NFR-004:
> Per-Monitor-V2 DPI awareness。視窗跨越不同 DPI 的螢幕時,全部 pane 正確縮放。

DPI 的意涵:本 ticket 的函式接收的是**已經是實體像素**的 client area 尺寸與**已經按 DPI 縮放過**的最小值常數。`core` 不查詢 DPI——那需要 `windows.h`。DPI 縮放由呼叫端在傳入前完成。

## Files to read and trace first

- PD-004 的 `## 交接區` — `LayoutTemplate` 列舉與分隔比例的確切表示方式
- `src/core/` — PD-004 建立的型別
- `docs/design-spec.md` §FR-004a — 退化尺寸的要求
- `tests/CMakeLists.txt` — table-driven 測試註冊方式

## Scope

1. 定義一個純值的矩形型別(x、y、width、height,整數)。**不得使用 `RECT`**——那需要 `windows.h`。
2. 定義最小 pane 寬度與最小 pane 高度常數,以及分隔線厚度常數。三者都是具名常數,不是散落的魔術數字。
3. 實作函式:輸入 client area 尺寸、`LayoutTemplate`、分隔比例;輸出 pane 矩形序列,順序與 `PaneState` 集合的順序一致。
4. 五種版型的排列:
   - 單一:一個矩形填滿
   - 左右:垂直分隔線,比例控制左側寬度
   - 上下:水平分隔線,比例控制上方高度
   - 三分割:左側一個全高,右側上下各一。兩個比例——垂直分隔位置、右側的水平分隔位置
   - 四宮格:一個垂直、一個水平分隔線,兩個比例
5. 一像素誤差由序列中最後一個 pane 吸收,使矩形恰好填滿 client area。
6. 退化尺寸:每個矩形的 width 與 height 夾在最小值以上。夾住後允許總和超出 client area。
7. 測試涵蓋:
   - 五種版型各自在一般尺寸下的矩形正確
   - 矩形之間無重疊、無縫隙(扣除分隔線厚度後總和等於 client area)
   - 比例為極端值(0.0、1.0)時不產生零尺寸矩形
   - client area 為零或負值時的行為有定義
   - client area 小於最小值總和時,每個矩形仍不小於最小值
   - 比例數量與版型不符時的行為有定義

## Non-goals

- 不實作分隔線拖曳的互動或命中測試。
- 不實作 `SetRect` 呼叫——那是 `explorer_host` 的事。
- 不查詢 DPI 或呼叫任何 Win32 函式。
- 不使用 `RECT`、`POINT`、`SIZE` 或任何 `windows.h` 型別。
- 不支援五種以外的版型,不預留自訂版型的擴充點。
- 不做動畫或過渡狀態的中間矩形計算。
- 不最佳化。這個函式每次 `WM_SIZE` 呼叫一次,不是熱路徑。

## Acceptance

1. 五種版型的矩形計算皆正確,且測試斷言的是具體座標值。
2. 三分割的排列為「左側全高、右側上下」,有測試確認。
3. 矩形無重疊、無縫隙,總和加上分隔線厚度等於 client area,有測試確認。
4. 比例為 0.0 與 1.0 時不產生零或負尺寸矩形。
5. client area 小於最小值總和時,每個矩形的 width 與 height 皆不小於最小值常數。
6. client area 為零或負值時行為有定義且有測試(不當機、不回傳未初始化值)。
7. 比例數量與版型不符時行為有定義且有測試。
8. `src/core` 內無 `windows.h` 型別。

## Agent checks

```powershell
cmake --build build
ctest --test-dir build -R "layout" --output-on-failure
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "RECT|POINT|SIZE|windows\.h|GetDpiForWindow" src\core
# 預期:無命中
rg -n "\b(4|8|16|24|32|120|160)\b" src\core\layout*
# 預期:魔術數字都應已具名為常數;命中須是常數定義本身
git diff --check
```

## Handoff requirements

交接時記錄:

- 最小 pane 寬度、最小 pane 高度、分隔線厚度三個常數的值與選定理由。
- 三分割的比例語意(哪個比例控制哪條分隔線)。
- 一像素誤差吸收的具體規則。
- client area 為零、負值,以及比例數量不符時的定義行為。
- 新增的測試名稱與註冊位置。
- 若發現 §FR-004a 的敘述與實作需求有落差,寫出落差並提出 spec 修改建議(**不要直接改 spec**——依 §1 先更新 spec 需要另開 ticket)。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-24 — PD-005 實作

- `src/core/layout.h` 定義純值 `PaneRect` 與三個 96-DPI 基準常數：最小 pane 寬 `120` px，讓窄視窗仍保有可操作的 Shell 清單欄位；最小 pane 高 `80` px，足以保留數列可見內容；divider 厚 `4` px，在分隔可辨識與 pane 空間之間取最小實用值。沒有加入 Win32 型別或依賴。
- `compute_layout_rects` 支援五種 `LayoutTemplate`。三分割的 `divider_ratios[0]` 控制左右分隔，`divider_ratios[1]` 控制右側上下分隔；輸出順序是左側全高、右上、右下。
- 每條軸先扣 divider，再以 `std::lround` 計算前一區；尾端區使用「可用尺寸減前一區」吸收任何一像素捨入餘數。四宮格的右欄／下列及三分割的右欄／右下因此貼齊 client area；退化尺寸夾住最小值時則依票券允許超出並由 UI 裁切。
- client width/height 為零或負值時仍從 `(0, 0)` 回傳該版型數量的矩形，所有 width/height 至少為具名最小值。比例數量不符時整組改用該版型的 `0.5` 預設比例；合法數量的比例由 PD-004 model invariant 保證在 `0.0`–`1.0`。
- `tests/unit/core_layout_test.cpp` 的 `panedock_core_layout` 於 `tests/CMakeLists.txt` 的 `PANEDOCK_TESTS` 表註冊。測試以具體座標涵蓋五種一般版型、奇數像素餘數與 divider 連續性、`0.0`／`1.0`、零／負 client area、小於最小總和及比例數量不符。
- 發現票券的 DPI 說明稱呼叫端會傳入「已按 DPI 縮放過的最小值常數」，但 scope 所定函式輸入只有 client size、版型與比例，固定常數無法由呼叫端縮放。依本票 scope 保留 96-DPI 基準常數；接線到 Per-Monitor-V2 UI 前，建議另票釐清並讓計算函式接收按 DPI 縮放後的 minimum/divider metrics，無須改變矩形演算法。
- Agent checks（Release，LLVM-MinGW/Ninja）：`cmake --build build` 成功；`ctest -R layout` 1/1、完整 `ctest` 2/2 通過；Win32 core 邊界 `rg` 無命中；魔術數字 `rg` 僅命中 `layout.h` 的具名常數定義；`git diff --check` 通過。票券原列的 `src\core\layout*` 在此 Windows `rg` 版本會回報非法路徑，因此以等價的 `rg ... src\core --glob "layout*"` 執行。

### 2026-08-24 驗證

獨立重跑 build 與完整 `ctest`(2/2 通過)、兩個邊界 `rg`、`git diff --check`,結果一致。讀過 `src/core/layout.h`／`layout.cpp`／`tests/unit/core_layout_test.cpp` 全文:`split()` 的餘數吸收與退化夾制邏輯正確,且發現一個已被交接記錄的整數溢位修正(`INT_MIN - kDividerThickness` 改成 `std::max(size, kDividerThickness) - kDividerThickness` 避免溢位)——這是實作過程自我抓到並修正的問題,不是遺留缺陷。三分割與四宮格的比例語意、輸出順序與 §FR-003／§FR-004a 一致。DPI 常數寫死一事已如實記錄為落差而非默默改 spec,已在 `docs/tickets.md` §候選 新增對應項目,待 Per-Monitor-V2 接線時開票處理,不阻塞本票。判定為完成。
