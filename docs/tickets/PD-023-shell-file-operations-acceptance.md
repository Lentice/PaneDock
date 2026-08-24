# PD-023 — 檔案操作、剪貼簿與拖放的驗收(FR-007／FR-008)

Phase 4 · 驗證 ticket(不寫產品程式碼) · Depends on: PD-019, PD-021, PD-022

- Source: `AGENTS.md`、`docs/design-spec.md` FR-006 / FR-007 / FR-008 / FR-009、`docs/testing.md`、`docs/tickets/PD-011-prototype-acceptance-and-go-no-go.md`
- Origin: 2026-08-24,`docs/roadmap.md` Phase 4 的前三個條列(`IFileOperation` wiring、Clipboard via Shell `IDataObject`、Cross-pane and external drag and drop)。
- Priority: HIGH——這是 Phase 4 的完成判定依據。

## Goal

`docs/roadmap.md` Phase 4 的前三個條列**已經由 `IExplorerBrowser` 承載的原生 Shell view 提供**,不需要我們自己寫 `IFileOperation` 呼叫、剪貼簿程式碼或 `IDropTarget`:複製/移動/刪除/重新命名走 Shell view 自己的右鍵選單與鍵盤操作,剪貼簿走 Shell view 自己的 `IDataObject`,拖放走 Shell view 自己的 OLE drag and drop。PD-011 的 Go 判定已經在真實桌面上證明過跨 pane 與對外部應用程式的雙向拖放可運作。

因此本 ticket 是一張**驗收 ticket,不是實作 ticket**:比照 PD-011 的做法,對目前的完整建置(而非 Phase 0 原型)逐項執行檔案操作驗收協定、把每一項的實際結果寫進交接區,並為任何發現的缺口開後續 ticket。**如果全部通過,本 ticket 的產出就只有交接區紀錄與 `docs/roadmap.md` 的 Phase 4 完成段落,沒有任何 `src/` 改動——那是預期結果,不是偷懶。**

## 已確認的產品決策

1. **不重新實作 Shell view 已經提供的東西。** `AGENTS.md`:「The file list is never reimplemented.」以及「File operations go through Shell `IDataObject` and `IFileOperation`. Never assemble a path string and call the filesystem directly」。原生 view 已經是這條規則的正解;在它之上再包一層我們自己的 `IFileOperation` 呼叫只會多出一條和原生右鍵選單語意分歧的路徑。
2. **本 ticket 預設不寫產品程式碼。** 若驗收發現真實缺口,**不要在本 ticket 內順手修**——依 `AGENTS.md` 的 ticket 規則另開 ticket(取當時 `docs/tickets.md` 總覽表最大號 +1),把缺口的重現步驟寫進新 ticket,並在本 ticket 交接區留下指標。這保住本 ticket 的驗收紀錄作為歷史證據。
3. **驗收在四分割版型、至少兩個不同磁碟(以觸發「移動」而非「複製」的預設語意)上執行。** 同磁碟拖放預設是移動、跨磁碟預設是複製,兩者都要看到。
4. **本專案目前不做鍵盤/滑鼠自動化。** 本 ticket 的絕大多數項目需要真實互動桌面,由使用者親自執行。執行 agent 只負責把協定寫成一份逐項清單、執行它能執行的部分(建置、程序層級檢查)、並把使用者回報的結果整理進交接區。**不得猜測或編造任何未實際執行的項目結果**——這條在 PD-007~PD-021 一路都成立,本 ticket 尤其重要,因為它的產出就是證據本身。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` FR-007:
> 複製、移動、刪除、重新命名經由 Shell `IFileOperation`,含原生進度對話框與衝突提示。剪貼簿操作經由 Shell `IDataObject`。

`docs/design-spec.md` FR-008:
> 支援 pane 之間、以及與其他應用程式之間的拖放,經由 OLE drag and drop 與 Shell `IDataObject`。

`docs/design-spec.md` FR-009:
> 可存取網路磁碟機、USB 磁碟區、OneDrive 佔位檔,以及標準 Shell 位置(桌面、文件、本機)。

`AGENTS.md`:
> File operations go through Shell `IDataObject` and `IFileOperation`. Never assemble a path string and call the filesystem directly — that loses virtual items, progress UI and conflict handling.

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

> Do not push branches, publish releases, or modify anything outside this repository without explicit approval.

## Files to read and trace first

- `docs/tickets/PD-011-prototype-acceptance-and-go-no-go.md` 的 交接區——Phase 0 已經驗過哪些拖放情境、用什麼方式驗的、結論是什麼。**本 ticket 不重複已驗過且結論明確的項目,只重驗「原型之後架構改變過」的部分**(PD-015 換掉狀態層、PD-017 的保活式 Group 切換、PD-019 的 tab 切換共用單一 `ExplorerHost`)。
- `docs/testing.md` — 既有驗收協定的寫法與 flakiness 的既有立場。
- `src/explorer_host/explorer_host.cpp` 的 `initialize()` — OLE 初始化(PD-014 修正過)是拖放能運作的前提,確認它沒有在後續 ticket 中被改壞。
- `docs/tickets/PD-014-explorer-host-ole-and-accelerator-wiring.md` 的 交接區——`OleInitialize` 與 accelerator 轉發的既有背景;`Ctrl+C`/`Ctrl+V`/`F2`/`Delete` 能不能傳到 Shell view 完全取決於這條路徑。
- `src/app_shell/main.cpp` 的訊息迴圈——PD-021 加入的快速鍵判斷在 `translate_accelerator` 之後,理論上不會攔截 `Ctrl+C`/`Ctrl+V`/`Ctrl+X`/`Delete`/`F2`,但 `Ctrl+W`(關閉 tab)在 Shell view 裡是否與任何原生操作衝突,要實際確認。

## Scope

把下列協定寫成 `docs/testing.md` 的一個新章節(比照既有原型驗收協定的寫法),逐項執行並記錄結果:

**A. 檔案操作(FR-007)**
1. 在一個 pane 內用右鍵選單複製一個檔案、貼到另一個 pane:出現原生進度對話框,結果正確。
2. 同上但用移動(剪下/貼上),同磁碟。
3. 大量或大檔案的複製:原生進度對話框出現且可取消,取消後來源不受損。
4. 貼到已有同名檔案的資料夾:出現原生衝突提示(取代/略過/兩者皆保留),三個選項都有效。
5. 刪除到資源回收筒、以及 `Shift+Delete` 永久刪除:都出現原生確認,行為與檔案總管一致。
6. 就地重新命名(`F2`):可用,`Esc` 取消,重名時出現原生提示。
7. 在**檔案操作進行中**切換 Group / 切換 tab / 拖曳分隔線:應用程式不崩潰、不當掉(這是 `AGENTS.md` 重入性那條規則的實際驗收)。

**B. 剪貼簿(FR-007 後半)**
8. `Ctrl+C` / `Ctrl+X` / `Ctrl+V` 在 pane 內可用(確認 PD-021 的快速鍵沒有攔截到它們)。
9. 從 PaneDock 複製、貼到 Windows 檔案總管;以及反向。
10. 從 PaneDock 複製檔案、貼到一個接受檔案的應用程式(例如郵件或聊天視窗)。

**C. 拖放(FR-008)**
11. pane A → pane B,同磁碟(預期:移動)。
12. pane A → pane B,跨磁碟(預期:複製)。
13. PaneDock → 外部檔案總管視窗;外部檔案總管 → PaneDock。
14. 拖到**非 active** 的 pane:確認落點正確、且該 pane 是否需要先變成 active 才能接收(記錄實際行為,不預設哪個是對的)。
15. 拖曳過程中滑鼠移出視窗再回來、以及按 `Esc` 取消:不留下卡住的拖曳狀態。

**D. 命名空間覆蓋(FR-009,抽樣)**
16. 在 OneDrive 佔位檔資料夾內做一次複製:確認佔位檔語意保留(不被強制下載成本機檔),或記錄實際行為。
17. 網路磁碟機、USB 磁碟區各做一次複製與一次刪除。

## Non-goals

- **不寫任何 `IFileOperation` / `IDataObject` / `IDropTarget` 的自有實作**(決策 1)。
- 不做「拖到 tab 標題上以複製到該 tab 的資料夾」這類 Explorer 之外的加值互動——spec 未要求,YAGNI;若使用者實際使用後想要,記在 `docs/tickets.md` 的 候選 區。
- 不做端到端 UI 自動化(`docs/tickets.md` §已否決的方向已否決 WinAppDriver/UIAutomation)。
- 不在本 ticket 內修任何發現的缺口(決策 2)。
- 不改 `core`、不改 session schema。

## Acceptance

1. `docs/testing.md` 新增了上述 A–D 協定章節,格式與既有原型驗收協定一致。
2. A–D 每一項在交接區都有一筆明確結果:`PASS` / `FAIL` / `未驗證,需真實桌面` 三選一,`FAIL` 附重現步驟。**沒有任何一項是空白或含糊的**。
3. 每一個 `FAIL` 都有對應的新 ticket(檔案已建立、`docs/tickets.md` 總覽表已加一列),或在交接區明確寫出為什麼不需要開票。
4. 若 A–D 全部 `PASS`(或僅剩「未驗證,需真實桌面」而無 `FAIL`),在 `docs/roadmap.md` Phase 4 段落補一筆完成紀錄,比照 Phase 1/2/3 段落的既有寫法,並列出交付 ticket(PD-022、PD-023 及任何後續修補 ticket)。**只要有一個 `FAIL` 未解決,就不得標記 Phase 4 完成。**
5. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過(本 ticket 預期不改 `src/`,測試結果應與執行前完全相同)。
6. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# 確認 OLE 初始化路徑仍在(拖放的前提)
rg -n "OleInitialize|OleUninitialize" src
git diff --check
git status
# 預期:改動只有 docs/*,沒有 src/* —— 若有 src/* 改動,代表違反了決策 2
```

```powershell
Start-Process .\build\PaneDock.exe
Start-Sleep -Seconds 2
Get-Process PaneDock | Select-Object Responding, HandleCount, WorkingSet64
# A–D 絕大多數項目需要真實互動桌面,由使用者親自執行(決策 4)。
```

## Handoff requirements

- A–D 每一項的逐項結果,含執行環境(Windows 版本、涉及的磁碟機代號與類型)。
- 明確列出「已由 PD-011 在 Phase 0 驗過因此本次略過」的項目,以及略過的理由。
- 任何 `FAIL` 對應開出的新 ticket 編號。
- **執行 agent 若沒有互動桌面,就把 A–D 整份協定寫好、把能自動執行的部分(建置、`rg` 檢查、程序層級啟動檢查)做完,然後把 A–D 全部標為「未驗證,需真實桌面」交回,由 reviewer 或使用者補上。這是可接受的完成方式;猜測結果不是。**

## 交接區

<!-- 驗收 agent 填寫,append-only -->

### 2026-08-24 非互動環境驗收交接

#### 產出與執行環境

- 已在 `docs/testing.md` 新增 `Shell file operations acceptance protocol (Phase 4)`，完整列出 A1–A7、B8–B10、C11–C15、D16–D17 的操作、預期結果、四 pane／兩個 volume／disposable test data 前置條件，以及每項只能記錄 `PASS`／`FAIL`／`未驗證,需真實桌面` 的規則。
- 本 session 可讀得的 OS 資訊為 Windows NT `10.0.26200.0`、DisplayVersion `25H2`、CurrentBuildNumber `26200`；registry 的 ProductName 回報 `Windows 10 Pro`（該欄位在新 Windows build 可能保留舊產品字串，因此只記錄原始讀值，不推測實際 edition）。
- 本 session 沒有互動桌面，沒有操作任何測試檔、磁碟機或外部應用程式。涉及的磁碟機代號與類型：**未驗證,需真實桌面**；reviewer 執行時必須補記同磁碟與跨磁碟代號、USB、mapped network drive 與 OneDrive 測試位置。
- 沒有修改任何 `src/` 產品碼、core、session schema、`docs/tickets.md` 或 `docs/roadmap.md`。雖然本輪沒有觀察到 `FAIL`，使用者明確要求不要更新 roadmap，因此 Phase 4 完成紀錄留給有真實 A–D 證據的 reviewing session。

#### A. 檔案操作（FR-007）

- **A1 右鍵 Copy／跨 pane Paste 與原生進度 UI：未驗證,需真實桌面。**
- **A2 同磁碟 Cut／Paste 移動：未驗證,需真實桌面。**
- **A3 大量／大檔複製、取消及來源完整性：未驗證,需真實桌面。**
- **A4 同名衝突 Replace／Skip／Keep both 三選項：未驗證,需真實桌面。**
- **A5 Recycle Bin Delete 與 `Shift+Delete` 永久刪除確認：未驗證,需真實桌面。**
- **A6 `F2` 就地 rename、`Esc` 取消及重名提示：未驗證,需真實桌面。**
- **A7 操作進行中切 Group／tab／拖 splitter 的重入穩定性：未驗證,需真實桌面。**

#### B. 剪貼簿（FR-007）

- **B8 pane 內 `Ctrl+C`／`Ctrl+X`／`Ctrl+V`：未驗證,需真實桌面。**
- **B9 PaneDock 與 Windows File Explorer 雙向 copy/paste：未驗證,需真實桌面。**
- **B10 貼至接受檔案的郵件／聊天應用程式：未驗證,需真實桌面。**

#### C. 拖放（FR-008）

- **C11 pane A → pane B，同磁碟預設 move：未驗證,需真實桌面。**
- **C12 pane A → pane B，跨磁碟預設 copy：未驗證,需真實桌面。**
- **C13 PaneDock 與外部 File Explorer 雙向拖放：未驗證,需真實桌面。**
- **C14 drop 至 non-active pane 的落點及 active 行為：未驗證,需真實桌面。**
- **C15 拖曳移出再返回及 `Esc` 取消後無卡住狀態：未驗證,需真實桌面。**

#### D. 命名空間抽樣（FR-009）

- **D16 OneDrive placeholder folder 內複製及 hydration 語意：未驗證,需真實桌面。**
- **D17 mapped network drive 與 USB volume 各一次 copy/delete：未驗證,需真實桌面。**

#### PD-011 歷史證據與本輪略過範圍

- PD-011 Phase 0 Step 3 曾在真實桌面確認一般 pane-to-pane drag/drop，Step 4 曾確認 PaneDock 與外部 File Explorer 雙向 drag/drop；本輪沒有重演這兩項。它們是 C11–C13 的歷史 baseline，但 Step 3 沒有記錄來源／目的磁碟代號，無法分辨同磁碟 move 與跨磁碟 copy，且 PD-015／017／019 後已更換 state、Group 與 tab/host 路徑，因此不能當成本 ticket 現行完整 build 的 C11／C12 PASS。C13 同樣因本輪沒有互動桌面而維持未驗證。
- PD-014 的真實桌面驗收另曾確認跨 pane `Ctrl+C`／`Ctrl+V`，可作為 B8 的部分歷史證據；它沒有涵蓋 `Ctrl+X`，且發生在 PD-021 快速鍵派送之前，因此本輪略過重演但不宣稱 B8 PASS。
- 其餘 A1–A7、B9–B10、C14–C15、D16–D17 沒有可直接沿用且符合本協定細節的 PD-011 證據。沒有任何本輪 `FAIL`，所以沒有建立後續 ticket。

#### 可自動驗證結果

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：通過。
- `cmake --build build`：通過，`ninja: no work to do`。
- `ctest --test-dir build --output-on-failure`：3/3 通過（`panedock_core_model`、`panedock_core_layout`、`panedock_core_session`）。
- `rg -n "OleInitialize|OleUninitialize" src`：命中 `src/app_shell/main.cpp` 的一個 `OleInitialize` 與五個對應 failure／shutdown `OleUninitialize` 路徑；OLE 初始化前提仍存在。
- 靜態訊息路徑：`translate_accelerator` 仍先於 PD-021 app-shell shortcuts；後者只攔截 T/W/Tab、Alt+Left/Right、Backspace、F6，未攔截 C/V/X/Delete/F2。這不是互動 PASS，只是 boundary trace。
- 程序 smoke check：啟動本 session 自己的 `build\\PaneDock.exe`，等待 2 秒後 `PID=24480`、`Responding=True`、`HandleCount=766`、`WorkingSet64=58179584`、`HasExited=False`；記錄後只終止該測試 process。
- 工作開始前已有未追蹤 `.claude/`，本輪未觸碰；未 commit。

### 2026-08-24 最終檢查補記

- `git diff --check`：通過。`git diff --name-only` 最終只有 `docs/testing.md` 與本 ticket，沒有任何 `src/*` diff；`docs/tickets.md`、`docs/roadmap.md` 均未修改。
