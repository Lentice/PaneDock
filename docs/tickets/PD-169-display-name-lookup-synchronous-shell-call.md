# PD-169 — Pinned/Tab/Startup 顯示名稱查詢仍在 UI 執行緒同步呼叫 `SHCreateItemFromParsingName`

Phase 7 · app_shell / shell_core · Depends on: PD-168, PD-100, PD-146

## 來源

2026-09-03 三方稽核（Claude / Codex / OpenCode，OpenCode 獨立提出）。經 fork 對照現有原始碼核對，判定為 CONFIRMED-NEW，與 PD-168 同一根因（UI 執行緒同步呼叫慢速 Shell API），但call site與影響範圍不同，故獨立開票。

## 背景與現況

`display_text_for_parsing_name`（`src/app_shell/main.cpp:671-677` 一帶）：

```cpp
std::wstring display_text_for_parsing_name(
    AppState& state, std::wstring_view parsing_name) {
    if (!parsing_name.starts_with(L"::")) return std::wstring(parsing_name);
    ...  // 呼叫 shell_core 的同步 SHCreateItemFromParsingName + GetDisplayName(SIGDN_NORMALDISPLAY)
}
```

只在 parsing name 是虛擬資料夾/CLSID 形式（`::` 開頭）時才會真正觸發同步 Shell 查詢；一般檔案系統路徑走零成本的 fast path。呼叫點包括：

- Pinned Locations 選單開啟時（`main.cpp:3623` 一帶）
- Pinned Locations 管理視窗每一列（`main.cpp:2447` 一帶）
- 虛擬資料夾 tab 標籤（`main.cpp:1675` 一帶）
- 啟動時的初始標籤（`main.cpp:3092` 一帶）

`PD-146-display-name-shell-reentry-guard.md` 已經把這個呼叫包進 `ShellCallScope`，但那只解決「關閉期間重入」的安全性，交接區明確把「同步」本身列為下一輪待查項目，從未被獨立開票。

## 為什麼這是真的問題

一個釘選的網路路徑（pinned network path）若離線或緩慢回應，會讓 Pinned Locations 選單、管理視窗開啟，或含有該虛擬/網路路徑的 tab 重新整理標籤時卡住整個訊息迴圈——範圍比 PD-168 小（只影響虛擬/CLSID 形式的 parsing name），但觸發時機更零散（選單、管理視窗、標籤刷新都可能各自觸發一次），仍違反 `docs/design-spec.md` NFR-003 的反應性要求精神。

## Fix 方向

先完成 PD-168 對「Shell 自身非同步機制」的查證結果，若該查證得出一個可重用的非阻塞/有時限的解析模式，本票套用同一模式到 `display_text_for_parsing_name`；若 PD-168 判定沒有更好的 Shell 原生非同步路徑可用，本票改為對這幾個呼叫點套用與 PD-168 交接區記錄的相同逾時/降級策略（例如逾時後顯示原始 parsing name 而非卡住，不阻塞選單開啟）。

## 綁定限制（引用）

- `docs/design-spec.md NFR-003`（見 PD-168 引用的完整文字）。
- `AGENTS.md`：「Display names are never identifiers.」——本票的 fix 不得把顯示名稱的解析結果回寫進任何持久化的 identity 欄位，維持現狀（只影響 UI 顯示）。
- `AGENTS.md`：「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」——優先重用 PD-168 產出的機制，不要另起一套。

## 檔案與範圍

- `src/app_shell/main.cpp`：`display_text_for_parsing_name`（:671-677 一帶）及其四個呼叫點（:1675, 2447, 3092, 3623，以實際 `rg -n "display_text_for_parsing_name" src/app_shell/main.cpp` 結果為準）。
- `src/shell_core/shell_core.cpp`：實際執行 `SHCreateItemFromParsingName` + `GetDisplayName(SIGDN_NORMALDISPLAY)` 的函式（PD-100 引入）。
- `docs/tickets/PD-100-virtual-folder-display-name-instead-of-parsing-code.md`、`PD-146-display-name-shell-reentry-guard.md`、`PD-168-navigate-synchronous-shell-resolution-blocks-ui.md`（本票的前置依賴與機制來源）。

## Scope

1. 待 PD-168 完成，重用其產出的非阻塞/逾時模式套用到 `display_text_for_parsing_name`。
2. 確認四個呼叫點在逾時/降級情況下都能顯示合理的 fallback（原始 parsing name 或既有的「未解析」佔位），不留白畫面或卡住的選單。
3. 新增一個聚焦 self-check 驗證 fallback 行為（可用假造一個會逾時的 parsing name 輸入來源模擬，不需要真實網路環境）。

## Non-goals

- 不重新設計 PD-100 的顯示名稱正確性邏輯，只處理同步阻塞。
- 不改變 Pinned Locations 的資料模型或管理對話框的其他行為。
- 不在本票內處理一般檔案系統路徑（本來就是零成本 fast path，不受影響）。

## Acceptance Criteria

1. 對一個模擬的緩慢/逾時虛擬路徑，Pinned Locations 選單開啟、管理視窗開啟、tab 標籤刷新三個路徑都不應無限期阻塞訊息迴圈。
2. 一般虛擬資料夾（如 This PC，快速可解析）的顯示名稱行為與現有測試涵蓋的結果完全不變。
3. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "display_text_for_parsing_name" src/app_shell/main.cpp
```

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 實作交接（2026-09-03）

- `panedock::shell_core::display_text_for_parsing_name` 保留 `::` 以外的零成本 fast path；虛擬 parsing name 會建立 `IBindCtx`，沿用 PD-168 的 1000ms `dwTickCountDeadline`，並將 context 傳給 `SHCreateItemFromParsingName`。建立/設定 context、Shell 解析或 `GetDisplayName` 失敗時均回傳原始 parsing name，不改動任何 persisted identity。
- 顯示名稱解析仍集中在 `src/shell_core/shell_core.cpp`；`refresh_navigation_chrome`、`tab_display_text`（含 tab refresh/paint）、pinned locations manager、startup chrome 與 pinned locations menu 的既有路徑均繼續共用 `AppState`/`ShellCallScope` helper，沒有新增同步查詢或 caller-specific fallback。
- focused self-check：擴充既有 `shell_reentry_gate_check` 驗證 bind deadline 與 fallback 契約，並在 `shell_core_view_mode` 加入無效虛擬 parsing name 保留原字串的 runnable check；沒有新增 thread、timer、async runtime 或 fake COM seam。
- Agent checks：configure PASS；`cmake --build build` PASS；`ctest --test-dir build --output-on-failure` PASS（18/18，含 `panedock_launch_smoke`）；`rg -n "display_text_for_parsing_name" src/app_shell/main.cpp` PASS；`git diff --check` PASS。
- 未以真實離線網路 provider 或第三方 Shell extension 驗證其是否遵守 binding deadline；此邊界與 PD-168 相同，實際 provider 若忽略 Shell binding deadline，Windows 沒有可在同一 STA 安全強制中斷的 API。
