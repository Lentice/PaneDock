# PD-068 — 關閉程式時在 `SHELL32.dll` 當機:`IShellFolderView::SetCallback` 的 out 參數傳了 `nullptr`

Phase 7 · explorer_host · Depends on: PD-051

- Source: 使用者回報(2026-08-26),附「PaneDock.exe - 應用程式錯誤:位於 0x00007FFCC572CE3F 的指令參考於 0x0000000000000000 的記憶體。該記憶體不能被 written。」對話框截圖。
- Origin: 使用者原文追加項。
- Priority: **CRITICAL**——這是當機,而且是**穩定可重現**的當機,優先於所有 UI 修飾票。

## 已確認的根因(有 crash dump 堆疊佐證,不是猜測)

### 症狀可穩定重現

不需要任何使用者互動:啟動 `build\PaneDock.exe`、等待 8 秒讓 pane 完成導覽、再以不帶 `/F` 的 `taskkill /PID` 優雅關閉。**連續 3 輪產生 3 個 crash dump,重現率 100%。**

Windows 事件記錄(「應用程式錯誤」)一致指向:

```
失敗的應用程式名稱: PaneDock.exe
錯誤模組名稱:      SHELL32.dll  版本 10.0.26100.9168
例外狀況代碼:      0xc0000005   (存取違規)
錯誤位移:          0x00000000002bce3f
```

### Crash dump 堆疊(以 Windows Kits 的 `cdb.exe` 配合 Microsoft 公開符號分析)

```
FAULTING_IP:      shell32!CDefView::SetCallback+f
ExceptionAddress: 00007ffcc572ce3f
ExceptionCode:    c0000005 (Access violation)
FAILURE_BUCKET_ID: NULL_POINTER_WRITE_c0000005_shell32.dll!CDefView::SetCallback

STACK_TEXT:
  shell32!CDefView::SetCallback+0xf
  PaneDock+0x2af16
  PaneDock+0x33ad
  ...
```

失敗分類明確寫出 **`NULL_POINTER_WRITE`**,而且位移只有 `+0xf`——是函式開頭的一個儲存指令。

### 程式碼層級的根因

`src/explorer_host/explorer_host.cpp` 的 `ExplorerHost::destroy()`(第 657-665 行,PD-051 新增)在拆除選取變化回呼時:

```cpp
(void)folder_view->SetCallback(previous_view_callback_.Get(),
                               nullptr);          // ← 第二個參數是 out 參數
```

`IShellFolderView::SetCallback(IShellFolderViewCB* pNewCB, IShellFolderViewCB** ppOldCB)` 的第二個參數是**輸出參數**,用來回傳被換下來的舊回呼。**shell32 的 `CDefView::SetCallback` 實作不會檢查這個指標是否為 null,而是直接寫入 `*ppOldCB`。** 傳 `nullptr` 進去就是對位址 `0x0` 寫入——正是使用者截圖中「參考於 `0x0000000000000000` 的記憶體。該記憶體不能被 written」的字面意思。

**對照組佐證:** 同一個檔案第 545-546 行安裝回呼時傳的是 `&previous_view_callback_`(合法位址),所以安裝路徑從來不當機;**只有 `destroy()` 的拆除路徑會當機**,這與「只在關閉程式時當機」的症狀完全吻合。

### 這個當機造成的連鎖影響(說明為何它必須優先修)

當機發生在關閉流程中,導致程序沒有正常結束而留下殘留:

- 殘留程序仍持有 `Ctrl+Shift+L` 全域熱鍵,下一個啟動的實例在 `WM_CREATE` 中 `RegisterHotKey` 失敗,跳出 `PaneDock could not register its layout hotkey.` 對話框後 `return -1` 中止建立主視窗。
- 症狀表現為 `Get-Process` 的 `MainWindowHandle` 為 `0`、`GetWindowRect` 回傳 `0,0,0,0`、`PrintWindow` 回傳 `False`,**很容易被誤判成「本環境不支援截圖/自動化」**——PD-055 與 PD-056 的實作 agent 都曾因此誤判並放棄實機驗證。
- 殘留程序也會鎖住 `build\PaneDock.exe`,使 `cmake --build` 以 `unable to remove file: Permission denied` 失敗。

## 已確認的修法(已實作並驗證)

改為提供一個真實的接收槽,再把結果丟棄:

```cpp
// CDefView::SetCallback stores the outgoing callback through the
// second parameter without checking it for null, so passing
// nullptr faults inside shell32 during teardown. Hand it a real
// slot and drop the value instead.
Microsoft::WRL::ComPtr<IShellFolderViewCB> replaced;
(void)folder_view->SetCallback(previous_view_callback_.Get(),
                               replaced.GetAddressOf());
```

`replaced` 是區域 `ComPtr`,離開作用域時自動 `Release`,不會洩漏。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

`AGENTS.md`:
> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`docs/design-spec.md` NFR(穩定性):
> 應用程式不得在正常操作或關閉流程中當機。

## Files to read and trace first

- `src/explorer_host/explorer_host.cpp` 第 645-690 行(`ExplorerHost::destroy()`)——**本票修改處在第 657-665 行。**
- `src/explorer_host/explorer_host.cpp` 第 533-550 行(`navigation_complete()` 安裝回呼)——對照組,傳的是合法位址,不需要改。
- `src/explorer_host/explorer_host.cpp` 第 17 行(`kIidShellFolderView`)、`ViewCallback` 類別——PD-051 的回呼實作。
- `docs/tickets/PD-051-pane-status-bar.md`——這段程式碼的來源票。

## Scope

1. `ExplorerHost::destroy()` 的 `SetCallback` 改傳合法的 out 參數。

## Non-goals

- 不改回呼的安裝路徑(`navigation_complete`)。
- 不改 `ViewCallback` 的實作或 refcounting。
- 不改 `SFVM_SELECTIONCHANGED` 的偵測機制或狀態列行為(PD-051/PD-060)。
- 不改關閉流程的其他順序(`Unadvise`、`IUnknown_SetSite(nullptr)`、`Destroy` 的先後維持不變)。

## Acceptance

1. **連續 5 輪「啟動 → 等待導覽完成 → 優雅關閉」不產生任何新的 crash dump。**
2. 關閉後程序確實結束,不留殘留(`Get-Process PaneDock` 為空)。
3. Windows 事件記錄不再新增 `SHELL32.dll` 的 `0xc0000005` 記錄。
4. 狀態列的項目數/選取數顯示(PD-051)未回歸。
5. 切換 Group、切換版型、切換 tab 後再關閉,同樣不當機。
6. `cmake --build build`、`ctest --test-dir build --output-on-failure` 全數通過。
7. `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "SetCallback|kIidShellFolderView|previous_view_callback_" src\explorer_host\explorer_host.cpp
git diff --check
```

**當機驗證方法(本票已驗證可用,後續遇到當機請直接沿用):**

```powershell
# 1. 用 dump 數量做前後對照,比讀事件記錄可靠
$before = (Get-ChildItem "$env:LOCALAPPDATA\CrashDumps" -Filter "PaneDock*").Count
# ... 反覆啟動/關閉 N 輪 ...
$after  = (Get-ChildItem "$env:LOCALAPPDATA\CrashDumps" -Filter "PaneDock*").Count

# 2. 讀事件記錄取得錯誤模組與位移
Get-WinEvent -FilterHashtable @{LogName='Application'; ProviderName='Application Error'} -MaxEvents 5 |
  Where-Object { $_.Message -like '*PaneDock*' }

# 3. 用 cdb 取得真正的堆疊(本機已安裝 Windows Kits)
$cdb = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe"
$env:_NT_SYMBOL_PATH = "srv*C:\symbols*https://msdl.microsoft.com/download/symbols"
& $cdb -z <dump> -c ".ecxr; k 25; !analyze -v; q"
```

首次下載 Microsoft 符號可能需要數分鐘,請放背景執行。

**測試後用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉,不要 `Stop-Process -Force`。**

## Handoff requirements

- 修改前後的 crash dump 數量對照(輪數與新增數)。
- `cdb` 分析出的堆疊(若重新驗證)。
- 關閉後是否仍有殘留程序。
- 狀態列功能是否回歸。

## 交接區

<!-- 實作 agent 填寫,append-only -->

### 2026-08-26 實作交接(dispatcher 直接修復)

本票由 dispatcher 直接診斷並修復,未派給實作 agent,理由是:這個當機會讓所有其他票的實機驗證無法進行(殘留程序占用熱鍵導致新實例無法建立主視窗、並鎖住 `build\PaneDock.exe` 使建置失敗),屬於阻塞性缺陷,且根因已由 crash dump 完全確定。

**修改:** `src/explorer_host/explorer_host.cpp` `destroy()` 中 `SetCallback` 的第二個參數由 `nullptr` 改為區域 `ComPtr<IShellFolderViewCB> replaced` 的 `GetAddressOf()`。

**驗證結果:**

| 建置 | 輪數 | 新增 crash dump |
|---|---|---|
| 修改前 | 3 | **3**(100% 重現) |
| 修改後 | 5 | **0** |

修改後五輪關閉,程序每次都在數秒內確實結束,**不再出現殘留**(先前每輪都需要額外的 `Stop-Process -Force` 清理)。`clean_shutdown` 正確寫入 `true`。

另外以 PD-055 之前的版本(commit `9ee9c17`)在獨立 worktree 建置測試 3 輪,`0` 個 crash dump——一度看似指向 PD-055/PD-057 是禍首,但 crash dump 堆疊明確指出 `CDefView::SetCallback`,與那兩張票的改動無關。合理推論是這個缺陷自 PD-051 起就存在,只是需要「pane 完成導覽並安裝過回呼」才會在關閉時觸發,舊版測試那 3 輪的時序剛好沒有滿足條件。**這說明「換個版本測幾輪沒重現」不足以排除嫌疑,crash dump 的堆疊才是決定性證據。**

`cmake --build build` 成功;`ctest` `100% tests passed out of 4`;`git diff --check` 通過。