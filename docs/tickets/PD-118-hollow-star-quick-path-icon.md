# PD-118 — Quick path 按鈕改用空心星號圖示

Phase 7 · app_shell · Depends on: PD-111

- Source: 使用者要求(2026-08-29)。
- Origin: 使用者原文:「For quick path it should use 空心star instead of pin icon.」
- Override: PD-111 交接區記錄的 `U+E718` Pin glyph 選擇由本票覆寫；功能名稱與 Pinned Locations 資料模型不變。

## Binding constraints — quoted, do not go looking for them

`AGENTS.md`:
> Prefer the smallest working change. Reuse existing code before adding helpers or abstractions.

`AGENTS.md`:
> App UI text must be English. No Chinese strings ship in the binary.

`docs/development.md`:
> Reach for the standard library and Win32 before adding a dependency.

## Files to read and trace first

- `src/app_shell/main.cpp`: `kNavigationGlyphs`, `draw_navigation_icon_button`, and `draw_navigation_fallback_glyph`.
- `docs/tickets/PD-111-pinned-locations-menu.md`: existing Pinned Locations button wiring and glyph handoff.
- `docs/tickets/PD-075-unify-pane-chrome-icon-style.md`: Segoe MDL2 Assets as the existing pane icon system.

## Scope

1. Replace only the Pinned Locations navigation glyph with Segoe MDL2 Assets' hollow Favorite Star glyph `U+E734`.
2. Replace the matching GDI fallback pin drawing with a hollow outlined star so the fallback has the same meaning.
3. Keep the button command, tooltip, menu, geometry, hover, disabled state, and persistence unchanged.

## Non-goals

- Do not rename Pinned Locations to Quick paths or change the glossary terminology.
- Do not add bitmap assets, an icon dependency, or a new drawing abstraction.
- Do not change the other five pane navigation glyphs.

## Acceptance

1. The Pinned Locations button displays a hollow star on the normal Segoe MDL2 Assets path.
2. If the icon font cannot be used, the fallback is a hollow star, not a pin.
3. Clicking the button still opens the existing Pinned Locations menu in every visible pane.
4. `cmake --build build`, `ctest --test-dir build --output-on-failure`, and `git diff --check` pass.

## Agent checks

```powershell
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
rg -n "E734|fallback pinned|fallback star|glyph_kind.*5|kPinnedButtonIdBase" src\app_shell\main.cpp
git diff --check
```

```powershell
.\build\PaneDock.exe
# 手動:確認每個 pane 的 Pinned Locations 按鈕是空心星號，點擊後選單仍正常開啟。
```

**測試後請用不帶 `/F` 的 `taskkill /PID <pid>` 優雅關閉，不要 `Stop-Process -Force`。**

## Handoff requirements

- 記錄 `U+E734` 的選用理由與 fallback 星號繪製方式。
- 未能做的實機圖示驗證要如實記錄。

## 交接區

<!-- 實作 agent 填寫, append-only -->

### 實作交接（2026-08-29）

- Pinned Locations 使用 Segoe MDL2 Assets `U+E734`（Favorite Star 的空心版本），與既有導覽列字型 glyph 路徑一致；只替換第 6 顆按鈕的 glyph。
- 字型失敗時的 fallback 改為以既有 GDI pen 畫十個頂點的封閉星形折線，不填滿，因此仍是空心星號；按鈕 command、tooltip、menu 與 layout 沒有變更。
- Agent checks：CMake configure PASS；`cmake --build build` PASS；`ctest --test-dir build --output-on-failure` PASS（6/6）；`git diff --check` PASS。
- 未執行實機圖示畫面驗證；需使用者確認正常字型與 fallback 視覺結果。
