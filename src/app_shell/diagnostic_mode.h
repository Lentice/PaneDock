#pragma once

#include <windows.h>

#include <array>
#include <charconv>
#include <cwchar>
#include <system_error>

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

// The one diagnostic stdout channel. `panedock.<key>=<value>`, one line,
// written straight to the handle so nothing is buffered past a crash.
template <typename Value>
void write_diagnostic_line(const char* key, Value value) noexcept {
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (output == nullptr || output == INVALID_HANDLE_VALUE) return;

    constexpr char prefix[] = "panedock.";
    std::array<char, 96> line{};
    char* cursor = line.data();
    cursor = std::copy_n(prefix, sizeof(prefix) - 1, cursor);
    for (const char* name = key; *name != '\0'; ++name) {
        if (cursor + 2 >= line.data() + line.size()) return;
        *cursor++ = *name;
    }
    if (cursor + 2 >= line.data() + line.size()) return;
    *cursor++ = '=';
    const auto converted =
        std::to_chars(cursor, line.data() + line.size() - 1, value);
    if (converted.ec != std::errc{}) return;
    *converted.ptr = '\n';
    DWORD written = 0;
    (void)WriteFile(output, line.data(),
                    static_cast<DWORD>(converted.ptr - line.data() + 1),
                    &written, nullptr);
}

// PD-209: the switching-path latencies in docs/performance-baseline.md had no
// numbers because nothing measured them. Disabled means *nothing* runs, not
// even QueryPerformanceCounter.
class ScopedTiming final {
public:
    ScopedTiming(bool enabled, const char* key) noexcept : key_(key) {
        if (!enabled) return;
        LARGE_INTEGER start{};
        if (!QueryPerformanceCounter(&start)) return;
        start_ = start.QuadPart;
    }

    ~ScopedTiming() noexcept {
        if (start_ == 0) return;
        LARGE_INTEGER end{};
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceCounter(&end) ||
            !QueryPerformanceFrequency(&frequency) || frequency.QuadPart == 0)
            return;
        const double milliseconds =
            static_cast<double>(end.QuadPart - start_) * 1000.0 /
            static_cast<double>(frequency.QuadPart);
        write_diagnostic_line(key_, milliseconds);
    }

    ScopedTiming(const ScopedTiming&) = delete;
    ScopedTiming& operator=(const ScopedTiming&) = delete;

private:
    const char* key_;
    long long start_{};
};

}  // namespace panedock::app_shell
