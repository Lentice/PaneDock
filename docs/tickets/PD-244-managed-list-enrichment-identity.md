# PD-244 — 圖示與屬性請求使用 Shell item identity

Phase 8 · PD-234 拆分 · 目標半天至一天

## 來源與交付

來源：[PD-234](PD-234-managed-list-shell-namespace.md)、[自建列表規格](../research/2026-10-16-managed-list-spec.md) Implementation Decisions／Testing Decisions。依賴與狀態只見 `docs/tickets.md` Ticket 總覽。

交付一個結果：enrichment 的目標與完成比對使用 PD-243 的 item identity，虛擬列不會被當成 parent path＋display name。沿用已有 image／property worker 與容量，不重寫裝飾系統。

## Overrides

覆寫 PD-234 單票實作的切分；保留其功能要求。追查證據：`Pane::list_icon_key` 把 Row::name 放入 IconKey，`read_property` 及 shell_core 的 load_item_icon／load_item_thumbnail 拼接 folder＋name；即使列舉回傳正確 parsing name，這條 sibling path 仍會對錯誤 item 查詢。未重開 fake COM host、UI 自動化或任何選單改造方向。

## 綁定限制（引用）

- design-spec FR-006：「Shell identity 不使用顯示名稱或持久化 PIDL。」「慢 location 或 metadata enrichment 不得阻塞 list input。」NFR-005：「必要狀態(版型、location、tab、view mode、排序)必須精確還原」。
- development：「A pane-only feature is implemented in Pane or one of its sub-modules, not in the coordinator.」「Use Microsoft::WRL::ComPtr for every interface pointer.」「Never destroy a parent HWND while a hosted view is alive.」
- AGENTS：「Keep src/core free of HWND, COM and windows.h.」「Event-driven idle path only. No busy loops, no polling timers.」「Never persist a PIDL or a COM pointer.」「New non-trivial logic needs one focused runnable test or self-check.」
- AGENTS：「A read that encounters a field it does not recognize preserves that field rather than silently dropping it on the next write-back.」不改 schema／session 格式、atomic replace／backup。
- worker 只限 `src/managed_list/list_worker.cpp`，COM 在其 STA 建立並先於 CoUninitialize 釋放；只收傳 owned values，無 UI-owned COM／PIDL／HWND。UI 仍單 STA，不加 locks；pump 後重查 identity。initialized legacy host 仍先 Destroy。
- 沿用 `icon_source.h`／performance baseline 的 worker、pending、interest、results、image byte caps，abandoned provider 仍佔 slot；不以新 worker 繞過容量。新增 identity capacity 納入有界請求／cache 核帳。正式預設仍待 PD-241；右鍵、檔案操作不在本票。

## 必讀檔案與 caller

- `AGENTS.md`、`docs/development.md`、`docs/design-spec.md` FR-006、NFR-002/003/005、§9–11；`docs/testing.md`、`docs/performance-baseline.md`、來源規格、兩份 `docs/adr/0001-cpp-wrl-win32-over-rust-and-csharp.md`、`0002-stable-pane-identity-across-layout-switch.md`。
- [PD-243](PD-243-managed-list-item-identity.md) 交付的 Row／identity 值，重讀實際實作，不另造平行模型。
- `src/managed_list/icon_source.h`、`icon_source_test.h`、`list_worker.cpp` 的 read_property、run_icons、IconSource replace／take／cancel；查所有 IconKey aggregate initializer 與 equality caller。
- `src/shell_core/icon_image.h`、`shell_core.h`、`shell_core.cpp` 的 load_item_icon／load_item_thumbnail 與所有 caller；保留 legacy 所需入口，不向 UI 暴露 COM。
- `src/app_shell/pane_list_icons.cpp` 的 list_icon_key、list_item_image、request_list_images、process_icon_results；`pane_list_properties.cpp` 的 append_property_requests、accept_property_result、finish_property_batch；`pane.h` 的 icon cache／interest。
- `tests/unit/test_pane_host.h`、`managed_list_icons_test.cpp`、`managed_list_sort_test.cpp`、`managed_list_cache_test.cpp`、`tests/CMakeLists.txt`。

## 具體範圍

1. IconKey／IconRequest 攜帶 PD-243 絕對 child identity（本機允許原 parent＋leaf 形式）；cache、interest 與完成驗證用 identity。display name 只供顯示，不能變成 parsing name。
2. 在既有 worker STA 上將 absolute item identity 接到 image／thumbnail／property 查詢；需要共用 helper 時先 trace 所有 caller，在 shared entry 修正，保留現有本機與 legacy 行為。不在 paint、selection 或 sort comparator 上解析 Shell。
3. 同名不同 identity、換 tab／Group、排序換 row index 後的結果不可套錯。缺少裝飾資訊仍使用 placeholder／既有缺值策略，保留 expensive sort 的完成規則。
4. 新字串長度／capacity 及 cache key 納入現有 caps；cancel／shutdown 不等待 provider，不新增 retry polling。不得降低本機 icon／property 支援以避開 identity 遷移。

## Non-goals

不接入 namespace enumeration／parent／routing，不新增 property／thumbnail 功能，不修改 PD-226、不改右鍵／file operations／OneDrive、不做 session migration 或 completed ticket 文件修改；無新第三方依賴、服務、telemetry 或 elevation。

## Acceptance criteria

- [ ] Pane 的 injected IconSource 可觀察到兩個同名虛擬列有不同 absolute identity，且 parsing／known-folder／fallback 未被 display name 或 filesystem fallback 取代。
- [ ] stale identity／generation／viewport／row index 結果被拒絕，正確結果只更新對應列；placeholder、sort 完成及 cache eviction 行為保持。
- [ ] 新 identity 資料長度與核帳有界，既有 worker capacity／cancel 行為保持；無 UI provider 查詢。
- [ ] 本機 enrichment 的既有聚焦案例通過；不以此宣稱真實 namespace icon／property PASS。

## Agent checks

沿用 PaneHost／IconSource 注入 seam，擴充現有 CTest。僅跑聚焦檢查：

```powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target panedock_managed_list_icons_test panedock_managed_list_sort_test panedock_managed_list_cache_test
ctest --test-dir build --output-on-failure -R '^panedock_managed_list_(icons|sort|cache)$'
git diff --check
```

使用者已延後實機／UI 驗證；真實 Shell image／property apartment、provider lifetime 仍「未驗證，需真實桌面」，交付不得聲稱 seam 等於 COM 驗證。新增交接區只放未解風險或不可重推結論。
