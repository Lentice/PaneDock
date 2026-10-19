# PD-246 — Pane 虛擬導覽、預設位置與 session 還原

Phase 8 · PD-234 拆分 · 目標 1–2 天

## 來源與交付

來源：[PD-234](PD-234-managed-list-shell-namespace.md)、[自建列表規格](../research/2026-10-16-managed-list-spec.md) User Stories 19/20/22–25/30/31、Implementation Decisions／Testing Decisions。狀態與依賴只見 `docs/tickets.md` Ticket 總覽。

交付一個結果：將 PD-243 的 item identity、PD-244 的 enrichment 與 PD-245 的 namespace Source 接上 Pane 公開導覽／還原，使 opt-in managed list 可瀏覽 This PC／Desktop 及 children，保留原生 Shell identity 與正確 parent／history。

## 切片估算與停止條件

估計 1–2 天，維持單一「Pane 虛擬導覽整合」結果。前三票已處理 Row／interaction／enrichment identity 與 worker canonical／parent 契約；本票不再遷移那些模型。追查顯示滑鼠、鍵盤、tab／Group restore 均匯入 Pane 共用導覽入口；預設常數、core 的新 tab／replacement／Group 模型無需更改。session 的 decode_shell_location 已保留三個 identity 欄位（允許空 fallback），encode 從 preserved JSON 的 nested shell_location 合併已知欄位，因此不預期修改 serializer。這部分的工作是驗證 navigation commit 保留完整值，不是另一項 persistence 實作。

估算分配：共用 routing／parent／history／manual Refresh 接線約 0.75 天；Pane seam 導覽、預設及 late-result 案例約 0.75 天；既有 serializer 的 unknown-field round-trip／cache 回歸與聚焦 build 約 0.5 天。若發現必須修改 session schema／serializer、core default／replacement 模型，或 PD-245 契約仍不足，先停下並拆出自含票件、調整依賴，不在本票吸收新的遷移。下游仍等待完整整合，不能僅因 identity 基礎票完成就提前接入。

## Overrides

覆寫 PD-234 原單票切分，並取代 opt-in path 對所有虛擬 location 一律回 legacy 的暫時路由；不改正常預設、不移除 legacy host。追查證據：Pane::realize／navigate_content 目前只接受 filesystem syntax，open_list_row 拼接父路徑與 name、navigate_up_one_level 截斷路徑；resolve_list_navigation 直接提交 request.location。這些路徑必須共同改用 worker 回傳值，不能只修雙擊入口。未重開 fake ExplorerBrowser、Shell UI 自動化或選單改造方向。

## 綁定限制（引用）

- design-spec FR-005：「每個 pane 至少一個 tab。」「已保存的既有 tab location 不因這項預設變更而修改。」該節指定 My Computer parsing name `::{20D04FE0-3AEA-1069-A2D8-08002B30309D}`；新 Group／新 tab／最後 tab replacement 均沿用同一既有預設。
- design-spec FR-010：「每個 tab 提供網址輸入(可鍵入或貼上)、上一頁、下一頁、上層。導覽歷史為每個 tab 各自獨立。」FR-012：「不得因此刪除任何已儲存的設定。」
- design-spec NFR-003：「Group／版型／tab 切換與 pane input 不得因為某個 Shell location、metadata provider 緩慢或無法連線而凍結 UI。」NFR-005：「必要狀態(版型、location、tab、view mode、排序)必須精確還原」。
- design-spec §10：「持久化的 identity 為 parsing name ＋ known-folder identity ＋ fallback path。display name 永不作為 identity。」
- development：「A pane-only feature is implemented in Pane or one of its sub-modules, not in the coordinator.」「Use Microsoft::WRL::ComPtr for every interface pointer.」「Never destroy a parent HWND while a hosted view is alive.」
- AGENTS：「Keep src/core free of HWND, COM and windows.h.」「Never persist a PIDL or a COM pointer.」「Event-driven idle path only. No busy loops, no polling timers.」「New non-trivial logic needs one focused runnable test or self-check.」「A read that encounters a field it does not recognize preserves that field rather than silently dropping it on the next write-back.」schema version、atomic replace／backup 不變。
- UI 為單 STA，不用 locks／worker UI state access；worker COM 只能在 `src/managed_list/list_worker.cpp` 的自有 STA 建立及釋放。pump 後重驗 active／request／shutdown；先 invalidate 再 Destroy legacy view，不能在其 completion callback 裡 Destroy。保留 stable pane／chrome HWND 與 stage-reveal layout。
- 沿用四 enumeration slots（含 abandoned）、1 pending／2 batches、128 rows/batch、100,000 rows／24 MiB active rows、4 MiB index、32 MiB inactive cache；取消不等待 provider，不輪詢。正式預設待 PD-241，移除 legacy 僅 PD-242；不改右鍵、file operations 或 PD-226。

## 必讀檔案與所有入口

- `AGENTS.md`、`docs/development.md`、`docs/design-spec.md` §4.7–4.10、FR-002/005/006/009–013、NFR-002/003/005、§9–11；`docs/testing.md`、`docs/performance-baseline.md`；來源規格；兩份 `docs/adr/0001-cpp-wrl-win32-over-rust-and-csharp.md`、`0002-stable-pane-identity-across-layout-switch.md`；PD-243／244／245 實際公開交付。
- `src/app_shell/pane.cpp` 的 realize、navigate_to、navigate_up_one_level、navigation_complete/failed、navigate_history、refresh_view、capture_location、submit_address、process_content_retry、begin_navigation、finish_tab_change、add_default_tab／close_tab、derealize／destroy；`pane.h` 的 request／history／content snapshot。
- `pane_managed_list.cpp` 的 navigate_content、start_managed_list、resolve_list_navigation、process_content_results、open_list_row、cached rescan／interaction／status；`pane_list_icons.cpp`、`pane_list_properties.cpp` 的 identity 與 stale result 驗證。
- `src/app_shell/main.cpp` 的 default_shell_location、default_application_state、new_group_state、navigate_realized_panes、apply_layout／startup realization、tab drag replacement／restore、deferred replay／shutdown；`pane_tab_strip.cpp` 新 tab 入口。只改必要路由，不搬跨 pane 協調進 Pane。
- 滑鼠：pinned Desktop／This PC、導覽／Refresh buttons、雙擊列、tab activation；鍵盤：Enter、Backspace、history/up actions、address submit；Group／layout restore、tab move、deferred legacy completion 路由與 shutdown。逐一 trace shared caller，不只測 ticket 點名入口。
- `src/shell_core/shell_core.cpp` capture_location／resolve_location，`explorer_host.cpp` navigate／navigate_up／completion，`src/core/model.h`、`model.cpp` history/defaults，`src/core/session.cpp` decode_shell_location、encode／preserved_object／preserved_tab_object、serialize/deserialize/write_session，`src/app_shell/session_writer.cpp` 及 caller。
- `tests/unit/test_pane_host.h`、`managed_list_restore_test.cpp`、`managed_list_cache_test.cpp`、`managed_list_selection_test.cpp`、`core_session_test.cpp`、`tests/CMakeLists.txt`。

## 具體範圍

1. realize 與 navigate_content 共用 managed routing 契約，標準 virtual folder 交給 Source，不在 UI 呼叫 resolve/capture／parent COM。保留原 Request 供 batch identity 檢查，成功後才將獨立 resolved location 提交 history／model；無 path 不是丟棄理由。
2. open_list_row 用 absolute child identity；Up 用 worker 的 parent outcome，Desktop root 無 parent 回 S_FALSE；等待／失敗時不得猜字串 parent 或把 virtual fallback 當目的地。filesystem Up 保持既有有效語意。
3. Back/Forward、Refresh、restore／deferred retry 使用完整 ShellLocation。失敗不改已存 location／history index，可重試；canonicalization 不新增假 history，stale batch／late parent 不污染新 tab／Group。
4. 檢查新 Group／新 tab／最後 tab replacement 的 My Computer、Desktop→child→parent、跨 virtual／filesystem history。不要改預設常數為 filesystem path，也不要以 localized display text 生成識別。
5. 無 reliable notification 時顯示英文 `Automatic refresh unavailable. Use Refresh to update.`，沿用既有 Refresh command 顯式重列舉，不新增 timer；本機 PD-232 通知維持。cached virtual snapshot 有界顯示並重驗，不誤報 watching。
6. session 沿用 parsing／known-folder／fallback 三欄位與 schema。測 nested unknown shell_location 欄位、root／tab unknown 欄位、跨 pane tab move 後再寫、空 fallback、失敗／pending save；無 migration，不能將 fallback 或 display text 蓋過 virtual parsing name。

## Commander 實作提示

[既有 Commander 研究](../research/2026-10-16-commander-performance.md) 支持分階段列表／裝飾、lazy inactive tabs、可見項目 enrichment、過期結果拒收。具體採 progressive reveal：第一批可操作後繼續 incremental enumeration；依 PD-233 cached-first 顯示及 Updating 後重驗，保留 identity anchors，不將 cache presence 當檔案仍存在。identity 獨立於 display name；bounded work／cancellation／零 polling idle 成本沿用既有 policy，不能靠預熱全部 tabs 換速度。

Shell-native fallback 僅在確有既有相容性需求時保留，不以 blanket virtual routing 隱藏未完成的新列表；unsupported／bind failure 優先顯示可重試錯誤並保留 identity。若需要 legacy fallback，須明列觸發條件與 request／Destroy sequencing，不能在慢 provider 失敗後自動跳 UI STA 再 bind，也不改右鍵行為。上述為 PaneDock 實作提示，不宣稱其他產品已證明本專案效能。

## Non-goals

不重寫 worker、另建 namespace model、plugin framework 或 session schema；不改 OneDrive、原生選單／right-click／file operations、stage-reveal layout、PD-226 或 completed ticket 文件。無第三方 runtime、telemetry、服務或 elevation。

## Acceptance criteria

- [ ] PaneHost＋Source 注入測 This PC／Desktop ShellLocation 三欄位、無 fallback、同名不同 identity；新 tab／Group／最後 tab replacement 保持 My Computer。
- [ ] Desktop→child→parent、root Up、virtual↔filesystem Back/Forward、失敗後 Refresh、canonical completion 不多記 history；舊結果／parent 在 tab／Group 更換與 shutdown 後被拒收。
- [ ] cached-first Refresh 保持正確 selection／anchor，manual-only notification policy 可觀察且無 polling；新增 owned identity／parent 資料計入 caps。
- [ ] session round-trip 保留 virtual parsing／known-folder／fallback 與 unknown fields，pending／error 不覆蓋良好保存值，無 schema migration。
- [ ] focused CTest 通過；真實 Shell This PC／Desktop／known-folder restore、STA lifetime／stuck-provider shutdown 仍「未驗證，需真實桌面」。不得以 seam 取代 PD-241 gate 證據。

## Agent checks

新增 `tests/unit/managed_list_namespace_test.cpp`，用現有 PaneHost／Source injection，於 `tests/CMakeLists.txt` table 註冊 `managed_list_namespace`。不要 fake IExplorerBrowser，不斷言 private 呼叫序列；hidden controls 只作既有 seam，無 live Shell／GUI 自動化。

```powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target PaneDock panedock_managed_list_namespace_test panedock_managed_list_restore_test panedock_managed_list_cache_test panedock_core_session_test
ctest --test-dir build --output-on-failure -R '^panedock_(managed_list_(namespace|restore|cache)|core_session)$'
git diff --check
```

使用者延後實機／UI 驗證，不執行 launch smoke 或 full suite。交付記 exact checks 及 deferred real Shell validation；交接區僅放未解風險、不可重推結論。若實作仍超過兩天，先再拆，不擴張。
