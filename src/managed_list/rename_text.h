#pragma once

#include <cstddef>
#include <cwctype>
#include <string>
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

// The extension Explorer hides for a local file whose display name is its
// leaf without the extension ("report" for "report.txt"); empty when the
// extension is shown, for folders and for namespace items.
inline std::wstring_view hidden_extension(std::wstring_view display, std::wstring_view leaf,
                                          bool not_a_local_file) noexcept {
    if (not_a_local_file || display.empty() || leaf.size() <= display.size() + 1 ||
        leaf.substr(0, display.size()) != display || leaf[display.size()] != L'.' ||
        leaf.find(L'.', display.size() + 1) != std::wstring_view::npos) return {};
    return leaf.substr(display.size());
}

// Commits keep a hidden extension, as Explorer does, unless the user typed it.
inline std::wstring restore_hidden_extension(std::wstring name, std::wstring_view extension) {
    if (extension.empty() || name.empty()) return name;
    if (name.size() >= extension.size()) {
        const auto tail = std::wstring_view(name).substr(name.size() - extension.size());
        bool same = true;
        for (std::size_t i = 0; i < tail.size() && same; ++i)
            same = std::towlower(tail[i]) == std::towlower(extension[i]);
        if (same) return name;
    }
    name.append(extension);
    return name;
}

} // namespace panedock::managed_list
