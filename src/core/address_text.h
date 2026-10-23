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

// A filesystem path without trailing backslashes, except the one a drive
// root needs: "C:\a\\" -> "C:\a", "C:\\" -> "C:\".
inline std::wstring_view trim_trailing_backslashes(std::wstring_view path) noexcept {
    while (path.size() > 3 && path.back() == L'\\') path.remove_suffix(1);
    return path;
}

// The containing directory, syntactically: "C:\a\b" -> "C:\a" -> "C:\" -> "";
// "\\srv\share\a" -> "\\srv\share" -> "". A share root has no filesystem
// parent; its server is a Shell namespace folder (unc_server_of_share_root).
inline std::wstring filesystem_parent(std::wstring_view path) {
    path = trim_trailing_backslashes(path);
    if (path.size() <= 3 && path.size() >= 2 && path[1] == L':') return {};
    const auto slash = path.rfind(L'\\');
    if (slash == std::wstring_view::npos) return {};
    if (path.starts_with(L"\\\\")) {
        const auto server = path.find(L'\\', 2);
        if (server == std::wstring_view::npos ||
            path.find(L'\\', server + 1) == std::wstring_view::npos)
            return {};
        return std::wstring(path.substr(0, slash));
    }
    return std::wstring(slash == 2 && path[1] == L':' ? path.substr(0, 3)
                                                      : path.substr(0, slash));
}

// The last component: "C:\a\b\" -> "b"; a drive root has none.
inline std::wstring_view filesystem_leaf(std::wstring_view path) noexcept {
    path = trim_trailing_backslashes(path);
    const auto slash = path.rfind(L'\\');
    return slash == std::wstring_view::npos ? path : path.substr(slash + 1);
}

}  // namespace panedock::core
