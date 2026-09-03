#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>

#include "shell_core/shell_core.h"

#include <charconv>

#include <shlobj.h>
#include <utility>
#include <wrl/client.h>

namespace panedock::shell_core {
namespace {

constexpr DWORD kDisplayNameLookupTimeoutMs = 1000;

std::wstring item_name(IShellItem* item, SIGDN kind) {
    PWSTR text = nullptr;
    if (item == nullptr || FAILED(item->GetDisplayName(kind, &text)) ||
        text == nullptr) return {};
    std::wstring result;
    try {
        result.assign(text);
    } catch (...) {
        CoTaskMemFree(text);
        throw;
    }
    CoTaskMemFree(text);
    return result;
}

Microsoft::WRL::ComPtr<IShellItem> parse(std::wstring_view text) {
    Microsoft::WRL::ComPtr<IShellItem> item;
    if (text.empty()) return item;
    const std::wstring value(text);
    (void)SHCreateItemFromParsingName(value.c_str(), nullptr,
                                      IID_PPV_ARGS(&item));
    return item;
}

}  // namespace

std::string view_mode_name(FOLDERVIEWMODE mode, int image_size) {
    switch (mode) {
        case FVM_ICON:
            return image_size > 0 ? "FVM_ICON:" + std::to_string(image_size)
                                  : "FVM_ICON";
        case FVM_SMALLICON:
            return image_size > 0 ? "FVM_ICON:" + std::to_string(image_size)
                                  : "FVM_ICON:16";
        case FVM_LIST: return "FVM_LIST";
        case FVM_DETAILS: return "FVM_DETAILS";
        case FVM_TILE: return "FVM_TILE";
        case FVM_CONTENT: return "FVM_CONTENT";
        default: return {};
    }
}

std::optional<ViewModeSelection> parse_view_mode(std::string_view name) {
    if (name == "FVM_ICON") return ViewModeSelection{FVM_ICON, kLargeIconSize};
    if (name == "FVM_SMALLICON")
        return ViewModeSelection{FVM_ICON, kSmallIconSize};
    if (name == "FVM_LIST") return ViewModeSelection{FVM_LIST, -1};
    if (name == "FVM_DETAILS") return ViewModeSelection{FVM_DETAILS, -1};
    if (name == "FVM_TILE") return ViewModeSelection{FVM_TILE, -1};
    if (name == "FVM_CONTENT") return ViewModeSelection{FVM_CONTENT, -1};

    constexpr std::string_view prefix = "FVM_ICON:";
    if (name.starts_with(prefix)) {
        int image_size{};
        const auto first = name.data() + prefix.size();
        const auto last = name.data() + name.size();
        const auto parsed = std::from_chars(first, last, image_size);
        if (parsed.ec == std::errc{} && parsed.ptr == last && image_size > 0)
            return ViewModeSelection{FVM_ICON, image_size};
    }
    return std::nullopt;
}

core::ShellLocation capture_location(std::wstring parsing_name) {
    core::ShellLocation result{std::move(parsing_name), {}, {}};
    const auto item = parse(result.parsing_name);
    if (item == nullptr) return result;

    const std::wstring canonical =
        item_name(item.Get(), SIGDN_DESKTOPABSOLUTEPARSING);
    if (!canonical.empty()) result.parsing_name = canonical;
    result.fallback_path = item_name(item.Get(), SIGDN_FILESYSPATH);

    PIDLIST_ABSOLUTE pidl = nullptr;
    if (FAILED(SHGetIDListFromObject(item.Get(), &pidl)) || pidl == nullptr)
        return result;
    Microsoft::WRL::ComPtr<IKnownFolderManager> manager;
    Microsoft::WRL::ComPtr<IKnownFolder> folder;
    if (SUCCEEDED(CoCreateInstance(CLSID_KnownFolderManager, nullptr,
                                   CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&manager))) &&
        SUCCEEDED(manager->FindFolderFromIDList(pidl, &folder))) {
        KNOWNFOLDERID id{};
        wchar_t guid[39]{};
        if (SUCCEEDED(folder->GetId(&id)) &&
            StringFromGUID2(id, guid, ARRAYSIZE(guid)) != 0)
            result.known_folder_identity = guid;
    }
    CoTaskMemFree(pidl);
    return result;
}

std::wstring resolve_location(const core::ShellLocation& location) {
    if (!location.known_folder_identity.empty()) {
        GUID id{};
        Microsoft::WRL::ComPtr<IKnownFolderManager> manager;
        Microsoft::WRL::ComPtr<IKnownFolder> folder;
        Microsoft::WRL::ComPtr<IShellItem> item;
        if (SUCCEEDED(CLSIDFromString(
                location.known_folder_identity.c_str(), &id)) &&
            SUCCEEDED(CoCreateInstance(CLSID_KnownFolderManager, nullptr,
                                       CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&manager))) &&
            SUCCEEDED(manager->GetFolder(id, &folder)) &&
            SUCCEEDED(folder->GetShellItem(0, IID_PPV_ARGS(&item)))) {
            const std::wstring target =
                item_name(item.Get(), SIGDN_DESKTOPABSOLUTEPARSING);
            if (!target.empty()) return target;
        }
    }
    // Leave parsing to the navigation call, where one bind deadline can cover
    // both the primary identity and its fallback without probing twice here.
    return location.parsing_name;
}

std::wstring display_text_for_parsing_name(std::wstring_view parsing_name) {
    if (!parsing_name.starts_with(L"::")) return std::wstring(parsing_name);

    const std::wstring parsing_text(parsing_name);
    Microsoft::WRL::ComPtr<IBindCtx> bind_context;
    if (FAILED(CreateBindCtx(0, &bind_context))) return parsing_text;

    BIND_OPTS options{};
    options.cbStruct = sizeof(options);
    options.dwTickCountDeadline =
        GetTickCount() + kDisplayNameLookupTimeoutMs;
    if (FAILED(bind_context->SetBindOptions(&options))) return parsing_text;

    Microsoft::WRL::ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromParsingName(
            parsing_text.c_str(), bind_context.Get(), IID_PPV_ARGS(&item)))) {
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
