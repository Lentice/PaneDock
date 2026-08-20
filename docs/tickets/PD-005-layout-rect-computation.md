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
