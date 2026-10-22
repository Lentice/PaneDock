# PD-250 — 移除無障礙支援：列表使用不被 MSAA／UIA 辨識的視窗類別

Phase 8 · 目標 0.5 天

## 來源與交付

來源：使用者 2026-10-22 回報 `D:\Downloads`／`D:\Documents` 切換與排序仍卡 4–10 秒，並指示「應該完全移除 PaneDock 無障礙的功能」。

交付一個結果：managed list 視窗不再被 MSAA／UIA 的 SysListView32 代理辨識，外部無障礙／自動化用戶端無法觸發逐列讀取；規格、測試矩陣不再把無障礙列為需求或 gate 項目。

## Overrides

延續並補強 PD-247。PD-247 只讓 `WM_GETOBJECT` 回傳 0；證據顯示不夠：取樣堆疊中 comctl32 在 ListView 內部觸發 WinEvent，掛在行程內的 UIA hook 經 OLEACC 預設 ListView 代理對 `LVS_OWNERDATA` 逐列發 `LVN_GETDISPINFOW`，UI 執行緒 CPU 滿載 4 s（1443 列）至 10 s。以未知類別名稱的 superclass 取代，代理不再走訪列。移除 `docs/testing.md` 的 Accessibility 功能矩陣列，以及 design-spec 中以無障礙為驗證項目的敘述；不影響 PD-241 其他項目。

## 綁定限制（引用）

- AGENTS：「Event-driven idle path only.」本票不新增 timer 或輪詢。
- AGENTS：「UI/Shell host hazard is reentrancy, never concurrency.」本票不新增執行緒或鎖。
- AGENTS：App UI 文字必須是英文；本票不新增文字。

## 具體範圍

1. `kManagedListClassName`（`pane.h`）與 `register_managed_list_class()`（`pane_managed_list.cpp`）：以 `GetClassInfoExW(nullptr, WC_LISTVIEWW)` 複製視窗程序，註冊 `PaneDockManagedList`；`realize_managed_list` 用它建立列表。
2. 保留 `list_icon_subclass` 內 `WM_GETOBJECT → 0`。
3. 測試以 `kManagedListClassName` 尋找列表視窗。
4. 更新 `docs/testing.md`、`docs/design-spec.md`、`docs/roadmap.md`。

## Non-goals

不改 legacy `IExplorerBrowser` 路徑（其無障礙由 Windows 檔案總管提供，PD-242 會移除）；不改歷史證據文件。

## 驗收標準

- [ ] 使用者的 `D:\Downloads` 排序與 `D:\Documents`⇄`D:\Downloads` 快速切換，UI 不再封鎖 ≥ 100 ms（`SendMessageTimeout(WM_NULL)` 探針，60 秒）。
- [ ] 全部 ctest 通過；`git diff --check` 通過。

## Agent checks

```powershell
$env:PATH = 'E:\Dev\LLVM-MinGW\bin;E:\Dev\Ninja;' + $env:PATH
cmake --build build
ctest --test-dir build --output-on-failure
git diff --check
```

實機探針需要使用者操作，見驗收標準第一項。

## 交接區

- 未驗證：superclass 是否解除實機卡頓（取決於使用者機器上哪個用戶端掛了 in-context hook）；若仍卡，下一步取樣 `PaneDock.exe` 的 UI 執行緒堆疊，確認是否為其他視窗（header、tab、sidebar）。
