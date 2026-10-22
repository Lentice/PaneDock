#pragma once

#include <cstdint>
#include <string>

namespace panedock::managed_list {

// Explorer's Details "Size": whole KB rounded up, thousands separated, e.g.
// "19,254,688 KB". Even an empty file shows "0 KB" and one byte shows "1 KB".
inline std::wstring file_size_text(std::uint64_t bytes) {
    const std::wstring digits = std::to_wstring(bytes / 1024 + (bytes % 1024 != 0 ? 1 : 0));
    std::wstring grouped;
    for (std::size_t index = 0; index < digits.size(); ++index) {
        if (index != 0 && (digits.size() - index) % 3 == 0) grouped += L',';
        grouped += digits[index];
    }
    return grouped + L" KB";
}

} // namespace panedock::managed_list
