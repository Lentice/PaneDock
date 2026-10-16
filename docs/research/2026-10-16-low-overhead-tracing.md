# PaneDock 偶發卡頓：低負擔追蹤選型

研究日期：2026-10-16。這是研究建議，不是已實作決策，也不是卡頓根因報告。
其他檔案管理器的研究見 [Commander performance](2026-10-16-commander-performance.md)。

## 結論

建議先使用固定容量、單一 STA 寫入的記憶體事件環形緩衝區，保留最近操作；
需要 Shell 內部堆疊與等待原因時，再啟用短時間 ETW/WPR 錄製。
若決定導入標準 ETW provider，優先評估 TraceLogging，不先導入 WPP 或通用非同步文字 logger。
選擇依據是本專案的低事件量、單執行緒、LLVM-MinGW 與 idle 零 I/O 約束，
不是「自建緩衝區一定比 ETW 快」的通用結論；本次沒有量測任何方案的實際 overhead。

## 機制的關係與取捨

| 方案 | 已確認的特性 | 對 PaneDock 的判斷 |
|---|---|---|
| WPP | ETW 上的 tracing 工具鏈，支援 user-mode app；需 trace preprocessor，解碼常涉及 TMF/PDB。Microsoft 將其主要用途定位在開發除錯。[1][2] | 可以用，但新增的建置／解碼流程對本案不划算；不是因為它只能用於 driver。 |
| TraceLogging / ETW | self-describing ETW events，不需事件 manifest；事件名稱／欄位 metadata 在編譯期產生；無 listener 時先跳過事件參數求值。[3][4] | 深入診斷的優先評估方案。provider 存在不等於正在保存歷史事件，需要 collector/session。 |
| 非同步文字 logger | 寫入工作搬到 consumer 不會消除 producer 的格式化、配置、入列或儲存 I/O。這是設計成本分析，不是特定 logger 的 benchmark。 | 不先引入。需要有界佇列、明確溢出政策，且不能由 log 佇列反壓卡住 UI；新 thread／同步也增加本專案成本。 |
| 固定記憶體環形紀錄 | 容量與每筆工作量可以由格式限制；這是提案，尚未實作或量測。 | 適合低頻操作邊界。無 disk flush、無 consumer thread、無 timer，符合目前 STA 約束。程序消失時紀錄會遺失。 |

ETW 官方文件描述了 buffer 用盡時事件可被捨棄；不能把它當成絕對不丟資料的 audit log。[5]
「低 overhead」也不等於「零成本」，昂貴 payload 與大量事件依然需要控制。

## 平常記錄的具體邊界（提案）

- 固定大小 POD record：QPC timestamp、operation id、parent operation id、event id、phase、
  pane slot、navigation generation、數值形式的結果。需要識別 tab／Group 時，使用程序內 numeric token；
  不能為了 tracing 去 Shell 查 display name、解析 PIDL 或讀取檔案屬性。
- 例如 8,192 筆 × 64 bytes = 512 KiB payload；這只是容量起點，並非已驗證 record layout，
  也不保證能涵蓋固定分鐘數。metadata/header 記憶體另計。
- 只記錄操作邊界與 navigation callbacks，不記每個 item、paint、mouse move、timer tick。
- 記錄函式不可呼叫會泵訊息的 Shell API；一筆 record 完整提交後才返回，不引入 locks／atomics。
  外層 Shell 呼叫的巢狀事件用 operation id 關聯，不用「最後一個開始的操作就是同一個 tab」推測。
- 滿了覆寫最舊項，帶 sequence／覆寫計數；不能為保留 log 等待消費者。
- 手動匯出時才序列化與寫檔，範圍限制在 `%LOCALAPPDATA%\PaneDock`。
  若設計成跨重啟保留的診斷檔，需明確 schema version、符合資料保存／atomic replace 規則，
  並界定未知欄位的處理；不把它混進 session 文件。
- 「只在超時後寫一筆」無法捕捉永不返回的操作。開始事件必須先寫；慢呼叫返回後可保留該片段，
  但同一 STA 在卡住期間不能自動執行匯出指令或自救 timer。
- 記憶體紀錄不能提供 Shell DLL 內部堆疊。永久 hang 要靠外部 dump／正在進行的 trace；
  程序被結束後也不能承諾找回 app-local ring。

## ETW 與 WPR 的部署限制

ETW 的 `EVENT_TRACE_BUFFERING_MODE` 本身就是 circular memory buffer，滿後淘汰最舊事件，
不會自動寫檔或 real-time delivery；可用 FlushTrace 取 snapshot。[6]
它也能作為環形紀錄的候選，但需驗證 session 建立、容量、匯出、權限與生命週期，
不能只選 TraceLogging macro 就宣稱已完成 always-on 記錄。

Windows 10 1703 起低權限使用者可建立其啟動程序內的 private logger；
這不表示所有 system/kernel tracing 都不需權限，private logger 也有模式限制。[6][7]
若使用 WPR，另行驗證欲採 profile 的權限及工具安裝需求，不在 PaneDock 啟動時要求 admin。
深度 trace 應只覆蓋重現前後的短窗口，選需要的 CPU sampling、context-switch/wait、
storage 與 image-load 訊號；其負擔和 app 的低頻事件不是同一個量級。
WPA 的 critical-path / wait analysis 可追 UI delay 的等待鏈。[8]

2026-10-16 本機唯讀檢查：`E:\Dev\LLVM-MinGW` 內找到 `evntprov.h`、`evntrace.h`，
未找到 `TraceLoggingProvider.h`（以 `rg --files` 搜尋）。這不是已證明 TraceLogging 不相容；
正式選型前需用本專案 LLVM-MinGW/Clang 做最小 compile/link、實際 emit/decode probe。
若採 Windows SDK header 或其他 header，須先確認相容性與引入成本，不改用 MSVC 作為解法。

## 現有量測缺口與驗證

`src/app_shell/diagnostic_mode.h` 的 ScopedTiming 目前在 enabled 時以同步 WriteFile 寫 stdout；
不能當成低成本 always-on 記錄器，stdout 被接到慢 pipe／disk 時也是干擾源。
`Pane::finish_tab_change` 的 `tab_realize_ms` 只涵蓋 navigate_to 呼叫區段；
`switch_active_tab` 前面的 capture_location 與之後實際列表呈現不在同一量測內。
正常模式追蹤必須與 Microsoft-signed-only DLL 診斷政策分開控制，才能觀察真實 extension 組合。

導入後應比較 Release 的 off／memory ring／ETW collection 三組：
同資料夾、同視圖、分開 cold/warm 樣本；量 tab 操作 p50/p95/max、每次事件數、
ring 覆寫數與 ETW lost events、固定容量、10 分鐘 idle CPU／I/O。
此處不預填「小於幾微秒」或「沒有影響」；沒有實測就不能宣告通過。
起迄事件與 navigation completion 需分開，不能把 OnNavigationComplete 命名為 full render complete。

## 來源

1. [Microsoft: WPP Software Tracing](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/wpp-software-tracing)
2. [Microsoft: Survey of Software Tracing Tools](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/survey-of-software-tracing-tools)
3. [Microsoft: TraceLoggingProvider.h](https://learn.microsoft.com/en-us/windows/win32/api/traceloggingprovider/)
4. [Microsoft: TraceLoggingWrite](https://learn.microsoft.com/en-us/windows/win32/api/traceloggingprovider/nf-traceloggingprovider-traceloggingwrite)
5. [Microsoft: EventWrite](https://learn.microsoft.com/en-us/windows/win32/api/evntprov/nf-evntprov-eventwrite)
6. [Microsoft: Logging Mode Constants](https://learn.microsoft.com/en-us/windows/win32/etw/logging-mode-constants)
7. [Microsoft: Configuring and Starting a Private Logger Session](https://learn.microsoft.com/en-us/windows/win32/etw/configuring-and-starting-a-private-logger-session)
8. [Microsoft: Understand Critical Path and Wait Analysis](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/optimizing-performance-and-responsiveness-exercise-3)
