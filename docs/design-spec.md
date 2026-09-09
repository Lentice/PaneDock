# PaneDock Design Spec

> Windows 多分割檔案總管：可切換的 Group ＋ 1–4 個原生 Shell 檔案檢視

| 欄位 | 內容 |
|---|---|
| 文件版本 | 1.0 |
| 產品暫定名稱 | PaneDock |
| 文件狀態 | Phase 0 可行性基準 |
| 目標平台 | Windows 10 22H2／Windows 11，x64 |
| 技術基準 | C++20、原生 Win32、Shell COM（WRL ComPtr） |
| 最後更新 | 2026-09-03 |

## 1 文件目的

本文件是 PaneDock 的產品權威。ticket 引用本文件的條款（`§FR-005`、`§9.2` 或 `docs/design-spec.md:120` 形式），不得與本文件衝突。若產品決策變更，**先更新本文件，再調整受影響的 ticket**。

## 2 產品定義

### 2.1 一句話定位

一個視窗、左側可切換的 Group、右側 1–4 個原生 Windows Shell 檔案檢視。

### 2.2 要解決的問題

使用者在同一台機器上有數個彼此獨立的工作情境：客戶專案、個人媒體庫、程式碼 repo 與其建置輸出、待整理的下載資料夾。每個情境需要同時看見一組不同的資料夾。

Windows 檔案總管一個視窗只有一個資料夾，要湊出一個工作情境必須開好幾個視窗、逐一拖到位置並各自導覽。切換到別的情境時整個排列就沒了——要嘛留著十幾個視窗然後在裡面找,要嘛每次重建。

Q-Dir 解決了一半：一個視窗內 1–4 個 pane。但它沒有「已儲存的工作情境」概念,pane 排列與路徑是單一的全域狀態,所以在客戶專案與媒體庫之間切換仍然要手動重新導覽每個 pane。

### 2.3 核心抽象

**Group** 是本產品的主要抽象,不是資料夾捷徑。選取一個 Group 會還原整個右側工作情境。

### 2.4 設計原則

1. **不重刻檔案清單。** 檔案檢視一律承載原生 Shell view,原生行為由 Windows 提供。
2. **閒置零成本。** 全天開著也不得消耗可量測的 CPU 或磁碟 I/O。
3. **固定版型優於任意分割。** 八種版型涵蓋實際需求,遞迴分割只增加模型與實作複雜度。
4. **記憶體由架構控制,不由語言控制。** 見 §6 NFR-002。

### 2.5 目標使用者

單一使用者的本機桌面工具。無多使用者、無同步、無帳號。

### 2.6 術語

術語定義見 `CONTEXT.md`。本文件與 UI 字串一律使用該表的詞彙。

## 3 範圍

### 3.1 MVP 包含

- Group 側邊欄:新增、重新命名、複製、刪除、排序
- 八種固定版型:單一、左右、上下、左一右二、左二右一、上一下二、上二下一、四宮格
- 每個 pane 一個以上的 tab
- 承載原生 `IExplorerBrowser` 檔案檢視
- 網址輸入與上一頁／下一頁／上層導覽
- 原生縮圖、原生右鍵選單、跨 pane 拖放
- 持久化並還原全部必要狀態
- 基本鍵盤快速鍵與 active pane 路由
- 啟動／工作階段復原

### 3.2 明確不包含

- 任意遞迴 pane 分割
- 巢狀 Group
- 自製檔案操作引擎
- 內容預覽窗格
- 搜尋索引
- FTP／SFTP 或任何非 Shell 的遠端協定
- 資料夾同步或比對
- 自訂外掛系統
- 內建終端機
- 跨平台
- 超出系統主題的佈景自訂
- 選取項目、捲動位置、欄寬的「保證精確」還原（見 §6 NFR-005）
- 以獨立 process 隔離第三方 shell extension（見 §14）

## 4 使用者體驗

### 4.1 視窗結構

單一頂層視窗。左側為可調整寬度的 Group 側邊欄,右側為 pane 區域。兩者之間有可拖曳的分隔線,寬度屬於全域設定而非個別 Group 的狀態。側邊欄、pane 分隔線與頂層視窗邊框的調整皆為 live resize；每個中繼尺寸的 app chrome 與 Shell view 必須同步更新,不得以節流造成階梯式跳動或以 erase 造成閃爍。

### 4.2 Group 切換

側邊欄單擊即切換。切換必須感覺即時——實作上以保活既有 view 並重新導覽達成,不重建 HWND。

### 4.3 版型切換

在當前 Group 內可切換八種版型。pane 數量由版型決定。

### 4.4 分隔線

pane 之間的分隔線可拖曳,比例記錄於該 Group。

### 4.5 active pane 指示

恰有一個 pane 為 active,以視覺方式明確指示。點擊 pane 即設為 active;亦提供鍵盤快速鍵在 pane 間移動焦點。

### 4.6 tab

每個 pane 有一個以上的 tab,可新增、關閉、切換。tab 條在 pane 上緣；在 tab 條未被 tab、`+` 或捲動按鈕占用的空白區雙擊會新增 tab。

### 4.7 導覽

每個 tab 有自己的網址欄與獨立的導覽歷史。提供上一頁、下一頁、上層。

### 4.8 原生行為

pane 內部的一切互動由 Shell view 處理:多選手勢、右鍵選單、拖放、就地重新命名、鍵盤操作。PaneDock 不介入。

每個 pane footer 提供一個 folder background context menu affordance，讓小 pane
在 Shell view 沒有可用空白處時仍能開啟目前資料夾的原生背景選單。這是有限的
host-level 入口：PaneDock 只觸發同一個 Shell view 的 background `IContextMenu`，
不重建、過濾或包裝選單內容。

### 4.9 錯誤狀態

無法解析的 Shell location 在該 tab 內顯示可復原的錯誤狀態,並保留其設定。重新連線後可重試。

### 4.10 復原

崩潰後重新啟動應復原到可用狀態。session document 損壞時退回上一個良好版本。

## 5 功能需求

### FR-001 Group 生命週期

使用者可新增、重新命名、複製、刪除、重新排序 Group。刪除需二次確認。複製會複製完整狀態但產生新的 identity。

Group 右鍵選單的每個操作皆搭配簡潔的單色線條圖示：重新命名用鉛筆、複製用重疊文件、刪除用垃圾桶、排序用向上／向下箭頭。保留英文文字標籤，圖示隨 DPI 縮放，停用項目以灰色呈現。

當鍵盤焦點在 sidebar 的 Group row 時，按 `F2` 開始該 Group 的 inline rename；鍵盤焦點在 Shell view 時，`F2` 維持 Windows 原生檔案 rename 行為。

以拖曳重新排序時採 **remove source，再 insert at target index** 的語意。拖曳中的 placeholder 必須顯示被拖曳 Group 的淡化內容；來源 Group 不再留在原列，插入位置起的其他 Group 依最終順序位移，不得以 placeholder 覆蓋或隱藏原本位於目標位置的 Group。此預覽順序必須與放開後的實際順序一致，包含單一 Group、原位放開、第一個移到最後一個、最後一個移到第一個，以及任何中間位置的雙向拖曳。

### FR-002 Group 切換與還原

選取 Group 時還原:版型、分隔比例、每個 pane 的 tab 集合、每個 tab 的 Shell location、view mode、排序欄位與方向、active pane、每個 pane 的 active tab。

### FR-003 版型

支援且僅支援八種版型:單一、左右、上下、左一右二、左二右一、上一下二、上二下一、四宮格。**pane 是穩定 identity(每個 Group 至多四個),永久擁有自己的 tabs;切換版型只改變「哪些 pane 可見」與「如何排列」,任何情況下都不把 tab 從一個 pane 搬移或刪除到另一個 pane。** 縮小版型只隱藏多餘的 pane;放大版型時,若該 pane identity 已存在則直接顯示(保留其 tabs 與 active tab),若尚未存在才新增一個以預設 location 開啟的 tab。

### FR-004 分隔比例

pane 分隔線可拖曳,比例以 0.0–1.0 的相對值儲存於該 Group,視窗縮放時維持比例。

調整 pane 分隔線、Group 側邊欄或頂層視窗時,版面幾何在同一個 layout frame 以 parent-scoped deferred-position 批次提交；導覽按鈕、網址列、分頁列、狀態列與 Shell view 不得在同一個中繼 frame 暴露不一致的位置。Win32 的 `DeferWindowPos` 要求同一批次內的視窗共用 parent,因此主視窗 child 與各 Explorer container child 各自使用一批,但在同一個 layout pass 完成提交。拖曳中的更新只重排幾何,內容重算與持久化在拖曳結束時處理。

#### FR-004a 退化尺寸

視窗過小導致 pane 寬度或高度低於下限時,維持比例但不得產生負值或零尺寸的 pane 矩形。

### FR-005 tab 管理

每個 pane 至少一個 tab。可新增、關閉、切換 tab；在 tab 條未被 tab、`+` 或捲動按鈕占用的空白區雙擊會新增 tab。新 tab 的預設 Shell location 是 My Computer（persisted parsing name 為 `::{20D04FE0-3AEA-1069-A2D8-08002B30309D}`），且初始 application state、新 Group 與版型增加 pane 都遵循此預設；需要特殊目的地的流程可以明確傳入另一個 `ShellLocation`。關閉 pane 的最後一個 tab 或跨 pane 搬走來源 pane 的最後一個 tab 時,留下的 tab 也導覽至同一個預設 location,而非留下空 pane。已保存的既有 tab location 不因這項預設變更而修改。

### FR-006 原生 Shell 檔案檢視

每個已 realize 的 tab 透過 `IExplorerBrowser` 承載 Shell view,提供原生圖示與縮圖、原生右鍵選單與已安裝的 shell extension、多選、就地重新命名。

### FR-007 檔案操作

複製、移動、刪除、重新命名經由 Shell `IFileOperation`,含原生進度對話框與衝突提示。剪貼簿操作經由 Shell `IDataObject`。

### FR-008 拖放

支援 pane 之間、以及與其他應用程式之間的拖放,經由 OLE drag and drop 與 Shell `IDataObject`。

### FR-009 Shell 命名空間覆蓋

可存取網路磁碟機、USB 磁碟區、OneDrive 佔位檔,以及標準 Shell 位置(桌面、文件、本機)。

### FR-010 導覽

每個 tab 提供網址輸入(可鍵入或貼上)、上一頁、下一頁、上層。導覽歷史為每個 tab 各自獨立。

### FR-011 持久化

Group 與其狀態自動儲存,無需使用者手動存檔。視窗位置、大小與狀態一併儲存。

### FR-012 不可解析的 location

無法解析的 Shell location 在 tab 內顯示可復原錯誤,保留設定,並可重試。不得因此刪除任何已儲存的設定。

### FR-013 復原

崩潰後重新啟動復原到可用狀態。session document 無法解析時退回上一個良好版本,並在 UI 告知已退回。

### FR-014 鍵盤

提供 pane 間移動焦點、切換 tab、新增／關閉 tab、上層導覽的快速鍵。快速鍵一律送往 active pane。

`Tab`／`Shift+Tab` 在可見 pane 間正向／反向循環，優先於 Shell accelerator，避免先將焦點移到排序欄位標題。位址列有焦點時不攔截；`Ctrl+Tab`／`Ctrl+Shift+Tab` 維持 pane 內分頁切換。此為 2026-09-07 使用者回報後對 PD-101「Shell 攔截 Tab 時不覆蓋」決策的覆寫。

## 6 非功能需求

### NFR-001 閒置資源（release gate）

使用者未互動時:CPU 佔用不可量測(取樣期間平均 &lt; 0.1%),磁碟 I/O 為零。此為封閉式發佈門檻——未量測即視為不通過。

### NFR-002 記憶體

記憶體由架構決定,不由語言決定。恰有一個條件保證上界:**只有可見 pane 的 active tab 持有 live `IExplorerBrowser`**。其餘 tab 僅以資料存在。縮圖 pipeline 的快取與尺寸上限須明確設定,不得沿用預設。實際數字見 `docs/performance-baseline.md`,全部須以量測取代估計。

### NFR-003 反應性

Group 切換、版型切換、tab 切換不得因為某個 Shell location 緩慢或無法連線而凍結 UI。

### NFR-004 DPI

Per-Monitor-V2 DPI awareness。視窗跨越不同 DPI 的螢幕時,全部 pane 正確縮放。

### NFR-005 還原精確度

**必要狀態**(版型、location、tab、view mode、排序)必須精確還原,否則視為缺陷。**best-effort 狀態**(選取項目、捲動位置、欄寬)不保證。

### NFR-006 穩定性邊界

第三方 shell extension 是 in-process 的外部程式碼。PaneDock 保證自身程式碼正確,不保證寫得差的 extension 不會 hang 或 crash 本 process。因此需要一個可抑制第三方 shell extension 的診斷模式,且崩潰復原路徑不是選配。

### NFR-007 相依性

無網路、無遙測、無第三方 runtime、無服務、無 driver、無管理員權限。

## 7 技術選型

- C++20,原生 Win32,無 UI 框架層。
- Shell COM 以 `Microsoft::WRL::ComPtr` 管理生命週期。
- 單一執行檔。使用者資料為 `%LOCALAPPDATA%\PaneDock` 下的版本化 JSON。

選型理由與被否決的替代方案(Rust、C#、C++/WinRT、Delphi、Zig、C# UI + C++ DLL 混合)記錄於 `docs/adr/0001-cpp-wrl-win32-over-rust-and-csharp.md`。

## 8 開發與執行環境

見 `AGENTS.md` 的 Validation 區塊與 `docs/development.md`。

## 9 系統架構

### 9.1 模組責任

| 模組 | 負責 | 不得負責 |
|---|---|---|
| `app_shell` | WinMain、STA 初始化、訊息迴圈、主視窗、命令路由 | 資料模型計算、Shell 呼叫 |
| `core` | Group／pane／tab 資料模型、版型矩形計算、session 序列化與遷移 | **任何 HWND、COM 或 `windows.h`** |
| `sidebar` | Group 列表繪製與切換 | Group 資料的權威狀態 |
| `explorer_host` | 每個已 realize pane 的 `IExplorerBrowser`、site 物件、生命週期、事件 | 產品層決策、持久化 |
| `shell_core` | `IShellItem`、PIDL、Shell location identity、Shell 變更通知 | 對外傳遞原始 COM 指標 |
| `file_operations` | `IFileOperation`、剪貼簿、OLE 拖放 | 直接檔案系統呼叫 |

`core` 刻意不含 COM——它是本專案主要的自動測試 seam。`shell_core` 對外提供 location 與 identity 這類值,而非原始 COM 指標,這同時保留一條退路:若 `IExplorerBrowser` 的宿主契約證明不可行,改為在 `IShellFolder`／`IContextMenu`／`IFileOperation` 之上自建清單檢視時,其上層無須重寫。

### 9.2 執行緒模型

單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。緩慢的 location 解析以 Shell 自身的非同步機制處理,不自建工作執行緒池。

### 9.3 啟動序列

1. `CoInitializeEx` STA
2. 設定 Per-Monitor-V2 DPI awareness
3. 讀取 session document(失敗則退回備份,再失敗則以預設 Group 啟動)
4. 建立主視窗與側邊欄,套用視窗位置
5. 套用 active Group 的版型,**先 realize active pane 的 active tab**
6. 其餘 pane 延後 realize,避免被網路或離線路徑阻塞

主視窗顯示後才開啟 Shell realize gate；啟動復原、不乾淨關閉、storage 或
部分 pane／drag-and-drop 失敗屬可復原問題，使用只有 `OK` 的 modeless
notification，不 disable 主視窗與 pane。deferred realize 可在 notification
顯示後繼續完成，後續 warning 更新同一通知；使用者按 `OK` 後通知關閉。
若 COM、data folder、window class 或必要 UI 建立失敗，則不顯示主視窗，改用
同步 ownerless error dialog，按下後離開 process。

### 9.4 關閉序列

1. 擷取現行狀態並原子寫入 session document
2. **destroy 全部 live `IExplorerBrowser`**(每個曾 `Initialize` 的都必須 `Destroy`)
3. destroy pane HWND
4. destroy 主視窗
5. 退出訊息迴圈
6. `CoUninitialize`

順序不可調換。view 存活期間 destroy parent HWND 是已知的崩潰面。
`WM_CLOSE` 先讓主視窗顯示 `PaneDock — Closing...` 並回到訊息迴圈一個 turn，
再開始同步 Shell teardown；
只有全部 live view destroy 且主視窗 destroy 後才退出程序。

## 10 資料儲存

- 位置:`%LOCALAPPDATA%\PaneDock`
- 格式:版本化 JSON,含 schema version,自首個版本即支援遷移
- 寫入:原子替換(`session.json.tmp` 寫入並 flush,再 rename 為 `session.json`),
  保留上一版為備份。備份由舊 primary 直接 rename 成 `session.json.bak` 產生,
  不複製資料、每次寫入只 flush 一次。
- 讀取候選順序:`session.json` → `session.json.tmp` → `session.json.bak`,
  每個都以「能完整 parse 且通過模型驗證」為閘門。採用 `.tmp` 的情況是寫入在兩次
  rename 之間被中斷,此時 `.tmp` 是磁碟上最新的完整文件,沒有遺失,不發警告;
  只有退到備份才告知使用者近期變更可能遺失。
- 寫入時機:模型變更只標記 dirty 並重設 60 秒的 debounce timer,閒置滿 60 秒才落盤。切換 tab／pane／Group 這類廉價變更因此不會每次寫檔;為避免持續操作把 timer 無限往後推,debounce 有 10 分鐘上限,超過後讓已武裝的 timer 直接燒完。以上皆為約略值,允許誤差。
  關閉 AP 時若仍為 dirty 一定強制寫入(見 9.4),因此崩潰／斷電最多遺失最後一段閒置窗內的切換狀態,正常關閉不會遺失。寫入失敗保留 dirty 並重試。
- **不得寫入 PIDL 或 COM 指標。** 持久化的 identity 為 parsing name ＋ known-folder identity ＋ fallback path。display name 永不作為 identity。

## 11 錯誤處理

- 無法解析的 location:tab 內可復原錯誤,保留設定(FR-012)
- session document 損壞:退回備份並告知(FR-013)
- startup 的可復原問題:主視窗已可用時以 modeless、只有 `OK` 的英文通知告知，
  不得以同步 `MessageBoxW` 阻塞主視窗或 pane
- startup 的致命問題:主視窗尚未可用時以同步 ownerless error dialog 告知，按下
  按鈕後清理並結束 process
- COM 失敗:記錄診斷事件,不得靜默忽略,不得使整個視窗不可用
- 第三方 extension 崩潰:不可預防,但復原路徑必須存在(NFR-006)

## 12 測試策略

`core` 是主要自動測試 seam，且必須保持不含 HWND、COM 與 `windows.h`。其他不需 live Shell view、可透過公開介面驅動的單元也應測試。由 `shell32` 定義的整合行為改在真實桌面人工驗證，不以假的 `IExplorerBrowser` 或易 flaky 的端到端 UI 自動化取代。範圍、環境與驗收流程見 `docs/testing.md`。

## 13 驗收標準

### AC-001 四分割穩定性

四個 pane 各自可獨立導覽,具備原生縮圖與右鍵選單,正常導覽期間無未處理的 COM 例外。

### AC-002 跨 pane 拖放

自一個 pane 拖檔至另一個 pane 觸發標準 Windows 行為。

#### AC-002b 對外拖放

與其他應用程式之間的拖入拖出可用。

### AC-003 生命週期

在二分割與四分割版型之間反覆切換,不持續累積 view,不破壞焦點,handle 數不單調成長。

### AC-004 還原

關閉再開啟後,版型與全部 tab 的 location 精確還原。

### AC-005 韌性

還原狀態中含一個無法連線的網路路徑時,UI 保持反應。

### AC-006 閒置資源

閒置十分鐘後量測 CPU、記憶體與 handle 數,符合 NFR-001。

## 14 未來可能重啟的方向

以獨立 process 隔離第三方 shell extension。目前明確不在範圍(§3.2),但這是唯一能真正解決 NFR-006 穩定性上界的架構,也是唯一值得重新考慮多 process 架構的情境。重啟需先有實際的 extension 崩潰紀錄。
