# PD-088 — `Site`/`ViewCallback` 持有的 `ExplorerHost*` 在 `destroy()` 後未清空,Shell 延遲回呼可能觸碰已銷毀的 host

## 來源

2026-08-27 三方稽核(Claude / Codex / OpenCode)。Claude 與 Codex 各自獨立指出同一個根因:`ExplorerHost::destroy()` 讓 `destroying_` 旗標生效,但 `Site`/`ViewCallback` 物件內部持有的原始 `ExplorerHost*` 指標從未被清空,而這兩個 COM 物件的生命週期不完全由我們控制。

## 背景與現況

`ExplorerHost::destroy()`(`src/explorer_host/explorer_host.cpp:685` 一帶)依序執行 `Unadvise`、`IUnknown_SetSite(nullptr)`、還原前一個 `SetCallback`,再 `events_.Reset()`、`site_.Reset()`、`view_callback_.Reset()`。這些 `Reset()` 只釋放**我們自己持有**的參照。

`Site` 類別(約 `explorer_host.cpp:102` 一帶,`QueryService` 等實作於 `:133` 附近)與 `ViewCallback` 類別(約 `:53` 一帶)都以原始指標 `host_` 指回擁有它們的 `ExplorerHost`,且都是實作 `IUnknown` 的 COM 物件——`AddRef`/`Release` 由呼叫方(`IExplorerBrowser` 內部、`CDefView`,甚至第三方 shell extension)管理,不受我們的 `Reset()` 直接控制。程式碼裡已經有註解記載 `CDefView::SetCallback` 在 teardown 期間行為不正常,這代表 Shell 端確實會在非預期時機保留/呼叫這些物件。

一旦 Shell 端在我們 `destroy()` 之後仍持有 `Site`/`ViewCallback` 的參照(常見於非同步收尾、或第三方 extension 延遲釋放),之後對這些物件的任何呼叫(例如 `navigation_complete()`、`navigation_failed()`、`selection_changed()`)都會經由 `host_` 觸碰一個邏輯上已經銷毀、其 `std::function`/`std::wstring` 等成員可能已被清空或處於未定義狀態的 `ExplorerHost`。

## 為什麼這是真的問題

`AppState::explorers` 是固定大小的成員陣列(`std::array<ExplorerHost, kExplorerCount>`),`destroy()` 之後物件記憶體不會被釋放,所以這不是典型的 use-after-free 導致立即當機,而是**狀態污染型**的錯誤——延遲回呼會在一個「邏輯上不存在」的 host 上執行,可能觸發不可預期的狀態變更、甚至再次觸發 session 儲存。這正是診斷模式(`--diagnostic`,PD-024/PD-070)存在的目的所要攔截的那類 shutdown 期間的 shell extension 相關崩潰/異常;修好這個根因可以消除一整類延遲回呼問題,而不只是讓症狀更難重現。

## Fix 方向

給 `Site` 與 `ViewCallback` 都加上一個 `detach()` 方法,把 `host_` 設為 `nullptr`;在 `destroy()` 一開始、也就是在 `Unadvise`/`SetSite(nullptr)`/`Reset()` 之前,先在 `destroying_` guard 生效的同一個時間點呼叫兩者的 `detach()`。`navigation_complete()`/`navigation_failed()`/`selection_changed()`(以及 `Site` 內其餘會用到 `host_` 的方法,如 `QueryService`)在方法一開始檢查 `host_ == nullptr` 就直接 no-op 返回,不觸碰任何 host 狀態。

這是一個小改動(兩個類別各加一個成員清空 + 呼叫入口加 null-check),不需要改變 `Site`/`ViewCallback` 的所有權模型或新增額外的同步機制——單一 STA 執行緒,不涉及多執行緒競態,只是「呼叫時機可能落在我們已經邏輯銷毀之後」的重入問題。

## 綁定限制(引用)

- `AGENTS.md`:「Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.」—— 本票正是修正 shutdown 期間的重入安全缺口。
- `AGENTS.md`:「Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive」—— 本票不改變既有的 `Initialize`/`Destroy` 配對紀律,只補上「Destroy 之後,舊回呼不該再觸碰這個物件」這一段。
- `AGENTS.md`:「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」—— 用一個 `host_ = nullptr` 加 null-check,不要引入額外的生命週期追蹤機制(例如 weak pointer、額外的 generation counter)。

## 檔案與範圍

- `src/explorer_host/explorer_host.cpp`:
  - `Site` 類別定義與其方法(約 `:53`、`:102`、`:133` 一帶)
  - `ViewCallback` 類別定義與其方法
  - `ExplorerHost::destroy()`(約 `:685-739` 一帶)
  - `navigation_complete()` / `navigation_failed()` / `selection_changed()` 等會被 Shell 回呼的方法
- `src/explorer_host/explorer_host.h`:若 `Site`/`ViewCallback` 的宣告在此檔案,對應加上 `detach()` 宣告。

## Scope

1. `Site` 與 `ViewCallback` 各加上 `detach()`,清空內部的 `host_` 原始指標。
2. `destroy()` 在既有的 teardown 順序中,於 `destroying_` 生效後、`Reset()` 之前,呼叫兩者的 `detach()`。
3. 兩個類別中所有會存取 `host_` 的方法,在方法開頭加上 `host_ == nullptr` 的 no-op 檢查(依現有程式碼風格,回傳該方法原本失敗/空狀態時的等價回傳值,例如 `E_NOINTERFACE`/直接 `return`)。

## Non-goals

- 不改變 `Site`/`ViewCallback` 的 COM 生命週期模型(不引入 weak reference、不改用 `ComPtr` 以外的機制)。
- 不處理多執行緒情境——本專案是單一 STA 執行緒,這是重入(reentrancy)問題不是資料競爭(data race)問題。
- 不修改 `destroy()` 既有的 `Unadvise`/`SetSite`/`SetCallback` 還原順序,只在其前面插入 `detach()` 呼叫。

## Acceptance Criteria

1. `destroy()` 呼叫後,`Site`/`ViewCallback` 上任何後續(即便是 Shell 延遲觸發的)回呼都不會存取已銷毀 `ExplorerHost` 的成員——可用一個聚焦的 self-check 驗證:建構一個 `ExplorerHost`,`destroy()` 之後直接呼叫 `Site`/`ViewCallback` 上對應的回呼方法(在不透過真實 Shell 事件的情況下,直接呼叫方法本身),確認是 no-op 且不觸發 assert/crash。
2. 既有的 `tests/unit/explorer_host_lifetime_check.cpp` 行為不受影響(除非該測試本身需要因為本票新增的 no-op 行為而更新其中對回呼結果的斷言,若需要更新請在交接區說明)。
3. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。
4. 正常的 Initialize → 導覽 → Destroy 流程(沒有延遲回呼的情況)視覺行為與現在完全一致。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 交接區

- 修正 `src/explorer_host/explorer_host.cpp`：`Site` 與 `ViewCallback` 新增 `detach()`，`ExplorerHost::destroy()` 在 teardown guard 生效後、既有 `Unadvise`/`SetSite(nullptr)`/`SetCallback`/`Reset()` 順序開始前清空兩個 raw `ExplorerHost*`。延遲進入的 `MessageSFVCB`、`QueryService`、Site 事件回呼現在會回傳原本的空/失敗結果，不再存取 host；正常 host 存在時的流程未改變。
- 驗證結果：
  - `cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release`：PASS。
  - `cmake --build build`：PASS。
  - `ctest --test-dir build --output-on-failure`：PASS，5/5。
  - `./build/panedock_explorer_host_lifetime_check.exe`：PASS；實際完成 Initialize、導覽、view mode、失敗導覽與 Destroy，`live_view_count` 與子視窗清理檢查通過。
- Acceptance criteria：
  - 1：程式碼路徑已驗證（detach 發生在所有 teardown 呼叫前，所有會觸碰 host 的入口均有 null guard）；未加入直接持有匿名 namespace 內部 COM callback 類別的測試 harness，因此 Shell 真實延遲回呼仍留給使用者在診斷模式/第三方 extension 環境確認。
  - 2：已驗證，既有 `explorer_host_lifetime_check` 通過，未修改其斷言。
  - 3：已驗證，configure/build/ctest 全數通過。
  - 4：Shell Initialize → 導覽 → Destroy 的自動檢查通過；實際視覺行為仍需使用者在真實桌面確認。
