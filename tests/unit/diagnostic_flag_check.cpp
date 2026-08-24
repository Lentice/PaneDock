#include "unit/test_util.h"

#include "app_shell/diagnostic_mode.h"

#include <array>

int main() {
    using panedock::app_shell::diagnostic_requested;

    EXPECT(!diagnostic_requested(0, nullptr));

    const std::array<const wchar_t*, 1> no_arguments{L"PaneDock.exe"};
    EXPECT(!diagnostic_requested(static_cast<int>(no_arguments.size()),
                                 no_arguments.data()));

    const std::array<const wchar_t*, 2> long_flag{L"PaneDock.exe",
                                                   L"--diagnostic"};
    EXPECT(diagnostic_requested(static_cast<int>(long_flag.size()),
                                long_flag.data()));

    const std::array<const wchar_t*, 2> slash_flag{L"PaneDock.exe",
                                                    L"/diagnostic"};
    EXPECT(diagnostic_requested(static_cast<int>(slash_flag.size()),
                                slash_flag.data()));

    const std::array<const wchar_t*, 2> uppercase_flag{L"PaneDock.exe",
                                                        L"--DIAGNOSTIC"};
    EXPECT(diagnostic_requested(static_cast<int>(uppercase_flag.size()),
                                uppercase_flag.data()));

    const std::array<const wchar_t*, 2> plural_flag{L"PaneDock.exe",
                                                    L"--diagnostics"};
    EXPECT(!diagnostic_requested(static_cast<int>(plural_flag.size()),
                                 plural_flag.data()));

    const std::array<const wchar_t*, 2> short_flag{L"PaneDock.exe",
                                                   L"--diag"};
    EXPECT(!diagnostic_requested(static_cast<int>(short_flag.size()),
                                 short_flag.data()));

    const std::array<const wchar_t*, 3> third_argument{
        L"PaneDock.exe", L"--profile", L"--diagnostic"};
    EXPECT(diagnostic_requested(static_cast<int>(third_argument.size()),
                                third_argument.data()));

    const std::array<const wchar_t*, 3> quoted_path{
        L"PaneDock.exe", L"C:\\Program Files\\PaneDock.exe",
        L"/DIAGNOSTIC"};
    EXPECT(diagnostic_requested(static_cast<int>(quoted_path.size()),
                                quoted_path.data()));

    return panedock::test::summary("diagnostic_flag_check");
}
