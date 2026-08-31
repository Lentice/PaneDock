#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>

#include "shell_core/shell_core.h"

#include <shlobj.h>
#include <utility>
#include <wrl/client.h>

namespace panedock::shell_core {

core::ShellLocation location(std::wstring parsing_name) {
    return {std::move(parsing_name), {}, {}};
}

std::wstring display_text_for_parsing_name(std::wstring_view parsing_name) {
    if (!parsing_name.starts_with(L"::")) return std::wstring(parsing_name);

    const std::wstring parsing_text(parsing_name);
    Microsoft::WRL::ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(
            parsing_text.c_str(), nullptr, IID_PPV_ARGS(&item)))) {
        return parsing_text;
    }

    PWSTR display_name = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &display_name)) ||
        display_name == nullptr) {
        return parsing_text;
    }

    std::wstring result;
    try {
        result.assign(display_name);
    } catch (...) {
        CoTaskMemFree(display_name);
        return parsing_text;
    }
    CoTaskMemFree(display_name);
    return result;
}

std::optional<std::filesystem::path> session_directory() noexcept {
    PWSTR local_app_data = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr,
                                    &local_app_data))) {
        return std::nullopt;
    }

    try {
        const std::filesystem::path directory =
            std::filesystem::path(local_app_data) / L"PaneDock";
        CoTaskMemFree(local_app_data);
        return directory;
    } catch (...) {
        CoTaskMemFree(local_app_data);
        return std::nullopt;
    }
}

}  // namespace panedock::shell_core
