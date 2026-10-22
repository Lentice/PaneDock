#pragma once

#include <cstddef>
#include <string_view>

namespace panedock::managed_list {

// Length of the part of a name that Explorer preselects when rename starts: the
// whole name for folders, names without an extension and dot-files such as
// ".gitignore", otherwise everything before the last dot ("report.final.txt"
// selects "report.final").
inline std::size_t rename_selection_length(std::wstring_view name, bool directory) noexcept {
    if (directory) return name.size();
    const auto dot = name.rfind(L'.');
    return dot == std::wstring_view::npos || dot == 0 ? name.size() : dot;
}

} // namespace panedock::managed_list
