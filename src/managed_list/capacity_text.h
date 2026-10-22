#pragma once

#include <cstdint>
#include <cwchar>
#include <string>

namespace panedock::managed_list {

// Three significant digits in binary units, like Explorer's drive cards:
// 28.8 GB, 304 GB, 1.82 TB.
inline std::wstring capacity_size_text(std::uint64_t bytes) {
    constexpr const wchar_t* kUnits[] = {L"bytes", L"KB", L"MB", L"GB", L"TB", L"PB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < std::size(kUnits)) { value /= 1024.0; ++unit; }
    const int decimals = unit == 0 ? 0 : value >= 100.0 ? 0 : value >= 10.0 ? 1 : 2;
    wchar_t number[32]{};
    swprintf(number, std::size(number), L"%.*f", decimals, value);
    return std::wstring(number) + L" " + kUnits[unit];
}

inline std::wstring capacity_free_text(std::uint64_t free_bytes, std::uint64_t total_bytes) {
    return capacity_size_text(free_bytes) + L" free of " + capacity_size_text(total_bytes);
}

// Explorer turns the bar red once a drive is at least 90% full.
inline bool capacity_nearly_full(std::uint64_t free_bytes, std::uint64_t total_bytes) noexcept {
    if (total_bytes == 0 || free_bytes > total_bytes) return false;
    const auto used = total_bytes - free_bytes;
    return static_cast<long double>(used) / static_cast<long double>(total_bytes) >= 0.9L;
}

} // namespace panedock::managed_list
