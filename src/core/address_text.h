#pragma once

#include <string>
#include <string_view>

namespace panedock::core {

// What the address bar accepts before resolution, as Explorer does: leading
// and trailing whitespace and one pair of surrounding double quotes (a path
// pasted from "Copy as path") are not part of the location.
inline std::wstring trim_typed_address(std::wstring_view text) {
    const auto blank = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
    };
    const auto trim = [&](std::wstring_view value) {
        while (!value.empty() && blank(value.front())) value.remove_prefix(1);
        while (!value.empty() && blank(value.back())) value.remove_suffix(1);
        return value;
    };
    text = trim(text);
    if (text.size() >= 2 && text.front() == L'"' && text.back() == L'"')
        text = trim(text.substr(1, text.size() - 2));
    return std::wstring(text);
}

// "\\server" (optionally with one trailing backslash): a server, not a share.
// Its shares are a Shell namespace folder, never a filesystem directory.
inline bool is_unc_server_root(std::wstring_view path) noexcept {
    if (path.size() < 3 || path[0] != L'\\' || path[1] != L'\\' ||
        path[2] == L'\\' || path[2] == L'.' || path[2] == L'?')
        return false;
    const auto slash = path.find(L'\\', 2);
    return slash == std::wstring_view::npos || slash + 1 == path.size();
}

// "\\server\share" (optionally with a trailing backslash) -> "\\server";
// anything else -> empty. Up from a share root goes to its server.
inline std::wstring unc_server_of_share_root(std::wstring_view path) {
    while (path.size() > 2 && path.back() == L'\\') path.remove_suffix(1);
    if (path.size() < 3 || path[0] != L'\\' || path[1] != L'\\' ||
        path[2] == L'.' || path[2] == L'?')
        return {};
    const auto slash = path.find(L'\\', 2);
    if (slash == std::wstring_view::npos || slash == 2 || slash + 1 >= path.size() ||
        path.find(L'\\', slash + 1) != std::wstring_view::npos)
        return {};
    return std::wstring(path.substr(0, slash));
}

}  // namespace panedock::core
