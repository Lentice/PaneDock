# Computer Use `failed to activate captured window` 調查

日期：2026-09-01  
範圍：研究 Computer Use 無法對 PaneDock 視窗輸入的工具層問題，不修改產品程式。

## 結論

本次 PaneDock 視窗可以被 `list_apps`／`get_window_state` 讀取，但輸入動作回報 `failed to activate captured window`；重新列舉、重新選取唯一視窗並重新觀察後仍失敗。這個證據表示「視窗可觀察」與「工具程序能取得前景／輸入權」是兩個不同階段，不能把失敗歸因於 PaneDock UI。

Windows 的 `SetForegroundWindow` 本身受前景鎖定、目前是否有作用中的 menu、最後輸入事件及程序關係等條件限制；失敗時回傳 0。[Microsoft `SetForegroundWindow`](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow)

另外，公開的 OpenAI Codex Windows Computer Use issue 已記錄同類工具／helper 層故障：可見的目標應用仍無法控制，且可能伴隨 API 版本不一致、`EnumWindows` 失敗或列舉為空；該 issue 將問題定位在 bundled runtime/helper layer，而非目標應用。[Codex issue #37201](https://github.com/openai/codex/issues/37201) [Codex issue #37932](https://github.com/openai/codex/issues/37932)

因此目前最小解法是：保持 Windows session 解鎖、PaneDock 位於目前桌面且沒有開啟 context menu；每次動作前 fresh 列舉／選取／觀察，單次只做一個動作並立即 refresh。若連續兩次 activation recovery 仍失敗，停止該次 GUI 驗證，改用新的 Computer Use task／重啟 Codex runtime，並記錄完整錯誤與版本；不要用 UIA、終端機或強制終止程序繞過限制。

## 本次證據

- `list_apps` 找到實際 `E:\GitHub\PaneDock\build\PaneDock.exe`，`get_window_state` 可取得有效視窗狀態。
- 第一次 layout 輸入回報 `failed to activate captured window`。
- fresh `list_apps`、重新選取相同唯一目標、fresh `get_window_state` 後重試，仍回報相同錯誤。
- 依 Computer Use 操作規則停止後續 GUI 動作；沒有把這次失敗算作 PaneDock 功能 FAIL。
- PaneDock 程序仍為 `Responding=True`；沒有強制終止。

## 避免重犯

1. 不重用舊的 window id、座標、索引或 screenshot。
2. context menu／外部 Shell verb 開啟期間，不對 PaneDock 進行下一個 GUI 動作；先關閉或重新取得狀態。
3. 一次 activation 失敗先做一次完整 fresh recovery；第二次仍失敗就停止 GUI，保留錯誤，不反覆重試。
4. 將可重現資料回報為 Computer Use runtime 問題：Codex／Computer Use 版本、Windows build、錯誤文字、目標程序與是否能列舉視窗。

