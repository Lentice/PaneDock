# PD-014 — 修正 explorer_host 的 OLE 初始化與鍵盤 accelerator 轉發

Phase 0 · app_shell／explorer_host · Depends on: PD-009, PD-010

- Source: `AGENTS.md`、`docs/design-spec.md` §FR-008／113 行、`docs/testing.md` 原型驗收協定
- Origin: 2026-08-20,PD-011 驗收協定執行期間發現的阻斷性缺陷,退回為獨立 ticket。
- Priority: **HIGH**——阻斷 PD-011 的 step 3、4;且 `docs/design-spec.md` 113 行明文「pane 內部的一切互動由 Shell view 處理:...拖放...鍵盤操作。PaneDock 不介入」,目前的違反是宿主層的接線缺陷,不是分工未做。

## Goal

在真實桌面上人工執行 PD-011 驗收協定時發現:四個 pane 的 `IExplorerBrowser` 視圖中,**拖放完全不動作**(pane 對 pane、以及與外部應用程式雙向皆然),且 **Backspace、Alt+Left(上一頁)、Ctrl+C／Ctrl+V(複製貼上)全部沒有反應**。PaneDock 本身沒有崩潰或掛起,视圖仍可正常瀏覽、右鍵選單正常——只有這幾類仰賴宿主行程轉發的互動失效。

追查後定位兩個具體成因,修正範圍限定在這兩點:

1. `src/app_shell/main.cpp:380` 用 `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)` 初始化 COM。OLE 拖放(`RegisterDragDrop`／`DoDragDrop`)需要 **OLE** 被初始化,而 `CoInitializeEx` 不會做這件事——必須用 `OleInitialize(nullptr)`(其內部涵蓋一般 COM 初始化)。這解釋了拖放為何整個不動作。
2. `src/app_shell/main.cpp:419-424` 的訊息迴圈只有 `TranslateMessage`／`DispatchMessageW`,從未把訊息交給目前作用中 pane 的 `IShellView::TranslateAcceleratorW`。`IShellView` 自己的鍵盤 accelerator(Backspace 上一頁、Ctrl+C/V 等)必須由宿主的訊息迴圈明確呼叫它才會生效;沒有這一步,這些按鍵只會落到預設的視窗處理,shell view 收不到。這解釋了為何導覽鍵與複製貼上鍵完全沒反應。

## 已確認的產品決策

1. 只修這兩個具體成因,不做其他鍵盤／滑鼠功能增補。`docs/design-spec.md` 113 行已經把 pane 內互動的職責定給 Shell view;PaneDock 的責任只是把宿主層的兩個必要條件(OLE 初始化、accelerator 轉發)接好。
2. Accelerator 轉發只需要轉給**目前作用中(active)的 pane**——非作用中 pane 沒有鍵盤焦點,不需要轉發。作用中 pane 由現有的 `state.layout.active_pane()` 取得(`src/app_shell/main.cpp` 既有邏輯)。
3. 不因為這個修正而變更 `docs/design-spec.md` §9.4 關閉序列的步驟順序——只把該序列裡呼叫 `CoUninitialize` 的那一步換成 `OleUninitialize`,順序不變。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> The decisive technical risk is stable multi-instance `IExplorerBrowser` integration.

`AGENTS.md`:
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`:
> Event-driven idle path only. No busy loops, no polling timers.

`docs/design-spec.md` 113 行:
> pane 內部的一切互動由 Shell view 處理:多選手勢、右鍵選單、拖放、就地重新命名、鍵盤操作。PaneDock 不介入。

`docs/design-spec.md` §9.4 關閉序列(順序不可調換,原文見 PD-010 的引用):
> 1. 擷取現行狀態並原子寫入 session document
> 2. destroy 全部 live `IExplorerBrowser`
> 3. destroy pane HWND
> 4. destroy 主視窗
> 5. 退出訊息迴圈
> 6. `CoUninitialize`

## Files to read and trace first

- `src/app_shell/main.cpp` 全文,特別是 `wWinMain`(COM 初始化在 380 行、`CoUninitialize` 在 435 行、訊息迴圈在 419-424 行)與 `AppState`(42 行起)、`window_proc` 內對 `state->explorers[...]` 的存取模式
- `src/explorer_host/explorer_host.h`／`.cpp`,特別是 `focus()`(294 行起)與既有 `browser_->GetCurrentView(IID_PPV_ARGS(&view))` 的用法(135-146 行),新方法要照同樣的 `ComPtr`／`log_hresult` 慣例寫
- Microsoft 文件:`IShellView::TranslateAcceleratorW`、`OleInitialize`/`OleUninitialize`、`RegisterDragDrop`(確認 `IExplorerBrowser` 內部視圖是否已自行呼叫,不需要宿主重複呼叫——只需要 OLE 已初始化)

## Scope

1. `src/app_shell/main.cpp`:把 `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)` 換成 `OleInitialize(nullptr)`;對應的 `CoUninitialize()`(435 行)換成 `OleUninitialize()`。檢查回傳值的失敗處理方式不變(目前是回傳非零 exit code)。
2. 在 `ExplorerHost` 新增一個方法(例如 `HRESULT translate_accelerator(MSG* message) noexcept`),內部呼叫 `browser_->GetCurrentView(IID_PPV_ARGS(&view))` 取得目前的 `IShellView`,成功則呼叫 `view->TranslateAcceleratorW(message)` 並回傳其 `HRESULT`;`browser_` 為 null 或 `GetCurrentView` 失敗時回傳 `S_FALSE`(視為「未處理」)。
3. `wWinMain` 的訊息迴圈:在 `TranslateMessage`/`DispatchMessageW` 之前,呼叫目前作用中 pane(`state.explorers[state.layout.active_pane()]`)的 `translate_accelerator(&message)`;回傳 `S_OK` 時視為已處理,跳過該次迴圈剩餘的 `TranslateMessage`/`DispatchMessageW`,`continue` 到下一個 `GetMessageW`。
4. 確認 destroy 順序、`live_view_count` 斷言與既有自我檢查(`panedock_explorer_host_lifetime_check` 等)不受影響。

## Non-goals

- 不新增位址列、上一頁/下一頁 UI 按鈕(歸 Phase 2/3)。這裡只修「鍵盤 accelerator 沒有被轉發」這個宿主層缺陷,不是新增導覽 UI。
- 不處理非作用中 pane 的鍵盤事件轉發。
- 不修改 `IFileOperation`、剪貼簿以外的檔案操作路徑。
- 不重新設計 COM 初始化的執行緒模型(仍是單一 UI 執行緒、`COINIT_APARTMENTTHREADED` 語意由 `OleInitialize` 內部涵蓋,不需要額外執行緒處理)。
- 不對 PD-011 的其餘驗收步驟(idle 資源、選取還原可行性等)做任何事——那些仍由 PD-011 負責,PD-011 目前因這個缺陷而暫停,待本 ticket 完成後由 PD-011 重跑受影響的步驟(3、4,並重新確認 Backspace/Alt+Left/Ctrl+C/V)。

## Acceptance

1. `src/app_shell/main.cpp` 不再出現 `CoInitializeEx`／`CoUninitialize`;改用 `OleInitialize`／`OleUninitialize`,失敗處理邏輯與原本等價。
2. 訊息迴圈在 `TranslateMessage`/`DispatchMessageW` 之前,對目前作用中 pane 呼叫新的 accelerator 轉發方法;該方法回傳已處理時,迴圈不再呼叫 `TranslateMessage`/`DispatchMessageW`。
3. 手動:啟動四宮格,任一 pane 導覽兩層後按 Backspace,回到上一層;按 Alt+Left 效果相同。
4. 手動:在某 pane 選取一個檔案,Ctrl+C,切到另一 pane,Ctrl+V,檔案被複製過去(標準 Windows 衝突/進度 UI 可能出現,視檔案是否已存在)。
5. 手動:pane 0 拖放檔案到 pane 3(含放開 Ctrl 與按住 Ctrl 兩種),分別觸發標準的搬移／複製語意。
6. 手動:從 PaneDock 的 pane 拖一個檔案到外部應用程式(例如檔案總管視窗或桌面),以及反向從外部拖進 PaneDock 的 pane,兩個方向都成功。
7. `ctest --test-dir build --output-on-failure` 全數通過;既有的 `panedock_explorer_host_lifetime_check`、`panedock_layout_state_check`、`panedock_prototype_location_persistence_check`、`panedock_quadrant_layout_check` 四個 self-check 全數 `PASSED`,行為與修正前一致(本 ticket 不改變它們涵蓋的邏輯)。
8. 正常關閉(`WM_CLOSE` 與訊息迴圈提前結束兩條路徑)後,`panedock::explorer_host::live_view_count() == 0` 的既有斷言仍成立,沒有因為新增的 accelerator 轉發呼叫而改變 view 的生命週期或 destroy 順序。
9. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
.\build\panedock_explorer_host_lifetime_check.exe
.\build\panedock_layout_state_check.exe
.\build\panedock_prototype_location_persistence_check.exe
.\build\panedock_quadrant_layout_check.exe
```

```powershell
.\build\PaneDock.exe
# 手動:任一 pane 導覽兩層後 Backspace／Alt+Left 回上一層
# 手動:Ctrl+C 一個檔案、切換 pane、Ctrl+V
# 手動:pane 0 拖放到 pane 3(放開 Ctrl／按住 Ctrl 各試一次)
# 手動:與外部應用程式雙向拖放
```

```powershell
rg -n "CoInitializeEx|CoUninitialize" src/app_shell
git diff --check
```

**明確告知實作 agent**:本執行環境沒有可附著的互動桌面。凡是上面標「手動」的檢查項一律無法在此環境驗證,請誠實在交接區寫明「未驗證,需要真實桌面」,不要用程式碼推論或猜測結果替代實際操作證據,也不要宣稱通過。

## Handoff requirements

- 修改前後 `wWinMain` 的 COM 初始化與訊息迴圈片段的實際 diff 摘要。
- 新增的 `ExplorerHost::translate_accelerator` 簽名與回傳語意的最終定案(若與本 ticket 描述的簽名不同,說明原因)。
- 逐項標記手動檢查項(Acceptance 3–6)的驗證狀態:通過／失敗／未驗證(附原因)。
- 若 `OleInitialize` 或 accelerator 轉發在某個既有自我檢查或既有行為上造成任何意外差異,寫下具體現象。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 實作交接（2026-08-20）

- `wWinMain` 實際 diff：初始化由 `CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)` 改為 `OleInitialize(nullptr)`；註冊視窗類別失敗、建立主視窗失敗與正常訊息迴圈結束的三個 `CoUninitialize()` 改為 `OleUninitialize()`。失敗處理、live view 斷言與關閉順序未改動。
- 訊息迴圈實際 diff：在 `TranslateMessage`／`DispatchMessageW` 前呼叫 `state.explorers[state.layout.active_pane()].translate_accelerator(&message)`；只有回傳 `S_OK` 才 `continue`，否則維持原本的翻譯與派送。
- 最終簽名：`HRESULT ExplorerHost::translate_accelerator(MSG* message) noexcept`。`browser_ == nullptr` 或 `GetCurrentView` 失敗回傳 `S_FALSE`（未處理）；取得 `IShellView` 成功則直接回傳 `IShellView::TranslateAcceleratorW(message)` 的 `HRESULT`。

自動化 Agent checks：

- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`：通過（exit code 0）。
- `cmake --build build`：通過（exit code 0）。
- `ctest --test-dir build --output-on-failure`：通過，1/1 test passed（exit code 0）。
- `.\build\panedock_explorer_host_lifetime_check.exe`：`PASSED: explorer_host_lifetime_check`（exit code 0）。
- `.\build\panedock_layout_state_check.exe`：`PASSED: layout_state_check`（exit code 0）。
- `.\build\panedock_prototype_location_persistence_check.exe`：`PASSED: prototype_location_persistence_check`（exit code 0）。
- `.\build\panedock_quadrant_layout_check.exe`：`PASSED: quadrant_layout_check`（exit code 0）。
- `rg -n "CoInitializeEx|CoUninitialize" src/app_shell`：無匹配（exit code 1，符合預期）。
- `git diff --check`：通過（exit code 0）。

手動檢查狀態：本執行環境沒有可附著的互動桌面，未執行 `.\build\PaneDock.exe` 的互動驗收；以下項目均不可用程式碼推論替代：

- Acceptance 3（Backspace／Alt+Left 導覽）：未驗證,需要真實桌面。
- Acceptance 4（跨 pane Ctrl+C／Ctrl+V）：未驗證,需要真實桌面。
- Acceptance 5（pane 0 到 pane 3，放開／按住 Ctrl 的拖放）：未驗證,需要真實桌面。
- Acceptance 6（與外部應用程式雙向拖放）：未驗證,需要真實桌面。

四個既有 self-check 與 ctest 均未出現意外差異；真實桌面上的 `OleInitialize` 與 accelerator 轉發行為仍未驗證,需要真實桌面。

### 2026-08-24 真實桌面人工驗收與範圍修正

在真實桌面上人工測試(非自動化):

- **Acceptance 3(Backspace／Alt+Left 導覽)：不通過,且判定為「不在本 ticket 範圍」而非缺陷。** 追查 `docs/roadmap.md` 發現「Keyboard shortcuts routed to the active pane」與「Per-tab address field, back, forward, parent」明文列在 **Phase 3**,不是 Phase 0。`IShellView::TranslateAcceleratorW` 本身不提供瀏覽器式的上一頁/下一頁導覽歷史——那是宿主應用程式自己維護導覽歷史、重新呼叫 `BrowseToObject` 的責任,屬於 Phase 3 的「per-tab 導覽歷史」功能,目前根本還沒實作,不是本 ticket 想修的宿主接線缺陷。**本 ticket 撤回 Acceptance 3,不再要求它通過**;這個觀測結果轉記給未來的 Phase 3 導覽歷史 ticket(`docs/tickets.md` §候選 已有「Back/Forward/位址列」候選項,此處補充實測證據:Backspace/Alt+Left 目前完全無反應,不是部分可用)。
- **Acceptance 4(跨 pane Ctrl+C／Ctrl+V)：通過。** 選取檔案、Ctrl+C、切換 pane、Ctrl+V,檔案確實複製過去。確認 accelerator 轉發修正對此按鍵有效。
- **Acceptance 5(pane 對 pane 拖放)：通過。** pane 對 pane 拖放正常動作。
- **Acceptance 6(與外部應用程式雙向拖放)：部分通過。** PaneDock 的 pane 拖到外部應用程式(檔案總管)**成功**;反向——從檔案總管拖進 PaneDock 的 pane——**失敗**,沒有任何反應。這是唯一存活的真實缺陷,且屬於 `docs/roadmap.md` Phase 0 明文列出的「Cross-pane drag and drop」與 `docs/testing.md` step 4「External drag and drop」驗收範圍,不是分工外的功能。

**本 ticket 剩餘範圍收斂為只修「從外部應用程式拖進 PaneDock 的 pane 沒有反應」這一項。** Acceptance 3 已撤回;Acceptance 4、5 已確認通過,不需要重做。下一步請鎖定這個不對稱現象調查(pane 對 pane 與 pane 對外都正常,只有外部拖進來的方向失敗),依 `AGENTS.md`「Read the relevant spec section and trace every caller before touching shared code」追查可能原因(例如:是否每個 pane 各自的 `IShellView` 有正確呼叫 `RegisterDragDrop`、是否与 UIPI/完整性等級有關、是否訊息迴圈新增的 accelerator 轉發影響了外部 `DoDragDrop` 訊息幫浦的重入)。禁止用程式碼推論猜測結果替代真實桌面操作驗證——若需要人工驗證,誠實寫「未驗證,需要真實桌面」。

### 2026-08-24 外部拖入修正交接

- 調查 `IExplorerBrowser`／`IShellView` drop-target 路徑：目前沒有 app-side `RegisterDragDrop`／`RevokeDragDrop`／`IDropTarget` 實作；每個 `IExplorerBrowser` 由 Windows Shell 建立原生 `IShellView`，drop target 由該 view 管理。程式沒有呼叫 `FillFromObject`，因此沒有以 `EBF_NODROPTARGET` 關閉 drop target。這些是靜態程式碼檢查結果，沒有冒充 runtime OLE trace。
- `Site::QueryService` 仍對未支援 service 回傳 `E_NOINTERFACE`；沒有證據顯示它是此方向特有的 drop 失效原因，因此未新增 speculative service 或 fake `IShellBrowser`。`set_active` 只切換 `WS_EX_CLIENTEDGE`，沒有修改 drop 相關視窗樣式；`OleInitialize` 與 `GetMessageW(&message, nullptr, 0, 0)` 保持不變。
- 修正：`ExplorerHost::translate_accelerator(MSG* message) noexcept` 現在只在 `message` 非 null 且訊息為 `WM_KEYDOWN` 或 `WM_SYSKEYDOWN` 時呼叫目前 `IShellView::TranslateAcceleratorW`；其他訊息（包括 OLE 拖放期間的滑鼠／視窗訊息）直接回傳 `S_FALSE`，交回既有 `TranslateMessage`／`DispatchMessageW`。`browser_` 為 null 或 `GetCurrentView` 失敗也回傳 `S_FALSE`。這是本輪唯一程式碼變更。
- 手動驗證狀態：Acceptance 3 已由 2026-08-24 真實桌面交接撤回，屬 Phase 3，不在本輪範圍；Acceptance 4（Ctrl+C／Ctrl+V）與 Acceptance 5（pane-to-pane drag）沿用該交接的 CONFIRMED PASSING，未重做；Acceptance 6 的修正後外部拖入結果：**未驗證,需要真實桌面**。本執行環境沒有互動桌面，不能宣稱修正已通過；原先觀察到的「Windows Explorer → PaneDock 無反應」仍是待真實桌面確認的 defect。UIPI／完整性等級差異也是可能的外部條件，但本輪沒有 runtime 證據，且沒有改動禁止 admin elevation 的架構邊界。

自動化 Agent checks（本輪實際結果）：

- `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`：通過（exit code 0）。
- `cmake --build build`：失敗（exit code 1），編譯與 library／self-check linking 已完成；最後連結 `PaneDock.exe` 時，`ld.lld` 回報 `failed to write output 'PaneDock.exe': Permission denied`／`unable to remove file`。檢查確認現有執行中的 `PaneDock.exe` 鎖住該輸出檔；沒有終止使用者程序。
- `cmake -S . -B build-pd-014 -G Ninja -DCMAKE_BUILD_TYPE=Release`：通過（exit code 0），作為不覆蓋鎖定輸出的隔離驗證目錄。
- `cmake --build build-pd-014`：通過（exit code 0）。
- `ctest.exe --test-dir build --output-on-failure`：通過，1/1 test passed（exit code 0）。
- `ctest.exe --test-dir build-pd-014 --output-on-failure`：通過，1/1 test passed（exit code 0）。
- `build-pd-014\panedock_explorer_host_lifetime_check.exe`：`PASSED: explorer_host_lifetime_check`（exit code 0）。
- `build-pd-014\panedock_layout_state_check.exe`：`PASSED: layout_state_check`（exit code 0）。
- `build-pd-014\panedock_prototype_location_persistence_check.exe`：`PASSED: prototype_location_persistence_check`（exit code 0）。
- `build-pd-014\panedock_quadrant_layout_check.exe`：`PASSED: quadrant_layout_check`（exit code 0）。
- `rg -n "CoInitializeEx|CoUninitialize" src\app_shell`：無匹配（exit code 1，符合預期）。
- `git diff --check`：待本段追加完成後執行。

四個 self-check 與 ctest 未顯示因本修正造成的意外差異；外部拖入是否恢復視覺 feedback 與 drop 行為仍**未驗證,需要真實桌面**。

### 2026-08-24 交接補記

- 上段所列 `git diff --check` 已在本段追加完成後實際執行：通過（exit code 0）。

### 2026-08-24 真實桌面最終驗收

在真實桌面上重新 build 主目錄（先關閉鎖住 `PaneDock.exe` 輸出檔的舊行程，刪除 codex 用的隔離目錄 `build-pd-014`），獨立重跑 `cmake --build build`、`ctest --test-dir build`、四個 self-check、`git diff --check`，結果與交接記錄一致,全數通過。啟動 `PaneDock.exe` 人工測試:**從 Windows 檔案總管拖曳檔案進 PaneDock 的 pane,成功放下**。

Acceptance 最終狀態:
- AC1／AC2（`OleInitialize`／`OleUninitialize`、accelerator 轉發呼叫時機）：通過。
- AC3（Backspace／Alt+Left）：撤回,判定為 Phase 3 範圍,非本 ticket 缺陷,不計入本 ticket 驗收。
- AC4（跨 pane Ctrl+C／Ctrl+V）：通過。
- AC5（pane 對 pane 拖放）：通過。
- AC6（與外部應用程式雙向拖放）：通過(兩個方向皆確認)。
- AC7（自動化 checks／self-check）：通過。
- AC8（`live_view_count` 斷言與 destroy 順序不變）：通過,未見異常。
- AC9（`git diff --check`）：通過。

PD-014 判定為 `done`。PD-011 可解除封鎖,重跑受影響的 step 3、4,不需重做已完成的 step 1、2。
