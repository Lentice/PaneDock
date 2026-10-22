# PaneDock Tickets

`docs/design-spec.md` 是產品權威。本頁只負責同步「工作切分、依賴、狀態與證據」,不重述 spec 的內容。

## 使用方式

1. 接手既有 ticket 時，先用 ID 找到本頁總覽列，讀該 ticket、AGENTS.md、來源 Spec 章節與票內指定入口；只有追查相依或目前 frontier 時才查相關總覽列。
2. 撰寫新 ticket 前，讀本頁的 [已否決方向](#已否決的方向--不要重開) 與候選清單；歷史決策紀錄只有在新證據與舊決策相關時才讀。
3. 只處理一個 ticket 的範圍，不順手實作相鄰 ticket；完成前執行票內 Agent checks 並保留結果。
4. 狀態與依賴只更新本頁 Ticket 總覽。受阻時記錄具體原因與需要的外部條件。

## 狀態

| 狀態 | 意義 |
|---|---|
| `planned` | 已定義但依賴尚未完成 |
| `ready` | 依賴已具備,可交給 Agent |
| `in_progress` | 正在實作 |
| `blocked` | 具體外部條件阻塞,不能自行繞過 |
| `done` | Agent checks 通過且交接資料完整 |
| `waived` | 使用者明確豁免工作排序 gate；未驗證條件與證據保留，不代表完成或通過 |
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
| PD-003 | 閒置資源量測基準 | 0 | `done` | PD-011 | [PD-003](tickets/PD-003-idle-resource-baseline.md) |
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
| PD-097 | 拖曳分隔線時,幾何重排本身仍在每個 `WM_MOUSEMOVE` 都執行,需節流 | 7 | `done` | PD-095 | [PD-097](tickets/PD-097-splitter-drag-geometry-throttling.md) |
| PD-098 | 檢視模式按鈕圖示由 4 方格(GridView)改為清單樣式(List)圖示 | 7 | `done` | PD-075 | [PD-098](tickets/PD-098-view-mode-icon-grid-to-list-glyph.md) |
| PD-099 | 新增第 6 種版型:上方兩格併排、下方一格全寬(2 up / 1 down) | 7 | `done` | PD-005, PD-016, PD-046 | [PD-099](tickets/PD-099-add-two-over-one-layout-template.md) |
| PD-100 | 虛擬資料夾(如 This PC)的位址列/分頁標題顯示原始 parsing code,應改為友善顯示名稱 | 7 | `done` | 無 | [PD-100](tickets/PD-100-virtual-folder-display-name-instead-of-parsing-code.md) |
| PD-101 | 新增 Tab 鍵在可見 pane 間循環切換 active pane | 7 | `done` | PD-016 | [PD-101](tickets/PD-101-tab-key-cycle-active-pane.md) |
| PD-102 | Tab「+」新增按鈕加上圓角外框,並修正「+」字符置中 | 7 | `done` | PD-081 | [PD-102](tickets/PD-102-tab-add-button-rounded-border-and-centering.md) |
| PD-103 | 移除 Group 列表項右側的 tab 數量圓形徽章(與副標題重複) | 7 | `done` | PD-028 | [PD-103](tickets/PD-103-remove-sidebar-group-tab-count-badge.md) |
| PD-104 | 側邊欄寬度可拖曳調整,並跨啟動持久化;預設寬度隨 PD-103 徽章移除而縮小 | 7 | `done` | PD-097, PD-103 | [PD-104](tickets/PD-104-resizable-persisted-sidebar-width.md) |
| PD-106 | Graceful close 後 Shell teardown 殘留程序 | 7 | `done` | PD-007, PD-032, PD-068 | [PD-106](tickets/PD-106-graceful-shutdown-shell-teardown.md) |
| PD-105 | 雙擊 pane 分隔線,重設回置中(平分兩側) | 7 | `done` | 無 | [PD-105](tickets/PD-105-double-click-splitter-resets-to-center.md) |
| PD-107 | Tab 條捲動按鈕視覺位置偏移超出自己的熱區,波及「+」新增按鈕邊界 | 7 | `done` | PD-080, PD-085 | [PD-107](tickets/PD-107-tab-nav-and-add-button-hit-test-visual-mismatch.md) |
| PD-108 | 拖曳分隔線時,矩形沒有變動的 pane 仍被重新 `SetWindowPos`/重繪 | 7 | `done` | PD-095, PD-077 | [PD-108](tickets/PD-108-skip-unchanged-pane-relayout-during-splitter-drag.md) |
| PD-109 | 一般模式啟動後主視窗未出現(HIGH,阻塞其他票驗收) | 7 | `done` | PD-093, PD-106 | [PD-109](tickets/PD-109-normal-mode-launch-shows-no-main-window.md) |
| PD-110 | Pane tab 允許跨 pane 拖曳搬移,延續 PD-035 決策 1 的「若未來需要另開新票」 | 7 | `done` | PD-035, PD-050, PD-074 | [PD-110](tickets/PD-110-cross-pane-tab-drag.md) |
| PD-111 | 每個 tab 的導覽列新增 Pinned Locations 下拉選單(Desktop / My Computer + 自訂路徑) | 7 | `done` | 無 | [PD-111](tickets/PD-111-pinned-locations-menu.md) |
| PD-112 | Pinned Locations 管理對話框(移除／排序) | 7 | `done` | PD-111 | [PD-112](tickets/PD-112-pinned-locations-manage-dialog.md) |
| PD-113 | Pane tab 右鍵選單:Close Tab / Close Other Tabs / Close All Tabs / Close Tabs to the Right | 7 | `done` | 無 | [PD-113](tickets/PD-113-tab-close-context-menu.md) |
| PD-114 | 補齊三窗格固定版型:1 up / 2 bottom 與 2 left / 1 right | 7 | `done` | PD-005, PD-016, PD-046, PD-099 | [PD-114](tickets/PD-114-add-missing-three-pane-layout-orientations.md) |
| PD-115 | 版型按鈕依 pane 數量由 1 到 4 排列 | 7 | `done` | PD-046, PD-114 | [PD-115](tickets/PD-115-order-layout-controls-by-pane-count.md) |
| PD-116 | Pane footer 資訊段落加入 Windows Explorer 風格垂直分隔線 | 7 | `done` | PD-060, PD-069 | [PD-116](tickets/PD-116-pane-footer-information-dividers.md) |
| PD-117 | Pane 內所有按鈕補上 tooltip | 7 | `done` | PD-083, PD-111 | [PD-117](tickets/PD-117-pane-button-tooltips.md) |
| PD-118 | Quick path 按鈕改用空心星號圖示 | 7 | `done` | PD-111 | [PD-118](tickets/PD-118-hollow-star-quick-path-icon.md) |
| PD-119 | Pinned Locations 管理視窗改用 Apply / Cancel | 7 | `superseded` | PD-112 | [PD-119](tickets/PD-119-pinned-locations-apply-cancel.md) |
| PD-120 | Pinned Locations 管理視窗區分 Apply、OK 與 Cancel | 7 | `done` | PD-119 | [PD-120](tickets/PD-120-pinned-locations-apply-ok-cancel-semantics.md) |
| PD-121 | 跨 Group 剪貼簿 Copy / Paste | 7 | `done` | PD-017, PD-019, PD-021, PD-023, PD-086, PD-091 | [PD-121](tickets/PD-121-cross-group-clipboard-copy-paste.md) |
| PD-122 | 跨 Group 檔案拖放 | 7 | `done` | PD-034, PD-050, PD-090, PD-091, PD-121 | [PD-122](tickets/PD-122-cross-group-file-drag-and-drop.md) |
| PD-123 | 複製進行中關閉 PaneDock 的驗證、確認與修正 | 7 | `done` | PD-023, PD-032, PD-106, PD-121, PD-122 | [PD-123](tickets/PD-123-close-during-shell-copy-validation.md) |
| PD-124 | 巢狀 modal loop 內關閉時 `WM_QUIT` 被吃掉,外層 message loop 永不退出(程序殘留) | 7 | `done` | PD-007, PD-032, PD-068, PD-106 | [PD-124](tickets/PD-124-nested-modal-loop-close-lost-quit-message.md) |
| PD-125 | 關閉序列重入與 `WM_ENDSESSION` 未退出訊息迴圈(違反 §9.4) | 7 | `done` | PD-007, PD-032, PD-106, PD-124 | [PD-125](tickets/PD-125-close-sequence-reentrancy-and-endsession-exit.md) |
| PD-126 | 啟動後立即關閉(UI 剛出現)會在啟動 MessageBox 的 modal loop 內吃掉 `WM_QUIT`,外層 loop 空佇列時永遠阻塞 | 7 | `done` | PD-124, PD-125 | [PD-126](tickets/PD-126-early-close-after-start-lost-quit-before-loop.md) |
| PD-127 | teardown 重入未受守衛:queued 訊息在 `IExplorerBrowser::Destroy` pump 內重入,重新建立 live view 後 parent 才被銷毀 | 7 | `done` | PD-124, PD-125, PD-126 | [PD-127](tickets/PD-127-teardown-reentrancy-guard-and-deferred-startup-error.md) |
| PD-128 | `panedock_launch_smoke`／關閉時間歇性 `STATUS_STACK_BUFFER_OVERRUN`(0xC0000409)的調查 | 7 | `deferred` | 再次出現 `0xC0000409` 的 runtime repro | [PD-128](tickets/PD-128-investigate-launch-smoke-stack-buffer-overrun.md) |
| PD-129 | single-instance 啟動競態:前一個 AP 正在關閉／卡死會靜默 no-op,第二個執行個體無 UI 無提示 | 7 | `done` | (none) | [PD-129](tickets/PD-129-single-instance-startup-race-relay.md) |
| PD-130 | startup 絕不能因「可復原的 pane/view 失敗」而整窗開不起來或靜默空白;每個失敗都要有提示 | 7 | `done` | (none) | [PD-130](tickets/PD-130-startup-recoverable-failure-never-blocks-window.md) |
| PD-131 | single-instance mutex 重新取得競態可啟動兩個 PaneDock 並競寫 session | 7 | `done` | PD-084, PD-129 | [PD-131](tickets/PD-131-single-instance-reacquire-race.md) |
| PD-132 | 被取消的 Windows shutdown 會留下錯誤 clean marker，掩蓋後續 force kill／crash | 7 | `done` | PD-025, PD-032, PD-125 | [PD-132](tickets/PD-132-session-query-marks-cancelled-shutdown-clean.md) |
| PD-133 | startup／normal close 的 session save failure 對使用者完全靜默 | 7 | `done` | PD-006, PD-025, PD-078, PD-132 | [PD-133](tickets/PD-133-session-save-failure-is-silent.md) |
| PD-134 | 主視窗還原到已不存在監視器的位置：app「開啟」但無可見 UI | 7 | `done` | — | [PD-134](tickets/PD-134-offscreen-window-restore.md) |
| PD-135 | 啟動警告 MessageBox 的 modal loop 內被關閉：後續啟動序對已銷毀 HWND 執行 | 7 | `done` | PD-126, PD-127, PD-130 | [PD-135](tickets/PD-135-startup-dialog-close-destroyed-hwnd.md) |
| PD-136 | close save failure 在 `WM_DESTROY` 重試且吞掉進行中的 Windows shutdown | 7 | `done` | PD-125, PD-133 | [PD-136](tickets/PD-136-shutdown-save-decision-reentrancy.md) |
| PD-137 | session backup copy/restore 不是 atomic replace，force kill 可能留下半份設定檔 | 7 | `done` | PD-006, PD-078 | [PD-137](tickets/PD-137-atomic-session-backup-replacement.md) |
| PD-138 | startup 將 filesystem status error／backup-only recovery 當成無需提示 | 7 | `done` | PD-025, PD-078 | [PD-138](tickets/PD-138-session-read-error-recovery-signal.md) |
| PD-139 | single-instance relay 對 stale/closing/hung HWND 靜默退出 | 7 | `done` | PD-084, PD-129, PD-131 | [PD-139](tickets/PD-139-single-instance-activation-handshake.md) |
| PD-140 | Shell API re-entry 可在 outer call 未返回前 destroy parent 或繼續碰死 COM | 7 | `done` | PD-125, PD-127, PD-136 | [PD-140](tickets/PD-140-shell-call-reentry-shutdown-gate.md) |
| PD-141 | startup 先顯示 frame，再進行 active/其餘 Shell realization | 7 | `done` | PD-093, PD-109, PD-130, PD-140 | [PD-141](tickets/PD-141-startup-frame-before-shell-realize.md) |
| PD-142 | 移除 startup frame 顯示前重複的 virtual-folder label Shell lookup | 7 | `done` | PD-141 | [PD-142](tickets/PD-142-startup-no-prewindow-shell-label-lookup.md) |
| PD-143 | clean marker 延後到完整 Shell/COM teardown 後，避免 force-kill false-clean | 7 | `done` | PD-025, PD-032, PD-132, PD-136 | [PD-143](tickets/PD-143-clean-marker-after-teardown.md) |
| PD-144 | startup failure warnings 不得互相覆蓋，deferred Shell failure 必須提示 | 7 | `done` | PD-130, PD-135, PD-141, PD-142 | [PD-144](tickets/PD-144-startup-warning-aggregation.md) |
| PD-145 | startup message loop 前不得同步執行可避免的 Shell chrome lookup | 7 | `done` | PD-141, PD-142, PD-144 | [PD-145](tickets/PD-145-defer-startup-shell-chrome.md) |
| PD-146 | 補齊 display-name Shell lookup 的 re-entry shutdown guard | 7 | `done` | PD-140, PD-145 | [PD-146](tickets/PD-146-display-name-shell-reentry-guard.md) |
| PD-147 | `ExplorerHost` Shell callback 未進入 app re-entry gate，close 可在 callback 內 teardown | 7 | `done` | PD-140, PD-146 | [PD-147](tickets/PD-147-explorerhost-callback-reentry-shutdown-guard.md) |
| PD-148 | activation handshake 將 `SetForegroundWindow` 拒絕誤判為成功 | 7 | `done` | PD-139 | [PD-148](tickets/PD-148-activation-handshake-reports-foreground-failure.md) |
| PD-149 | 版型按鈕失焦後殘留虛線 focus border | 7 | `done` | PD-056, PD-115 | [PD-149](tickets/PD-149-layout-button-focus-border-ghost.md) |
| PD-150 | Group row 按 F2 開始 inline rename | 7 | `done` | PD-017, PD-021, PD-023 | [PD-150](tickets/PD-150-group-row-f2-rename.md) |
| PD-151 | pane 為穩定 identity，切換版型不搬移 tab（覆寫 design-spec FR-003 併入語意） | 7 | `done` | PD-004, PD-005, PD-006, PD-009, PD-087 | [PD-151](tickets/PD-151-stable-pane-identity-across-layout-switch.md) |
| PD-152 | Group row inline rename editor 偶發只剩 caret、背景與文字不可見 | 7 | `done` | PD-017, PD-041, PD-150 | [PD-152](tickets/PD-152-group-row-rename-editor-occasionally-invisible.md) |
| PD-153 | 新 tab 預設使用詳細資料檢視 | 7 | `done` | PD-052, PD-079, PD-082 | [PD-153](tickets/PD-153-default-view-mode-details.md) |
| PD-154 | 雙擊 pane tab 條空白區新增 tab | 7 | `done` | PD-019, PD-049, PD-055 | [PD-154](tickets/PD-154-double-click-empty-tab-strip-adds-tab.md) |
| PD-155 | Window、pane splitter 與 Groups sidebar 共用原子 live-resize geometry transaction | 7 | `done` | PD-077, PD-095, PD-097, PD-104, PD-108 | [PD-155](tickets/PD-155-atomic-live-resize-geometry-transaction.md) |
| PD-156 | 擷取、持久化並還原 Shell 排序欄位與方向 | 7 | `done` | PD-006, PD-052, PD-153 | [PD-156](tickets/PD-156-shell-sort-state-restore.md) |
| PD-157 | 建立 `shell_core` 並移出 app_shell 的 Shell location/value 操作 | 7 | `done` | PD-006, PD-022, PD-140, PD-146 | [PD-157](tickets/PD-157-shell-core-location-boundary.md) |
| PD-158 | 建立 `file_operations` 並讓 Ctrl+V 正確區分 Cut／Copy | 7 | `done` | PD-023, PD-121, PD-123, PD-140, PD-157 | [PD-158](tickets/PD-158-cut-aware-clipboard-paste.md) |
| PD-159 | pane 空白處的背景 Shell verb 無法取得目前資料夾 | 7 | `done` | PD-007, PD-014, PD-024 | [PD-159](tickets/PD-159-background-shell-verb-current-folder.md) |
| PD-160 | 完成 Shell location 三段式 identity 擷取與還原 | 7 | `done` | PD-006, PD-022, PD-140, PD-157 | [PD-160](tickets/PD-160-shell-location-identity-capture-and-resolution.md) |
| PD-161 | pane footer 新增目前資料夾背景選單按鈕（含 hover 與 tooltip） | 7 | `done` | PD-060, PD-083, PD-117, PD-159 | [PD-161](tickets/PD-161-pane-footer-folder-context-menu-button.md) |
| PD-162 | 把關閉／重入決策抽成 `core` 的 shutdown sequence reducer | 7 | `done` | PD-123, PD-140, PD-148, PD-155, PD-159 | [PD-162](tickets/PD-162-shutdown-sequence-decision-reducer.md) |
| PD-163 | 把 pane 的 chrome 子視窗收進單一 `PaneChrome` 型別 | 7 | `done` | PD-020, PD-040, PD-060, PD-117, PD-151, PD-155 | [PD-163](tickets/PD-163-pane-chrome-child-window-ownership.md) |
| PD-164 | 把 realize／de-realize 決策抽成 `core` 的純函式 | 7 | `done` | PD-005, PD-009, PD-093, PD-151, PD-155, PD-163 | [PD-164](tickets/PD-164-realization-plan-pure-function.md) |
| PD-165 | 把檢視模式字串 codec 移進 `shell_core` | 7 | `done` | PD-052, PD-079, PD-153, PD-156, PD-157 | [PD-165](tickets/PD-165-view-mode-codec-to-shell-core.md) |
| PD-166 | startup recoverable warning 改為非同步 `OK` 通知，fatal startup error 維持同步退出 | 7 | `done` | PD-130, PD-135, PD-141, PD-144 | [PD-166](tickets/PD-166-startup-notification-dialog-routing.md) |
| PD-167 | 所有新 tab 的預設 Shell location 統一為 My Computer | 7 | `done` | PD-019, PD-154, PD-160 | [PD-167](tickets/PD-167-default-tab-location-my-computer.md) |
| PD-168 | 導覽/Tab/Group 切換在 UI 執行緒同步呼叫 Shell resolve/BrowseTo，慢速路徑凍結整個 app（NFR-003） | 7 | `done` | PD-140, PD-157 | [PD-168](tickets/PD-168-navigate-synchronous-shell-resolution-blocks-ui.md) |
| PD-169 | Pinned/Tab/Startup 顯示名稱查詢仍在 UI 執行緒同步呼叫 `SHCreateItemFromParsingName` | 7 | `done` | PD-168, PD-100, PD-146 | [PD-169](tickets/PD-169-display-name-lookup-synchronous-shell-call.md) |
| PD-170 | 導覽完成/失敗回呼沒有 request/tab/Group identity，pending 導覽可能寫進錯的 tab | 7 | `done` | PD-020, PD-086 | [PD-170](tickets/PD-170-navigation-completion-lacks-request-identity.md) |
| PD-171 | `apply_layout` 持有的 Group reference 在 Shell 呼叫重入時可能失效（dangling reference） | 7 | `done` | PD-140 | [PD-171](tickets/PD-171-shell-reentry-invalidates-group-vector-reference.md) |
| PD-172 | deferred shutdown 訊息在 nested Shell call 內被吃掉，程式可能永遠關不掉 | 7 | `done` | PD-140, PD-162 | [PD-172](tickets/PD-172-deferred-shutdown-message-swallowed-in-nested-shell-call.md) |
| PD-173 | 拖曳進行中關閉：OLE 拖曳生命週期沒有納入任何 shell-call/shutdown gate | 7 | `done` | PD-090, PD-140, PD-162 | [PD-173](tickets/PD-173-close-during-active-drag-not-gated.md) |
| PD-174 | 把 session save 的 500ms debounce 擴大到 tab/Group/layout/splitter 等其餘同步 `save_now` 呼叫點 | 7 | `done` | PD-091 | [PD-174](tickets/PD-174-extend-session-save-debounce-to-remaining-call-sites.md) |
| PD-175 | 位址列在導覽失敗後被還原成舊路徑，使用者剛輸入的內容沒有保留 | 7 | `done` | PD-020, PD-022 | [PD-175](tickets/PD-175-address-bar-reverts-to-stale-path-on-failed-navigation.md) |
| PD-176 | 追查閒置 10 分鐘仍有 307294 bytes 磁碟 I/O 的來源（NFR-001 磁碟門檻 FAIL） | 7 | `done` | PD-003, PD-026 | [PD-176](tickets/PD-176-diagnose-idle-disk-io-source.md) |
| PD-177 | Shutdown 恢復條件在 shell_call_left/drag_finished 各自只檢查一部分，收斂為共用 predicate | 7 | `done` | PD-172, PD-173 | [PD-177](tickets/PD-177-shutdown-gate-conditions-duplicated-across-call-sites.md) |
| PD-178 | 以 `decode_pane_control` 純函式取代 15 處 control ID 算術，並清掉五組逐字複製 | 7 | `done` | PD-161, PD-163 | [PD-178](tickets/PD-178-pane-control-id-decode-and-mechanical-dedup.md) |
| PD-179 | 把 `window_proc` 的巨型 case 依所有權域拆成具名函式 | 7 | `done` | PD-178 | [PD-179](tickets/PD-179-split-window-proc-cases-by-ownership-domain.md) |
| PD-180 | 把三個對話框搬出 `main.cpp`，並收斂 window class 註冊樣板 | 7 | `done` | PD-179 | [PD-180](tickets/PD-180-extract-dialogs-from-main.md) |
| PD-181 | 把 error window 從 `ExplorerHost` 抽成獨立的 `PaneErrorOverlay` | 7 | `done` | PD-180 | [PD-181](tickets/PD-181-extract-pane-error-overlay-from-explorer-host.md) |
| PD-182 | `PaneChrome` 升級為 `Pane`，接手六個純 UI 的 pane-parallel 欄位 | 7 | `done` | PD-163, PD-178, PD-179 | [PD-182](tickets/PD-182-pane-type-owns-ui-state.md) |
| PD-183 | `Pane` 接手 `ExplorerHost`，把 §9.4 的 destroy 順序變成解構子不變量 | 7 | `done` | PD-182 | [PD-183](tickets/PD-183-pane-owns-explorer-host-lifetime.md) |
| PD-184 | `core` 保證 `PaneState` 位址穩定性，讓 UI 層可長期持有 `PaneState*` | 7 | `done` | PD-004, PD-006 | [PD-184](tickets/PD-184-panestate-address-stability-invariant.md) |
| PD-185 | `Pane` 綁定 `core::PaneState*`，Group 切換改為重新綁定而非複製 | 7 | `done` | PD-182, PD-183, PD-184 | [PD-185](tickets/PD-185-pane-binds-core-pane-state.md) |
| PD-186 | 以 `Pane` 綁定指標取代約 85 處 `active_group(state).panes[i]` 與重複 guard | 7 | `done` | PD-185 | [PD-186](tickets/PD-186-pane-scoped-state-access-through-binding.md) |
| PD-187 | 為每個 pane 註冊 `PaneDock.Pane` window class，子控制項改掛 pane HWND | 7 | `done` | PD-183, PD-185 | [PD-187](tickets/PD-187-per-pane-window-class-and-hwnd.md) |
| PD-188 | pane 卡片與導覽列背景改由 pane 自己繪製，移出 `paint_client_background` | 7 | `done` | PD-187 | [PD-188](tickets/PD-188-pane-paints-its-own-card.md) |
| PD-189 | pane 子控制項的命令與通知改由 pane proc 就地處理，`decode_pane_control` 收斂 | 7 | `done` | PD-186, PD-187 | [PD-189](tickets/PD-189-pane-proc-handles-its-own-commands.md) |
| PD-190 | 六支 pane chrome 刷新函式與 `pending_navigation` 收進 `Pane` | 7 | `done` | PD-186 | [PD-190](tickets/PD-190-pane-scoped-chrome-refresh-and-navigation-request.md) |
| PD-191 | `PaneHost` 協調層服務介面，讓 `Pane` 能持有 pane 自己的行為 | 7 | `done` | PD-190 | [PD-191](tickets/PD-191-pane-host-coordinator-service-interface.md) |
| PD-192 | pane 的導覽群收進 `Pane` | 7 | `done` | PD-191 | [PD-192](tickets/PD-192-pane-owns-its-navigation.md) |
| PD-193 | 檢視模式、排序與位置擷取收進 `Pane` | 7 | `done` | PD-191 | [PD-193](tickets/PD-193-pane-owns-view-mode-sort-and-location-capture.md) |
| PD-194 | pane 的 tab 生命週期命令收進 `Pane` | 7 | `done` | PD-191, PD-192, PD-193 | [PD-194](tickets/PD-194-pane-owns-its-tab-commands.md) |
| PD-195 | 位址列與釘選位置收進 `Pane` | 7 | `done` | PD-191, PD-192 | [PD-195](tickets/PD-195-pane-owns-address-bar-and-pinned-locations.md) |
| PD-196 | tab strip 收進 `Pane`（`PaneTabStrip`） | 7 | `done` | PD-191, PD-194 | [PD-196](tickets/PD-196-pane-owns-its-tab-strip.md) |
| PD-197 | pane 命令、chrome 與導覽完成的收尾 | 7 | `in_progress` | PD-192, PD-193, PD-194, PD-195, PD-196 | [PD-197](tickets/PD-197-pane-command-and-chrome-consolidation.md) |
| PD-198 | 剩餘 pane 局部訊息與 popup 行為歸位 | 7 | `done` | PD-197 | [PD-198](tickets/PD-198-pane-local-message-and-popup-ownership.md) |
| PD-199 | 導覽請求身分與分頁身分的正確性修正 | 7 | `done` | PD-170, PD-183, PD-196 | [PD-199](tickets/PD-199-navigation-request-identity-and-tab-identity-fixes.md) |
| PD-200 | group list 重排手勢收進 `sidebar::Sidebar` | 7 | `done` | PD-057, PD-196 | [PD-200](tickets/PD-200-sidebar-owns-the-group-reorder-gesture.md) |
| PD-201 | session 存檔時機收進 `app_shell::SessionWriter` | 7 | `done` | PD-091, PD-200 | [PD-201](tickets/PD-201-session-writer-owns-persistence-timing.md) |
| PD-202 | pane chrome 幾何抽為純函式並補測試 | 7 | `done` | PD-197, PD-201 | [PD-202](tickets/PD-202-pane-chrome-geometry-is-a-pure-tested-unit.md) |
| PD-203 | OS session end 先寫 durable checkpoint，Shell teardown 改 best-effort（覆寫 PD-143 排序） | 7 | `done` | PD-025, PD-032, PD-132, PD-143 | [PD-203](tickets/PD-203-endsession-checkpoint-before-shell-teardown.md) |
| PD-204 | back/forward 導航失敗時回滾 model，避免保存從未顯示過的位置 | 7 | `done` | PD-203 | [PD-204](tickets/PD-204-rollback-failed-history-navigation.md) |
| PD-205 | Shell 呼叫期間的延遲訊息改為 pending flush，消除 PostMessage 自旋 | 7 | `done` | PD-171, PD-204 | [PD-205](tickets/PD-205-deferred-shell-reentry-messages-stop-spinning.md) |
| PD-206 | Group transition 全程抑制 location capture，避免半切換狀態污染新 Group | 7 | `done` | PD-203, PD-204 | [PD-206](tickets/PD-206-suppress-location-capture-across-the-whole-group-transition.md) |
| PD-207 | 移除切換熱路徑上的冗餘 Shell 呼叫與重繪 | 7 | `done` | PD-206 | [PD-207](tickets/PD-207-drop-redundant-work-from-the-switching-hot-path.md) |
| PD-208 | Group 切換時 realize 失敗必須有使用者可見提示 | 7 | `done` | PD-093 | [PD-208](tickets/PD-208-group-switch-realization-failure-is-user-visible.md) |
| PD-209 | Group 切換／tab realize 的延遲計時儀器（覆寫 PD-026 的儀器排除） | 7 | `done` | PD-024, PD-026 | [PD-209](tickets/PD-209-switching-latency-instrumentation.md) |
| PD-210 | 同步失敗的導覽必須把 `latest_navigation_generation_` 還給仍在飛的請求 | 7 | `done` | PD-199, PD-207 | [PD-210](tickets/PD-210-restore-latest-generation-when-a-navigation-fails-synchronously.md) |
| PD-211 | PaneDock 自己的 popup 選單必須持有 `ShellCallScope` | 7 | `done` | PD-171, PD-205 | [PD-211](tickets/PD-211-our-own-popup-menus-must-hold-a-shell-call-scope.md) |
| PD-212 | 延遲的指標／hover 訊息只保留最後一筆意圖 | 7 | `done` | PD-205 | [PD-212](tickets/PD-212-deferred-pointer-messages-keep-only-the-latest-intent.md) |
| PD-213 | 把「單執行緒訊息迴圈」寫成明示的不變量，並移除為並行而存在的機制 | 7 | `done` | — | [PD-213](tickets/PD-213-single-threaded-message-loop-is-a-stated-invariant.md) |
| PD-214 | 一個 `ShellReentryGuard` 擁有 Shell re-entry 的暫存、重播與欠下的關閉 | 7 | `done` | PD-205, PD-211, PD-212, PD-213 | [PD-214](tickets/PD-214-one-shell-reentry-guard-owns-hold-replay-and-owed-shutdown.md) |
| PD-215 | `SessionWriter` 擁有擷取抑制與擷取步驟（覆寫 PD-201 的界線） | 7 | `done` | PD-201, PD-206, PD-214 | [PD-215](tickets/PD-215-session-writer-owns-capture-suppression-and-the-capture-step.md) |
| PD-216 | `ShutdownCoordinator` 擁有 shutdown 的 effect 順序（覆寫 2026-09-03 F2） | 7 | `done` | PD-203, PD-214, PD-215 | [PD-216](tickets/PD-216-shutdown-coordinator-owns-the-effect-order.md) |
| PD-217 | `run_group_transition` 在 core 執行 Group 切換的步驟 | 7 | `done` | PD-206, PD-215 | [PD-217](tickets/PD-217-group-transition-runs-its-steps-in-core.md) |
| PD-218 | 取消系統結束工作階段後恢復關閉狀態 | 7 | `done` | PD-203, PD-216 | [PD-218](tickets/PD-218-cancelled-session-end-restores-close-state.md) |
| PD-219 | session 最後改名失敗時還原主檔 | 7 | `done` | PD-137 | [PD-219](tickets/PD-219-restore-primary-after-session-rename-failure.md) |
| PD-220 | launch smoke 使用隔離的 session 儲存位置 | 7 | `done` | PD-218 | [PD-220](tickets/PD-220-isolate-launch-smoke-session-storage.md) |
| PD-221 | Active pane 改用標題區淡藍底、實心 tab 與 inactive 淡化（覆寫 PD-063 頂線） | 7 | `done` | PD-063, PD-076, PD-188 | [PD-221](tickets/PD-221-active-pane-chrome-tint.md) |
| PD-222 | 對齊自建列表的規格與工程限制 | 8 | `done` | — | [PD-222](tickets/PD-222-managed-list-product-contract.md) |
| PD-223 | Pane 內容入口 expand，保持既有行為 | 8 | `done` | PD-222 | [PD-223](tickets/PD-223-pane-content-expand.md) |
| PD-224 | 單 pane 本機資料夾分批列表 | 8 | `done` | PD-223 | [PD-224](tickets/PD-224-managed-list-local-batches.md) |
| PD-225 | 自建列表接通 tab、Group 與 session 復原 | 8 | `done` | PD-224 | [PD-225](tickets/PD-225-managed-list-tab-group-restore.md) |
| PD-226 | 慢網路、容量耗盡與 pending shutdown | 8 | `in_progress` | PD-224 | [PD-226](tickets/PD-226-managed-list-slow-location-capacity.md) |
| PD-227 | 列表選取、鍵盤與 accessibility | 8 | `done` | PD-224 | [PD-227](tickets/PD-227-managed-list-selection-keyboard.md) |
| PD-228 | 基本排序與穩定更新 | 8 | `done` | PD-227 | [PD-228](tickets/PD-228-managed-list-basic-sort.md) |
| PD-229 | 可見項目圖示與 overlay | 8 | `done` | PD-224 | [PD-229](tickets/PD-229-managed-list-visible-icons.md) |
| PD-230 | 縮圖與既有 view mode 對應 | 8 | `done` | PD-229 | [PD-230](tickets/PD-230-managed-list-thumbnails-view-modes.md) |
| PD-231 | 昂貴屬性欄位與排序 | 8 | `done` | PD-228 | [PD-231](tickets/PD-231-managed-list-expensive-properties.md) |
| PD-232 | 本機變更通知與恢復掃描 | 8 | `done` | PD-228 | [PD-232](tickets/PD-232-managed-list-change-notifications.md) |
| PD-233 | tab 列表 snapshot 的全域有界快取 | 8 | `done` | PD-225, PD-228, PD-232 | [PD-233](tickets/PD-233-managed-list-tab-cache.md) |
| PD-234 | 虛擬 Shell location 導覽（umbrella；交付拆為 PD-243～PD-246） | 8 | `in_progress` | PD-246 | [PD-234](tickets/PD-234-managed-list-shell-namespace.md) |
| PD-235 | OneDrive placeholder 瀏覽 | 8 | `in_progress` | PD-229, PD-246 | [PD-235](tickets/PD-235-managed-list-onedrive-placeholders.md) |
| PD-236 | 自建選取接回剪貼簿與 Shell 檔案操作 | 8 | `in_progress` | PD-227, PD-246 | [PD-236](tickets/PD-236-managed-list-shell-file-operations.md) |
| PD-237 | 自建列表 inline rename | 8 | `done` | PD-232, PD-236 | [PD-237](tickets/PD-237-managed-list-inline-rename.md) |
| PD-238 | 跨 pane 與外部 drag/drop | 8 | `in_progress` | PD-225, PD-236 | [PD-238](tickets/PD-238-managed-list-drag-drop.md) |
| PD-239 | 原生選單目標相容性接線 | 8 | `in_progress` | PD-227, PD-246 | [PD-239](tickets/PD-239-managed-list-native-menu-compatibility.md) |
| PD-240 | 新列表的 live resize 與 mixed DPI | 8 | `done` | PD-224 | [PD-240](tickets/PD-240-managed-list-live-resize-dpi.md) |
| PD-241 | 自建列表相容性及效能 gate，通過後切預設 | 8 | `waived` | PD-226, PD-230, PD-231, PD-233, PD-235, PD-237, PD-238, PD-239, PD-240 | [PD-241](tickets/PD-241-managed-list-rollout-gate.md) |
| PD-242 | contract：移除舊 Shell view 路徑 | 8 | `in_progress` | 無 | [PD-242](tickets/PD-242-managed-list-remove-shell-view.md) |
| PD-243 | PD-234 切片：列表 item identity 與顯示名稱分離 | 8 | `done` | PD-227, PD-228, PD-232, PD-233 | [PD-243](tickets/PD-243-managed-list-item-identity.md) |
| PD-244 | PD-234 切片：圖示與屬性請求使用 Shell item identity | 8 | `done` | PD-243, PD-229, PD-230, PD-231 | [PD-244](tickets/PD-244-managed-list-enrichment-identity.md) |
| PD-245 | PD-234 切片：Worker STA 的 namespace 列舉與 parent 值 | 8 | `done` | PD-243 | [PD-245](tickets/PD-245-managed-list-namespace-worker.md) |
| PD-247 | 列表不提供 accessibility provider（修正排序／複製卡頓） | 8 | `done` | PD-227 | [PD-247](tickets/PD-247-managed-list-no-accessibility-provider.md) |
| PD-248 | 「本機」磁碟並排顯示與容量用量條 | 8 | `in_progress` | PD-247 | [PD-248](tickets/PD-248-managed-list-this-pc-drive-tiles.md) |
| PD-249 | 有實體路徑的 Shell namespace 自動更新（桌面） | 8 | `in_progress` | PD-247 | [PD-249](tickets/PD-249-managed-list-namespace-auto-refresh.md) |
| PD-250 | 移除無障礙支援：列表使用不被 MSAA／UIA 辨識的視窗類別 | 8 | `in_progress` | PD-247 | [PD-250](tickets/PD-250-managed-list-opaque-window-class.md) |
| PD-251 | 快速連點欄標題時排序要連續切換 | 8 | `in_progress` | PD-247 | [PD-251](tickets/PD-251-managed-list-column-title-double-click.md) |
| PD-246 | PD-234 切片：Pane 虛擬導覽、預設位置與 session 還原 | 8 | `done` | PD-225, PD-244, PD-245 | [PD-246](tickets/PD-246-managed-list-virtual-navigation.md) |


### PD-241 自動化 checkpoint（2026-10-20）

本回合由 Codex 執行的證據：Release PaneDock 與 16 個 managed-list unit targets 建置成功；fixture-only 修復 `a624e96` 後執行選定 20-test 集合的 CTest 命令，全部通過。另行執行的 `panedock_managed_list` 因 fixture 缺 parent HWND、`navigate_to` 失敗而失敗／segfault，排除於通過集合；其使用者既有 WIP 不修改。完整範圍與失敗證據見 [PD-241 checkpoint](tickets/PD-241-managed-list-rollout-gate.md#自動化-checkpoint2026-10-20)。

依使用者指示不跑完整 CTest、GUI、launch smoke 或真實 Shell／OneDrive／network 測試；真實矩陣、10 分鐘 idle CPU／disk／memory、UNC／OneDrive、真正 Shell extensions／verbs 與 launch／session 行為仍未驗證，gate 尚未通過。當時保留 `IExplorerBrowser` 預設並要求 PD-242 等待 gate；該排序限制由下列 2026-10-21 waiver 覆寫。右鍵優先原生 Windows Shell menu，僅必要時才自訂，本次不需要。

### PD-241 explicit waiver checkpoint（2026-10-21）

使用者明確授權跳過真實驗證並繼續後續 tickets；rollout measurement gate 僅就工作排序豁免，並未通過。具體覆寫見 [PD-242 Overrides](tickets/PD-242-managed-list-remove-shell-view.md#overrides) 與 [design-spec Phase 8 override](design-spec.md#phase-8-override2026-10-21)。原有未勾選驗收條件、未驗證風險、失敗與自動化證據全部保留，不得解讀為 PASS 或完成。此 checkpoint 只改文件，不改實作，也不執行真實測試、GUI、launch smoke 或完整測試套件。

### PD-242 implementation checkpoint（2026-10-21）

依同日 sequencing-only waiver 實作 managed-list-only，移除 legacy host 及 view-only targets，保留 Pane HWND／list realization 與 Shell 原生 menu／namespace／operations／OLE／drag-drop。Release build 在 ticket 變更及測試 fixture 更新後通過；相關 27 項 CTest 均通過（26 項於一次聚焦執行通過，`panedock_managed_list` 補上 Pane parent HWND 後重跑通過）。該測試在目前身分無法改寫臨時目錄 DACL 時，明確跳過真實 ACL-denial 分支；受控 source 的錯誤 seam 有測試。完整 CTest 與實機驗收未執行，resource lifetime／binary gate 仍未完成。詳見 [PD-242 交接區](tickets/PD-242-managed-list-remove-shell-view.md#交接區)；總覽維持 `in_progress`。

### PD-197 範圍複驗 checkpoint（2026-10-21）

指定的四支 Pane 成員、完整 `Pane::navigation_complete` 與兩個過渡 hook 的清除均已存在；本次沒有剩餘可在原 scope 內完成的程式搬移。完整逐項對照、六處 `pane_index` 命中與驗證命令見 [PD-197 本次複驗](tickets/PD-197-pane-command-and-chrome-consolidation.md#範圍複驗2026-10-21)。2026-09-05 全檔盤點與模組契約更新現存於 [ticket history](ticket-history.md#2026-09-05--pd-197-全檔複驗指定整併完成剩餘局部行為開-pd-198)，tab-strip 候選的刪除線仍在本頁。

目前 `main.cpp` 為 **4,246 行**，距 Acceptance 5 的嚴格「3,300 行以下」仍須減少至少 **947 行**。剩餘類別為跨 pane tab drag／hover／active-pane、layout／splitter／全視窗繪製、Group CRUD／rebind／sidebar、app header／全域 popup manager、startup／single-instance／message loop／DPI、session／shutdown／file-operation gates 與 singleton hover 狀態；各自涉及多 pane、全域資源或 app 生命週期，均屬原票明列的 coordinator non-goals。目標 pane 的訊息解碼仍經共同 shutdown／re-entry gate 後委派 Pane；PaneHost 的 rename／drag adapter 供應全域 operation／shutdown gates。沒有發現原系列仍漏搬的 pane-only 行為，也沒有為行數門檻搬走這些 non-goals。

focused Release `panedock_pane_test` 建置為最新，`panedock_pane` **1/1 通過**；`address_bar_failure_check.ps1` 與 `shell_reentry_gate_check.ps1` 均通過。未執行完整 CTest、launch smoke、GUI、真實 Shell 或實機測試。跳過驗證的授權不改寫需求：行數落差及行為／視覺未驗證仍存在，總覽保留 `in_progress`。

## 自建列表遷移（2026-10-16 使用者核准）

本地 tracker 承接已核准的 21 個切片，詳見 [功能規格](research/2026-10-16-managed-list-spec.md)。
PD-222～PD-242 的狀態與依賴只以以上 Ticket 總覽為準；先 expand 保留舊內容路徑，
再逐項接通新列表；原訂通過相容性與效能 gate 才切換預設、最後 contract 移除舊 Shell view 的排序，由 2026-10-21 explicit waiver 覆寫，量測與相容性仍未通過。
優先順序為 UX → CPU／memory／power → 架構與可維護性。
右鍵選單不改造；PD-239 只做替換 view 所必要的目標／生命週期相容性接線。
這是使用者直接變更產品方向，不再以 Phase 0 No-Go 或 PD-209 量測作為「允許規劃自建列表」的前提；
實際效能改善仍須量測，既有 Shell 路徑未隨切票改變。舊 completed ticket 與未結案工作不在本次重寫。
GitHub tracker／triage 尚未設定，本系列未發布為 GitHub issues；本地工作可依總覽 frontier 開始。

### PD-234 自動化驗證 checkpoint（2026-10-21）

PD-242 後，PD-243～PD-246 focused Release targets 聯集建置成功，10 項 CTests 全數通過（9.99 秒）；覆蓋 virtual identity／enumeration 值、Pane navigation／history／session restore、controlled worker capacity／re-entry／shutdown。詳見 [PD-234 checkpoint](tickets/PD-234-managed-list-shell-namespace.md#自動化驗證-checkpoint2026-10-21)。僅自動化契約通過；真實 This PC／Desktop／known-folder restore、provider STA lifetime／shutdown 未驗證，需真實桌面。未執行完整 CTest、GUI、launch smoke、真實 Shell／OneDrive／network／provider 測試。產品 acceptance 與 PD-241 gate 不宣告 PASS；下一個本票 gate 是真實 Shell 驗收。

### PD-234 切分依據（2026-10-19）

追查既有實作後，單一 namespace backend 不足以交付 PD-234：Row::name 同時作 display／selection／anchor／recovery key；圖示與屬性 worker 拼接 parent＋name；Batch 缺少獨立 canonical location／parent；Pane 的 realize、navigate、Up、history／restore 又有 filesystem-only 假設。跨上述契約遷移及回歸驗證超出半天至兩天切片，因此候選交付拆為 PD-243（item identity）、PD-244（enrichment identity）、PD-245（namespace worker）、PD-246（Pane／session 整合），各票均含 caller、邊界與聚焦 checks。切分不縮減 PD-234 驗收，不改 stage-reveal layout。

PD-234 保留為產品 umbrella，子票完成不自動宣告其真實 Shell 驗收通過。實機／UI 驗證由使用者延後，This PC／Desktop children、known-folder restore、provider apartment／shutdown 仍未驗證，需真實桌面；seam tests 不代表 Shell PASS。PD-235／236／239 直接等待實際整合切片；具體依賴與狀態僅見總覽表。PD-245 沿用 PD-226 的容量／shutdown 交付，不代替或修改該票。未重開「已否決的方向」中的 fake ExplorerBrowser、Shell UI 自動化、runtime rewrite 或程序隔離。

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

**2026-09-08 註記(PD-137 的備份輪替做法已被取代)**:PD-137 設計的 `session.json.bak.tmp` 中間檔——把有效 primary `copy_file` 到該檔、跑第二次 durability hook、再 rename 成 `session.json.bak`——已改為**直接 `rename(session.json → session.json.bak)`**。舊做法把當初已 flush 過的位元組再複製並再 flush 一次,買不到任何保證;新做法每次寫入只有一次 `FlushFileBuffers`、零資料複製、磁碟上少一個檔案。PD-137 要保護的兩個不變式都保留:寫入仍是「temp ＋ flush ＋ rename」的原子替換,且「損壞的 primary 不得覆蓋 last known-good backup」的驗證閘門仍在(改為以 `read_file(primary)` 的結果直接判斷,取代原本多餘的 `exists()`)。

rename 輪替在兩個 rename 之間存在「primary 暫時不存在」的視窗期;為此 `read_session` 的候選順序改為 **primary → tmp → backup**,`.tmp` 只在 primary 讀不到時才被採用,並回報新的 `SessionSource::interrupted_write`(`recovered_from_corruption = false`,因為沒有任何遺失,不該對使用者發警告)。採用 `.tmp` 的安全性依據是實測:把一份 513 bytes 的真實文件在每個位元組截斷(512 種)與從每個位置起填零(513 種),`deserialize_session` 全部拒絕——嚴格 JSON parser 要求括號成對且吃完整個輸入,加上欄位型別檢查與 `is_valid`,已足以擋掉未寫完的 `.tmp`。唯一擋不到的是值內部單一位元組被改(結構仍合法),其後果是單一 location 解析失敗,已由 FR-012 的 tab 內可復原錯誤覆蓋,因此**不採用** header/footer signature 或 CRC(signature 攔不到那個情況,CRC 的代價換不到對應價值)。

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
| ~~產品內的計時儀器(Group 切換／tab realize／cold start 延遲)~~ | **已於 2026-09-18 開票(PD-209)，不再是候選（cold start 一項仍在候選外，未開票）。** 觸發條件「使用者實際回報切換有感延遲」已由使用者當日直接要求改善切換速度成立；同日雙方稽核另證明 `BrowseToObject`(`explorer_host.cpp:638`)完全沒有時限。原始描述：PD-026(2026-08-24)刻意排除：三者都沒有 blocking 門檻，加儀器要動產品程式碼。 |
| 崩潰迴圈的自動安全模式(連續 N 次不乾淨關閉即自動以 `--diagnostic` 啟動) | PD-025(2026-08-24)刻意排除:沒有真實崩潰資料前 N 是憑空調的,且自動重啟需要 `CreateProcess`,會在單一 process 架構上開一個口子。若使用者實際遇到崩潰迴圈再開票。 |
| 縮圖 pipeline 的快取與尺寸上限 | 待 PD-003 量出縮圖對記憶體的實際貢獻後再開,避免憑估計調參數。PD-003 2026-08-29 的自動化量測跑出差值 0 bytes,但兩個組態都重用同一次 run 內已導覽過的資料夾(縮圖早已快取),不是有效讀數;要開票前需先用全新啟動的 process 分別量測純文字與縮圖資料夾。 |
| ~~診斷閒置磁碟 I/O 的來源~~ | **已於 2026-09-03 開票(PD-176),不再是候選。** PD-003 2026-08-29 量到閒置 10 分鐘期間有 307294 bytes 磁碟 I/O(NFR-001 磁碟門檻 FAIL,零 bytes 才算過),觸發條件早已成立;2026-09-03 三方稽核在原始碼層面排除了 app 自身的 busy-spin 與輪詢計時器,把範圍收斂為「歸因到 app 之外的來源」。 |
| `activate_group` 橫跨 Shell 重入持有 `GroupState&` | 2026-09-06 稽核發現：`activate_group`（`main.cpp`）取得 `auto& group = active_group(state);` 後，該參考跨越 `navigate_realized_panes()` 與 `apply_layout()`——兩者都會重入訊息迴圈——才被 `active_pane_index(group)` 使用；`navigate_realized_panes` 的 `const GroupState&` 參數同樣在每次 `ShellCallScope` 之後繼續解參考。若重入期間 `state.application.groups` 被新增或刪除而重新配置，該參考即懸空。**稽核未能證明可達路徑**：Group CRUD 走 `WM_COMMAND`，而 `defer_shell_reentry_mouse_message` 在 `shell_call_depth != 0` 時會把滑鼠訊息延後，`activate_group` 自身也有 closing/shutdown 閘。因此這是**間接**防護，不是這個函式自己的不變量。觸發條件：出現一次 Group 切換期間的崩潰或狀態錯亂回報，或有人要放寬滑鼠訊息延後機制時；屆時的修法是改持 index 或在重入後重新查找，而不是再加一層外部閘。 |
| 追查 `panedock_launch_smoke` 間歇性崩潰(0xC0000409) | PD-003 2026-08-29 執行期間偶發一次 `STATUS_STACK_BUFFER_OVERRUN`,發生在關閉一個含 4 個 Group、其中一個 42 個 tab 的真實 `session.json` 之後;立即重跑同一測試與完整 suite 皆通過,無法穩定重現。觸發條件:再次出現(尤其是大量 tab 的 Group)時開票追查,屆時附上本次的復現條件與 session.json 特徵作為起點。 |
| ~~把 tab strip 的 hover／drag／paint／`WNDPROC` 收進 `app_shell::TabStrip` 模組~~ | **已於 2026-09-05 開票且完成（PD-196），不再是候選；PD-197 已複驗 `PaneTabStrip` 由 Pane 以值持有。** 原始描述保留：2026-09-01 架構審查原本以「paint 與 hit-test 各自重新推導 layout」為理由列為高優先，但 Codex 唯讀核對**推翻**了該理由:layout 由 `layout_tab_strip`(`main.cpp:1798`)計算，tab 與 scroll 命中測試直接委派 `tab_overflow.h` 的純函式(`main.cpp:3817`)，paint(`:4052`)與 `WNDPROC`(`:4238`)消費同一份 `tab_strip_geometry`——PD-085／PD-107 已經把這條共用 seam 建好了。剩下的真實問題只有「這 853 行仍在 `main.cpp`、且 drag／hover／paint 的**呼叫路徑**沒有測試涵蓋」。觸發條件（**2026-09-04 更新**）:原條件為「再出現一個 hit-test／repaint 類的實機回報(PD-107、PD-152、PD-154 之後的下一個)」;現增列第二個觸發點——**PD-187～PD-189 完成後重新評估**。理由:那三張票之後 tab strip 會自然落在 `Pane` 底下（pane 有自己的 HWND 與 proc、繪製與命令都已就地處理），屆時這群 ~830 行的邊界會比現在清楚，切票會準得多。2026-09-04 全檔盤點的補充事實:`layout_tab_strip` 其實不在 `main.cpp`，它是 `tab_overflow.h:176-336` 的無狀態純函式（不依賴 `AppState`／HWND），`Pane` 現在就能直接呼叫;而 `cancel_tab_drag`／`finish_tab_drag`／`update_tab_drag`／`register_tab_drag_hover_targets` 依契約 (3) 天生跨 pane，不可能整群變成 `Pane` 成員——真正可搬的比 853 這個數字小。 |
| `SessionWriter`——把 `session_dirty`、500ms 防抖、clean/crash marker 與 atomic replace 收進單一型別 | 2026-09-01 架構審查列為 Speculative 並自我否決:目前只有一個寫入者，注入的 "write these bytes" adapter 是**假想 seam**(一個 adapter 只是假設，兩個才是真的)。觸發條件:出現第二個持久化寫入者(獨立 settings 檔、匯出功能)，或 PD-162 實作時發現 shutdown reducer 本來就需要透過介面呼叫存檔決策——後者請在 PD-162 交接區記錄，再據以開票。 |
| ~~把剩餘 9 個 pane-parallel 陣列搬進 `PaneChrome`~~ | **已於 2026-09-03 開票，不再是候選。** 觸發條件（PD-163 完成且交接區已寫出每個未搬欄位的具體阻礙）已成立。依所有權界線拆成兩張：[PD-182](tickets/PD-182-pane-type-owns-ui-state.md) 搬六個純 UI 欄位（`tab_visuals`、`tab_strip_geometry`、`tab_hover_indices`、`tab_scroll_hover_indices`、`tab_drag_targets`、`folder_context_buttons`），[PD-183](tickets/PD-183-pane-owns-explorer-host-lifetime.md) 搬三個 Shell 生命週期欄位（`explorers`、`realized`、`suppress_history_record`）。原候選描述保留於下一列。 |
| 〔已開票，原描述存查〕把剩餘 9 個 pane-parallel 陣列搬進 `PaneChrome` | PD-163 刻意只搬 12 個純 HWND／GDI 欄位，留下 `explorers`、`realized`、`tab_visuals`、`tab_strip_geometry`、`tab_hover_indices`、`tab_scroll_hover_indices`、`suppress_history_record`、`tab_drag_targets` 等 9 個——它們牽涉 `ExplorerHost` 生命週期與 tab 互動狀態，一起搬會讓單票超過兩天。觸發條件:PD-163 完成且其交接區已寫出每個未搬欄位的具體阻礙後開票。 |
| ~~為每個 pane 註冊獨立的 `PaneDock.Pane` window class，子控制項改掛 pane HWND~~ | **已於 2026-09-04 開票（PD-187／PD-188／PD-189），不再是候選。** 新證據為使用者原話「為了權責分割更乾淨」，並修正了下列理由中不完整的部分:當初只評估「訊息路由」一項效益（該項確實已被 PD-178 解掉），漏掉了**繪製與剪裁**——`paint_client_background`（`main.cpp:2313-2379`）在主視窗畫布上計算並繪製全部 4 個 pane 的卡片，這個結構已直接產生 PD-041／PD-042／PD-045／PD-063 四張 bug 票。當初列出的三項代價全部承認並以切票處理。以下為原始登記內容:2026-09-03 使用者原始需求字面上包含這一項（「pane 用 CreateWindowW，讓 win message 也能切割乾淨處理」），但經 `/grill-with-docs` 與 opencode 獨立審查後**由使用者決定先不做**。理由：它買到的是訊息路由方便，**不是解耦**——解耦由 PD-182／PD-183 的單向模組契約達成，不需要 pane 自己的 HWND；而「pane 用起來像擁有自己的控制項」由 PD-178 的 `decode_pane_control` 純函式以約 20 行、可單元測試的方式達成。代價則包含重述 `docs/design-spec.md` FR-004 的 `DeferWindowPos` parent-scoped 批次契約（2 批變 5 批，動到 PD-155 的 atomic live resize）、重新分工 `paint_client_background`／`draw_pane_card`／`draw_brand_bar` 的背景繪製，且全程無法自動化驗證（UI 自動化是已否決方向）。觸發條件：PD-178～PD-183 完成後，若實際維護時仍因 pane 訊息路由踩到具體問題，屆時附上該問題再開票。 |
| `PaneOutcome` — `Pane` 以意圖 enum 回報、由協調層執行副作用 | 2026-09-03 使用者已核准此模式，但**經 opencode 審查後改為條件觸發**，觸發條件為 PD-182 交接區的「`Pane` 向上呼叫點清單」超過 3 處。**PD-182 完成後判定：0 處**——`Pane` 沒有任何回呼機制，資料流全部是協調層讀 `Pane` 查詢方法、算完寫回命令方法，沒有「向上通知」的呼叫路徑存在。遠低於門檻，**不開票**。**PD-183 完成後重新判定：仍是 0 處**——`realize()`/`derealize()`/`navigate()` 同樣是純命令，導覽/選取變更等回呼由協調層透過 `Pane::host()` 直接向 `ExplorerHost` 註冊（捕捉 `&state`），不經過 `Pane` 本身；`Pane` 仍未持有任何回呼。若日後有票為 `Pane` 引入真正需要向上通知的场景，可重新評估。 |
| 移除 `AppState` 的 14 個 shutdown reference alias（`main.cpp:506-528`） | **2026-09-03 查證後主動撤案，不列為待辦。** 原本被誤判為 PD-162 遺留的技術債。實測 `closing_` 用於 91 處、`shutdown_deferred` 88 處，移除後約 200 個呼叫點會從 `state.closing_` 變成 `state.shutdown_sequence.state().closing_`，更長更難讀；且它們是 reference，不存在「兩份真相會不同步」的風險。要重開必須先舉出一個因這些 alias 而實際發生的缺陷。 |
| 拆 `explorer_host.cpp` 剩餘三塊職責（COM 回呼 shim、reentrancy／navigation generation queue、context menu hosting） | PD-181 只抽 error window（約 200 行 + 4 個 header 成員），因為那是一個與 host `IExplorerBrowser` 完全無關的第二 UI。其餘三塊都與 COM 契約糾纏，而 `docs/testing.md` 明訂 `explorer_host` 無自動化測試，拆錯沒有測試網。觸發條件：出現一個具體的、可歸因到這三塊之一的實機缺陷。 |
| 統一 `{ ShellCallScope } + if (closing_ \|\| shutdown_deferred) return X;` 慣用法（30+ 處，約 120 行） | 2026-09-03 重構掃描發現，但**刻意排除**。回傳值分別是 `void`／`E_ABORT`／`0`，統一時錯一個就破一條 reentrancy 路徑；PD-172／PD-173／PD-177 剛修完這一區，是本專案最貴的崩潰面。觸發條件：先有一個因這個慣用法被複製錯而產生的實際缺陷。 |
| 依消費者把 `PaneHost` 拆成多個介面（2026-09-29 架構審查候選 5） | **2026-09-30 評估後不做。** PD-214／PD-215 已把重入與擷取抑制移出 `PaneHost`，剩下 13 個方法只有 `Pane` 與 `PaneTabStrip` 兩個消費者，實作者也只有 `AppState` 與 `TestPaneHost`。拆開不搬動任何邏輯，只多出幾個指標要接線；刪除測試下沒有一個拆出的介面會變深。要重開必須先有第二個實作者，或一個因介面過寬而實際發生的缺陷。 |
| `IExplorerBrowserEvents` 沒有 request token：FIFO 歸屬可能錯配 | 2026-09-18 第二輪稽核由兩個 agent 從不同方向指到同一根源。Codex：`NavigationLedger` 的 FIFO 假設「被 supersede 的導覽一定會收到 complete 或 failed」，若 Shell 省略該事件就出現 queue hole，之後所有回呼持續錯位、`completed_` 追不上 `latest_`。Claude：`NavigationLedger::take()` 在 queue 空時鑄新 generation，而 `Pane::navigation_request_is_current`（`pane.cpp:811-815`）對`generation > pending_navigation_.generation` 的反應是**收養**——改寫成當下的 active group 與 tab 並回 `true`；若 `BrowseToObject` 的 pump 期間 view 自己起了一次導覽，`mark_pending()` 會把那個 pending 錯記到我們的 record 上，第二個完成事件就走鑄新 generation ＋ 收養的路，把舊 Group 的位置寫進新 Group 的 tab。**兩者皆標 PLAUSIBLE**：`IExplorerBrowserEvents` 沒有請求身分可比對，純靜態追蹤無法證明該交錯真的會發生。**觸發條件**：先加一個 `--diagnostic` 的 event trace（記錄 `OnNavigationPending`／complete／failed 的序列與當時的 ledger 狀態），在實機上快速切換慢速位置，證實 queue hole 或錯配確實出現。屆時的落點是 `panedock::core::NavigationLedger`（PD-210 已把這個狀態機收斂成單一、可測的型別），修法方向是「新的 host-issued navigation 取代尚未開始／已被 supersede 的舊 record」加上「`Pane` 側收養前先確認 `pending_navigation_.group_id` 仍等於當前 group」。不要只在 `Pane` 層再加 generation 判斷——錯配發生在 `ExplorerHost`。 |
| Group 切換的導覽改為非阻塞（逐 pane posted message，不再序列化四次同步 `BrowseToObject`） | 2026-09-18 雙方稽核一致認定為最大效能問題：`navigate_realized_panes`(`main.cpp:732-751`)在 UI 執行緒逐一等待，而 `BrowseToObject`(`explorer_host.cpp:638`)沒有時限，1000 ms 的 `dwTickCountDeadline`(`explorer_host.cpp:24,55`)只掛在 `SHCreateItemFromParsingName` 上，網路 provider 常無視 `BIND_OPTS`。四個 pane 都指向不可達網路路徑時UI 阻塞無上限，違反 NFR-003。**修法方向已收斂**：Codex 主張把解析搬到 thread-pool，**不採用**——`IExplorerBrowser` 是 STA-bound，`BrowseToObject` 仍要回到 UI 執行緒排隊，換不到對應價值；Claude 的方向較小：把迴圈改成「issue 第一個 pane 後，其餘透過 `kDeferredLayoutMessage` 式的 posted message 逐一觸發」，讓每次 Shell 呼叫之間迴圈能回到 idle。**觸發條件**：PD-209 的實測數字填入 `docs/performance-baseline.md:14-16`，且「one unreachable network path」一列確認為 FAIL——`docs/performance-baseline.md` 規定任何優化提案必須先有本表的數字。屆時開票並依該數字定驗收門檻。 |
| `IShellFolder` 自建清單檢視(fallback) | **2026-10-16 使用者決定自行管理列表，已拆為 PD-222～PD-242。** 舊「僅 PD-001 No-Go 才開」觸發條件由 PD-222 明確覆寫；不預先指定所有位置必須使用同一個列舉 backend。 |
| 統一 header 版型按鈕圖示與導覽列圖示的筆畫粗細 | PD-075(2026-08-26)刻意排除:版型按鈕的五個圖示是**版面示意圖**(一格／雙欄／上下／2×2／更多),沒有任何 `Segoe MDL2 Assets` 字符能表達「這個版型長什麼樣」,只能手繪。但 `draw_layout_glyph` 用 `CreatePen(PS_SOLID, 1, ...)` 而 `draw_navigation_icon_button` 是 2px,兩者並列時粗細不同是真的。觸發條件:PD-075 完成後若使用者仍覺得 header 與 pane 的圖示不成套,再開票調整手繪線寬(注意 1px 是 `RoundRect` 版面示意圖能保持清晰的實際上限,加粗可能反而糊掉,屆時需先截圖比對)。 |
| 側邊欄寬度的全域設定持久化 | 若使用者回報每次啟動都要重拖再開;目前預設值可接受。 |
| Group 圖示與顏色 | Spec 未列為 MVP;若 Group 數量成長到難以用文字辨識再開。 |
| ~~每個 pane 的導覽列(上一頁／下一頁／上一層按鈕 ＋ editable path bar)~~ | **已於 2026-08-24 開票,不再是候選。** 2026-08-20 使用者提出時 Phase 0 尚無 tab/Group/pane chrome 基礎設施;Phase 2 完成後基礎設施到位,拆成 PD-018(核心導覽歷史)、PD-019(tab 條)、PD-020(網址列與導覽按鈕)三張,PD-021 補上對應鍵盤快速鍵。 |
| 拖到 tab 標題以複製到該 tab 的資料夾 | PD-023(2026-08-24)列為 non-goal:這是 Windows 檔案總管沒有的加值互動,spec 未要求。若使用者實際使用後想要,再開 ticket。 |
| 網址列自動完成(`IAutoComplete2`) | PD-020(2026-08-24)刻意排除,只做純 `EDIT` + Enter。若使用者實際使用後認為缺自動完成造成明顯不便,再開 ticket 接 Shell 的 `IAutoComplete2`,不預先做。 |
| 讓 `compute_layout_rects` 接收 DPI 縮放後的最小尺寸／分隔線厚度,取代目前寫死的 96-DPI 基準常數 | PD-005 2026-08-24 交接發現:`docs/design-spec.md` §FR-004a 的敘述預期呼叫端傳入「已按 DPI 縮放過的最小值常數」,但 PD-005 定義的函式簽章只收 client size、版型、比例,常數是寫死在 `src/core/layout.h` 的 96-DPI 基準值,呼叫端目前無法覆寫。等 app_shell 接上 Per-Monitor-V2 `WM_DPICHANGED`(NFR-004)且需要跨 DPI 正確縮放時開票,把最小尺寸/分隔線厚度改成函式參數,矩形演算法本身不需要動。 |
| 啟動時的不乾淨關閉標記(`clean_shutdown`)改用比整份 `write_session` 更輕量的單欄位寫入 | PD-094(2026-08-27)撤回時發現:目前啟動時仍會呼叫完整 `save_now`/`write_session` 管線(重新解析整份 `preserved_json`+雙 flush+備份複製)只為了把 `clean_shutdown` 這一個布林欄位改成 `false`,PD-025 決策 2 已判定這次寫入本身可接受,但沒有評估過「用更輕量的手段達成同一個標記語意」。觸發條件:先用 PD-026 排除的計時儀器或使用者實際回報量到啟動延遲確實可感知,再開票設計專屬的輕量標記寫入(需注意仍要遵守 `AGENTS.md` 的 atomic-replace 與 schema 前向相容規則,不能只是省略 flush)。 |
| 把 PD-095 的「幾何重排/內容重算」分離套用到 `WM_SIZE`(拖曳主視窗邊框即時縮放) | PD-095(2026-08-27)只把 `update_splitter_drag` 的逐幀呼叫改成幾何模式,`WM_SIZE`(`main.cpp:3809-3811` 一帶)仍呼叫預設的完整內容重算 `apply_layout`。這是同一類熱路徑(Windows 預設的 live resize 會在拖曳視窗邊框期間連續送出 `WM_SIZE`),PD-095 原始三方發現的背景段落也點名過,但 Scope/Acceptance Criteria 最終只鎖定分隔線拖曳,是撰票時的範圍疏漏,不是實作缺陷。觸發條件:若使用者實際感受到拖曳視窗邊框縮放時的卡頓,可直接沿用 PD-095 已經做好的 `apply_layout(window, state, false, recompute_content)` 參數,讓 `WM_SIZE` 也在連續縮放期間傳 `false`,只在縮放結束(`WM_EXITSIZEMOVE`)時做一次完整重算。 |

## 歷史脈絡（按需讀取）

過往 dependency lanes 快照與計畫決策紀錄已移至 [ticket history](ticket-history.md)。它只供查歷史理由與證據；目前狀態與相依以本頁 Ticket 總覽為準。
