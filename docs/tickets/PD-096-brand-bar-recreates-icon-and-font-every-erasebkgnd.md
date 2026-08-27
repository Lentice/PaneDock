# PD-096 — `draw_brand_bar` 每次 `WM_ERASEBKGND` 都重新載入圖示、建立/刪除字型

## 來源

2026-08-27 三方效能研究(Claude / Codex / OpenCode)。Claude 與 OpenCode 各自獨立指出同一段程式碼與同一個成本組成。

## 背景與現況

`draw_brand_bar`(`main.cpp:1555-1572` 一帶)在每次 `WM_ERASEBKGND` → `paint_client_background` 時都執行:

- `LoadImageW(...IDI_APP_ICON...)` 載入 App 圖示,結束後 `DestroyIcon`(`main.cpp:1555-1561`)。
- `brand_font(window)` 內部呼叫 `ui_font()` → `SystemParametersInfoForDpi` + `CreateFontIndirectW` + `GetDC`/`GetTextFaceW`,再 `GetObjectW` + 第二次 `CreateFontIndirectW`(`main.cpp:1529-1540`),繪製完成後 `DeleteObject`(`main.cpp:1566`)。

`WM_ERASEBKGND` 會被 PD-095 所描述的 `apply_layout`(`InvalidateRect(window, nullptr, TRUE)`)頻繁觸發——包含每次分隔線拖曳的 mousemove、每次視窗縮放。也就是說,一次分隔線拖曳可能造成數十到數百次的「載入圖示 + 建立兩個字型 + 銷毀」。

## 為什麼這是真的問題

App 圖示與字型在 DPI 沒有變化的情況下是不變的資源——沒有理由每次重繪 chrome 背景就重新載入/建立一次。這是純粹的重複工作,疊加在 PD-095 描述的高頻重繪路徑上時,成本會被放大到每秒數十次。

## Fix 方向

把 App 圖示與 brand 字型改成快取:只在第一次需要、或 DPI 實際變更(`WM_DPICHANGED`)時才重新載入/建立,其餘時間直接複用已快取的 handle。`main.cpp:1495` 已存在的 `state.chrome_font` 是類似「跨重繪快取字型」的既有模式,可以參考其快取方式,或直接評估是否能重用同一個字型物件。

## 綁定限制(引用)

- `AGENTS.md`:「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」—— 只需要把「每次繪製都建立/銷毀」改成「快取一份、DPI 變更時重建」,不需要新的資源管理框架。
- `AGENTS.md`:「Per-Monitor-V2 DPI awareness with explicit `WM_DPICHANGED` handling that resizes all panes. Mixed-DPI multi-monitor is a normal case, not an edge case.」—— 快取的字型/圖示必須在 `WM_DPICHANGED` 時正確失效並重建,不能因為快取而在跨螢幕拖曳到不同 DPI 時顯示錯誤大小的字型。

## 檔案與範圍

- `src/app_shell/main.cpp`:
  - `draw_brand_bar`(約 `:1555-1572`)
  - `brand_font`/`ui_font`(約 `:1529-1552`)
  - `WM_DPICHANGED` 處理(確認快取失效時機接在既有的 DPI 變更處理路徑上)

## Scope

1. App 圖示改為載入一次並快取(例如存在 `AppState` 或等價的持久生命週期位置),`draw_brand_bar` 直接使用快取的 handle,不再每次 `LoadImageW`/`DestroyIcon`。
2. Brand 字型改為快取,只在首次或 DPI 實際變更時重建,`draw_brand_bar` 不再每次 `CreateFontIndirectW`/`DeleteObject`。
3. 確認快取的圖示/字型在視窗銷毀時被正確釋放(不造成 GDI 資源洩漏)。

## Non-goals

- 不改變圖示或字型本身的視覺樣式、大小計算邏輯。
- 不處理 PD-095 描述的「哪些事件會觸發 `WM_ERASEBKGND`」本身——本票只降低每次觸發時的成本,不改變觸發頻率(觸發頻率由 PD-095 處理)。

## Acceptance Criteria

1. 連續多次觸發 `WM_ERASEBKGND`(例如透過分隔線拖曳或連續呼叫繪製路徑),App 圖示的載入/銷毀與字型的建立/刪除次數應遠低於觸發次數(理想是啟動後只發生一次,DPI 變更時再發生一次)。
2. DPI 變更(切換螢幕或縮放比例改變)後,brand bar 的圖示與文字大小仍正確依新 DPI 顯示,無視覺 regression。
3. 視窗關閉時,快取的圖示/字型 handle 被正確釋放,無 GDI 資源洩漏(可用工作管理員的 GDI 物件數量粗略觀察,或既有的資源追蹤機制)。
4. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 交接區

（實作完成後由實作者填寫）
