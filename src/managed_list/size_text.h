#pragma once

#include <cstdint>
#include <iterator>
#include <optional>
#include <string>

namespace panedock::managed_list {

// "1234567" -> "1,234,567". The app UI is English, so the separator is fixed.
inline std::wstring grouped_number_text(std::uint64_t value) {
    const std::wstring digits = std::to_wstring(value);
    std::wstring grouped;
    for (std::size_t index = 0; index < digits.size(); ++index) {
        if (index != 0 && (digits.size() - index) % 3 == 0) grouped += L',';
        grouped += digits[index];
    }
    return grouped;
}

// Explorer's Details "Size": whole KB rounded up, thousands separated, e.g.
// "19,254,688 KB". Even an empty file shows "0 KB" and one byte shows "1 KB".
inline std::wstring file_size_text(std::uint64_t bytes) {
    return grouped_number_text(bytes / 1024 + (bytes % 1024 != 0 ? 1 : 0)) + L" KB";
}

// Explorer's status bar / StrFormatByteSize: "N bytes" under 1 KB, otherwise three
// significant digits in the largest binary unit with the extra digits truncated, e.g.
// "1.50 KB", "10.5 KB", "123 KB", "0.97 MB" (1000 KB moves to the next unit). Kept
// pure instead of calling shlwapi, whose unit words follow the OS display language.
inline std::wstring byte_size_text(std::uint64_t bytes) {
    if (bytes < 1024) return std::to_wstring(bytes) + L" bytes";
    static constexpr const wchar_t* kUnits[]{L" KB", L" MB", L" GB", L" TB", L" PB", L" EB"};
    unsigned __int128 unit = 1024;
    for (std::size_t index = 0;; ++index, unit *= 1024) {
        const bool last = index + 1 == std::size(kUnits);
        if (!last && bytes >= unit * 1000) continue;
        const int decimals = bytes < unit * 10 ? 2 : bytes < unit * 100 ? 1 : 0;
        const unsigned scale = decimals == 2 ? 100 : decimals == 1 ? 10 : 1;
        const auto scaled = static_cast<std::uint64_t>(bytes * static_cast<unsigned __int128>(scale) / unit);
        std::wstring text = std::to_wstring(scaled / scale);
        if (decimals > 0) {
            auto fraction = std::to_wstring(scaled % scale);
            if (static_cast<int>(fraction.size()) < decimals) fraction.insert(0, decimals - fraction.size(), L'0');
            text += L'.' + fraction;
        }
        return text + kUnits[index];
    }
}

// Explorer's "Length" / System.Media.Duration display: hh:mm:ss, e.g. "00:03:25".
inline std::wstring duration_text(std::uint64_t hundred_nanoseconds) {
    const std::uint64_t seconds = hundred_nanoseconds / 10000000;
    const auto two = [](std::uint64_t value) {
        return (value < 10 ? L"0" : L"") + std::to_wstring(value);
    };
    return two(seconds / 3600) + L":" + two(seconds / 60 % 60) + L":" + two(seconds % 60);
}

// Explorer status bar: "1 item", "1,234 items".
inline std::wstring item_count_text(std::uint64_t count) {
    return grouped_number_text(count) + (count == 1 ? L" item" : L" items");
}

// Explorer status bar selection part: "3 items selected  1.50 MB". Explorer omits the
// size when the selection contains a folder, so `bytes` is empty then.
inline std::wstring selection_status_text(std::uint64_t selected, std::optional<std::uint64_t> bytes) {
    std::wstring text = item_count_text(selected) + L" selected";
    if (bytes) text += L"  " + byte_size_text(*bytes);
    return text;
}

} // namespace panedock::managed_list
