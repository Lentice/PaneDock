# PD-094 — ~~啟動時讀完 session 立刻做一次完全冗餘的同步寫回,發生在主視窗建立之前~~(前提有誤,已撤回)

## 撤回說明(2026-08-27,實作前發現)

本票在派工給 Codex 實作時被攔下。Codex 讀完 `src/app_shell/main.cpp:4093-4145` 與 `docs/tickets/PD-025-crash-recovery-path.md` 後指出:啟動時這次 `save_now(state)` 呼叫**不是**無資料變更的冗餘寫入,而是 PD-025「已確認的產品決策 2」明文要求的不乾淨關閉偵測標記:

> 2. **標記時機:啟動讀取成功後立刻寫一次 `clean_shutdown = false`;正常關閉的那一次寫入寫 `true`。** 執行期的 `save_now()` 一律寫 `false`。這樣「啟動後尚未做任何變更就崩潰」——也就是最重要的那個情境(某個 extension 在第一次 realize 時弄垮 process)——一樣偵測得到。**啟動時多一次磁碟寫入是可接受的:NFR-001 的門檻是閒置期間零 I/O,啟動不是閒置。**

也就是說,這次寫入的「內容不變」是我(撰票者)的誤讀——`clean_shutdown` 欄位**確實**在這次寫入時從上次關閉留下的 `true` 被改寫成 `false`,這正是崩潰偵測機制運作所需的那一個 bit 變更,而不是「什麼都沒變」。PD-025 決策 2 也已經**明確評估過**這次啟動寫入的磁碟成本,並判定可接受(理由:NFR-001 只要求閒置期間零 I/O,啟動不算閒置)。

本票原始的三方效能研究(Claude/Codex/OpenCode)只從「寫入內容是否和讀入內容相同」的角度判斷冗餘,沒有比對 PD-025 已經記錄的決策,因此把一個刻意設計的崩潰偵測寫入誤判成死碼。**本票的 Fix 方向因此整段撤回,不得實作**——依 `docs/tickets.md` 的 ticket authoring rules,要覆寫 PD-025 決策 2 必須在新 ticket 內提出覆寫理由與新證據,而本票並未做到這件事,只是基於錯誤的前提誤判。

若未來要降低這次寫入的成本,正確方向是**保留「啟動時必須把 `clean_shutdown` 標記為 `false`」這個語意,只降低達成它的手段成本**(例如只序列化/寫入 `clean_shutdown` 這一個欄位,而不是整份 `preserved_json` 重新解析+雙 flush+備份複製的完整 `write_session` 管線)——但這需要新的、更輕量的持久化 API,不是「刪掉這次呼叫」這麼簡單的一行改動,規模已經超出「半天到兩天」的單票原則,且尚無證據顯示這次啟動寫入(毫秒等級)的成本真的值得為它新增一條輕量寫入路徑。因此不重開新票,留在 `docs/tickets.md` 的候選清單,觸發條件見該處。

## 原始內容(僅供歷史記錄,已不適用)

## 來源

2026-08-27 三方效能研究(Claude / Codex / OpenCode)。三方**各自獨立**指出同一行程式碼與同一個結論:這次寫入沒有任何資料變更,純屬冗餘。

## 背景與現況

`main.cpp:3936` 一帶 `read_session` 讀入既有 session,緊接著 `main.cpp:3949`(`CreateWindowExW` 之前)呼叫 `save_now(state)`。此時完全沒有任何使用者動作或資料變更——這是啟動流程裡的第一次 `save_now`,寫回的內容和剛讀進來的內容應該完全相同。

`save_now` 觸發的完整成本(見 `src/core/session.cpp:520-570` 的 `write_session`)包含:序列化整份 `preserved_json`(重新 parse 一次原始 JSON 字串)、寫暫存檔並 `FlushFileBuffers`、讀回 primary 檔驗證(`session.cpp:546`,又是一次完整 read+parse)、複製到 `.bak` 並對備份再 `FlushFileBuffers`、最後 atomic rename。兩次強制磁碟 flush 在 SSD 上約各 1–20ms,HDD/BitLocker 更久,而且這一切都在 `CreateWindowExW`(主視窗建立)之前的同步路徑上發生,直接延後「使用者第一次看到畫面」的時間點。

## 為什麼這是真的問題

`docs/performance-baseline.md` 把冷啟動延遲列為尚未量測但列管的 NFR。這次寫入不對應任何實際狀態變更,是純粹的浪費——刪掉它不會改變任何使用者可觀察的行為,卻能直接砍掉一次完整的「序列化+雙 flush+複製+rename」在冷啟動關鍵路徑上的成本。

## Fix 方向

刪除或跳過 `main.cpp:3949` 這次啟動時的 `save_now(state)` 呼叫。若這次呼叫原本承擔了某個隱性用途(例如把 `read_session` 遷移/修正後的內容立即落盤,對應 `docs/design-spec.md`/`AGENTS.md` 的 schema 前向相容規則),需要在交接區明確指出並保留該用途對應的最小寫入(例如只在 `read_session` 實際發生了 migration/欄位修正時才寫回,而不是每次啟動都無條件寫);若確認純屬冗餘,直接刪除即可,不需要新增條件判斷。

## 綁定限制(引用)

- `AGENTS.md`:「Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.」—— 這是刪除一行冗餘呼叫,不是新增邏輯;若需要保留 migration 場景的寫回,也只需要一個既有的旗標判斷,不需要新機制。
- `AGENTS.md`:「Every persisted config/setting file must be designed for forward extensibility... A schema change is additive」—— 若 `read_session` 內部已有 migration 邏輯需要把結果落盤,本票不得移除該落盤,只能移除「沒有任何變更也無條件寫」這個多餘部分。實作前務必先確認 `read_session`/相關 migration 函式是否依賴這次呼叫來完成落盤。

## 檔案與範圍

- `src/app_shell/main.cpp`:
  - 啟動路徑中 `read_session` 與其後的 `save_now(state)` 呼叫(約 `:3936-3949` 一帶)
- `src/core/session.cpp`:`decode()`/migration 相關邏輯(約 `:340-420` 一帶)——僅用於確認是否有依賴啟動時立即落盤的場景,不預期需要修改。

## Scope

1. 確認 `read_session` 是否存在「讀入時做了 migration/欄位修正、需要立即落盤」的場景。
2. 若不存在,直接移除啟動路徑上這次無條件的 `save_now(state)` 呼叫。
3. 若存在,把寫回限縮成只在該場景發生時才寫(而不是每次啟動都寫),並在交接區記錄判斷依據。

## Non-goals

- 不改變 `save_now`/`write_session` 本身的實作(那是 PD-091 的範圍,若兩票都要做,建議先做本票,PD-091 再疊加)。
- 不改變 session migration 的 schema 或版本號邏輯。

## Acceptance Criteria

1. 正常啟動(session 檔案存在且合法、無需 migration)不再觸發任何一次 `write_session`,直到使用者第一次做出實際變更或程式正常關閉。
2. 若 `read_session` 觸發了 migration,migration 後的內容仍會被正確落盤(不因本票而遺失 migration 結果)。
3. `cmake --build build` 與 `ctest --test-dir build --output-on-failure` 全數通過。
4. 正常關閉時 `session.json` 內容與變更前完全一致(驗證移除這次啟動寫入沒有造成任何資料遺失)。

## Agent Checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## 交接區

（實作完成後由實作者填寫）
