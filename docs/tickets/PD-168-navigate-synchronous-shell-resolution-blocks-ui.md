# PD-168 — 導覽/Tab/Group 切換在 UI 執行緒同步呼叫 Shell resolve/BrowseTo，慢速路徑會凍結整個 app

Phase 7 · explorer_host / shell_core · Depends on: PD-140, PD-157

## 來源

2026-09-03 三方稽核（Claude / Codex / OpenCode）。Codex 與 OpenCode 各自獨立指出同一段程式碼與同一個成本組成，本票經 fork 重新對照現有原始碼逐行核對，判定為 CONFIRMED-NEW（未被任何既有 ticket 涵蓋）。

## 背景與現況

`ExplorerHost::navigate`（`src/explorer_host/explorer_host.cpp:612-636`）是所有導覽路徑共用的唯一入口：

```cpp
HRESULT ExplorerHost::navigate(const core::ShellLocation& location) {
    ...
    location_ = location;
    const std::wstring location_text = shell_core::resolve_location(location_);   // :620
    Microsoft::WRL::ComPtr<IShellItem> item;
    HRESULT hr = SHCreateItemFromParsingName(location_text.c_str(), nullptr,      // :622
                                              IID_PPV_ARGS(&item));
    ...
    hr = browser_->BrowseToObject(item.Get(), SBSP_ABSOLUTE);                      // :630
    ...
}
```

`shell_core::resolve_location`（`src/shell_core/shell_core.cpp:113` 一帶）在已知資料夾識別的情況下也會呼叫 `IKnownFolderManager`/`SHCreateItemFromParsingName`（同檔 :144）。這三個 Shell 呼叫全部在呼叫者所在的執行緒（即 UI 執行緒）同步執行，且完全在 `ShellCallScope`（`main.cpp:612-628`）保護範圍內——`ShellCallScope` 只負責讓重入計數正確，不會讓呼叫本身變成非同步。

所有導覽入口都直通這個函式：

- 位址列直接輸入 Enter：`submit_address`（`main.cpp:3648-3661`）
- Back / Forward：`navigate_tab_history` 呼叫點（`main.cpp:3486` 一帶）
- Up 一層：`navigate_up` 呼叫點（`main.cpp:3499` 一帶）
- Tab 切換 realize：`switch_active_tab`（`main.cpp:3357` 一帶）
- Group 切換：`activate_group` → `apply_layout` 的 pane 初始化/navigate 迴圈（`main.cpp:2965`、`2978` 一帶）

## 為什麼這是真的問題

`docs/design-spec.md` 明文要求：

> ### NFR-003 反應性
> Group 切換、版型切換、tab 切換不得因為某個 Shell location 緩慢或無法連線而凍結 UI。

> ### AC-005 韌性
> 還原狀態中含一個無法連線的網路路徑時，UI 保持反應。

> ### 9.2 執行緒模型
> 單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。緩慢的 location 解析以 Shell 自身的非同步機制處理，不自建工作執行緒池。

目前的實作剛好違反 NFR-003/AC-005：一個離線的網路磁碟機、逾時的 UNC 路徑，或緩慢回應的第三方 shell extension，會讓 `SHCreateItemFromParsingName`（甚至 `BrowseToObject` 本身，Microsoft 文件僅保證「後續」導覽非同步，首次導覽同步——見 PD-020 交接區已查得的官方文件引用）卡住整個訊息迴圈，凍結全部 4 個 pane，直到該次 Shell 呼叫返回或逾時。這正好落在使用者本次要求稽核的四個操作類別（address 直接輸入、上一頁/下一頁/上層、tab 切換、Group 切換）的共同根因上。

## Fix 方向

§9.2 明確指名方向是「用 Shell 自身的非同步機制」，不是自建執行緒池。需要研究並確認：

1. `IExplorerBrowser` 對「後續」（非首次）導覽本身是否已經走 `OnNavigationPending → OnViewCreated → OnNavigationComplete/Failed` 的非同步流程（PD-020 交接區已查到 Microsoft 文件有此描述，但未驗證）；若是，真正的同步瓶頸只在 `navigate()` 於呼叫 `BrowseToObject` **之前**做的 `resolve_location` + `SHCreateItemFromParsingName` 這段路徑解析。
2. 確認是否存在不需要預先解析出 `IShellItem` 就能導覽的路徑（例如直接把 parsing name 交給 `IExplorerBrowser` 自己的內部解析，或用 `SHParseDisplayName` 取得 PIDL 後改走 `BrowseToIDList`，兩者是否一樣同步需查證，不能假設）。
3. 若查證後確認 Shell 本身沒有提供不阻塞的路徑解析 API，把決策與依據記錄在交接區，並提出下一個最小可行方案（例如：先同步嘗試一個有時間上限的快速路徑，逾時則退回 PD-022 既有的可復原錯誤 UI，而不是無限期卡住——需與 §9.2「不引入 async runtime」的限制核對後再決定，不能自行假設可以，需在交接區列出查證過程）。

不要在沒有查證前就假設方案，也不要用背景執行緒池繞過限制。

## 綁定限制（引用）

- `docs/design-spec.md §9.2`：「單一 STA UI 執行緒...不引入 async runtime...緩慢的 location 解析以 Shell 自身的非同步機制處理,不自建工作執行緒池。」
- `docs/design-spec.md NFR-003 / AC-005`（見上）。
- `AGENTS.md`：「Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.」——任何非同步化都必須維持既有 `ShellCallScope`/`shutdown_sequence` 的重入語意，不能繞過。
- `AGENTS.md`：「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」

## 檔案與範圍

- `src/explorer_host/explorer_host.cpp`：`ExplorerHost::navigate`（:612-636）、`ExplorerHost::initialize`（呼叫 `navigate`，:609）。
- `src/shell_core/shell_core.cpp`：`resolve_location`（:113 一帶）、`parse`（:36-43）。
- `src/app_shell/main.cpp`：`submit_address`（:3648-3661）、`switch_active_tab`、`navigate_tab_history`、`navigate_up`、`apply_layout` 內對 `state.explorers[pane].navigate(...)` 的呼叫點（:2965, 2978, 3186, 3357, 3486, 3499, 3659 一帶，以實際 `rg -n "\.navigate\("  src/app_shell/main.cpp` 結果為準）。
- `docs/tickets/PD-020-address-bar-and-navigation-buttons.md`（已查得的 `BrowseToObject`/事件順序官方文件引用）、`PD-146-display-name-shell-reentry-guard.md`（既有的 shell 呼叫重入保護模式）。

## Scope

1. 查證 §9.2 所指的「Shell 自身的非同步機制」在本專案實際可用的形式，並在交接區記錄查證過程與引用來源（官方文件或既有 ticket 已驗證的行為）。
2. 依查證結果，縮小或消除 `navigate()` 在呼叫 `BrowseToObject`/`BrowseToIDList` 之前的同步阻塞範圍；若某段解析在技術上無法避免同步（例如已知資料夾 GUID 對應），需在交接區明確說明理由。
3. 若第 2 點無法完全消除同步阻塞，改為在同步段落加上合理的時間上限與逾時後退回 PD-022 既有錯誤 UI 的路徑，並在交接區記錄選擇的數值與理由。
4. 為新增的行為新增一個聚焦 self-check（若邏輯落在 `shell_core` 內，可在既有的 core 測試基礎上新增；若必須留在 `explorer_host`，依 `docs/testing.md` 的原型驗收協定人工核對，並在交接區說明為何不能自動化）。

## Non-goals

- 不引入自建工作執行緒池或第三方 async runtime。
- 不改變 `IExplorerBrowser` 的 COM 生命週期契約（`Initialize`/`Destroy`/`Advise` 順序）。
- 不重新設計 Group/Tab/Layout 的資料模型或 session schema。
- 不在本票內處理 C2（導覽完成/失敗回呼缺乏 request identity，見 PD-170）——本票只處理「同步阻塞」本身，導覽完成事件的身分關聯問題留給 PD-170。

## Acceptance Criteria

1. 對一個會逾時／緩慢回應的 location（可用一個刻意設計的慢速測試 fixture 或既有測試手法模擬），address 直接輸入、Back/Forward、Up、Tab 切換、Group 切換這五種操作觸發的導覽，訊息迴圈不應被單一次 Shell 呼叫長時間（超過交接區記錄的具體上限）阻塞。
2. 一般（快速可解析）路徑的導覽行為與現有測試涵蓋的結果完全不變。
3. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "SHCreateItemFromParsingName|BrowseToObject|BrowseToIDList|resolve_location" src/explorer_host/explorer_host.cpp src/shell_core/shell_core.cpp
```

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 實作紀錄（2026-09-03）

- 官方文件查證確認：`IExplorerBrowser::BrowseToObject` 的第一次導覽是同步，之後的導覽透過 `IExplorerBrowserEvents` 非同步回報；事件順序為 `OnNavigationPending` → `OnViewCreated` → `OnNavigationComplete` 或 `OnNavigationFailed`。[BrowseToObject](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-iexplorerbrowser-browsetoobject) 與 [IExplorerBrowserEvents](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-iexplorerbrowserevents) 沒有提供把 parsing name 直接交給 ExplorerBrowser 的 API。
- `BrowseToIDList` 只接受 PIDL；把字串轉成 PIDL/`IShellItem` 仍需 Shell parsing，因此不能作為非同步解析替代品。既有 Up 路徑已直接使用 `BrowseToIDList(nullptr, SBSP_PARENT)`，不再額外加入同步解析。
- `shell_core::resolve_location` 保留已知資料夾 identity 的必要解析，但不再先以 `SHCreateItemFromParsingName` 探測 primary/fallback。`ExplorerHost::navigate` 建立單一 `IBindCtx`，設定 `BIND_OPTS.dwTickCountDeadline = GetTickCount() + 1000`，在同一 deadline 內嘗試 primary parsing name，失敗才嘗試 fallback path；兩者都失敗時沿用既有 recoverable navigation failure UI。[SHCreateItemFromParsingName](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-shcreateitemfromparsingname) 接受 bind context，[IBindCtx::SetBindOptions](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-ibindctx-setbindoptions) 支援設定 binding deadline。
- address、Back/Forward、Up、tab realize 與 Group realize 的既有呼叫路徑都繼續經由 `navigate`/`navigate_up`，因此不新增執行緒、async runtime 或 COM 生命週期變更。`IBindCtx` deadline 是 Shell binding 的原生限制；若第三方 provider 完全忽略 binding deadline，Windows 沒有可在同一 STA 強制中斷該 provider 的 API，這個邊界不能由本票安全繞過。

### 驗證

- `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：PASS
- `cmake --build build`：PASS
- `ctest --test-dir build --output-on-failure`：18/18 PASS（包含 `panedock_launch_smoke`）
- `rg -n "SHCreateItemFromParsingName|BrowseToObject|BrowseToIDList|resolve_location" src/explorer_host/explorer_host.cpp src/shell_core/shell_core.cpp`：PASS
- `rg -n "kNavigationResolutionTimeoutMs|CreateBindCtx|dwTickCountDeadline|SetBindOptions|bind_context\.Get" src/explorer_host/explorer_host.cpp`：PASS
- 真實桌面上的離線 UNC／mapped drive 逾時、五種操作的 UI 反應性與 provider 是否遵守 deadline 未執行；依 `docs/testing.md` 記為 `未驗證,需真實桌面`，不以 headless smoke test 代替。
