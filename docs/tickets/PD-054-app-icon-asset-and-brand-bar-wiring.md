# PD-054 — 設計 App icon 並取代側邊欄品牌列的手繪「+」圖示

Phase 6 · app_shell · Depends on: PD-028

- Source: 使用者比對 `docs/panedock-ui-demo-01-refined-quiet-header.html` 目標畫面與實機截圖後回報(2026-08-25)。
- Origin: 「請 codex 設計 app icon」「左上角顯示 app icon 代替現在的『+』」。
- Priority: LOW——視覺資產與品牌識別,不影響功能;但目前完全沒有 app icon 這件事本身值得記錄。

## 已確認的根因(有程式碼證據,不是猜測)

1. **本專案目前沒有任何 app icon 資產或資源。** 搜尋確認:沒有 `.rc` 資源檔、沒有 `.ico` 檔案、`src`/`CMakeLists.txt` 內沒有任何 `IDI_`/`LoadIconW`/`hIcon`/視窗圖示相關程式碼。應用程式視窗、工作列目前使用的是系統預設圖示。
2. **側邊欄品牌列(`draw_brand_bar`,`src/app_shell/main.cpp` 第 1172-1229 行)左上角看起來像圖示的方塊,其實是用 `RoundRect` 畫一個藍色圓角方塊(第 1193-1205 行),再用兩條 `MoveToEx`/`LineTo` 白色線條疊加畫出一個十字「+」形狀(第 1207-1218 行)——這是純手繪的幾何圖案,不是任何形式的 logo 或圖示資源,使用者說的「代替現在的『+』」精確對應到這段程式碼。

## 已確認的產品決策

1. **App icon 的視覺設計本身委外由 Codex 產生一個實際的圖示資產(`.ico`,至少包含 16x16/32x32/48x48/256x256 多種尺寸),不是由本票的實作 agent 自己手繪 GDI 幾何圖形湊數。** 設計風格延續目前品牌列已經確立的視覺語言(品牌藍 `RGB(37,99,235)`,圓角方塊),具體構圖(例如維持簡化的資料夾/面板意象)由 Codex 決定,只要達成 Acceptance 的可辨識度即可。
2. **產生的 `.ico` 檔案加入專案(建議路徑 `resources/app.ico` 或等效位置,由實作 agent 決定並在交接區記錄),透過新增的 `.rc` 資源檔(例如 `resources/panedock.rc`)以 `IDI_APP_ICON ICON "app.ico"` 的形式編譯進執行檔,並在主視窗類別註冊(`WNDCLASSEXW::hIcon`/`hIconSm`)與/或視窗建立後的 `WM_SETICON` 訊息套用,讓工作列、Alt-Tab、視窗左上角都顯示這個圖示(這是本票的次要驗收項,主要驗收是側邊欄品牌列,但既然要新增圖示資源,一併把視窗系統圖示接上是同一份改動的自然延伸,不是範圍蔓延)。
3. **側邊欄品牌列(`draw_brand_bar`)的手繪圓角方塊+十字線條(第 1186-1219 行)整段替換成 `DrawIconEx` 或 `LoadImageW`+`StretchBlt` 繪製新的 `.ico` 資源**,尺寸/位置比照現有的 `icon`/`icon_size`/`icon_margin` 計算方式(第 1186-1192 行),不需要重新設計版面配置,只換繪製內容。
4. **CMake 建置設定需要新增 `.rc` 檔案的編譯規則**(LLVM-MinGW 工具鏈下用 `llvm-rc` 編譯資源檔,`docs/development.md`/現有 `CMakeLists.txt` 若已有 `.rc` 編譯慣例可沿用,若沒有需要新增,實作 agent 需查證 LLVM-MinGW 環境下 CMake 的 `.rc` 檔案是否被 `enable_language(RC)` 或需要手動呼叫 `llvm-rc` 自訂建置規則,並在交接區記錄查證結果)。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Reach for the standard library and Win32 before adding a dependency.

`AGENTS.md`:
> No network, no telemetry, no third-party runtime, no services, no drivers, no admin elevation.
(圖示資產產生過程不可以呼叫任何網路服務/線上圖示產生 API,必須是本機產生的靜態資產檔案。)

## Files to read and trace first

- `src/app_shell/main.cpp` 的 `draw_brand_bar`(第 1172-1229 行)——本票要替換繪製內容的函式。
- `src/app_shell/main.cpp` 找出主視窗類別註冊(`WNDCLASSEXW`,搜尋 `RegisterClassExW`)與視窗建立(`CreateWindowExW`)的位置——本票要新增 `hIcon`/`hIconSm`/`WM_SETICON` 的落腳處。
- 根 `CMakeLists.txt`——確認目前的建置目標與資源檔編譯規則(若有的話),新增 `.rc` 編譯步驟的落腳處。
- `docs/development.md`——確認 LLVM-MinGW 工具鏈下處理 `.rc`/`llvm-rc` 是否已有既定慣例或限制。

## Scope

1. 由 Codex 設計並產生一個 `.ico` app icon 資產(多尺寸)。
2. 新增 `.rc` 資源檔,把 `.ico` 編譯進執行檔。
3. `CMakeLists.txt` 新增 `.rc` 檔案的建置規則(`llvm-rc`)。
4. 主視窗註冊/`WM_SETICON` 套用新圖示,取代系統預設圖示。
5. `draw_brand_bar` 的手繪十字圖案替換為 `DrawIconEx`/`LoadImageW` 繪製新圖示。

## Non-goals

- 不重新設計側邊欄品牌列的版面配置(標題文字位置、間距)——只換圖示內容。
- 不新增第三方圖示產生工具或線上服務依賴。
- 不影響 Group 圖示或其他 UI 元件的圖示(spec 未列 Group 圖示為 MVP 範圍,見 `docs/tickets.md` 候選清單)。

## Acceptance

1. 存在一個實際的 `.ico` 圖示資產檔案,包含至少 16x16/32x32/48x48/256x256 尺寸。
2. 應用程式視窗標題列、工作列、Alt-Tab 切換畫面顯示這個圖示,不是系統預設圖示。
3. 側邊欄品牌列左上角顯示這個圖示,不再是手繪的十字「+」圖案。
4. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
5. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "\.ico|IDI_|hIcon|WM_SETICON" src\app_shell\main.cpp CMakeLists.txt
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:確認工作列/視窗標題列圖示、側邊欄品牌列圖示都是新設計的圖示,
# 不是系統預設圖示或手繪十字。本環境已具備螢幕截圖能力,請實際截圖比對。
```

## Handoff requirements

- 最終圖示的設計說明與產生方式(Codex 如何產生 `.ico`,例如透過腳本繪製多尺寸點陣圖再組裝成 `.ico`)。
- `.rc`/CMake 建置規則的具體改動與在 LLVM-MinGW 工具鏈下的查證結果。
- 真實桌面測試(工作列、標題列、品牌列圖示)的實際結果與截圖。

## 交接區

<!-- 實作 agent 填寫,append-only -->
