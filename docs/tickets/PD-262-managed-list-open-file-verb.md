# PD-262 — 自建列表雙擊檔案呼叫 Shell 預設開啟動作

Phase 8 · managed-list interaction

## 來源與交付

來源：使用者回報列表中的檔案無法雙擊開啟或執行。受控 `Pane` 測試確認資料夾列可沿用既有導覽，檔案列的 `open_list_row()` 則因 `directory == false` 直接回傳，沒有呼叫 Shell。受控 `NM_DBLCLK` 測試確認通知已到達 Pane；因此問題在檔案列沒有預設 verb 動作，不在雙擊通知路由。現行 `IExplorerBrowser` 預設路徑維持原生 Shell 行為；本票修復 PaneDock-managed list 路徑。

**What to build：**managed-list 的檔案列在雙擊或 Enter 時，使用父 Shell location 與目前 item identity 呼叫 Shell 的預設 `open` verb。資料夾列繼續導覽。所有 Shell 呼叫沿用目前 request binding 的新鮮度與 STA re-entry 邊界。

## 綁定限制（引用與適用方式）

- design-spec FR-006：「雙擊資料夾列沿用其 Shell location 導覽；雙擊檔案列使用目前父 Shell location 與 item identity 呼叫 Windows Shell 預設 `open` verb，`.exe` 等可執行項目依 Shell 行為啟動。不得從顯示名稱拼路徑或直接呼叫 filesystem API。」
- design-spec FR-006：「Shell identity 不使用顯示名稱或持久化 PIDL。」檔案身分取自 `Row::identity`；顯示文字只供畫面呈現。
- design-spec §9.2：「UI、`app_shell` 與 `explorer_host` 維持單一 STA」；Shell 呼叫及 callback 均留在 UI STA。
- design-spec §12：「managed-list 項目狀態、generation 過濾、排序／selection anchor 與有界容量應透過 Pane 可觀察的公開操作或純值結果邊界測試；不假造 `IExplorerBrowser`。」本票用受控 Shell presenter，不啟動真實檔案。
- development：「A pane-only feature is implemented in `Pane` or one of its sub-modules, not in the coordinator.」檔案列 activation 由 `Pane` 管理，Shell verb 組裝留在既有 `explorer_host` 邊界。
- development：「The UI, `app_shell`, and `explorer_host` remain single-threaded STA code.」不得為 verb invocation 引入 worker、lock、atomic 或跨 STA COM pointer。
- AGENTS：「File operations go through Shell `IDataObject` and `IFileOperation`. Never assemble a path string and call the filesystem directly — that loses virtual items, progress UI and conflict handling.」此票不做檔案變更；開啟動作使用 Shell context verb，並以 namespace parsing identity 保留 virtual item 支援。
- AGENTS：「Shell APIs re-enter our message loop during drag, `IFileOperation` progress and internal view work. Host-side sequencing and shutdown must be reentrancy-safe」；Shell pump 後重新檢查 pane、tab、Group 與 navigation generation，不跨 pump 持有 row reference。
- AGENTS：「New non-trivial logic needs one focused runnable test or self-check.」回歸測試以 fake presenter 觀察實際 `NM_DBLCLK` 路徑及 owned target；不得呼叫真實 verb。

## 閱讀與追查入口

- `docs/design-spec.md` FR-006、FR-007、§9.2、§12；`docs/development.md` Pane 所有權、STA／Shell re-entry 與測試規則；`docs/testing.md` managed-list Pane 公開操作測試。
- `src/app_shell/pane_managed_list.cpp`：`open_list_row()`、`list_notification()` 的 `NM_DBLCLK`、Enter routing；追查目標列在排序後的 index／identity。
- `src/app_shell/pane.h`：`Row` identity、`file_command_snapshot()`／`file_command_is_current()`、`ContextMenuPresenter` seam、`ShellCallScope` 與 Pane lifecycle。
- `src/app_shell/pane.cpp`：Enter accelerator、navigation generation、`content_location()` 與 stale binding 驗證；追查 `open_list_row()` 所有 callers。
- `src/app_shell/pane_context_menu.cpp`：`ContextMenuRequest` 建立、presenter 呼叫、`ShellCallScope` 和 stale target guard。
- `src/explorer_host/context_menu.h`、`src/explorer_host/context_menu.cpp`：父 Shell folder 與 child identity 解析、`IContextMenu` 建立、預設 verb invocation 和 COM lifetime。
- `tests/unit/managed_list_file_commands_test.cpp`、`tests/unit/managed_list_context_menu_test.cpp`；`CMakeLists.txt` 對應 CTest target。

## 範圍

- 檔案列的雙擊與 Enter 共用 `Pane::open_list_row()`；每次只以目前有效 Pane binding 對應的 item identity 送出預設 `open` verb。
- 本機檔案用目前 folder parsing name 加 `Row::identity.key` 作為 context menu child leaf；namespace file 使用原始 Shell parsing identity。不得使用 `Row::name`、PIDL 持久化或在 UI 自行拼接路徑呼叫 filesystem API。
- 在既有 `ContextMenuPresenter` 測試 seam 新增預設 verb invocation；正式路徑透過 Shell `IContextMenu::InvokeCommand` 呼叫 canonical `open`，不顯示右鍵 popup、不建第二套 shell identity／COM helper。
- invocation 前後及每個可能泵訊息的 Shell 呼叫後驗證完整 binding／folder 仍有效；Group、tab、location 或 generation 改變時不再使用舊 target。
- 保留資料夾雙擊與 Enter 導覽、既有右鍵選單、legacy `IExplorerBrowser` 路徑及 Shell file operation 行為。

## 驗收標準

- [x] 受控 `NM_DBLCLK` 對本機檔案列恰好提交一個預設 open request，folder 為當前 resolved Shell location、items 只有該列的 stable leaf identity；不啟動真實程式。
- [x] namespace 檔案列傳遞原始 Shell parsing identity，不以 display name 或 fallback path 取代；使用 fake presenter 驗證 request。
- [x] 透過 Enter activation 也呼叫同一預設 open 路徑；資料夾的雙擊與 Enter 仍導覽到正確 child location。
- [x] Shell presenter 重入並改變 location 後，舊 binding 被拒絕；沒有 COM pointer、PIDL 或 HWND 跨至 worker／測試 fake。
- [x] `panedock_managed_list_file_commands` 與 context-menu tests 通過；不啟動外部 app、不修改 fixture 檔案、不執行 GUI verb；`git diff --check` 通過。

## Non-goals

不變更 legacy `IExplorerBrowser` 預設或 PD-241 gate；不增加檔案操作、右鍵 UI、執行前確認、worker、timer、cache、session persistence 或 filesystem API；不改目錄導覽、Shell namespace resolution 或 file operation implementation。

## Agent checks 與交付

使用 LLVM-MinGW Clang／LLD，不尋找 MSVC。建置於 ignored `build/pd262/`，避免覆寫使用者現有 build 與執行檔：

~~~powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake -S . -B build/pd262 -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build/pd262 --parallel 4 --target panedock_managed_list_file_commands_test panedock_managed_list_context_menu_test
ctest --test-dir build/pd262 --output-on-failure -R '^panedock_managed_list_(file_commands|context_menu)$'
git diff --check
~~~

- 不用測試實際 `.exe`、document 或其他 verb，避免開啟外部應用程式；真實 Shell namespace provider／OneDrive／第三方 extension 實測仍由 PD-241 gate 負責。
