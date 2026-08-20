# PD-008 — 四宮格版型與矩形計算

Phase 0 · app_shell · Depends on: PD-007

- Source: `AGENTS.md`、`docs/design-spec.md` §3.2／§9.2／§9.4、`docs/development.md`、`docs/testing.md`
- Origin: 2026-08-20 由 PD-001 拆分。
- Override: 本 ticket 與 PD-007／PD-009／PD-010／PD-011 共同取代 PD-001。
- Priority: **HIGH**——本案的決定性風險是「多實例 `IExplorerBrowser` 能否在同一 process 內穩定共存」,這是第一個真正暴露該風險的 ticket。

## Goal

把 PD-007 的單一 view 視窗擴成四宮格,四個 pane 各自持有一個獨立的 `IExplorerBrowser`,可各自導覽到不同位置。

刻意是四分割而不是二分割:被測的風險就是「多實例」,二分割測不出來。

## 已確認的產品決策

1. 比例固定 0.5／0.5,不做可拖曳分隔線。
2. 四個 pane 的初始路徑可硬編碼或由命令列給定,彼此不同以便觀察獨立性。
3. 只做「四宮格」這一種版型。單一／四宮格的切換屬於 PD-009,五種版型屬於 Phase 2。
4. 矩形計算在本階段可放在 `app_shell`。正式的五種版型矩形計算歸 PD-005,屆時會移進 `core`;本片不要為了預先對齊 PD-005 而設計抽象。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`:
> Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> Keep `src/core` free of HWND, COM and `windows.h`.

`docs/design-spec.md` §9.2:
> 單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。

`docs/development.md`:
> Use `Microsoft::WRL::ComPtr` for every interface pointer. Raw `AddRef`/`Release` pairs are not acceptable in new code.

## Files to read and trace first

- `src/app_shell/`、`src/explorer_host/`(PD-007 建立)
- PD-007 的交接區:site 物件實際被查詢的 service ID,四實例時要比對
- `IExplorerBrowser::SetRect`、`BrowseToObject`

## Scope

1. 矩形計算:把 client area 均分為四,固定 0.5／0.5。輸入為 client 寬高,輸出為四個矩形。
2. 建立四個 `explorer_host` 實例,各自 `BrowseToObject` 到不同路徑。
3. `WM_SIZE` 與 `WM_DPICHANGED` 時重算四個矩形並逐一 `SetRect`。
4. 關閉序列沿用 PD-007:destroy 全部四個 view 之後才 destroy 主視窗,live view 計數 self-check 斷言為 0。
5. 一個可執行的 self-check 目標,驗證矩形計算在退化尺寸(寬或高為 0、1、奇數)下不產生零或負值的矩形,且四個矩形不重疊、聯集等於 client area。

## Non-goals

- 不做可拖曳分隔線。
- 不做單一／四宮格切換(歸 PD-009)。
- 不做 active pane 指示(歸 PD-009)。
- 不做位置持久化(歸 PD-010)。
- 不做驗收協定的記錄與 Go/No-Go(歸 PD-011)。
- 不做五種版型;不為 PD-005 預先設計抽象。
- 不做視覺打磨。

## Acceptance

1. 四宮格下四個 pane 各自可獨立導覽,互不影響。
2. 四個 pane 的原生圖示與縮圖都正常顯示。
3. 調整視窗大小時四個 view 同步正確重新配置,無殘影、無錯位、無閃爍到不可用的程度。
4. 跨不同 DPI 螢幕拖曳時四個 view 都正確重新配置。
5. 關閉時四個 view 全部 `Destroy`,live view 計數 self-check 通過,process 正常結束。
6. 矩形計算的退化尺寸 self-check 通過。
7. `rg -n "AddRef|->Release\(\)" src` 無命中。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
.\build\PaneDock.exe
# 手動:四個 pane 分別導覽、調整大小、跨 DPI 拖曳、關閉
```

```powershell
rg -n "AddRef|->Release\(\)" src
rg -n "windows\.h|HWND|IUnknown" src/core
git diff --check
```

## Handoff requirements

- 四實例並存時的初次啟動耗時、記憶體概略值(正式量測歸 PD-003／PD-011)。
- 四實例的 site 查詢是否與 PD-007 單實例一致;若不一致,列出差異。
- 任何只在多實例下才出現的行為異常——這是本 ticket 最有價值的產出。

## 交接區

<!-- 實作 agent 填寫,append-only -->
