#pragma once

#include <cwchar>

namespace panedock::app_shell {

inline bool diagnostic_requested(int argc,
                                 const wchar_t* const* argv) noexcept {
    if (argc <= 1 || argv == nullptr) return false;
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) continue;
        if (_wcsicmp(argv[index], L"--diagnostic") == 0 ||
            _wcsicmp(argv[index], L"/diagnostic") == 0)
            return true;
    }
    return false;
}

}  // namespace panedock::app_shell
