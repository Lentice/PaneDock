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
| PD-012 | 改用 LLVM-MinGW 工具鏈 | 0 | `done` | — | [PD-012](tickets/PD-012-llvm-mingw-toolchain.md) |
| PD-007 | 單一 `IExplorerBrowser` 宿主與關閉序列 | 0 | `done` | PD-012 | [PD-007](tickets/PD-007-single-explorer-host-and-shutdown.md) |
| PD-008 | 四宮格版型與矩形計算 | 0 | `done` | PD-007 | [PD-008](tickets/PD-008-four-pane-quadrant-layout.md) |
| PD-009 | active pane 指示與保活式版型切換 | 0 | `done` | PD-008 | [PD-009](tickets/PD-009-active-pane-and-layout-toggle.md) |
| PD-010 | 原型的位置持久化與還原 | 0 | `done` | PD-008 | [PD-010](tickets/PD-010-prototype-location-persistence.md) |
| PD-014 | 修正 explorer_host 的 OLE 初始化與鍵盤 accelerator 轉發 | 0 | `done` | PD-009, PD-010 | [PD-014](tickets/PD-014-explorer-host-ole-and-accelerator-wiring.md) |
| PD-011 | 原型驗收協定執行與 Go/No-Go 判定 | 0 | `done` | PD-014 | [PD-011](tickets/PD-011-prototype-acceptance-and-go-no-go.md) |
| PD-002 | 選取狀態還原可行性判定 | 0 | `done` | PD-011 | [PD-002](tickets/PD-002-selection-restore-feasibility.md) |
| PD-003 | 閒置資源量測基準 | 0 | `blocked` | PD-011 | [PD-003](tickets/PD-003-idle-resource-baseline.md) |
| PD-004 | `core` 資料模型與不變式 | 1 | `done` | PD-011 | [PD-004](tickets/PD-004-core-model-invariants.md) |
| PD-005 | 五種版型的矩形計算 | 1 | `done` | PD-004 | [PD-005](tickets/PD-005-layout-rect-computation.md) |
| PD-006 | session document 序列化與遷移 | 1 | `done` | PD-004 | [PD-006](tickets/PD-006-session-document-persistence.md) |
| PD-013 | 設定檔可擴充性慣例(跨 ticket 通用規則) | 1 | `done` | — | [PD-013](tickets/PD-013-config-file-extensibility-convention.md) |
| PD-015 | app_shell 改用 `core` 的 Group/session 型別取代原型狀態與持久化 | 2 | `done` | PD-006 | [PD-015](tickets/PD-015-app-shell-core-state-wiring.md) |
| PD-016 | 全部五種版型、可拖曳分隔線、DPI 縮放修正、鍵盤 pane 焦點切換 | 2 | `done` | PD-015 | [PD-016](tickets/PD-016-splitters-five-layouts-and-dpi-scaling.md) |
| PD-017 | Group 側邊欄:建立/重新命名/複製/刪除/重新排序/切換 | 2 | `done` | PD-015 | [PD-017](tickets/PD-017-group-sidebar.md) |
| PD-018 | `core` 每個 tab 獨立的導覽歷史(上一頁／下一頁) | 3 | `done` | PD-006 | [PD-018](tickets/PD-018-tab-navigation-history.md) |
| PD-019 | 每個 pane 的 tab 條:新增/關閉/切換,接上 realize-on-activation | 3 | `done` | PD-017 | [PD-019](tickets/PD-019-tab-strip-and-realize-on-activation.md) |
| PD-020 | 每個 pane 的網址列與上一頁/下一頁/上層按鈕 | 3 | `done` | PD-018, PD-019 | [PD-020](tickets/PD-020-address-bar-and-navigation-buttons.md) |
| PD-021 | 送往 active pane 的鍵盤快速鍵:切換/新增/關閉 tab、上一頁/下一頁/上層 | 3 | `done` | PD-019, PD-020 | [PD-021](tickets/PD-021-active-pane-keyboard-shortcuts.md) |
| PD-022 | 不可解析 location 的可復原錯誤狀態與重試 | 4 | `done` | PD-020 | [PD-022](tickets/PD-022-unresolvable-location-error-and-retry.md) |
| PD-023 | 檔案操作、剪貼簿與拖放的驗收(FR-007／FR-008) | 4 | `done` | PD-019, PD-021, PD-022 | [PD-023](tickets/PD-023-shell-file-operations-acceptance.md) |
| PD-024 | 診斷模式:抑制第三方 shell extension(NFR-006) | 5 | `done` | PD-015 | [PD-024](tickets/PD-024-diagnostic-mode-suppressing-shell-extensions.md) |
| PD-025 | 崩潰復原路徑:不乾淨關閉偵測、退回備份告知、備份保護(FR-013) | 5 | `done` | PD-024 | [PD-025](tickets/PD-025-crash-recovery-path.md) |
| PD-026 | release evidence 改為量測完整應用程式而非 Phase 0 原型 | 5 | `done` | PD-023 | [PD-026](tickets/PD-026-release-evidence-covers-shipped-app.md) |
| PD-027 | MVP 驗收清單在四種必要環境的執行與發佈閘門判定 | 5 | `done` | PD-024, PD-025, PD-026 | [PD-027](tickets/PD-027-mvp-acceptance-and-release-gate.md) |
| PD-032 | 系統關機／登出／重開機時誤報「不乾淨關閉」的修正 | 5 | `done` | PD-025 | [PD-032](tickets/PD-032-endsession-clean-shutdown-handling.md) |
| PD-028 | 側邊欄品牌列、Group 兩行摘要與 footer 按鈕改版 | 6 | `done` | PD-017 | [PD-028](tickets/PD-028-sidebar-brand-and-group-summary-restyle.md) |
| PD-029 | Quiet header 右對齊、版型圖示重繪與 more-actions 佔位按鈕 | 6 | `done` | PD-028 | [PD-029](tickets/PD-029-quiet-header-alignment-and-layout-icons.md) |
| PD-030 | Pane 卡片背景(圓角上緣＋陰影)與 tab header 圖示化重繪 | 6 | `done` | PD-028, PD-029 | [PD-030](tickets/PD-030-pane-card-chrome-and-tab-header-restyle.md) |
| PD-031 | 導覽列圖示化按鈕與圓角網址欄背景 | 6 | `done` | PD-030 | [PD-031](tickets/PD-031-navigation-row-icon-restyle.md) |
| PD-033 | Active pane 指示改為扁平彩色外框,移除舊式立體邊框 | 6 | `done` | PD-030 | [PD-033](tickets/PD-033-active-pane-flat-border-indicator.md) |
| PD-034 | 拖曳懸停自動切換(側邊欄 Group 列與 pane 的 tab) | 7 | `done` | PD-017, PD-019, PD-028 | [PD-034](tickets/PD-034-drag-hover-auto-switch.md) |
| PD-035 | Tab 拖拉排序(同一 pane 內) | 7 | `done` | PD-019 | [PD-035](tickets/PD-035-tab-drag-reorder.md) |
| PD-036 | Group 拖拉排序(覆寫 PD-017 決策 5) | 7 | `done` | PD-017, PD-028 | [PD-036](tickets/PD-036-group-drag-reorder.md) |
| PD-037 | Tab 動態寬度(比照 Chrome 縮放＋上限) | 6 | `done` | PD-030 | [PD-037](tickets/PD-037-dynamic-tab-width.md) |
| PD-038 | Pane 初次載入畫面空白,需要 hover 才刷新的修正 | 6 | `done` | PD-030 | [PD-038](tickets/PD-038-pane-blank-until-hover.md) |
| PD-039 | 移除「PANE LAYOUT」文字標籤,版面配置按鈕改用 tooltip | 6 | `done` | PD-029 | [PD-039](tickets/PD-039-layout-label-removal-and-tooltip.md) |
| PD-040 | Pane 卡片四角圓角(覆寫 PD-030 決策:下緣維持方角) | 6 | `done` | PD-030, PD-033 | [PD-040](tickets/PD-040-pane-card-full-corner-rounding.md) |
| PD-041 | 主視窗缺少 `WS_CLIPCHILDREN`,全視窗重繪蓋掉 pane 內容 | 6 | `done` | PD-038 | [PD-041](tickets/PD-041-main-window-missing-clipchildren.md) |
| PD-042 | Pane 容器誤圓角化內部邊界頂端兩角,active 外框轉角瑕疵 | 6 | `done` | PD-040 | [PD-042](tickets/PD-042-pane-container-top-corner-seam.md) |
| PD-043 | 導覽按鈕(上一頁/下一頁/上一層)改用 Windows Explorer 風格圖示 | 6 | `done` | PD-031 | [PD-043](tickets/PD-043-navigation-button-explorer-style-icons.md) |
| PD-044 | 網址列輸入時顯示子資料夾自動完成下拉選單 | 6 | `done` | PD-020 | [PD-044](tickets/PD-044-address-bar-autocomplete.md) |
| PD-045 | Active pane 下緣外框線被容器裁切,粗細與上緣不一致 | 6 | `done` | PD-041, PD-042 | [PD-045](tickets/PD-045-pane-card-bottom-border-clipped.md) |
| PD-046 | 版面配置按鈕群改為視覺相連的分段控制 | 6 | `done` | PD-029, PD-039 | [PD-046](tickets/PD-046-layout-buttons-segmented-control.md) |
| PD-047 | 版面配置按鈕的選中態高亮對比度不足 | 6 | `done` | PD-046 | [PD-047](tickets/PD-047-layout-button-active-highlight-contrast.md) |
| PD-048 | Pane 卡片外框/陰影顏色偏重,需調淡 | 6 | `done` | PD-045 | [PD-048](tickets/PD-048-pane-card-border-shadow-lightening.md) |
| PD-049 | Pane tab 條改為自繪控制項,取代原生 `SysTabControl32`(覆寫 PD-019 決策 1) | 6 | `done` | PD-037, PD-030 | [PD-049](tickets/PD-049-custom-tab-strip-control.md) |
| PD-050 | 在新自繪 tab 條上重接拖曳懸停自動切換與拖曳排序 | 7 | `done` | PD-049, PD-034, PD-035 | [PD-050](tickets/PD-050-tab-drag-behaviors-on-custom-strip.md) |
| PD-051 | 每個 pane 增加狀態列(項目數/選取數) | 7 | `done` | PD-007, PD-030 | [PD-051](tickets/PD-051-pane-status-bar.md) |
| PD-052 | Pane 增加 refresh 按鈕與檢視樣式切換按鈕,補上 `TabState::view_mode` 還原缺口 | 7 | `done` | PD-020, PD-006 | [PD-052](tickets/PD-052-pane-refresh-and-view-mode-switcher.md) |
| PD-053 | 側邊欄品牌列與 Group 清單之間的分隔線造成視覺割裂 | 6 | `done` | PD-028 | [PD-053](tickets/PD-053-sidebar-brand-divider-removal.md) |
| PD-054 | 設計 App icon 並取代側邊欄品牌列的手繪「+」圖示 | 6 | `done` | PD-028 | [PD-054](tickets/PD-054-app-icon-asset-and-brand-bar-wiring.md) |
| PD-055 | Tab 條 `STATIC` 缺 `SS_NOTIFY`,滑鼠訊息不送達,tab 無法點擊切換 | 7 | `done` | PD-049, PD-050 | [PD-055](tickets/PD-055-tab-strip-static-ss-notify-missing.md) |
| PD-056 | 切換版型後舊的 active 版型按鈕未重繪,highlight 沒清除 | 7 | `done` | PD-047 | [PD-056](tickets/PD-056-layout-button-stale-highlight.md) |
| PD-057 | Group 拖曳在 button-down 搶走 LISTBOX capture,通知變成 `LBN_SELCANCEL`,無法切換 Group | 7 | `done` | PD-036 | [PD-057](tickets/PD-057-group-list-selection-notification-suppressed.md) |
| PD-058 | 版型按鈕/tab/「+」/Group 列補上滑鼠 hover 視覺回饋 | 7 | `done` | PD-055, PD-046 | [PD-058](tickets/PD-058-hover-feedback-for-interactive-chrome.md) |
| PD-059 | 檢視模式按鈕改為下拉選單,取代單向循環切換 | 7 | `done` | PD-052 | [PD-059](tickets/PD-059-view-mode-dropdown-menu.md) |
| PD-060 | Pane 狀態列補上選取檔案總大小,並加上分隔線/底色 | 7 | `done` | PD-051 | [PD-060](tickets/PD-060-pane-status-bar-selection-size-and-separator.md) |
| PD-061 | 側邊欄 Group 名稱與副標題字級太小,改用系統 UI 字型 | 7 | `done` | PD-028 | [PD-061](tickets/PD-061-sidebar-group-typography.md) |
| PD-062 | Tab 改圓角外框,拆分 padding/gap/列高,「+」加大加粗 | 7 | `done` | PD-049, PD-055 | [PD-062](tickets/PD-062-tab-strip-visual-polish.md) |
| PD-063 | Active pane 圓角外框鋸齒,改用不依賴圓角描邊的強調方式 | 7 | `done` | PD-040, PD-048 | [PD-063](tickets/PD-063-active-pane-highlight-aliasing.md) |
| PD-064 | 移除無作用的 more-actions「...」按鈕,修正 Up 圖示對齊 | 7 | `done` | PD-029, PD-043 | [PD-064](tickets/PD-064-header-and-nav-icon-cleanup.md) |
| PD-065 | `EBO_NOBORDER` 在 `Initialize` 之後才設定,檔案區殘留深色細外框 | 7 | `done` | PD-040, PD-048 | [PD-065](tickets/PD-065-explorer-view-residual-border.md) |
| PD-066 | 拖曳排序改用撐開空位的 placeholder;修復 tab 插入指示線死碼 | 7 | `done` | PD-055, PD-050, PD-036 | [PD-066](tickets/PD-066-drag-reorder-placeholder-gap.md) |
| PD-067 | Group 清單捲軸視覺存在感過低(捲動功能實測已正常) | 7 | `superseded` | PD-028, PD-061 | [PD-067](tickets/PD-067-group-list-scrollbar-visibility.md) |
| PD-068 | 關閉程式時在 `SHELL32.dll` 當機:`SetCallback` 的 out 參數傳 `nullptr` | 7 | `done` | PD-051 | [PD-068](tickets/PD-068-shell-folder-view-setcallback-null-out-param.md) |
| PD-069 | pane footer 補左右內距;chrome 間距統一為 4px 級距 | 7 | `done` | PD-060, PD-062, PD-064 | [PD-069](tickets/PD-069-chrome-spacing-scale.md) |
| PD-070 | 診斷模式擋下第三方 extension 時跳出載入器模態對話框,無法無人值守執行 | 7 | `done` | PD-024 | [PD-070](tickets/PD-070-diagnostic-mode-loader-error-dialogs.md) |
| PD-071 | 移除 Ctrl+Shift+L 熱鍵:註冊失敗會讓程式完全無法啟動 | 7 | `done` | PD-009 | [PD-071](tickets/PD-071-remove-layout-hotkey.md) |
| PD-072 | chrome 字型改用固定 Latin 字面＋系統字型連結,取代語系相依的 `DEFAULT_GUI_FONT`／`lfMessageFont`(覆寫 PD-061 的 face 來源) | 7 | `done` | PD-061 | [PD-072](tickets/PD-072-replace-stock-gui-font.md) |
| PD-073 | Tab 溢出時變成零寬矩形完全無法點擊;加左移/右移捲動按鈕 | 7 | `done` | PD-055, PD-062, PD-066 | [PD-073](tickets/PD-073-tab-overflow-scroll-buttons.md) |
| PD-074 | 拖曳排序看不到「拖的是哪一個」:placeholder 內畫出淡化的被拖曳項目 | 7 | `done` | PD-066, PD-061, PD-062 | [PD-074](tickets/PD-074-drag-placeholder-shows-dragged-item.md) |
| PD-075 | Pane 導覽列五個圖示由三種技術繪製,統一到 `Segoe MDL2 Assets` | 7 | `done` | PD-052, PD-064 | [PD-075](tickets/PD-075-unify-pane-chrome-icon-style.md) |
| PD-076 | Group 與 pane tab 的 active/hover 樣式不一致,以 Group 現有樣式為準套用到 tab | 7 | `done` | PD-061, PD-062, PD-072 | [PD-076](tickets/PD-076-unify-group-tab-active-hover-style.md) |
| PD-077 | 拖曳調整主視窗大小時,pane 狀態列在舊位置留下殘影 | 7 | `done` | PD-041, PD-042 | [PD-077](tickets/PD-077-pane-footer-resize-repaint-ghost.md) |
| PD-078 | 確保電腦當機/斷電時 session.json 不會毀損 | 7 | `done` | PD-006 | [PD-078](tickets/PD-078-crash-safe-session-write.md) |
| PD-079 | 檢視模式選單擴充為 8 項,對齊真實檔案總管;僅「詳細資料」顯示欄位標題(覆寫 PD-059 決策 3) | 7 | `done` | PD-059 | [PD-079](tickets/PD-079-view-mode-menu-eight-items-and-column-header.md) |
| PD-080 | Tab 溢出捲動按鈕尺寸過大,和 tab 一樣高,改為緊湊小按鈕 | 7 | `done` | PD-073 | [PD-080](tickets/PD-080-tab-scroll-buttons-oversized.md) |
| PD-081 | Tab「+」按鈕手繪十字有缺角,改回字型字符繪製(覆寫 PD-062 決策 5) | 7 | `done` | PD-062 | [PD-081](tickets/PD-081-tab-add-button-glyph-notch.md) |
| PD-082 | 非「詳細資料」檢視模式仍顯示 column header | 7 | `done` | PD-079 | [PD-082](tickets/PD-082-view-mode-header-leaks-into-non-details-modes.md) |
| PD-083 | 導覽按鈕/New Group 按鈕/tab 捲動按鈕完全沒有 hover;版型按鈕 hover 對比度不足 | 7 | `done` | PD-058, PD-047 | [PD-083](tickets/PD-083-remaining-buttons-missing-or-weak-hover.md) |
| PD-084 | 限制單一 App 實例;第二次啟動改為喚醒既有視窗 | 7 | `done` | 無 | [PD-084](tickets/PD-084-single-instance-activate-existing-window.md) |
| PD-085 | 把 tab 捲動按鈕的幾何計算抽成純函式(可單元測試) | 7 | `done` | 無 | [PD-085](tickets/PD-085-tab-scroll-button-geometry-to-pure-module.md) |
| PD-086 | 切換 Group 時,同步完成的導覽把舊 Group 的路徑寫進新 Group 的儲存分頁(資料遺失) | 7 | `done` | 無 | [PD-086](tickets/PD-086-group-switch-overwrites-incoming-folders-with-outgoing-live-state.md) |
| PD-087 | 版型縮小時被隱藏的 pane 沒有 destroy `IExplorerBrowser`,放大回去顯示過期資料夾 | 7 | `done` | 無 | [PD-087](tickets/PD-087-layout-shrink-leaks-hidden-explorerhost.md) |
| PD-088 | `Site`/`ViewCallback` 持有的 `ExplorerHost*` 在 destroy 後未清空,延遲回呼可能觸碰已銷毀物件 | 7 | `done` | 無 | [PD-088](tickets/PD-088-explorerhost-callback-use-after-destroy.md) |
| PD-089 | PD-084 單一實例喚醒常見情境下靜默失敗(`SetForegroundWindow` 被拒絕、輪詢逾時無回饋) | 7 | `done` | PD-084 | [PD-089](tickets/PD-089-single-instance-activation-unreliable.md) |
| PD-090 | 拖曳懸停自動切換 Group/tab 在 OLE 拖曳迴圈內同步做 Shell view 建立/銷毀與 session 寫入 | 7 | `done` | PD-034 | [PD-090](tickets/PD-090-drag-hover-group-switch-reenters-ole-drag-loop.md) |
| PD-091 | 每次資料夾導覽完成都同步寫入 session(多次 JSON 解析＋兩次強制 flush) | 7 | `done` | PD-078 | [PD-091](tickets/PD-091-session-save-synchronous-on-every-navigation.md) |
| PD-092 | `apply_layout` 單一 pane 初始化失敗時整段退出,遺留未完成排版與不一致狀態 | 7 | `done` | 無 | [PD-092](tickets/PD-092-apply-layout-aborts-mid-loop-on-pane-init-failure.md) |
| PD-093 | 啟動時同步 realize 所有可見 pane,違反 spec §9.3 延後 realize 規則 | 7 | `done` | 無 | [PD-093](tickets/PD-093-startup-eagerly-realizes-all-panes-violates-deferred-realize-spec.md) |
| PD-094 | ~~啟動時讀完 session 立刻做一次完全冗餘的同步寫回~~(前提有誤,已撤回,見 ticket 文件與計畫決策紀錄) | 7 | `deferred` | 無 | [PD-094](tickets/PD-094-redundant-session-save-before-main-window-created.md) |
| PD-095 | 拖曳分隔線/縮放視窗時,每個 mousemove 都跑完整 `apply_layout`,含最多 1000 項 Shell property 重掃 | 7 | `done` | 無 | [PD-095](tickets/PD-095-splitter-drag-full-relayout-and-item-count-rescan-per-mousemove.md) |
| PD-096 | `draw_brand_bar` 每次 `WM_ERASEBKGND` 都重新載入圖示、建立/刪除字型 | 7 | `done` | 無 | [PD-096](tickets/PD-096-brand-bar-recreates-icon-and-font-every-erasebkgnd.md) |
| PD-097 | 拖曳分隔線時,幾何重排本身仍在每個 `WM_MOUSEMOVE` 都執行,需節流 | 7 | `ready` | PD-095 | [PD-097](tickets/PD-097-splitter-drag-geometry-throttling.md) |
| PD-098 | 檢視模式按鈕圖示由 4 方格(GridView)改為清單樣式(List)圖示 | 7 | `ready` | PD-075 | [PD-098](tickets/PD-098-view-mode-icon-grid-to-list-glyph.md) |
| PD-099 | 新增第 6 種版型:上方兩格併排、下方一格全寬(2 up / 1 down) | 7 | `ready` | PD-005, PD-016, PD-046 | [PD-099](tickets/PD-099-add-two-over-one-layout-template.md) |

## Dependency lanes

```text
Phase 0 — Go/No-Go gate
  PD-012 (LLVM-MinGW toolchain)
    └─ PD-007 (single host + shutdown sequence)
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
         └─ PD-015 (app_shell wired to core state + session, single Group, two layouts)
              ├─ PD-016 (all five layouts, draggable splitters, DPI scaling, F6 pane focus)
              └─ PD-017 (Group sidebar: create/rename/duplicate/delete/reorder/switch)
                   └─ PD-019 (tab strip UI, realize-on-activation within a pane)

Phase 3 — tabs and navigation, gated on PD-006/PD-017
  PD-006 ─── PD-018 (core per-tab back/forward history)
  PD-017 ─── PD-019 (tab strip: add/close/switch)
       PD-018 + PD-019 ─── PD-020 (address bar, back/forward/parent)
                 PD-019 + PD-020 ─── PD-021 (keyboard shortcuts to active pane)

Phase 4 — shell operations, gated on PD-020/PD-021
  PD-020 ─── PD-022 (unresolvable location: error panel + retry)
       PD-019 + PD-021 + PD-022 ─── PD-023 (file ops / clipboard / drag-drop acceptance)
  註:Phase 4 前三個 roadmap 條列(IFileOperation、剪貼簿、拖放)由原生 Shell view
      直接提供,不重新實作,因此只有一張實作票(PD-022)加一張驗收票(PD-023)。

Phase 5 — release gate,gated on Phase 4 完成
  PD-015 ─── PD-024 (診斷模式:MicrosoftSignedOnly,同一 process 內抑制 extension)
                └─ PD-025 (不乾淨關閉偵測 + 退回備份告知 + backup 不被損壞檔覆寫)
  PD-023 ─── PD-026 (release evidence 與 performance-baseline 對齊完整應用程式)
       PD-024 + PD-025 + PD-026 ─── PD-027 (MVP 驗收 × 四種環境 + 閘門判定)
  註:PD-003(閒置資源量測)自 Phase 0 起 `blocked`,阻塞條件是「人類在真實互動桌面
      執行 `-CollectMeasurements`」。PD-026 只修工具,不解除它;真正解除它的是 PD-027
      的量測執行。PD-024/026 可並行,兩者都不需要互動桌面即可完成程式碼與工具部分。
```

Phase 6 — 視覺改版,對照 `docs/panedock-ui-prototype.html`(Quiet Header 變體),不改變任何 Group/pane/tab 行為或 core 契約
  PD-017 ─── PD-028(側邊欄品牌列 + Group 兩行摘要 + footer 改版,含 GroupSummary 擴充與 context menu)
       PD-028 ─── PD-029(quiet header 右對齊 + 版型圖示重繪 + more-actions 佔位)
            PD-029 ─── PD-030(pane 卡片背景 + tab header 圖示化,owner-draw tab strip)
                 PD-030 ─── PD-031(導覽列圖示按鈕 + 圓角網址欄背景)
  註:四張票依視覺依賴順序串接(PD-030 的卡片背景繪製路徑是 PD-031 疊加導覽列圓角底的唯一插入點),
      但技術上彼此獨立、風險遞增(PD-028/029 純排版與 owner-draw 按鈕,PD-030 觸碰既有 `SysTabControl32`
      owner-draw 改造風險最高)。若時間有限,可只做到 PD-028/029 就先驗收,PD-030/031 分開排期。

  PD-030 ─── PD-033(active pane 邊框改為扁平彩色,取代舊式 `WS_EX_CLIENTEDGE`)

Phase 7 — 拖放互動,對照使用者 2026-08-25 grilling session 提出的六項需求
  PD-017 + PD-019 + PD-028 ─── PD-034(拖曳懸停自動切換:側邊欄 Group 列 + pane 的 tab,內部/外部來源皆支援)
  PD-019 ─── PD-035(tab 拖拉排序,僅限同一 pane 內)
  PD-017 + PD-028 ─── PD-036(Group 拖拉排序,覆寫 PD-017 決策 5)
  註:PD-034 用 `IDropTarget`/OLE 拖放偵測檔案懸停;PD-035/036 用滑鼠事件偵測清單/tab 項目本身的拖曳,
      刻意不用 OLE 拖放,兩種機制分開設計以避免同一個側邊欄 `LISTBOX` 上互相干擾——PD-036 的文件
      要求實作前需讀過 PD-034 與 PD-035 兩張票。三張票彼此技術獨立,可任意順序或並行實作。

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
| PD-067 的 `LBS_DISABLENOSCROLL` + pill 右側內距加大(4px→14px) | 2026-08-27 使用者實機驗收 | 使用者認為改回原本樣式(無 `LBS_DISABLENOSCROLL`、右側內距 4px)即可,已在 `src/sidebar/sidebar.cpp` revert。要重開需先有使用者具體回報捲軸仍不可見的情境。 |

**2026-08-24 註記(PD-024 撰寫時的界線確認)**:PD-024「診斷模式:抑制第三方 shell extension」**沒有**重開上表最後一列,也沒有重開 `docs/design-spec.md` §3.2／§14 的「以獨立 process 隔離第三方 shell extension」。兩者目標不同:NFR-006 要的是「可**抑制**」——讓 extension 不要載入本 process,用來歸因崩潰;被否決的是「**隔離**」——讓 extension 在另一個 process 崩潰而不影響我們。PD-024 的手段是同一 process 內的 `SetProcessMitigationPolicy(ProcessSignaturePolicy)` `MicrosoftSignedOnly`,不新增 process、不新增 IPC、不寫 registry。若日後真的累積到實際的 extension 崩潰紀錄,要開的是另一張獨立 process 的票,並依本節規則寫出覆寫與新證據。

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
| ~~診斷模式:抑制第三方 shell extension~~ | **已於 2026-08-24 開票(PD-024),不再是候選。** 手段為同一 process 內的 `MicrosoftSignedOnly` binary signature policy ＋ `--diagnostic` 命令列旗標;與被否決的「獨立 process 隔離」是不同方向,見上節 2026-08-24 註記。 |
| 以獨立 process 隔離第三方 shell extension | `docs/design-spec.md` §14 保留的方向,目前明確不在範圍。觸發條件不變:**先有實際的 extension 崩潰紀錄**。PD-024 的診斷模式正是產生那份紀錄(「一般模式崩潰、`--diagnostic` 不崩潰」)的工具;累積到具體案例再依 §已否決的方向 的規則開票。 |
| 產品內的計時儀器(Group 切換／tab realize／cold start 延遲) | PD-026(2026-08-24)刻意排除:三者都沒有 blocking 門檻,加儀器要動產品程式碼。若使用者實際回報切換有感延遲,再開票加 `QueryPerformanceCounter` 量測點,屆時 `docs/performance-baseline.md` 對應列才有數字可填。 |
| 崩潰迴圈的自動安全模式(連續 N 次不乾淨關閉即自動以 `--diagnostic` 啟動) | PD-025(2026-08-24)刻意排除:沒有真實崩潰資料前 N 是憑空調的,且自動重啟需要 `CreateProcess`,會在單一 process 架構上開一個口子。若使用者實際遇到崩潰迴圈再開票。 |
| 縮圖 pipeline 的快取與尺寸上限 | 待 PD-003 量出縮圖對記憶體的實際貢獻後再開,避免憑估計調參數。 |
| `IShellFolder` 自建清單檢視(fallback) | 僅在 PD-001 判定 No-Go 時開。 |
| 統一 header 版型按鈕圖示與導覽列圖示的筆畫粗細 | PD-075(2026-08-26)刻意排除:版型按鈕的五個圖示是**版面示意圖**(一格／雙欄／上下／2×2／更多),沒有任何 `Segoe MDL2 Assets` 字符能表達「這個版型長什麼樣」,只能手繪。但 `draw_layout_glyph` 用 `CreatePen(PS_SOLID, 1, ...)` 而 `draw_navigation_icon_button` 是 2px,兩者並列時粗細不同是真的。觸發條件:PD-075 完成後若使用者仍覺得 header 與 pane 的圖示不成套,再開票調整手繪線寬(注意 1px 是 `RoundRect` 版面示意圖能保持清晰的實際上限,加粗可能反而糊掉,屆時需先截圖比對)。 |
| 側邊欄寬度的全域設定持久化 | 若使用者回報每次啟動都要重拖再開;目前預設值可接受。 |
| Group 圖示與顏色 | Spec 未列為 MVP;若 Group 數量成長到難以用文字辨識再開。 |
| ~~每個 pane 的導覽列(上一頁／下一頁／上一層按鈕 ＋ editable path bar)~~ | **已於 2026-08-24 開票,不再是候選。** 2026-08-20 使用者提出時 Phase 0 尚無 tab/Group/pane chrome 基礎設施;Phase 2 完成後基礎設施到位,拆成 PD-018(核心導覽歷史)、PD-019(tab 條)、PD-020(網址列與導覽按鈕)三張,PD-021 補上對應鍵盤快速鍵。 |
| 拖到 tab 標題以複製到該 tab 的資料夾 | PD-023(2026-08-24)列為 non-goal:這是 Windows 檔案總管沒有的加值互動,spec 未要求。若使用者實際使用後想要,再開 ticket。 |
| 網址列自動完成(`IAutoComplete2`) | PD-020(2026-08-24)刻意排除,只做純 `EDIT` + Enter。若使用者實際使用後認為缺自動完成造成明顯不便,再開 ticket 接 Shell 的 `IAutoComplete2`,不預先做。 |
| 讓 `compute_layout_rects` 接收 DPI 縮放後的最小尺寸／分隔線厚度,取代目前寫死的 96-DPI 基準常數 | PD-005 2026-08-24 交接發現:`docs/design-spec.md` §FR-004a 的敘述預期呼叫端傳入「已按 DPI 縮放過的最小值常數」,但 PD-005 定義的函式簽章只收 client size、版型、比例,常數是寫死在 `src/core/layout.h` 的 96-DPI 基準值,呼叫端目前無法覆寫。等 app_shell 接上 Per-Monitor-V2 `WM_DPICHANGED`(NFR-004)且需要跨 DPI 正確縮放時開票,把最小尺寸/分隔線厚度改成函式參數,矩形演算法本身不需要動。 |
| 啟動時的不乾淨關閉標記(`clean_shutdown`)改用比整份 `write_session` 更輕量的單欄位寫入 | PD-094(2026-08-27)撤回時發現:目前啟動時仍會呼叫完整 `save_now`/`write_session` 管線(重新解析整份 `preserved_json`+雙 flush+備份複製)只為了把 `clean_shutdown` 這一個布林欄位改成 `false`,PD-025 決策 2 已判定這次寫入本身可接受,但沒有評估過「用更輕量的手段達成同一個標記語意」。觸發條件:先用 PD-026 排除的計時儀器或使用者實際回報量到啟動延遲確實可感知,再開票設計專屬的輕量標記寫入(需注意仍要遵守 `AGENTS.md` 的 atomic-replace 與 schema 前向相容規則,不能只是省略 flush)。 |
| 把 PD-095 的「幾何重排/內容重算」分離套用到 `WM_SIZE`(拖曳主視窗邊框即時縮放) | PD-095(2026-08-27)只把 `update_splitter_drag` 的逐幀呼叫改成幾何模式,`WM_SIZE`(`main.cpp:3809-3811` 一帶)仍呼叫預設的完整內容重算 `apply_layout`。這是同一類熱路徑(Windows 預設的 live resize 會在拖曳視窗邊框期間連續送出 `WM_SIZE`),PD-095 原始三方發現的背景段落也點名過,但 Scope/Acceptance Criteria 最終只鎖定分隔線拖曳,是撰票時的範圍疏漏,不是實作缺陷。觸發條件:若使用者實際感受到拖曳視窗邊框縮放時的卡頓,可直接沿用 PD-095 已經做好的 `apply_layout(window, state, false, recompute_content)` 參數,讓 `WM_SIZE` 也在連續縮放期間傳 `false`,只在縮放結束(`WM_EXITSIZEMOVE`)時做一次完整重算。 |

## 計畫決策紀錄

### 2026-08-27 — 三個 agent(Claude/Codex/OpenCode)平行執行 `improve-codebase-architecture`,新增 PD-085

在同一份 codebase 上同時開三個 Herdr tab,分別以 Claude、Codex、OpenCode 各自執行架構審查(Claude/Codex 用各自的 skill/custom-prompt 機制,OpenCode 沒有對應命令,改用等價的純文字指示)。三份報告(存於系統 Temp 目錄,不進 repo)獨立得出同一個結論:`src/app_shell/main.cpp`(4051 行,81/150 個 source commit 集中於此)是專案的 god module,且都點名同一個最高槓桿切片——chrome 繪製函式把「純幾何計算」與「HDC 繪製」揉在一起,PD-073/080/081 這類像素微調 commit 正是這個缺陷的直接證據。三份報告都引用專案既有的 `tab_overflow.h`(`tab_strip_viewport`/`clamp_tab_scroll_offset`,已有 `static_assert` 測試)作為已驗證可行的正確模式,建議推廣到其餘 chrome 幾何計算。開票時刻意只取其中最小、最低風險的一塊(`draw_tab_scroll_button` 的視覺矩形/圓角/箭頭端點計算)為 [PD-085](tickets/PD-085-tab-scroll-button-geometry-to-pure-module.md),其餘候選(layout-button glyph 幾何、status-bar compartments、pane-card 圓角、拖曳排序 slot 計算、`AppState` 拆分)留待後續個別開票,避免單票過大。本票明確定調為**純重構,不改變任何視覺輸出數值**。

### 2026-08-27 — 新增 PD-084(單一實例限制)

使用者需求:「only one ap instance allowed. activate the window when 2nd executed. notice the race condition.」全新需求,`docs/design-spec.md`／已否決的方向皆無相關記載。根因分析:`save_now` 是整份 `ApplicationState` 覆寫寫入 `session.json`(atomic replace),若允許多實例同時執行,後寫入的實例會用自己記憶體內的完整快照覆蓋先寫入實例的所有變更(last-writer-wins on the whole document),不是欄位級衝突而是整份設定檔遺失。決策採具名 kernel mutex(而非 `FindWindow` 標題比對——診斷模式視窗標題與正常模式不同,標題比對會漏判),第二實例偵測到已有實例時只喚醒既有視窗、完全不觸碰 session 讀寫即結束;啟動期間目標視窗尚未建立完成的短暫空窗期用有限次數輪詢處理,不做成 busy loop。開票為 [PD-084](tickets/PD-084-single-instance-activate-existing-window.md)。

### 2026-08-27 — 三個 agent(Claude/Codex/OpenCode)平行執行全面稽核(race condition/架構/control flow),新增 PD-086~092

同一份 codebase 上再次同時開三個 Herdr tab,這次不限於過度工程,而是分別以 Claude、Codex、OpenCode 各自對 `src/`、`tests/` 做唯讀稽核,涵蓋 race condition/reentrancy、架構、control-flow 正確性,over-engineering 為次要項目。三份報告(Codex/OpenCode 直接輸出於終端機;Claude 因終端機使用替代畫面、`recent-unwrapped` 讀不到完整內容,改請其把完整報告寫成暫存 Markdown 檔後讀取,檔案未進 repo)獨立產出 15(Claude)/12(OpenCode)/7(Codex)項發現,多項發現在兩到三份報告中**各自獨立**收斂到同一根因,信心最高的收斂項目據此開票:

- **PD-086**(Claude #1 + Codex #2 收斂,信心最高):`activate_group` 切換 Group 時,若 `IExplorerBrowser::BrowseToObject` 同步完成,`OnNavigationComplete` 會在 pane 導覽迴圈跑到一半、`active_group_id` 已指向新 Group 但其餘 pane 仍顯示舊 Group 資料夾時觸發 capture,把舊 Group 的路徑寫進新 Group 的儲存分頁並持久化——靜默資料遺失,直接打破「一鍵還原完整版面配置」的產品核心承諾。
- **PD-087**(Claude/Codex/OpenCode 三方一致):版型縮小時,`apply_layout` 隱藏多餘 pane 只 `ShowWindow(SW_HIDE)`,未呼叫 `destroy()`,違反 `AGENTS.md`「只有可見 pane 的 active tab 持有 live view」;版型放大回去時因為已標記 `realized` 而跳過重新導覽,顯示縮小前的過期資料夾。
- **PD-088**(Claude #3 + Codex #3 收斂):`ExplorerHost::destroy()` 未清空 `Site`/`ViewCallback` 內部指回 host 的原始指標,Shell 端(`CDefView`/extension)延遲持有的參照可能在 destroy 後仍觸發回呼,觸碰邏輯上已銷毀的物件。
- **PD-089**(Claude/Codex/OpenCode 三方一致,且是本次稽核中最新、最值得立即處理的一項——標的是剛完成的 PD-084):第二實例喚醒第一實例時直接呼叫 `SetForegroundWindow`,在呼叫方不擁有前景權限的常見情境(使用者雙擊圖示、PaneDock 在背景)下會被 Windows 靜默拒絕,PD-084 交接區已誠實記錄「未取得截圖驗證前景化」,本次稽核確認這不是驗證缺口而是邏輯缺陷。
- **PD-090**(Claude #4 + OpenCode #4 收斂):拖曳懸停自動切換(PD-034)的 `WM_TIMER` 在 `DoDragDrop` 的 OLE 拖曳迴圈重入期間觸發,同步做 Shell view 建立/銷毀與 session 寫入,是 `AGENTS.md` 明確點名的高風險重入路徑。
- **PD-091**(Claude #6 + OpenCode #2 收斂):每次導覽完成都同步呼叫 `save_now`,單次寫入含 3 次 JSON 解析與 2 次強制 `FlushFileBuffers`,Group 切換時可疊乘到 4 次,在 UI 執行緒上執行,與 PD-090 的拖曳重入路徑疊加風險更高。
- **PD-092**(Claude #13 + OpenCode #7 收斂):`apply_layout` 迴圈中單一 pane 初始化失敗就整段 `return`,其餘健康 pane 連帶維持未完成排版,沒有錯誤降級。

未開票的單一 agent 專屬發現(例如 Claude 指出的 `scaled_value` 最小 1px 箝制讓「歸零」的間距常數失效、`ExplorerHost::navigate` 回傳值恆為 `S_OK` 使呼叫端錯誤處理成為死碼、session parser 無遞迴深度上限;OpenCode 指出的 known-folder identity/fallback 欄位從未被填入、`explorer_host_lifetime_check` 測試未註冊進 ctest)因為只有單一報告提及、信心較低或影響範圍較小,暫不個別開票,留待之後有更多證據或使用者實際遇到再處理。過度工程/死碼類發現(三份報告都各自列出一批,如 `kTabCloseButtonSpace`、`Site::QueryService` 的除錯 log)依此次稽核指示「secondary,只列不修」,不在本批次開票範圍。

### 2026-08-27 — 三個 agent(Claude/Codex/OpenCode)平行執行效能研究(啟動/關閉速度、記憶體重複、CPU),新增 PD-093~096

再次同一份 codebase 上同時開三個 Herdr tab,這次分別以 Claude、Codex、OpenCode 各自做唯讀效能研究,聚焦四軸:app 啟動速度、關閉速度、記憶體重複使用、CPU 使用(熱路徑)。三份報告(Codex/OpenCode 直接輸出於終端機;Claude 同樣因終端機替代畫面問題,改請其把完整報告寫成暫存 Markdown 檔後讀取,檔案未進 repo,讀取後即刪除)獨立產出 21(Claude,F1~F21)/6+關閉結論(Codex)/S1~S4+H1+M1~M3+C1~C7(OpenCode)項發現。收斂到高信心、開票的項目:

- **PD-093**(Claude F14/F13 + Codex #1 + OpenCode S1/S4,三方一致,本次收斂最高):啟動時 `apply_layout` 在 `WM_CREATE` 內同步 realize 所有可見 pane(而非只有 active pane),直接違反 `docs/design-spec.md` §9.3 明文的「先 realize active pane,其餘延後,避免被網路或離線路徑阻塞」——這不是效能建議,是 spec 未被落實的行為。`SHAutoComplete` 對所有 pane 同步呼叫是同一根因的第二個症狀,併入同票。
- **PD-094**(Claude F12 + Codex #2 + OpenCode S2,三方一致):啟動時 `read_session` 之後、`CreateWindowExW` 之前,立刻做一次沒有任何資料變更的完整同步 `save_now`,純屬冗餘,直接延後主視窗顯示。
- **PD-095**(Claude F5/F6 + Codex #4 + OpenCode C1/C2,三方一致,CPU 熱路徑收斂最高的一項):拖曳分隔線/視窗縮放時,每個 `WM_MOUSEMOVE`/`WM_SIZE` 都觸發完整 `apply_layout`,其中 `refresh_status_bar`→`item_counts` 在有選取項目時最多逐一查詢 1000 次 Shell property,三方都指名同一組呼叫鏈。
- **PD-096**(Claude F9 + OpenCode C4 收斂):`draw_brand_bar` 每次 `WM_ERASEBKGND` 都重新載入 App 圖示、建立/刪除兩個字型,疊加在 PD-095 描述的高頻重繪路徑上時被放大。

**已知會被 PD-091(尚未實作)大幅緩解、故不重複開票的一群發現**:Claude F1/F2/F3/F18(`save_now` 的雙 flush + 全量重新解析 + `AppState`/`SessionDocument` 兩份 model 並存)、Codex #3(每次導覽完成同步持久化)、OpenCode H1/S3(全域「冗餘儲存」)——這些全部指向同一根因「每次導覽完成/使用者動作都同步觸發完整 `write_session`」,而 PD-091 的 dirty-flag + 防抖已經是這群發現的正確修法,其「可選次要優化」一節也已涵蓋 F1 提到的「寫入後又讀回驗證」這項細節。三方都各自獨立收斂到同一根因但沒有另開新票,是刻意決定,避免與 PD-091 範圍重疊。

**未開票的單一/低優先級發現**(信心較低、影響範圍小,或發現者自評為低優先):Claude F4(訊息迴圈逐訊息的線性掃描與 `GetKeyState`)、F7(`write_live_view_count()` 未受診斷模式 gate,已併入 PD-095 的 Scope 第 3 點,不獨立開票)、F8(sidebar hover 逐列重建字型)、F10/F11(`layout_rects`/`apply_tab_item_size` 的小型堆積配置,已在 PD-095 的討論脈絡內,不獨立開票)、F15(view mode 讀回兩次)、F16(`decode()` 對 no-op migration 的全量深拷貝)、F17(關閉路徑的兩次 save 與三次 `revoke_drag_hover_targets`,Codex 明確稽核後認定關閉路徑本身乾淨,且會隨 PD-091 一併緩解)、F19(`TabState::history` 無上限成長)、F20/Codex #6(Group 名稱三份儲存 + sidebar 全量重建,發現者自評「低至中」)、F21(`AppState` 固定 4 pane 陣列,發現者自評「correctness-neutral, low-priority, 僅供參考」)、OpenCode M3(`tab_visuals` 快取)。這些暫不個別開票,留待之後有更多證據或使用者實際感受到影響再處理。

### 2026-08-27 — PD-094 撤回:三方效能研究誤判 PD-025 的崩潰偵測標記為冗餘寫入

實作前派工檢查(Codex 在動手前先讀 `docs/tickets/PD-025-crash-recovery-path.md`)發現 PD-094 的前提是錯的:啟動時 `save_now(state)` 這次寫入**不是**「內容和讀入時相同、純屬冗餘」,而是 PD-025 已確認的產品決策 2 明文要求的不乾淨關閉偵測標記——把 `clean_shutdown` 欄位從上次關閉留下的 `true` 改寫成 `false`,讓「啟動後、任何操作前就崩潰」這個最重要的情境也能被下次啟動偵測到。PD-025 決策 2 已經明確評估過這次啟動多一次磁碟寫入的成本並判定可接受(NFR-001 只要求閒置期間零 I/O,啟動不算閒置)。三方效能研究(Claude/Codex/OpenCode)都只從「寫入內容是否變化」判斷冗餘,沒有比對 PD-025 的既有決策記錄,才會把一個刻意設計的機制誤判成死碼——本頁撰票時沿用了同樣的誤判,沒有先核對 PD-025。依 ticket authoring rules,覆寫既有決策必須有新證據,而三方研究提供的並不是新證據,只是誤讀,故 PD-094 整票撤回,標記為 `deferred`,詳細說明見 [PD-094](tickets/PD-094-redundant-session-save-before-main-window-created.md) 檔案內的撤回說明。若未來要降低這次寫入的成本,正確方向是保留「啟動時必須標記 `clean_shutdown = false`」的語意,只把達成手段換成更輕量的單欄位寫入(而非整份 `write_session` 管線),已列入下方候選,觸發條件為量到啟動延遲確實可感知。

### 2026-08-20 — 專案建立與選型收斂

以 NimbleRun 的專案機制為模型建立本 repo,work items 改稱 tickets(`PD-xxx`)。沿用的機制:`CLAUDE.md` 僅為 `@AGENTS.md` 的單行 import、狀態單一來源規則、已否決方向帳本(含重開條件)、fail-closed 的 release evidence 腳本、ADR、CONTEXT 詞彙表、`## 計畫決策紀錄`。

語言選型經過一次反轉。初始 handoff 文件建議 C#/.NET 8 ＋ WPF ＋ `HwndHost`;使用者傾向 Rust(理由是「更穩定」);最終定為 C++20 ＋ WRL ＋ 純 Win32。兩份獨立審查(codex、opencode)各自查證官方文件後得出同一結論,且都推翻了原本的推論過程:

- 「Rust 比較穩」不成立。崩潰面是 `shell32` 與第三方 shell extension,語言換不掉。
- 「C# 因記憶體出局」是錯的歸因。部署檔案大小不等於常駐記憶體;四分割下語言 runtime 只佔總量約 5–8%,主導項是 Shell view 與 extension。C# 該出局的理由是 marshal 層的長期維護成本。
- 真正能控制記憶體的是三個架構決策(realize-on-activation、縮圖上限、extension 延後載入),不是語言。這條寫進 §NFR-002 而非留在選型討論裡。

**刻意不做的事**:不建 CI。NimbleRun 沒有 CI,governance 全靠 Markdown 與一支 fail-closed 腳本,而本專案的關鍵驗證(四分割 Shell 行為、原生右鍵選單、跨 pane 拖放、混合 DPI)在 CI runner 上本來就跑不出有意義的結果——加 CI 只會產生「綠燈但沒驗到東西」的假保證。要重開需先有一個能在無桌面環境下驗到真實 Shell 行為的方法。

**刻意不預先開滿 ticket**。只開到 PD-006(Phase 0 全部 ＋ Phase 1 的三個根 ticket)。PD-001 的 Go/No-Go 結論會決定 Phase 1 以後的 ticket 該怎麼寫;先寫好一批再作廢,就是 NimbleRun 明令禁止的「預留編號」的變體。

**採用 LLVM-MinGW toolchain**。開發機沒有可用的 C++20 MSVC toolset,而 PD-012 的實測證明 LLVM-MinGW 可直接使用 Windows SDK、WRL 與 Shell COM；後續建置統一透過 `cmake/llvm-mingw.cmake` 與 Ninja 執行。

### 2026-08-20 — PD-001 拆分為五片 tracer bullet

PD-001 以單一 ticket 涵蓋整個四分割原型:scope 十項、acceptance 十條,從「repo 尚無任何程式碼」一路到「寫下 Go/No-Go」。它違反本頁自己訂的「半天到兩天」尺寸規則,也裝不進一個 context window。

拆成 PD-007～PD-011,每片都是縱切:各自穿過視窗、COM、view 生命週期與 self-check,單獨可 demo。切分**不改變任何技術判斷**——PD-001 的產品決策、binding constraints 與否決事項全部原樣沿用,只改變交付顆粒度。

PD-001 標為 `superseded`,文件不動,作為決策軌跡保留。原本依賴 PD-001 的 PD-002／PD-003／PD-004 改依賴 PD-011,因為 Go/No-Go 判定的職責移到了那裡。

刻意讓 PD-010(位置持久化)與 PD-009(保活式切換)並行而非串接:兩者只共同依賴 PD-008,合併會讓 PD-009 同時扛 churn 量測與持久化,handle 數異常時難以歸因。

刻意讓 PD-011 成為不寫產品程式碼的純驗證片:Go/No-Go 是全案閘門,混在實作片裡容易被草率蓋章。

### 2026-08-25 — 視覺改版拆成 PD-028~031,四項刻意不做的事

使用者比對執行中的 app 截圖與 `docs/panedock-ui-prototype.html`(Quiet Header 變體)後回報落差明顯,並授權大幅修改程式碼。落差拆成四張依風險遞增排序的 ticket(PD-028~031),而不是一張大票,理由同上面 PD-001 拆分 PD-007～011 的先例:單一 ticket 裝不下、且風險層級差異大(排版改動 vs. `SysTabControl32` owner-draw 改造)不該綁在一起驗收。

四項刻意不做、且已寫入對應 ticket 的 Non-goals 的決定,記在這裡供之後檢索:

- **不做設計稿的 "Preferences" 按鈕**(PD-028):產品目前沒有偏好設定頁面,加一顆不接行為的按鈕是空殼功能。
- **不做設計稿的 "more actions"(`...`)選單**(PD-029):同上,沒有定義過的功能不預先做 UI 佔位以外的事。
- **不刪除 Duplicate／Rename／Delete／Move Up／Move Down 的側邊欄按鈕功能,只搬進 Group 列的右鍵 context menu**(PD-028):這五個是 FR-001 的必要功能,設計稿沒畫出來不代表要拿掉,推論是被收進次要互動裡。
- **Pane 卡片只圓上緣,下緣(貼著真實 `IExplorerBrowser` 內容)維持方角,不做完整四角圓角**(PD-030):四角圓角需要裁切 Shell view 本身的 HWND,目前沒有安全的做法;上緣是自繪的 tab header,可以自然圓角。若之後要做完整四角圓角,觸發條件是「先有一個能安全裁切 `IExplorerBrowser` HWND 而不影響其生命週期與 site 契約的具體方案」,依 §已否決的方向 的規則辦理(此項目前不算「否決」,只是候選,尚未列入表格,因為還沒有人提出可行方案可供評估)。

### 2026-08-25 — PD-032:PD-025 的關閉序列遺漏系統關機路徑

使用者回報常常遇到「PaneDock did not shut down cleanly last time」警示,附截圖。讀 `main.cpp` 的 `window_proc` 後確認:`clean_shutdown` 只在 `WM_CLOSE` 分支被寫回 `true`,而 Windows 關機/登出/重開機送的是 `WM_QUERYENDSESSION`/`WM_ENDSESSION`,不是 `WM_CLOSE`——程式碼完全沒有攔截這兩個訊息。這代表**任何一次正常的系統關機或登出都會被誤判成不乾淨關閉**,不是使用者的 extension 或環境有問題,是 PD-025 完成時漏掉的一個終止路徑(PD-025 的驗收清單只驗證了 `Stop-Process -Force` 模擬崩潰,沒有涵蓋系統關機訊息)。開 PD-032 修正,做法是在 `WM_QUERYENDSESSION` 補存檔並標記乾淨、在 `WM_ENDSESSION`(`wParam=TRUE`)才真的收尾銷毀 view,詳見 ticket 文件的「根本原因」一節。

### 2026-08-25 — grilling session:六項行為/資料模型需求核對,新開 PD-033~036

使用者列出六項需求逐一要求核對現況(session persistence、active pane 視覺、drag-hover 切換 tab/Group、tab 拖拉排序、Group 拖拉排序、realize-on-activation),用一輪 fact-finding + grilling 問答核對。結論:

- **兩項已經完成,不需要新 ticket**:每個 Group/pane/tab 的 location 與 active tab 記錄與自動還原(`SessionDocument`/`capture_locations`/`save_now` 已涵蓋全部 Group,非僅預設 Group);realize-on-activation(`apply_layout`/`activate_group` 只對 `state.realized[pane]` 為真的 pane 動作,程式碼證實有落實)。
- **四項需要新 ticket**:PD-033(active pane 邊框改為扁平彩色,取代舊式 `WS_EX_CLIENTEDGE` 立體邊框)、PD-034(拖曳懸停自動切換,依使用者要求同時支援內部/外部來源、且涵蓋側邊欄 Group 列與 tab 兩種目標)、PD-035(tab 拖拉排序,依使用者確認範圍僅限同一 pane 內)、PD-036(Group 拖拉排序,明確覆寫 PD-017 決策 5——該決策記在 PD-017 文件內部,未登記進本頁「已否決的方向」表格,新證據為使用者本次直接提出的需求)。
- PD-034/035/036 三張票共用同一種「滑鼠事件拖曳偵測,不用 OLE `IDropTarget`」的排序互動語言(PD-035/036),與「`IDropTarget` 懸停偵測,不接受實際 drop」的檔案懸停互動語言(PD-034)分開設計,避免同一個控制項(側邊欄 `LISTBOX`)上兩套拖放機制互相干擾——細節見各票的已確認的產品決策。
- 新開為 Phase 7(PD-034~036,拖放互動類);PD-033 歸在 Phase 6(視覺改版收尾,依賴 PD-030)。

### 2026-08-25 — PD-028~036 完成後實機比對,再開 PD-037~040

使用者實際執行 `build\PaneDock.exe` 並附截圖與理想稿比對,回報四項具體落差,逐一開票:

- **PD-037**:tab 寬度目前是 `SysTabControl32` 在 `TCS_OWNERDRAWFIXED` 下的系統預設固定寬度,`refresh_tab_strip` 從未呼叫 `TCM_SETITEMSIZE`,與 tab 數量、文字長度無關——這是「太窄顯示不了字」的根因。改為依可用寬度與 tab 數量動態計算,夾在 `[kTabMinWidth, kTabMaxWidth]`。
- **PD-038**:pane 初次載入或切換後畫面空白、需滑鼠移動才刷新,判定為功能缺陷(不是視覺落差),優先度 HIGH。根因待實作 agent 用程式碼證據確認(`IExplorerBrowser::Initialize`/`navigate` 完成後可能沒有觸發同步繪製),修法鎖定「host 端主動觸發重繪」,明確排除「監聽滑鼠事件手動刷新」這種治標不治本的做法。
- **PD-039**:移除「PANE LAYOUT」文字標籤,版面配置按鈕與新的 more-actions 佔位按鈕改用原生 `TOOLTIPS_CLASS32` hover tooltip——本程式碼庫第一次使用 tooltip,選原生 Common Controls 而非自繪。
- **PD-040**:**覆寫上面「2026-08-25 — 視覺改版拆成 PD-028~031」記錄的候選結論**(pane 卡片只圓上緣、下緣維持方角,觸發條件是「先有安全裁切 `IExplorerBrowser` HWND 的方案」)。新證據:不需要裁切 `IExplorerBrowser` 本身的 HWND,`AGENTS.md` 已預先允許「幫每個 pane 加一層外層容器 HWND」——把容器 HWND 用 `SetWindowRgn`/`CreateRoundRectRgn` 裁成圓角,`IExplorerBrowser` 物件與其內部 Shell view 完全不受影響,滿足觸發條件的實質要求。詳見 ticket 文件的「覆寫聲明」一節。

四張都歸在 Phase 6(視覺/易用性收尾,依賴既有 PD-029/030/033),與 Phase 7 的拖放互動票分開。

### 2026-08-25 — PD-037~040 完成後真人桌面測試,發現兩個真實根因,開 PD-041/042

使用者實機操作附截圖回報三個現象:「active pane 外框轉角處很奇怪」「hover 的 file item 才會顯示,不會自動刷新」「切換 active pane 會清除 pane 內所有 item,變成一片空白,又要 hover 才刷新」。讀程式碼找到兩個各自獨立、有具體證據的根因,不是 PD-038 沒修好,而是同一類症狀的兩個不同觸發點/成因:

- **PD-041**:主視窗(`kWindowClassName`)`CreateWindowExW` 的 style 只有 `WS_OVERLAPPEDWINDOW`,從未加上 `WS_CLIPCHILDREN`。`paint_client_background` 對整個 client rect 做 `FillRect`,沒有這個 style 時會直接覆蓋子視窗(tab strip、PD-040 的 explorer container、Shell view)目前顯示在螢幕上的畫面,且不會觸發子視窗自行重繪。`set_active_pane` 結尾的 `InvalidateRect(window, nullptr, TRUE)` 正是觸發點——這解釋了「切換 active pane 清空畫面」;其他任何呼叫同樣 API 的既有路徑也會觸發同一症狀,這解釋了「需要 hover 才刷新」在 PD-038 修完之後依然存在(PD-038 修的是 `ExplorerHost` 內部導覽完成時機的問題,跟主視窗覆蓋子視窗是完全不同的根因)。修法是幫主視窗補上 `WS_CLIPCHILDREN`,一次解決所有呼叫點,而不是逐一稽核。
- **PD-042**:PD-040 新增的 explorer container 的 `rect.top` 是「導覽列下緣」(內部邊界),不是 pane 最外層上緣,但 `apply_pane_container_region` 無條件把 container 四角都裁圓,導致頂端兩角(導覽列與內容區交界處)憑空多出圓角缺口,與沿著整個 pane 外緣繪製的 active 藍色外框對不齊,形成使用者說的「轉角處很奇怪」。修法是 container 只圓化下緣兩角(用 `CombineRgn`/`RGN_OR` 把頂端圓角區域補回直角),頂端維持直角。

兩票都歸 Phase 6,依賴各自的前置票(PD-038、PD-040)。

### 2026-08-25 — PD-041/042 完成後再次真人測試,開 PD-043~046

使用者附截圖回報四項:導覽按鈕圖示太醜、網址列無自動完成、active pane 下緣框線比上緣細、版面配置按鈕應相連且需要 tooltip。逐一核對程式碼:

- **PD-043**:`draw_navigation_icon_button` 目前是簡單手繪箭頭。改用 Common Controls 公開系統點陣圖 `IDB_HIST_SMALL_COLOR`(`HINST_COMMCTRL`)畫上一頁/下一頁,這正是檔案總管歷史記錄工具列用的同一組圖示,不是私有/不保證的資源索引。上一層沒有對應公開圖示,維持手繪但改善比例。
- **PD-044**:改用 Win32 內建 `SHAutoComplete(edit, SHACF_FILESYS_DIRS)`,一行 API 掛到每個網址列 `EDIT` 控制項,不自製下拉選單。
- **PD-045**:根因是 PD-041 修正 `WS_CLIPCHILDREN` 之後才顯現的既有幾何缺陷——`draw_pane_card` 的 `card` 矩形左/上/右三邊比 `pane_rect`(=PD-040 容器邊界)多留 `outset` 空隙,下緣沒有,導致下緣外框筆畫有一半路徑落在容器內部,`WS_CLIPCHILDREN` 生效後那一半被裁掉,只剩下半寬度可見。修法是下緣也比照三邊留 `outset`。
- **PD-046**:5 個版面配置按鈕目前用固定 gap 分開排列,改為視覺相連的分段控制(共用圓角外框、細分隔線取代留白)。**同時查證 PD-039 的 tooltip 註冊程式碼確實存在**(`TTF_IDISHWND`/`TTM_ADDTOOLW`),但因為本專案一直沒有真人互動驗證能力,PD-039 當時只做到非互動煙霧測試——本票明確要求真人懸停驗證 tooltip 是否真的顯示,不能只憑程式碼審查判斷完成,若發現 bug 就地修正不另開票。

### 2026-08-25 — 首次具備螢幕截圖與滑鼠操作驗證能力,對照目標 mockup 開 PD-047~054

**環境能力更新:** 本次會話首次確認可以透過 PowerShell(`System.Windows.Forms`/`System.Drawing.Graphics.CopyFromScreen` 截圖、`user32.dll` `SetCursorPos`/`mouse_event` 模擬滑鼠)實際啟動 `build\PaneDock.exe` 並截圖、操作驗證,不再只有非互動 smoke check。這打破了先前每張 PD-0xx 票(PD-011 起)反覆記錄的「本環境無 Computer Use/互動桌面能力」限制——後續票的 Agent checks 應該要求實際截圖/操作驗證,而不是預設接受非互動驗證。

使用者附目標畫面(`docs/panedock-ui-demo-01-refined-quiet-header.html`)與實機截圖,列出 8 項落差,逐一核對程式碼後開票:

- **PD-047**:版面配置按鈕的選中態高亮(PD-046 已做,但對比度太弱)——`draw_layout_button` 選中態背景 `RGB(234,241,255)` 與未選中態 `RGB(248,250,252)` 太接近,改為深色實心填底+白色圖示。
- **PD-048**:`draw_pane_card` 的 inactive 外框與陰影顏色偏重,調淡但不動粗細(PD-045 已修正粗細一致性)、不動 active pane 的藍色強調外框(PD-033 決策維持)。
- **PD-049**:**覆寫 PD-019 決策 1**(tab 條使用原生 `SysTabControl32` 的理由)——`TCM_SETITEMSIZE` 只能設定統一寬度,無法逐一動態寬度(PD-037 已知限制);「+」新增按鈕也無法在原生控制項上獨立釘在右緣。改為自繪控制項。
- **PD-050**:PD-049 換掉 `SysTabControl32` 後,PD-034(拖曳懸停自動切換)、PD-035(拖曳排序)依賴的 `TCM_HITTEST` 等訊息全部失效,依賴 PD-049 完成後另開票重接,避免單票工作量爆炸。
- **PD-051**:每個 pane 增加狀態列(項目數/選取數),透過 `IExplorerBrowser::GetCurrentView(IID_PPV_ARGS(&folder_view))` 取得 `IFolderView2::ItemCount`,是本專案第一次接觸這個介面。
- **PD-052**:發現 `docs/design-spec.md` FR-002/NFR-005 早已把 view mode 列為必要還原狀態、`core::TabState::view_mode` 欄位也早已存在並隨 session 序列化,但 `app_shell` 從未讀寫這個欄位——這不是新需求,是既有 spec 承諾與實作之間的靜默落差,本票補上 `IFolderView2::SetCurrentViewMode`/`GetCurrentViewMode` 與 refresh 按鈕。
- **PD-053**:`draw_brand_bar` 的品牌列與側邊欄背景色其實已經一致(`RGB(251,252,254)`),割裂感純粹來自品牌列自己多畫的一條 1px 分隔線,刪掉即可。
- **PD-054**:目前完全沒有 app icon 資產,側邊欄品牌列左上角是手繪圓角方塊+十字線條,不是任何 logo。委由 Codex 設計實際的 `.ico` 資產並接上視窗圖示與品牌列繪製。

PD-047/048/053/054 為獨立小票;PD-049→PD-050 有嚴格順序依賴;PD-051/052 各自獨立但都涉及 `IFolderView2`,可平行進行。

四張都歸 Phase 6,依賴各自的前置票。

### 2026-08-26 — PD-047~054 實裝後的實機驗證發現三個功能性回歸,連同視覺落差開 PD-055~065

**驗證方法更新(取代 2026-08-25 記錄的做法):** `Graphics.CopyFromScreen` 已證實不可靠——它是「螢幕座標區域截圖」,當 `SetForegroundWindow` 靜默失敗(背景腳本呼叫者常見)時會截到疊在上面的其他視窗;桌面鎖定時則靜默截到鎖定畫面而不報錯。**後續一律改用 `PrintWindow(hwnd, hdc, 2 /* PW_RENDERFULLCONTENT */)`**,它直接請目標視窗把內容畫進指定的 HDC,不受 z-order、遮擋或前景狀態影響。細小元件(tab 條、16px 圖示、side bar 文字)必須截圖後以 `InterpolationMode = NearestNeighbor` 放大 3~6 倍才能判讀。

**另一個教訓:測試程序一律用不帶 `/F` 的 `taskkill /PID` 優雅關閉。** 上一輪驗證大量使用 `Stop-Process -Force`,等同於模擬當機,使 `clean_shutdown` 這個 dirty bit 停在 `false`,導致使用者下次啟動看到 crash 復原警告對話框。這不是產品缺陷(PD-025/032 的 dirty-bit 設計正確),是驗證方法的副作用。

三個功能性回歸都是「畫面看起來正常、實際功能失效」的類型,純程式碼審查與純截圖都抓不到,只有實際操作 + 讀資料層狀態才能發現:

- **PD-055**(CRITICAL):PD-049 把 tab 條換成 `STATIC` 子視窗時沒加 `SS_NOTIFY`。**未指定 `SS_NOTIFY` 的 `STATIC` 對 `WM_NCHITTEST` 回傳 `HTTRANSPARENT`**,滑鼠訊息全部穿透到父視窗,`tab_strip_proc` 的 `WM_LBUTTONDOWN` 分支成為死碼——tab 切換、「+」新增 tab、PD-050 的 tab 拖曳排序三者同時失效。
- **PD-056**:PD-047 已正確改成由 `AppState` 決定 checked 狀態,但沒有人 invalidate 舊按鈕。`apply_layout` 的 `InvalidateRect(window, nullptr, TRUE)` **不會讓子視窗控制項失效**,舊的 active 按鈕因此保留舊像素,畫面同時出現兩顆藍色按鈕。修法是在 `layout_header` 既有的按鈕同步迴圈補上逐顆 `InvalidateRect`。
- **PD-057**(CRITICAL):PD-036 的 Group 拖曳排序在 `group_list_proc` 的 `WM_LBUTTONUP` 分支中,**在把訊息交給 `DefSubclassProc` 之前**就呼叫了 `finish_group_drag`,而後者會 `ReleaseCapture()`。LISTBOX 收到 `WM_CAPTURECHANGED` 後放棄進行中的點擊追蹤,於是不送 `LBN_SELCHANGE`,Group 永遠切換不了。**決定性實驗:真實點擊後 `LB_GETCURSEL` 已變、`active_group_id` 未變;手動補送一個 `WM_COMMAND`/`LBN_SELCHANGE` 後立即切換成功**,證明 `activate_group` 本身完全正確。同一個 subclass 的 `WM_LBUTTONDOWN` 分支反而是正確示範(先 `DefSubclassProc` 再動自己的狀態)。

視覺與互動落差:

- **PD-058**:全應用程式沒有任何 hover 狀態。版型按鈕可試 `ODS_HOTLIGHT`(但 PD-047 已證實同類按鈕的 `BM_GETCHECK` 不可靠,不可假設);tab 條與 Group 清單是自繪/LISTBOX,必須自行以 `WM_MOUSEMOVE` + `TrackMouseEvent(TME_LEAVE)` 追蹤,且**只在 hover 項目改變時才 invalidate**,否則違反閒置 0% CPU 規則。
- **PD-059**:PD-052 的檢視模式只做單向循環,使用者無法直選。改用 `TrackPopupMenu`,照抄 Group 右鍵選單既有的 `CreatePopupMenu`/`TPM_RETURNCMD` 模式。
- **PD-060**:PD-051 的狀態列是「N items」/「N of M selected」二選一,缺選取總大小。改三段式,大小走 `IShellItem2::GetUInt64(PKEY_Size)` + `StrFormatByteSizeW`,**必須設選取項目數上限**(Ctrl+A 數萬項會卡死 UI),資料夾無 `PKEY_Size` 屬正常需跳過。同時補上狀態列的分隔線/底色。
- **PD-061**:側邊欄用 `GetStockObject(DEFAULT_GUI_FONT)`(舊 stock 字型,約 8pt),副標題再乘 0.82 更小。改用 `SystemParametersInfoForDpi` 取 `lfMessageFont`(Segoe UI),名稱加粗,列高依字型度量重算。
- **PD-062**:tab 用 `FillRect`+`FrameRect` 是方角,且外框用系統色 `COLOR_ACTIVEBORDER` 會隨主題跳動;更關鍵的是 padding 與 gap **共用同一個 `inset` 常數**,而使用者要求兩者往相反方向調(padding +2px、gap -2px),不拆開常數就無法同時滿足。「+」目前只是 `DrawTextW` 畫一個字元,改用 GDI 線條繪製。
- **PD-063**:**GDI 的 `RoundRect` 沒有反鋸齒,這是 API 本身的性質,無法靠調參數繞過。** 鋸齒只在 active pane 明顯,因為那是 2px 飽和藍 vs 白底的高對比;PD-048 調淡後的 inactive 邊框則被低對比掩蓋。本票要求實作 agent 至少實作並截圖比對兩條路徑(GDI+ 反鋸齒 / 多層柔和光暈 / 改用不依賴圓弧的強調)後再選定。
- **PD-064**:more-actions「...」自 PD-029 起就是永久停用的視覺佔位符,使用者判斷不需要,**本票明確覆寫 PD-029 決策 2** 予以移除(移除後 `layout_header` 的 `kButtonSlotCount`/`total_width` 必須重算,否則五顆按鈕會被當成六個 slot)。Up 圖示的直立桿畫在 `cx`、畫筆寬 2px,**GDI 偶數寬度線條無法真正置中**,重心比對稱的箭頭兩翼偏左半個像素——優先評估改用 `Segoe MDL2 Assets` 字型圖示,PD-052 的 refresh 圖示已經在手算 `Arc()` 失敗兩輪後付過這個學費。
- **PD-065**:`EBO_NOBORDER` **在 `Initialize` 之後才 `SetOptions`,已經太晚**——該旗標控制的是內部宿主視窗建立時的樣式,對已建立的視窗沒有回溯效果,所以這個旗標從來沒生效過。修法是把 `SetOptions` 移到 `Initialize` 之前,並把失敗路徑從 `destroy()` 改成 `reset_uninitialized_browser`(此時尚未 `Initialize`,不可呼叫 `Destroy`)。**紅線:不得 subclass 或改寫 Shell view 內部子視窗;若公開 API 無法移除該線,正確結果是如實記錄結案。**

- **PD-066**:使用者要求拖曳排序時有 placeholder。調查中發現**第二個 PD-049 遺留的死碼回歸**:`WM_DRAWITEM` 中處理 tab 的分支條件是 `item->CtlType == ODT_TAB`,而 `ODT_TAB` 只有原生 `SysTabControl32` 會送出;PD-049 換成 subclass 的 `STATIC` 之後,該分支永遠不成立,`draw_tab_item` 與 `draw_tab_insertion_indicator` 都成為死碼,而 `paint_tab_strip` 自己完全沒有繪製插入指示線——**所以 tab 拖曳排序至今沒有任何視覺提示**。Group 側的 `draw_group_insertion_indicator` 走 owner-draw `LISTBOX` 路徑,應仍有效但需實機確認。本票一併刪死碼並把細線升級為「撐開空位」的 placeholder。tab 側的正確做法是在 `apply_tab_item_size` 的幾何計算階段就重排矩形(讓位與 hit-test 都免費正確);Group 側受 `LISTBOX` 架構限制無法重排,只能在 `Sidebar::draw_item` 把 placeholder 那一列改畫成空槽。
- **PD-073**:`apply_tab_item_size` 的等比壓縮地板是 `kTabMinWidth = 72`,當 `tab 數 × 72 > available` 時壓縮已無法生效,接著配置迴圈把 `left`/`right` 都夾在 `available`,**溢出的 tab 因此拿到零寬矩形,`PtInRect` 一律回傳 FALSE,那些 tab 收不到任何滑鼠事件**。實測:單一版型 tab 條 client 寬 560,`available` = 524,`524 / 72` = 7.2,該 Group 的 15 個 tab 中有 8 個不可點擊。**這是功能性缺陷,不是視覺問題。** 使用者 2026-08-26 明確指示:溢出的 tab 應「自然被截斷」——矩形保持完整寬度,靠 GDI clip region 裁切,不得改寫幾何。捲動按鈕放在「+」左側,整格捲動,只在溢出時佔用寬度。
- **PD-074**:PD-066 的 placeholder 是**空槽**,而來源項目在原位又完全不繪製,所以整個拖曳過程中被拖曳的項目在畫面上不存在——使用者知道會插在哪裡,但不知道拖的是哪一個。做法是把被拖曳項目的內容以 `RGB(148, 163, 184)` 畫進 placeholder 裡,**不重開 PD-066 決策 2 否決的「跟隨游標的浮動縮圖」**。關鍵技術點:owner-draw 的 `item->itemID` 在 placeholder 那一列是**目標**索引,`groups_[itemID]` 是錯的那個 Group,所以 `Sidebar::draw_item` 的 `bool placeholder` 必須換成帶**來源**索引的參數,由呼叫端從 `group_drag->source_index` 傳入。順帶修掉 Group placeholder 用 `Rectangle`(方角)而 tab 用 `RoundRect` 的不一致。
- **PD-075**:`draw_navigation_icon_button` 的五個 case 走**三種不同繪製技術**——back/forward 是 Common Controls 的 history **點陣圖**(配色完全不受 `color` 控制)、up/view 是 2px 手繪線段(`CreatePen(PS_SOLID, size / 8, ...)`)、refresh 是 `Segoe MDL2 Assets` 字型字符(筆畫粗細由字型決定)。統一到字型字符,理由是 PD-052 為 refresh 手算 `Arc()` 失敗兩輪之後才改用該字型,那個學費不要付第二次。**已知但刻意不處理的差異:** header 版型按鈕 `draw_layout_glyph` 用 1px 畫筆,導覽列是 2px——版型圖示是版面示意圖,沒有字型字符能表達,必須手繪,見下方候選表。

- **PD-067**:使用者要求 Group 超過側邊欄高度時要有捲軸。**開票前先實測,結論是功能已經完整可用,本票因此從「新增功能」降級為「視覺微調」**:`WS_VSCROLL` 早已設定(執行期樣式 `0x50210151`),`GetScrollInfo` 回傳 `min=0 max=17 page=13 pos=5` 完全正確,滑鼠滾輪捲動實測 `LB_GETTOPINDEX` 由 5→0→5 正常,`PrintWindow` 截圖也看得到 thumb。真正的落差只是那條 Windows 11 細型捲軸又細又低對比,在淺色側邊欄上容易被當成不存在。優先解法是加 `LBS_DISABLENOSCROLL` 讓捲軸恆常可見,次要是調整 pill 右內距避免被捲軸壓到。

- **PD-068**(CRITICAL,關閉當機):使用者回報存取違規對話框。以 crash dump + `cdb` 取得堆疊,`FAILURE_BUCKET_ID` 直接指出 `NULL_POINTER_WRITE_c0000005_shell32.dll!CDefView::SetCallback`。根因是 PD-051 的 `ExplorerHost::destroy()` 把 `IShellFolderView::SetCallback` 的**第二個 out 參數**傳成 `nullptr`,而 shell32 的實作不檢查 null 就寫入 `*ppOldCB`。安裝路徑(`navigation_complete`)傳的是合法位址,所以只有關閉時當機。修改前 3 輪 3 次當機、修改後 5 輪 0 次。**這個當機是先前多次誤判「本環境不支援截圖/自動化」的真正原因**:當機使程序殘留,殘留程序持有 `Ctrl+Shift+L` 熱鍵,新實例 `RegisterHotKey` 失敗後中止建立主視窗,於是 `MainWindowHandle` 為 0、`PrintWindow` 回傳 False;殘留程序也會鎖住 exe 讓建置失敗。**教訓:當機時以 crash dump 堆疊為準,不要用「換版本測幾輪沒重現」來歸因**——本票以 `9ee9c17` 測 3 輪確實 0 次,差點誤指 PD-055/PD-057。

依賴與排程:PD-055 是 PD-058/PD-062/PD-066 的前置(tab 不能點就無法驗證 tab 的 hover、視覺與拖曳);PD-056/PD-057/PD-059/PD-060/PD-061/PD-063/PD-064/PD-065 彼此獨立可平行。PD-058、PD-062、PD-066 都會改到 `paint_tab_strip`,後做的那票需先 rebase。

### 2026-08-26 — 使用者直接要求 Group/tab active-hover 樣式一致,開 PD-076

使用者原文:「group and pane tab should apply uniform styles on active/hover item. base on current group active/hover style, apply to pane tab. pane tab can use its own proper font.」核對現況:側邊欄 `Sidebar::draw_item`(`kSidebarActiveBackground` 藍底 pill ＋ `kSidebarActiveText` 藍字)active/hover 用「底色 + 文字色」雙重強調;`paint_tab_strip` 的 tab 只靠灰階底色深淺區分,`SetTextColor` 在迴圈外設一次、active/hover/一般三態文字色完全相同,沒有任何強調色。另外發現一個獨立的死碼:tab strip 建立時送的 `WM_SETFONT`(`main.cpp` 第 2736-2739 行)因為 `tab_strip_proc` 把 `WM_PAINT` 整個交給 `paint_tab_strip` 自繪、從未讀回該字型,對實際渲染完全沒有效果——tab 文字目前用的是 GDI 的 stock 字型,不是任何專案定義的字型。此落差不在 PD-062(只處理幾何,色票決策明講不要求對齊側邊欄)或 PD-072(只處理字型 face 語系一致性,Non-goals 明講不改 tab 視覺樣式)範圍內,兩者都是本票的依賴而非涵蓋者。開 PD-076,決策方向:底色/文字色改套用側邊欄既有色票,不建跨模組共用色票標頭(沿用兩模組各自定義相近顏色常數的既有模式);不統一圓角(tab 6px 與側邊欄 10px 的層級關係是 PD-062 定案,不重開);字重不隨 active 改變(側邊欄本身也是靠色彩而非字重做強調);移除死碼 `WM_SETFONT`,改為 `paint_tab_strip` 內明確選字型繪製。

### 2026-08-26 — 拖曳 resize 主視窗時 pane 狀態列殘影,開 PD-077

使用者附截圖回報:拖曳調整主視窗大小後,左下 pane 的檔案列表中段出現一行孤立的舊狀態列文字「80 items」(紅色箭頭標示),與該 pane 真正的狀態列「4 items」並存——不是數字算錯,是舊位置的像素沒有被清除。追出兩個疊加的根因:(1) `WM_SIZE` 只呼叫 `apply_layout` 重新定位每個子視窗,從未主動對整個視窗或變動的 pane 區塊發出強制重繪,完全依賴各個 `SetWindowPos` 呼叫各自的內建失效連鎖;(2) 同一個 pane 內的 `explorer_containers`/導覽按鈕/`address_bars`/`status_bars` 全部沒有 `WS_CLIPSIBLINGS`,只有 `tab_strips` 有——`explorer_containers` 本身「從不處理自己的繪製訊息」(PD-040 既有註解),完全依賴外部正確的失效通知,兩個根因疊加就是「狀態列搬走後,新蓋上來的 container/Shell view 沒收到『這裡要重畫』的通知」的完整因果鏈。開 PD-077,決策方向:五個子視窗補齊 `WS_CLIPSIBLINGS`,並在 `apply_layout` 每個 pane 幾何區塊處理完後明確補一次涵蓋該 pane 矩形的強制重繪(不是每次 `WM_SIZE` 都全視窗重畫,避免閃爍);驗收要求連續拖曳過程中多張中繼尺寸截圖,不能只驗頭尾;若兩個修正仍不夠、根因其實在 Shell view 內部重繪時機,紅線是不得 subclass 或改寫其內部子視窗。

### 2026-08-27 — 使用者要求斷電安全與檢視模式對齊真實檔案總管,開 PD-078/079

使用者提出兩項需求:

- **PD-078**:「要保證電腦突然當機或斷電時,不會造成設定檔/記憶檔毀損」。讀 `src/core/session.cpp` 的 `write_session` 後確認:PD-006 決策 2 要求的「temp 檔 + flush + rename、備份保留」結構已經存在,但 `stream.flush()` 只把資料推進 OS 頁快取,不代表已經落盤,`std::filesystem::rename` 底層的 `MoveFileExW` 沒有帶 `MOVEFILE_WRITE_THROUGH` 也不保證同步。真正落盤需要 `FlushFileBuffers`,但那是 `windows.h` API,不能直接寫進 `core`(`AGENTS.md` 硬規則)。本票的核心是這個分層問題:落盤的**邏輯順序**留在 `core` 可測試,實際 Win32 呼叫由 `app_shell` 注入,具體介面形狀留給實作 agent 決定並在交接區說明理由。
- **PD-079**:使用者附真實 Windows 檔案總管「檢視」選單截圖,要求 8 個項目(超大/大/中/小圖示、清單、詳細資料、並排、內容)而非 PD-059 剛完成的 4 項,且只有「詳細資料」該顯示欄位標題。**明確覆寫 PD-059 已確認的產品決策 3**——PD-059 當時依據 PD-052 決策 2「額外模式是否加入由實作 agent 決定,不強制」只做了最低限度的 4 項,新證據(使用者截圖)把範圍變成強制的 8 項。根因調查發現「超大/大/中/小圖示」四級在真實 Shell 裡不是四個不同的 `FOLDERVIEWMODE`,而是同一個圖示模式配上 `IFolderView2::SetViewModeAndIconSize` 控制的不同像素尺寸——本票明確要求實作 agent 查證這個 API 在目前 LLVM-MinGW 工具鏈下的可用性,不得憑空假設像素值。Column header 只在「詳細資料」顯示很可能是真實 Shell view 的原生行為,不需要新程式碼,要求實作 agent 先截圖驗證再決定是否動手。

**同時記錄一條新的驗證方法約定(即時生效,適用於所有後續票):** 只有單次點擊/操作 + 截圖的驗證由 Agent(或本人)執行;需要連續、多步驟操控滑鼠鍵盤的測試(例如連續拖曳、多步驟 hover 序列)交給使用者本人執行,理由是電腦操作工具(computer-use)會佔用實體滑鼠鍵盤,長時間自動化操作會干擾使用者同時使用同一台機器。這條約定不寫進 `AGENTS.md`(那是產品/工程規則,這是協作流程規則),但後續每張票的 dispatch prompt 都應該包含這個限制。

### 2026-08-27 — 使用者實機回報 hover 覆蓋不全與對比度不足,開 PD-083

使用者原文兩項:「所有的 buttons 都應該有 onhover style」、「現在的 layout onhover style 不明顯,需要重新調整」。追查發現 PD-058(已完成)當時的 Scope 明確只涵蓋版型按鈕、tab、tab 的「+」、Group 側邊欄列四項,導覽按鈕(每 pane 五個)、側邊欄 New Group 按鈕、PD-080 新增的 tab 捲動按鈕三組從未被納入,`draw_navigation_icon_button`/`draw_sidebar_action_button`/`draw_tab_scroll_button` 完全沒有讀取 `ODS_HOTLIGHT` 或追蹤滑鼠的程式碼——不是回歸,是原本就沒做。版型按鈕的 hover 對比度問題根因與 PD-076 完全同一種缺陷:常態 `RGB(248,250,252)` 與 hover `RGB(242,245,248)` 只差 `6/5/4`,肉眼難以分辨。

### 2026-08-27 — 使用者實機回報 tab 條兩個視覺瑕疵,開 PD-080/081

使用者附截圖回報兩項獨立瑕疵(PD-073/075/076/079 已交付但尚未完成使用者實機驗證的期間發現):

- **PD-080**:tab 溢出時的左移/右移捲動按鈕(PD-073 新增)「太大,不好看」,並附一張瀏覽器分頁常見的緊湊小箭頭按鈕作為參考樣式。根因是 `kTabScrollButtonWidth`(28px)只控制寬度,高度直接沿用整個 tab strip 高度(`client.bottom`),沒有獨立的視覺高度收縮,導致按鈕外框和 tab 本身一樣大,箭頭本身很小,比例失衡。不涉及可點擊性(PD-073 已修好),純視覺尺寸調整。
- **PD-081**:tab 條「+」新增按鈕(PD-062 決策 5 才剛從字型字符改成手繪十字線段)在實機截圖上出現缺角瑕疵,使用者明確要求「用正常的字型達成,除非效能更好才用畫的」。**明確覆寫 PD-062 已確認的決策 5**(手繪加大加粗)與 PD-075 決策 9(因此把「+」列為非目標)——新證據是使用者實機截圖顯示手繪十字交會處不平整,不是風格偏好問題。預設改回字型字符渲染,只有在有實測效能數字支持時才允許保留手繪路徑,且必須先修好缺角。

兩票都歸 Phase 7,分別依賴 PD-073 與 PD-062,彼此互不依賴,可平行處理。

兩票都歸 Phase 7,PD-078 屬 core 加固,PD-079 依賴 PD-059。

### 2026-08-27 — 使用者實機截圖回報 column header 在非詳細資料模式仍顯示,開 PD-082

使用者附截圖(大圖示檢視,folder 為 7-7zip/Adobe/AMD/Application):畫面上方仍有一列「名稱、修改日期、類型、大小」欄位標題。這正是 PD-079 決策 5「Column header 只在『詳細資料』顯示」所依賴、但**從未實機驗證**的假設(該票驗收項 6 明確記為「未驗證」)。PD-079 文件依規則不編輯,開新票 PD-082 追根因與修正,並列出兩個尚待查證的假設(全新 tab 從未套用過 view mode、或從詳細資料切走時 header 沒有跟著隱藏),交給實作 agent 用單次點擊+截圖分別驗證再動手。

### 2026-08-28 — 使用者要求分隔線拖曳加節流以降低 CPU,開 PD-097

使用者原文:「resize pane size via pane separator should apply throttling, to prevent the render storm. long refresh interval is acceptable to lower cpu usage.」PD-095(2026-08-27)已把 `update_splitter_drag` 的逐幀呼叫從「幾何+內容」拆成「只做幾何」,但幾何重排本身仍是無節流的逐幀工作(每個 `WM_MOUSEMOVE` 都對每個可見 pane 呼叫 `SetWindowPos`)。使用者本次要求的是在幾何重排這一層再加節流,並明確表態接受較長刷新間隔換取更低 CPU。決策方向:沿用既有的 `kSessionSaveTimerId`(PD-091)的事件觸發 timer 模式,但性質是節流(throttle,固定間隔持續套用最新位置)而非防抖(debounce,安靜下來才套用一次)——兩者混淆會導致拖曳期間畫面凍結直到放開滑鼠,已在票內明確寫清楚差異以避免實作誤用防抖模式。開票為 [PD-097](tickets/PD-097-splitter-drag-geometry-throttling.md),依賴 PD-095。

### 2026-08-28 — 使用者要求檢視模式按鈕圖示改為清單樣式,開 PD-098

使用者原文:「change view size icon from [4 方格圖] to [清單圖]. should follow nav buttons styles.」附兩張參考截圖。目前檢視模式按鈕用 `Segoe MDL2 Assets` 的 GridView 字符(`U+E80A`),使用者要求換成清單樣式(三條橫線+項目符號),並要求沿用其餘四個導覽按鈕(上一頁/下一頁/上層/重新整理)既有的繪製機制與視覺規格,不另立新邏輯。開票為 [PD-098](tickets/PD-098-view-mode-icon-grid-to-list-glyph.md),依賴 PD-075(圖示統一到 `Segoe MDL2 Assets` 字型字符的既有決策)。

### 2026-08-28 — 使用者要求新增第 6 種版型(2 up / 1 down),覆寫 FR-003「僅五種版型」,開 PD-099

使用者原文:「Add new layout, 3 panes (2 at up half, 1 at bottom half)」附參考截圖。這與既有 `LayoutTemplate::three_pane`(左一右二疊放)是不同幾何,是座標軸轉置的新形狀,既有 5 種版型都無法表達。明確覆寫 `docs/design-spec.md` FR-003「支援且僅支援五種版型」,新證據為使用者本次直接提出的具體需求;同時明確聲明**不是**重開「已否決的方向」表的「任意遞迴 pane 分割」——本票只是比照既有 5 種版型的固定模式(`LayoutTemplate` enum + 各 `switch` 多一個 `case`)多加一個具名固定形狀,不是開放任意分割機制,詳細論證見 ticket 文件。開票為 [PD-099](tickets/PD-099-add-two-over-one-layout-template.md),依賴 PD-005(矩形計算)、PD-016(版型/分隔線基礎設施)、PD-046(版型按鈕列)。
