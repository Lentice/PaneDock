# PD-070 — 診斷模式每擋下一個第三方 shell extension 就跳一個系統模態對話框,無法無人值守執行

Phase 7 · app_shell · Depends on: PD-024

- Source: 使用者回報(2026-08-26),附「PaneDock.exe - 映像錯誤:可能是 C:\Program Files (x86)\Dropbox\Client\DropboxExt64.96.0.dll 並非設計為在 Windows 上執行,或它包含錯誤,請試著使用原始安裝媒體再次安裝程式,或連絡您的系統管理員或軟體廠商以取得支援。錯誤狀態 0xC0000428。」對話框截圖,出現在自動化驗證過程中。
- Origin: 使用者原文追加項。
- Priority: HIGH——這個對話框會**阻塞**程式,使 `--diagnostic` 無法無人值守執行,而該模式的存在目的正是在崩潰歸因時跑自動化;同時它把 PaneDock 自己造成的封鎖顯示成「Dropbox 的檔案壞了」,會誤導使用者去重灌無關的軟體。

## 已確認的根因(有實證,不是猜測)

### 那顆 DLL 沒有壞

對話框的文字說該 DLL「並非設計為在 Windows 上執行,或它包含錯誤」。實測結果相反:

```
Get-AuthenticodeSignature "C:\Program Files (x86)\Dropbox\Client\DropboxExt64.96.0.dll"

Status        : Valid
StatusMessage : Signature verified.
Signer        : CN="Dropbox, Inc", O="Dropbox, Inc", L=San Francisco, S=California, C=US
Issuer        : CN=DigiCert Trusted G4 Code Signing RSA4096 SHA384 2021 CA1
Not After     : 2027/12/15
```

**簽章有效、未過期、由 DigiCert 簽發。** 檔案完好,Windows 檔案總管本身載入它也毫無問題。

### 真正的原因是 PD-024 的診斷模式

`0xC0000428` 是 `STATUS_INVALID_IMAGE_HASH`。這個代碼的語意不是「檔案損毀」,而是「這個映像不符合本 process 現行的**程式碼完整性政策**」。

PD-024 在 `src/app_shell/main.cpp` `wWinMain`(第 3350-3370 行)為 `--diagnostic` 設定:

```cpp
PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY policy{};
policy.MicrosoftSignedOnly = 1;
diagnostic_mode = SetProcessMitigationPolicy(
                      ProcessSignaturePolicy, &policy, sizeof(policy)) != 0;
```

`MicrosoftSignedOnly` 要求本 process 之後載入的每一個 DLL **都必須由 Microsoft 簽署**。Dropbox 的 extension 是由 Dropbox 簽署的——簽章完全有效,但簽署者不是 Microsoft,於是載入器以 `STATUS_INVALID_IMAGE_HASH` 拒絕它。**這正是 PD-024 想要的行為**:抑制第三方 shell extension 進入本 process。

**缺陷不在「擋下來」,而在「擋下來的方式會跳出模態對話框」。** Windows 載入器在 DLL 載入失敗時,預設會顯示這個 hard-error 對話框(俗稱 image error box),而且**每一個被擋下的 extension 各跳一次**,每一次都要人按「確定」才能繼續。PD-024 的票與交接區完全沒有提到這個副作用,驗收也沒有涵蓋它。

### 為何這件事必須修

1. PD-024 的目的是「在懷疑第三方 extension 造成崩潰時,用診斷模式重跑一次做歸因」。歸因跑通常是自動化的、無人值守的——**一個需要人按確定的模態對話框讓這個模式失去意義**。
2. 對話框的文字是 Windows 提供的通用訊息,把責任指向那個 DLL 與它的廠商。使用者照著做會去重新安裝 Dropbox,而問題根本不在 Dropbox。
3. 安裝愈多 shell extension 的機器,對話框愈多。在典型的開發機上這可能是十幾個。

## 已確認的產品決策

1. **修法是在設定簽章政策成功之後,呼叫 `SetErrorMode(SEM_FAILCRITICALERRORS)`。** 這個旗標的文件化語意就是「不要顯示載入器的嚴重錯誤對話框,直接把失敗回傳給呼叫端」,正是本票需要的。載入仍然被擋(政策不變),只是不再彈窗。

2. **只在診斷模式套用,不要無條件套用。** 一般模式下,若真的有一個損毀的 extension,那個對話框是使用者唯一會看到的線索;把它在一般模式也關掉會把真實故障變成靜默失敗。診斷模式則相反——被擋是**預期行為**,不是故障。

3. **用 `SetErrorMode` 而不是 `SetThreadErrorMode`。** 被擋的載入發生在 Shell view 內部,可能落在任何一條 shell32 建立的執行緒上,per-thread 版本蓋不到。本 process 沒有需要保留 hard-error 對話框的其他用途。

4. **既有的 `SEM_` 值必須保留,採「讀出後 OR 回去」的寫法**,不要直接覆蓋:

   ```cpp
   // PD-070: MicrosoftSignedOnly makes the loader reject every
   // non-Microsoft-signed shell extension with STATUS_INVALID_IMAGE_HASH,
   // and by default the loader shows a modal image-error box for each one.
   // That box blocks the unattended crash-attribution run this mode exists
   // for, and its text blames the extension's vendor for a block we
   // requested. Suppress the box; the load still fails, silently.
   SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS);
   ```

5. **`diagnostic_mode == false` 時(包含 `SetProcessMitigationPolicy` 失敗而 fallback 成一般模式的情形)不得呼叫。** 政策沒設成功就沒有大量被擋的 DLL,也就沒有理由壓抑對話框。

6. **不改對話框的文字、不攔截載入、不嘗試逐一列出被擋的 DLL。** 列出被擋清單是有價值的診斷資訊,但那需要載入器回呼或 ETW,遠超本票範圍;若之後有需要,另開票。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> No network, no telemetry, no third-party runtime, no services, no drivers, no admin elevation.

——`SetErrorMode` 是純 per-process 的 Win32 呼叫,不需要權限提升、不寫 registry,符合此限制。

`AGENTS.md`:
> Do not bundle schema migrations or destructive cleanup into an unrelated change.

`docs/tickets/PD-024-diagnostic-mode-suppressing-shell-extensions.md` 決策 2:
> 抑制手段是 `SetProcessMitigationPolicy(ProcessSignaturePolicy, ...)` 的 `MicrosoftSignedOnly`。……設定後**不可逆**,因此必須在 `wWinMain` 的最早期、任何 Shell view 建立之前呼叫。

——本票不改這個順序,`SetErrorMode` 緊接在政策設定成功之後、`OleInitialize` 之前呼叫。

`docs/design-spec.md` NFR-006(可抑制第三方 shell extension):本票不改變抑制的範圍或手段,只修正其副作用。

## Files to read and trace first

- `src/app_shell/main.cpp` 第 3350-3372 行(`wWinMain` 的診斷模式區塊)——**本票唯一的修改處。**
- `src/app_shell/diagnostic_mode.h`——旗標解析,不需要改。
- `docs/tickets/PD-024-diagnostic-mode-suppressing-shell-extensions.md`——本票是它的後續修正,**不要編輯該票的內容**,只在本票寫明補充。
- `src/app_shell/main.cpp` 第 3400-3450 行——診斷模式的視窗標題與 PD-025 的 clean-shutdown 提示文字,確認不受影響。

## Scope

1. 診斷模式設定成功後,呼叫 `SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS)`。

## Non-goals

- 不改一般模式的錯誤對話框行為。
- 不改 `MicrosoftSignedOnly` 政策本身或 `--diagnostic` 的旗標解析。
- 不列舉、記錄或回報被擋下的 DLL 清單。
- 不改 PD-025 的 clean-shutdown 提示文字(即使它正是建議使用者加 `--diagnostic` 的地方)。
- 不動 `OleInitialize` 與 DPI awareness 的呼叫順序。

## Acceptance

1. **以 `--diagnostic` 啟動,完成四個 pane 的導覽,全程不出現任何「映像錯誤 / image error」對話框。** 驗證時機器上必須至少裝有一個非 Microsoft 簽署的 shell extension(本機的 `C:\Program Files (x86)\Dropbox\Client\DropboxExt64.96.0.dll` 即可);若機器上沒有,在交接區寫明無法驗證的原因,不要宣稱通過。
2. 診斷模式仍然確實抑制了 extension:視窗標題仍為 `PaneDock — Diagnostic Mode`,且行為與 PD-024 交接區記錄的一致。
3. **一般模式(不加旗標)的行為完全不變**,`GetErrorMode()` 在一般模式下與修改前相同。
4. 診斷模式與一般模式都能正常關閉,不留殘留程序,`clean_shutdown` 寫入 `true`。
5. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
6. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "SetErrorMode|SEM_FAILCRITICALERRORS|MicrosoftSignedOnly|SetProcessMitigationPolicy" src\app_shell\main.cpp
git diff --check
```

**對話框偵測方法(不要靠目視,對話框可能出現在畫面外或被遮住):** 用 `EnumWindows` + `GetWindowThreadProcessId` 過濾出本 process 的視窗,列出 class 為 `#32770` 的項目並讀取其子 `Static` 控制項文字。**必須用 `CharSet=CharSet.Unicode` 宣告 `GetWindowTextW`/`GetClassNameW`,否則取回亂碼。** 本環境已驗證此方法可用。

```powershell
# 修改前後各跑一次,對照 #32770 的出現次數
Start-Process .\build\PaneDock.exe -ArgumentList "--diagnostic"
# 等待 10 秒讓四個 pane 完成導覽後掃描
```

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。** 若程式卡在模態對話框而收不到 `WM_CLOSE`,先對該 `#32770` 送 `WM_CLOSE`(`0x0010`)。

## Handoff requirements

- 修改前後、以 `--diagnostic` 啟動時出現的 `#32770` 對話框數量與標題文字對照。
- 驗證當時機器上實際存在的非 Microsoft 簽署 shell extension(至少一個的路徑與簽署者)。
- 一般模式的 `GetErrorMode()` 值未改變的證據。
- 兩種模式的關閉結果與 `clean_shutdown` 值。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-26 撰票時的既有證據(dispatcher 記錄,供實作者直接引用)

- `Get-AuthenticodeSignature` 對 `DropboxExt64.96.0.dll` 回報 `Status: Valid`、簽署者 `CN="Dropbox, Inc"`,**證明對話框宣稱的「檔案損毀」不成立**,錯誤來自本 process 的簽章政策。
- 撰票時以 `--diagnostic` 啟動一次並掃描視窗,只看到 `PaneDockMainWindow` 標題 `PaneDock — Diagnostic Mode`,**沒有**攔到 `#32770`;同一輪不加旗標的一般模式反而攔到一個 `#32770`,但內容是 PD-025 的 `PaneDock did not shut down cleanly last time.`,與本票無關。這表示被擋的 extension 是**延遲載入**的,只有在 Shell view 走到會觸發該 extension 的路徑(例如導覽到 Dropbox 資料夾、或觸發 overlay/context menu handler)時才會跳。**實作者驗證時必須導覽到會實際觸發第三方 extension 的位置,否則會得到假陰性。**
