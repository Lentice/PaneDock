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
