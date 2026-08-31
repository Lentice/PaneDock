# PD-158 — 建立 `file_operations` 並讓 Ctrl+V 正確區分 Cut／Copy

Phase 7 · file operations · Depends on: PD-023, PD-121, PD-123, PD-140, PD-157

- Source: 2026-08-31 spec／實作差異審查；使用者確認「create ticket to fix it」，並決定拆出 `file_operations`。
- Priority: HIGH——PaneDock 攔截 Ctrl+V 後永遠呼叫 `IFileOperation::CopyItems`；來自 Ctrl+X 的 `IDataObject` 即使帶有 `DROPEFFECT_MOVE`，目前也會被複製而非移動。

## Outcome

Ctrl+V 從 OLE clipboard 讀取 Shell 的 preferred drop effect：Copy 走 `IFileOperation::CopyItems`，Cut 走 `IFileOperation::MoveItems`。來源與目的均維持 Shell identity／`IDataObject`，不組 path 自行操作檔案；相關 clipboard、`IFileOperation` 與 progress sink 實作移入新的 `panedock_file_operations` 模組。

## 已確認的現況

- message loop 在 `IExplorerBrowser::TranslateAccelerator` 前攔截 Ctrl+V，呼叫 `src/app_shell/main.cpp::perform_clipboard_paste`。
- 該函式以 `OleGetClipboard` 取得 `IDataObject`，建立 destination `IShellItem` 與 `IFileOperation`，但固定呼叫 `CopyItems(data.Get(), destination.Get())`。
- repo 沒有 `MoveItems`、`CFSTR_PREFERREDDROPEFFECT`、`CFSTR_PERFORMEDDROPEFFECT` 或 `CFSTR_PASTESUCCEEDED` 處理。
- `FileOperationProgressSink`、operation-in-progress、cancel 與 close-after-transfer 狀態已由 PD-123 建立，必須保留。
- Microsoft Shell contract：`CopyItems` 與 `MoveItems` 接受相同的 `IUnknown* items, IShellItem* destination`；動作由 method 決定。Cut／Copy preference 由 `CFSTR_PREFERREDDROPEFFECT` 的 `DROPEFFECT_MOVE`／`DROPEFFECT_COPY` 表示。

## Binding constraints — quoted, do not go looking for them

`docs/design-spec.md` FR-007：

> 複製、移動、刪除、重新命名經由 Shell `IFileOperation`,含原生進度對話框與衝突提示。剪貼簿操作經由 Shell `IDataObject`。

`docs/design-spec.md` §9.1：

> | `file_operations` | `IFileOperation`、剪貼簿、OLE 拖放 | 直接檔案系統呼叫 |

`docs/design-spec.md` §9.2：

> 單一 STA UI 執行緒。所有 Shell view 與 COM 回呼都在該執行緒。不引入 async runtime。

`AGENTS.md`：

> File operations go through Shell `IDataObject` and `IFileOperation`. Never assemble a path string and call the filesystem directly — that loses virtual items, progress UI and conflict handling.

`AGENTS.md`：

> Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side locking and shutdown sequencing must be reentrancy-safe.

`AGENTS.md`：

> Every `IExplorerBrowser` that was `Initialize`d must have `Destroy` called on it, or the instance leaks. Never destroy a parent HWND while a view is alive; on shutdown destroy all views before the message loop exits.

## Files to read and trace first

- `src/app_shell/main.cpp`：`FileOperationProgressSink`、`perform_clipboard_paste`、Ctrl+V message routing、`ShellCallScope`、`file_operation_*`／`close_after_file_operation`／cancel state、`complete_deferred_close` 及所有 caller。
- `CMakeLists.txt` 的 `panedock_file_operations` placeholder 與 Windows link libraries。
- `src/shell_core`（PD-157 完成後）：location value boundary；不得把 raw COM pointer 拉回 app_shell。
- `docs/tickets/PD-023-shell-file-operations-acceptance.md`、`PD-121-cross-group-clipboard-copy-paste.md`、`PD-123-close-during-shell-copy-validation.md`、`PD-140-shell-call-reentry-shutdown-gate.md`。
- `docs/testing.md` 的 Clipboard、cross-Group、shutdown 驗收矩陣。

## Scope

1. 新增 `src/file_operations/file_operations.h/.cpp` 與 `panedock_file_operations` static target；移入 `OleGetClipboard`、preferred-effect decode、destination `IShellItem` resolution、`IFileOperation` 建立／設定／執行及 progress sink implementation。
2. 對 `IDataObject` 註冊並讀取 `CFSTR_PREFERREDDROPEFFECT`：
   - `DROPEFFECT_MOVE`：呼叫 `MoveItems`；
   - `DROPEFFECT_COPY`：呼叫 `CopyItems`；
   - format 不存在：維持目前安全預設，呼叫 `CopyItems`；
   - `DROPEFFECT_LINK` 或不支援的組合：不得猜測或退化成 move；回報 unsupported，讓既有 Shell accelerator／錯誤路徑接手或安全失敗。
3. `IDataObject::GetData` 的 `STGMEDIUM` 必須以 `ReleaseStgMedium` 釋放；`TYMED_HGLOBAL` 以 `GlobalLock`／`GlobalUnlock` 讀取單一 `DWORD`，檢查 medium type、大小與 null。
4. operation 成功且未 aborted 後，依 Shell clipboard contract 透過 `IDataObject::SetData` 回報實際 `CFSTR_PERFORMEDDROPEFFECT`；Cut/delete-on-paste 需要的 `CFSTR_PASTESUCCEEDED` 也必須以實際 effect 回報。不得自行刪除來源檔案。
5. app_shell 保留 UI orchestration 與 shutdown intent，但不再 include/implement `IFileOperationProgressSink` 或直接呼叫 `OleGetClipboard`／`CoCreateInstance(CLSID_FileOperation)`／`CopyItems`／`MoveItems`。file_operations API 只回傳值型 result（HRESULT、aborted、chosen effect），不暴露 operation／data-object raw pointer。
6. 所有 operation 仍在既有 STA 與 `ShellCallScope` 中執行。PD-123 的 operation-in-progress、cancel、Close After Transfer、nested-loop barrier 與 teardown 順序不得退化；Move 與 Copy 共用同一條 lifecycle path。
7. 將 preferred-effect → operation kind 的判斷抽成不碰 COM 的最小純函式，新增 focused runnable check 覆蓋 MOVE、COPY、missing、LINK／unsupported；不要為其建立 interface 或 mock framework。
8. 真實桌面以 disposable data 驗證 PaneDock pane、Windows File Explorer 與跨 Group 的 Copy／Cut Paste；同磁碟與跨磁碟各至少一個 Move case。

## Non-goals

- 不用 path string、`CopyFile`、`MoveFile`、`std::filesystem::copy`／`rename` 執行產品檔案操作。
- 不改 Ctrl+X／Ctrl+C 的來源端實作；它們繼續由 live Shell view 產生 `IDataObject`。
- 不新增 background thread、async runtime、helper process、polling timer 或第三方 dependency。
- 不改 drag-and-drop effect、context-menu paste、delete、rename 或 link creation；app-owned drag-hover UI routing 不因本票搬動。
- 不改 session schema、ShellLocation identity、Group/tab behavior 或 `IExplorerBrowser` lifetime policy。
- 不把 PD-123 已完成的 close-transfer UX 重新設計。

## Acceptance criteria

1. Ctrl+C → Ctrl+V：目的地出現 copy，來源仍存在；conflict/progress UI 由 Shell 提供。
2. Ctrl+X → Ctrl+V：目的地出現 moved item，來源消失；不是先 copy 後由 PaneDock 自行 delete。
3. 同磁碟、跨磁碟、PaneDock pane↔pane、Explorer→PaneDock 與跨 Group 的代表案例都符合 Cut／Copy 語意；virtual item 若來源支援，仍以 `IDataObject` 交給 Shell。
4. missing preferred effect 保持 Copy；LINK／malformed medium 不會被誤判成 Move，也不 crash 或洩漏 `STGMEDIUM`。
5. transfer 進行中正常 close 仍遵守 PD-123：operation 完成／取消後才 destroy live views；Copy 與 Move 都沒有 hang、殘留 process 或 source corruption。
6. app_shell 不再直接實作 clipboard／`IFileOperation`，`panedock_file_operations` 為獨立 target；`src/core` 仍不含 Windows types。
7. Release build、完整 CTest、focused operation-selection check、graceful close smoke 與 `git diff --check` 通過。

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

```powershell
rg -n "OleGetClipboard|CLSID_FileOperation|CopyItems|MoveItems|IFileOperationProgressSink" src/app_shell/main.cpp
# 預期無 direct implementation；若只剩註解或 UI orchestration，交接區逐項說明。
```

```powershell
.\build\PaneDock.exe
# 真實、已解鎖桌面；只用 disposable source/destination：
# 1. Ctrl+C/Paste：source remains, destination hash matches。
# 2. Ctrl+X/Paste：source absent, destination hash matches。
# 3. Move 進行中正常 Close：完成或明確取消後才退出，無 partial/source corruption。
```

## 交接區

<!-- 實作 agent 填寫，append-only -->

2026-08-31 implementation handoff:

- Added `panedock_file_operations` with a value-only public result/callback API.
  It owns `OleGetClipboard`, preferred-effect decoding, destination Shell-item
  resolution, `IFileOperation`, and the progress sink; no COM pointer crosses
  back into `app_shell`.
- Missing preferred effect selects Copy; exact `DROPEFFECT_COPY` and
  `DROPEFFECT_MOVE` select `CopyItems` and `MoveItems`. LINK, combined, failed,
  or malformed media are unsupported and fall through safely. `STGMEDIUM` and
  `HGLOBAL` ownership follow their native release contracts.
- Successful, non-aborted operations report the performed and paste-succeeded
  effects through the clipboard data object. PaneDock never deletes a source
  itself. Existing STA `ShellCallScope`, cancellation callback, transfer-close
  dialog, and deferred-close barrier remain app-shell orchestration.
- Deterministic checks: Release configure/build PASS; focused effect-selection,
  shell-core boundary, and Shell re-entry checks PASS; non-launch CTest 12/12
  PASS; `git diff --check` PASS. The app-shell direct-implementation grep is
  empty.
- Real desktop Cut/Copy cases were not run because this environment cannot use
  disposable desktop data without touching user files. No existing process was
  stopped and no user file was created, moved, copied, or deleted.
- Current documentation lookup indexed Windows App SDK but not the legacy
  Desktop Shell clipboard pages; implementation therefore follows the binding
  contract quoted in this ticket and the installed Windows SDK declarations.

### 2026-08-31 — 主代理審查與自動驗證

- 依 Microsoft Learn 的 Shell Clipboard Formats、Shell Data Object 與 `IDataObject::SetData` ownership 契約重新審查。修正 Copy 不應回報 `CFSTR_PASTESUCCEEDED`；只有成功且未 aborted 的 Move 才回報該 delete-on-paste 完成訊號，Copy／Move 都回報實際 `CFSTR_PERFORMEDDROPEFFECT`。`SetData(TRUE)` 成功後 ownership 交給 data object，失敗則由 caller `GlobalFree`。
- 將 `PasteResult::handled` 延後到 `CopyItems`／`MoveItems` 成功排入 operation 後，讓 clipboard、destination、COM 建立或 unsupported-effect 失敗仍可安全交回 live Shell accelerator；destination parsing-name 配置失敗在 `noexcept` API 內明確回傳 `E_OUTOFMEMORY`，不 terminate。
- `GetAnyOperationsAborted` 失敗採 conservative aborted，不回報成功 effect；`PerformOperations`、aborted query、Unadvise 與 effect `SetData` failures 均輸出 HRESULT 診斷，不再靜默忽略。
- Release configure/build PASS；sandbox 內非 launch CTest 12/12 PASS，包含 focused effect-selection、shell-core boundary 與 re-entry checks；elevated `panedock_launch_smoke` 1/1 PASS（1.05 秒）；`git diff --check` PASS。
- 真實桌面 disposable Ctrl+C／Ctrl+X 矩陣仍未執行，因此 tracker 保持 `in_progress`；不得在沒有來源／目的存在性與 hash 證據前把本票標成 `done`。

2026-08-31 review remediation:

- Restored the PD-140 shutdown-abatement behavior across the extracted module. Clipboard setup now checks a non-owning abort callback after every re-entrant Shell/COM call and after queueing CopyItems/MoveItems, unadvises when necessary, and never enters PerformOperations after deferred shutdown.
- `file_operation_call_active` now protects setup lifetime without claiming that a transfer is already running. A re-entered close during setup follows the existing Shell-call deferred-shutdown path; the transfer dialog remains limited to `StartOperations` through `FinishOperations`.
- Strengthened `panedock_shell_reentry_gate` for this distinction. No fake `IDataObject`/COM test seam was added because `docs/testing.md` and the rejected-directions ledger explicitly prohibit fake COM abstractions; malformed-medium and `SetData` ownership remain covered by code review plus the pending real-desktop disposable-data matrix.
