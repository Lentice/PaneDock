#pragma once

#include <string_view>

namespace panedock::app_shell {

inline bool diagnostic_requested(int argc,
                                 const wchar_t* const* argv) noexcept {
    constexpr std::wstring_view long_flag = L"--diagnostic";
    constexpr std::wstring_view slash_flag = L"/diagnostic";
    const auto equals_case_insensitive = [](std::wstring_view value,
                                            std::wstring_view expected) {
        if (value.size() != expected.size()) return false;
        for (std::size_t index = 0; index < value.size(); ++index) {
            wchar_t actual = value[index];
            wchar_t wanted = expected[index];
            if (actual >= L'A' && actual <= L'Z') actual += L'a' - L'A';
            if (wanted >= L'A' && wanted <= L'Z') wanted += L'a' - L'A';
            if (actual != wanted) return false;
        }
        return true;
    };
    if (argc <= 1 || argv == nullptr) return false;
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) continue;
        const std::wstring_view argument(argv[index]);
        if (equals_case_insensitive(argument, long_flag) ||
            equals_case_insensitive(argument, slash_flag))
            return true;
    }
    return false;
}

}  // namespace panedock::app_shell
