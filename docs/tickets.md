# PaneDock Tickets

`docs/design-spec.md` 是產品權威。本頁只負責同步「工作切分、依賴、狀態與證據」,不重述 spec 的內容。

## 使用方式

1. Agent 先讀本頁、[AGENTS.md](../AGENTS.md)、來源 Spec 章節與該 ticket 文件。
   撰寫新 ticket 前另需讀本頁的 [§已否決的方向](#已否決的方向--不要重開)。
2. 只處理一個 ticket 的範圍;不要順手實作相鄰 ticket。
3. 完成前執行 ticket 文件指定的 Agent checks,保留命令與結果作為證據。
4. 更新本頁的狀態與 ticket 文件的交接備註;若被阻塞,寫出具體原因與需要的外部決策。

## 狀態

| 狀態 | 意義 |
|---|---|
| `planned` | 已定義但依賴尚未完成 |
| `ready` | 依賴已具備,可交給 Agent |
| `in_progress` | 正在實作 |
| `blocked` | 具體外部條件阻塞,不能自行繞過 |
| `done` | Agent checks 通過且交接資料完整 |
| `deferred` | 保留在 Spec／roadmap,但刻意延後 |
| `superseded` | 曾完成,但產品決策改變後由另一個 ticket 取代;文件與完成紀錄保留作為決策軌跡 |

**狀態與依賴只存在於本頁的 Ticket 總覽表格,ticket 文件不得自行宣告。** 同一份狀態存兩個地方必然會分岔:冷讀某個 ticket 檔的 agent 會把檔頭的過期狀態當成待辦工作,而表格早已是 `done`。ticket 文件的檔頭只寫撰寫當下的定位(Phase、Depends on),狀態一律看本頁。

## Agent 交付規則

- 每個 ticket 只負責一個主要成果,避免跨 ticket 的隱性工作。
- 必須保持既有 build／CTest 可用;不得用關閉測試來取得綠燈。
- 每個非平凡邏輯至少新增一個 focused runnable test 或 self-check。若該邏輯不在 `core`,必須在交接區說明為何不能,以及用什麼人工檢查替代。
- Agent 只需能執行命令、測試程式、啟動／終止程序;不要求操作視窗或人工確認畫面。
- UI ticket 的 Agent checks 應驗證建置、視窗生命週期、狀態資料、訊息與可測的 Win32 結果;視覺人工驗證不屬於本追蹤表,屬於 `docs/testing.md` 的原型驗收協定。
- 不新增網路、第三方 runtime、服務、driver、管理員權限或超出 Spec 的功能。
- 預設每個 ticket 的實作範圍為半天至兩天;若超過,先拆 ticket。

## Ticket 總覽

| ID | Ticket | Phase | Status | Depends on | 文件 |
|---|---|---|---|---|---|
| PD-001 | 四分割 `IExplorerBrowser` 可行性原型 | 0 | `superseded` | — | [PD-001](tickets/PD-001-four-pane-feasibility-prototype.md) |
| PD-007 | 單一 `IExplorerBrowser` 宿主與關閉序列 | 0 | `ready` | — | [PD-007](tickets/PD-007-single-explorer-host-and-shutdown.md) |
| PD-008 | 四宮格版型與矩形計算 | 0 | `planned` | PD-007 | [PD-008](tickets/PD-008-four-pane-quadrant-layout.md) |
| PD-009 | active pane 指示與保活式版型切換 | 0 | `planned` | PD-008 | [PD-009](tickets/PD-009-active-pane-and-layout-toggle.md) |
| PD-010 | 原型的位置持久化與還原 | 0 | `planned` | PD-008 | [PD-010](tickets/PD-010-prototype-location-persistence.md) |
| PD-011 | 原型驗收協定執行與 Go/No-Go 判定 | 0 | `planned` | PD-009, PD-010 | [PD-011](tickets/PD-011-prototype-acceptance-and-go-no-go.md) |
| PD-002 | 選取狀態還原可行性判定 | 0 | `planned` | PD-011 | [PD-002](tickets/PD-002-selection-restore-feasibility.md) |
| PD-003 | 閒置資源量測基準 | 0 | `planned` | PD-011 | [PD-003](tickets/PD-003-idle-resource-baseline.md) |
| PD-004 | `core` 資料模型與不變式 | 1 | `planned` | PD-011 | [PD-004](tickets/PD-004-core-model-invariants.md) |
| PD-005 | 五種版型的矩形計算 | 1 | `planned` | PD-004 | [PD-005](tickets/PD-005-layout-rect-computation.md) |
| PD-006 | session document 序列化與遷移 | 1 | `planned` | PD-004 | [PD-006](tickets/PD-006-session-document-persistence.md) |

## Dependency lanes

```text
Phase 0 — Go/No-Go gate
  PD-007 (single host + shutdown sequence)
    └─ PD-008 (four-pane quadrant layout)
         ├─ PD-009 (active pane + keep-alive layout toggle)
         └─ PD-010 (prototype location persistence)
              PD-009 + PD-010 ─┬─ PD-011 (acceptance protocol + Go/No-Go)
                               ├─ PD-002 (selection feasibility verdict)
                               └─ PD-003 (idle resource baseline)

Phase 1 — core, gated on PD-011 returning Go
  PD-004 (model + invariants)
    ├─ PD-005 (layout rects)
    └─ PD-006 (session persistence)
```

PD-011 gates everything. A No-Go verdict there redirects Phase 1 onward to the `IShellFolder` fallback in `docs/design-spec.md` §9.1, and the tickets below it must be rewritten rather than adjusted.

## 已否決的方向 — 不要重開

**要重開是允許的,但新 ticket 內必須寫出覆寫與新證據。**

| 方向 | 依據 | 否決理由 |
|---|---|---|
| 改用 Rust ＋ windows-rs | `docs/adr/0001` | 本專案的決定性風險是 `IExplorerBrowser` 宿主契約,而 COM host、callback、vtable、marshalling 全部落在 `unsafe`,拿不到記憶體安全的實質好處;同時 windows-rs 沒有可參照的 ExplorerBrowser host,每個 site/callback 契約都要自行從 Win32 文件重推。要重開需先提出一個可運作的四分割 Rust 宿主原型。 |
| 改用 C#／.NET | `docs/adr/0001` | 出局理由是開發便利與長期維護,**不是記憶體**——純 Win32 P/Invoke 的 CoreCLR 常駐約 20–60 MB,在四分割 Shell view 已佔 300–600 MB 的前提下屬雜訊。真正的問題是 site 契約、`IServiceProvider` 回呼與 PIDL 生命週期的 marshal 包覆層。要重開必須提出一個能證明 marshal 層不成為維護負債的實作。 |
| 用「部署檔案大小」推論記憶體 | 2026-08-20 選型審查 | self-contained 部署的 100 MB 檔案大小與常駐記憶體無關,曾據此誤判 C# 出局。任何以檔案大小論證記憶體的 ticket 一律退回。 |
| 為 `IExplorerBrowser` 加抽象層以便 fake | `docs/testing.md` | 只有一個真實實作的介面,買到的覆蓋率不對應真實風險,且 fake 驗證的是我們對 COM 契約的假設而非契約本身。要重開必須先舉出一個「真實整合已壞而 core 測試無法察覺」的具體缺陷。 |
| 端到端 UI 自動化(WinAppDriver／UIAutomation) | `docs/testing.md` | 對 live Shell view 極易 flaky。要重開需先示範在兩台機器上連續 20 次穩定通過。 |
| 任意遞迴 pane 分割 | `docs/design-spec.md` §3.2 | 固定五種版型已涵蓋實際需求,遞迴分割使版型狀態、還原與矩形計算複雜度大幅上升。要重開需先有使用者實際回報五種版型不足的情境。 |
| 每個 tab 都保留 live `IExplorerBrowser` | `docs/design-spec.md` §NFR-002 | 這是記憶體無上界成長的唯一原因;閒置資源目標與完整 tab 狀態的共存,靠的就是「只有可見 pane 的 active tab 是 live」。要重開必須先量到 realize-on-activation 的延遲對使用者可感知。 |
| C# UI 殼層 ＋ C++ shell host DLL 混合 | `docs/adr/0001` | 同一 process 仍須付 .NET runtime,RAM 沒有改善,卻多出 ABI、除錯與打包複雜度;本專案 UI 只有側邊欄與固定版型,C# 殼層可省的工作量趨近於零。唯一值得重開的情境是改為**獨立 process** 隔離第三方 extension 崩潰,且需先有實際崩潰紀錄。 |

### 建議實作順序(open tickets)

| 順序 | ID | 難度 | 為什麼排這裡 |
|---|---|---|---|
| 1 | PD-007 | 中 | repo 的第一段程式碼。它建立 COM 生命週期、site 契約與建置骨架,後面四片全部站在它上面。 |
| 2 | PD-008 | 高 | 第一次暴露本案的決定性風險——多實例 `IExplorerBrowser` 共存。 |
| 3 | PD-009 | 高 | 保活式切換是崩潰面與洩漏面。獨立成片才能反覆切換並歸因。 |
| 4 | PD-010 | 低 | 只依賴 PD-008,可與 PD-009 並行。刻意用最笨的格式,不預先實作 §10 契約。 |
| 5 | PD-011 | 中 | 全案 Go/No-Go。純驗證,不寫產品程式碼。 |
| 6 | PD-003 | 低 | 原型還在手上時最容易量;晚做會需要重建原型。 |
| 7 | PD-002 | 中 | 結論可能砍掉一個需求,越早知道越好,但必須有原型才能試。 |
| 8 | PD-004 | 中 | Phase 1 的根;PD-005／PD-006 都依賴它的型別。 |
| 9 | PD-005 | 低 | 純計算,`core` 內最容易測的一塊。 |
| 10 | PD-006 | 中 | 排在 PD-005 之後:它會定下 schema,而版型欄位是 schema 的一部分,順序顛倒會造成一次無謂的遷移。 |

### 候選(尚未開 ticket)

| 候選 | 觸發條件 |
|---|---|
| 診斷模式:抑制第三方 shell extension | Phase 5 前必須開;若在 Phase 0 原型就遇到 extension 造成的崩潰則提前。 |
| 縮圖 pipeline 的快取與尺寸上限 | 待 PD-003 量出縮圖對記憶體的實際貢獻後再開,避免憑估計調參數。 |
| `IShellFolder` 自建清單檢視(fallback) | 僅在 PD-001 判定 No-Go 時開。 |
| 側邊欄寬度的全域設定持久化 | 若使用者回報每次啟動都要重拖再開;目前預設值可接受。 |
| Group 圖示與顏色 | Spec 未列為 MVP;若 Group 數量成長到難以用文字辨識再開。 |

## 計畫決策紀錄

### 2026-08-20 — 專案建立與選型收斂

以 NimbleRun 的專案機制為模型建立本 repo,work items 改稱 tickets(`PD-xxx`)。沿用的機制:`CLAUDE.md` 僅為 `@AGENTS.md` 的單行 import、狀態單一來源規則、已否決方向帳本(含重開條件)、fail-closed 的 release evidence 腳本、ADR、CONTEXT 詞彙表、`## 計畫決策紀錄`。

語言選型經過一次反轉。初始 handoff 文件建議 C#/.NET 8 ＋ WPF ＋ `HwndHost`;使用者傾向 Rust(理由是「更穩定」);最終定為 C++20 ＋ WRL ＋ 純 Win32。兩份獨立審查(codex、opencode)各自查證官方文件後得出同一結論,且都推翻了原本的推論過程:

- 「Rust 比較穩」不成立。崩潰面是 `shell32` 與第三方 shell extension,語言換不掉。
- 「C# 因記憶體出局」是錯的歸因。部署檔案大小不等於常駐記憶體;四分割下語言 runtime 只佔總量約 5–8%,主導項是 Shell view 與 extension。C# 該出局的理由是 marshal 層的長期維護成本。
- 真正能控制記憶體的是三個架構決策(realize-on-activation、縮圖上限、extension 延後載入),不是語言。這條寫進 §NFR-002 而非留在選型討論裡。

**刻意不做的事**:不建 CI。NimbleRun 沒有 CI,governance 全靠 Markdown 與一支 fail-closed 腳本,而本專案的關鍵驗證(四分割 Shell 行為、原生右鍵選單、跨 pane 拖放、混合 DPI)在 CI runner 上本來就跑不出有意義的結果——加 CI 只會產生「綠燈但沒驗到東西」的假保證。要重開需先有一個能在無桌面環境下驗到真實 Shell 行為的方法。

**刻意不預先開滿 ticket**。只開到 PD-006(Phase 0 全部 ＋ Phase 1 的三個根 ticket)。PD-001 的 Go/No-Go 結論會決定 Phase 1 以後的 ticket 該怎麼寫;先寫好一批再作廢,就是 NimbleRun 明令禁止的「預留編號」的變體。

**刻意不沿用 LLVM-MinGW toolchain**。NimbleRun 用它,但 WRL 與 Shell COM header 是 MSVC 取向,而本專案選 C++ 的整個理由就是直接坐在 Windows SDK 上、不隔翻譯層。改用 MSVC 並記錄於 `docs/development.md`。

### 2026-08-20 — PD-001 拆分為五片 tracer bullet

PD-001 以單一 ticket 涵蓋整個四分割原型:scope 十項、acceptance 十條,從「repo 尚無任何程式碼」一路到「寫下 Go/No-Go」。它違反本頁自己訂的「半天到兩天」尺寸規則,也裝不進一個 context window。

拆成 PD-007～PD-011,每片都是縱切:各自穿過視窗、COM、view 生命週期與 self-check,單獨可 demo。切分**不改變任何技術判斷**——PD-001 的產品決策、binding constraints 與否決事項全部原樣沿用,只改變交付顆粒度。

PD-001 標為 `superseded`,文件不動,作為決策軌跡保留。原本依賴 PD-001 的 PD-002／PD-003／PD-004 改依賴 PD-011,因為 Go/No-Go 判定的職責移到了那裡。

刻意讓 PD-010(位置持久化)與 PD-009(保活式切換)並行而非串接:兩者只共同依賴 PD-008,合併會讓 PD-009 同時扛 churn 量測與持久化,handle 數異常時難以歸因。

刻意讓 PD-011 成為不寫產品程式碼的純驗證片:Go/No-Go 是全案閘門,混在實作片裡容易被草率蓋章。
