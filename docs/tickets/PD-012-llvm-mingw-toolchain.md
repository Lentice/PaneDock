# PD-012 — 改用 LLVM-MinGW 工具鏈

Phase 0 · Depends on — · 狀態一律看 `docs/tickets.md` 的 Ticket 總覽表格

## Override 宣告

本 ticket **推翻**三處已記錄的決策：

- `docs/development.md:47`「MSVC, C++20, `/W4 /permissive- /EHsc`」
- `docs/development.md:49`「MSVC rather than LLVM-MinGW: WRL and the Shell COM headers are MSVC-oriented」
- `docs/tickets.md:129`「刻意不沿用 LLVM-MinGW toolchain」

推翻後的決策：**LLVM-MinGW（Clang／LLD，target `x86_64-w64-windows-gnu`），透過 `cmake/llvm-mingw.cmake` toolchain file，Ninja generator，C++20。**

`docs/adr/0001` 的語言選擇（C++ 直接坐在 Windows SDK 上、不隔翻譯層）不受影響，本 ticket 只換編譯器。

## 新證據

原決策的理由是一項未經實測的推斷：「WRL 與 Shell COM header 是 MSVC 取向」。實測推翻它。

1. **開發機沒有可用的 MSVC。** `vswhere` 只回報 VS2017 Professional，MSVC toolset `14.16.27023`（C++17，不支援 C++20）；`C:\Program Files\Microsoft Visual Studio\2022\` 是空目錄，`cl.exe` 不在 PATH。已安裝：Ninja `E:\Dev\Ninja\ninja.exe`、Windows SDK 最高 `10.0.19041.0`、LLVM-MinGW Clang 22.1.8 於 `E:\Dev\LLVM-MinGW`。原決策使整個 repo 在開發機上無法建置。

2. **LLVM-MinGW 編得過也跑得起來。** 探針原始碼（記錄於本節，非 repo 檔案）：

   ```cpp
   #include <windows.h>
   #include <shobjidl.h>
   #include <shlobj.h>
   #include <objbase.h>
   #include <wrl/client.h>

   int main() {
       CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
       Microsoft::WRL::ComPtr<IExplorerBrowser> eb;
       HRESULT hr = CoCreateInstance(CLSID_ExplorerBrowser, nullptr,
                                     CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&eb));
       if (FAILED(hr)) { CoUninitialize(); return 1; }
       FOLDERSETTINGS fs{};
       fs.ViewMode = FVM_DETAILS;
       fs.fFlags = FWF_AUTOARRANGE;
       RECT rc{0, 0, 400, 300};
       hr = eb->Initialize(GetDesktopWindow(), &rc, &fs);
       if (FAILED(hr)) { CoUninitialize(); return 2; }
       Microsoft::WRL::ComPtr<IShellItem> item;
       if (SUCCEEDED(SHCreateItemFromParsingName(L"C:\\", nullptr, IID_PPV_ARGS(&item)))) {
           eb->BrowseToObject(item.Get(), SBSP_ABSOLUTE);
       }
       eb->Destroy();
       CoUninitialize();
       return 0;
   }
   ```

   ```
   clang++ -std=c++20 -target x86_64-w64-windows-gnu -DUNICODE -D_UNICODE \
     -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_WIN32_WINNT=0x0A00 -Wall -Wextra -O2 \
     probe.cpp -o probe.exe -lole32 -loleaut32 -lshell32 -luuid
   → 零警告、零錯誤
   probe.exe → exit 0
   ```

   mingw-w64 有可用的 `wrl/client.h`，`shobjidl.h` 含 `IExplorerBrowser`、`CLSID_ExplorerBrowser`、`FOLDERSETTINGS`、`SBSP_*`。`Microsoft::WRL::ComPtr` 保留，`AGENTS.md` 的「Shell COM via `Microsoft::WRL::ComPtr`」不需改。

3. **`E:\github\nimblerun` 是同一台機器上的既有先例**：C++20、LLVM-MinGW、`cmake/llvm-mingw.cmake`、Ninja、Shell COM（`IShellLink`、`IPersistFile`、WIC PNG codec），23 個測試全綠，出貨中。

## 要讀與追蹤的檔案

- `AGENTS.md` — §Validation
- `docs/development.md` — §Build configuration（第 45–49 行）
- `docs/tickets.md` — §計畫決策紀錄 第 129 行
- `CMakeLists.txt` — 全檔（`if(NOT MSVC)` 警告、`panedock_set_warnings`）
- `tests/CMakeLists.txt`
- `E:\github\nimblerun\cmake\llvm-mingw.cmake` — 直接照抄的來源
- `E:\github\nimblerun\CMakeLists.txt` — compile definitions 的先例

## 範圍

1. 新增 `cmake/llvm-mingw.cmake`，內容與 nimblerun 版本相同：

   ```cmake
   set(CMAKE_SYSTEM_NAME Windows)

   set(CMAKE_C_COMPILER clang)
   set(CMAKE_CXX_COMPILER clang++)
   set(CMAKE_RC_COMPILER llvm-rc)

   set(CMAKE_C_COMPILER_TARGET x86_64-w64-windows-gnu)
   set(CMAKE_CXX_COMPILER_TARGET x86_64-w64-windows-gnu)

   set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
   ```

2. `CMakeLists.txt`：移除 `if(NOT MSVC) message(WARNING ...)` 整段與其上方三行 MSVC 註解。改為在**非** Clang 時發警告，理由改寫成本 ticket 的決策。`panedock_set_warnings` 保留 MSVC 分支（不主動破壞它），Clang 分支維持 `-Wall -Wextra -Wpedantic`。

3. `AGENTS.md` §Validation 的命令區塊改為帶 toolchain file 的版本，並在其上加一段先決條件，寫明工具鏈要求與「本專案不使用 MSVC」，讓冷讀的 agent 不會再去找 `cl.exe`：

   ```powershell
   cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```

   `-D` 後面必須加引號：PowerShell 會對未加引號的 `.` 切分參數（nimblerun NR-001 的實測結果）。

4. `docs/development.md` §Build configuration：`MSVC` 改為 LLVM-MinGW，warning flags 改為 `-Wall -Wextra -Wpedantic`，並把第 49 行的理由段整段換成本 ticket 的 override 與證據摘要（含「WRL 在 mingw-w64 可用，已實測 `IExplorerBrowser` 建立、`Initialize`、`BrowseToObject`、`Destroy` 全程通過」）。

5. `docs/tickets.md` 第 129 行：改寫為採用 LLVM-MinGW，指向 PD-012。§Ticket 總覽新增 PD-012 一列，PD-007 的 `Depends on` 改為 PD-012。§Dependency lanes 的 Phase 0 圖在 PD-007 之上加 PD-012。

6. 刪除既有的 `build/` 目錄。它的 CMake cache 是先前無 toolchain file 的 configure 產物，會沿用舊設定。

## 非目標

- 不動 `src/`、`tests/` 的任何原始碼。
- 不改語言、不改 `Microsoft::WRL::ComPtr` 的使用決策、不新增 `src/win/com.h` 之類的 COM 包裝（nimblerun 那樣做是因為它從沒引入 WRL；本專案 WRL 可用，不需要第二套）。
- 不加 LTO、`-static`、CFG 或任何 release 強化選項。那是 release 決策，不在 Phase 0。
- 不安裝任何東西，不改 PATH，不寫 `CMakePresets.json`。
- 不開始 PD-007。

## 驗收條件

- AC1 `cmake/llvm-mingw.cmake` 存在，內容與範圍第 1 項逐字相同。
- AC2 `grep -riE "MSVC rather than|刻意不沿用 LLVM-MinGW" AGENTS.md docs/ CMakeLists.txt` 零命中。
- AC3 clean configure（`build/` 已刪除）以 §Validation 的命令通過，`build/CMakeCache.txt` 的 `CMAKE_CXX_COMPILER` 指向 LLVM-MinGW 的 `clang++`。
- AC4 `cmake --build build` 成功，零警告。
- AC5 `ctest --test-dir build --output-on-failure` 全綠。
- AC6 `docs/tickets.md` 的 Ticket 總覽含 PD-012 一列，且 PD-007 的 `Depends on` 為 PD-012。
- AC7 `git diff --check` 無輸出。
- AC8 `git status --short` 只含：新增 `cmake/llvm-mingw.cmake`、新增本 ticket 檔、修改 `AGENTS.md`、`CMakeLists.txt`、`docs/development.md`、`docs/tickets.md`。`src/` 與 `tests/` 不得出現。

## Agent checks

```powershell
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
cmake -S . -B build -G Ninja -D"CMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
Select-String -Path build/CMakeCache.txt -Pattern "CMAKE_CXX_COMPILER:"
git diff --check
git status --short
```

```powershell
# 必須零命中
Select-String -Path AGENTS.md,CMakeLists.txt,docs/development.md,docs/tickets.md -Pattern "MSVC rather than","刻意不沿用"
```

## 交接區

- 工具鏈：LLVM-MinGW Clang 22.1.8（target `x86_64-w64-windows-gnu`）、Ninja `E:\Dev\Ninja\ninja.exe`；Windows SDK 最高版本為 `10.0.19041.0`。
- 建置使用 `cmake/llvm-mingw.cmake`；本 ticket 要求的 probe 未重跑，沿用本文件已記錄的 compile+run 證據。
- `src/` 與 `tests/` 未修改；PD-007 尚未開始。
- Clean configure、build、CTest 均通過；`build/CMakeCache.txt` 的 `CMAKE_CXX_COMPILER` 為 `E:/Dev/LLVM-MinGW/bin/clang++.exe`，build 輸出零警告，CTest 為 1/1 通過。
- 舊決策文字檢查與 `git diff --check` 均無輸出。`git status --short` 另列出未追蹤 `.claude/`；該目錄不屬本 ticket，未修改，因此 AC8 的嚴格條件仍待使用者決定是否清理。
