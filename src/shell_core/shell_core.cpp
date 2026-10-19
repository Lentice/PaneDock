#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>

#include "shell_core/shell_core.h"

#include <algorithm>
#include <charconv>
#include "shell_core/icon_image.h"
#include <shellapi.h>

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

std::string_view sort_property_name(SortProperty property) noexcept {
    switch (property) {
        case SortProperty::name: return "System.ItemNameDisplay";
        case SortProperty::size: return "System.Size";
        case SortProperty::date_modified: return "System.DateModified";
    }
    return {};
}

std::optional<SortProperty> parse_sort_property(std::string_view name) noexcept {
    if (name == "{B725F130-47EF-101A-A5F1-02608C9EEBAC} 10") return SortProperty::name;
    if (name == "{B725F130-47EF-101A-A5F1-02608C9EEBAC} 12") return SortProperty::size;
    if (name == "{B725F130-47EF-101A-A5F1-02608C9EEBAC} 14") return SortProperty::date_modified;
    if (name == "System.ItemNameDisplay") return SortProperty::name;
    if (name == "System.Size") return SortProperty::size;
    if (name == "System.DateModified") return SortProperty::date_modified;
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
    // Honor a process-local LOCALAPPDATA override so isolated launches (such
    // as the smoke test) never read or write the interactive user's session.
    try {
        const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        if (required > 1) {
            std::wstring local_app_data(required, L'\0');
            const DWORD length = GetEnvironmentVariableW(
                L"LOCALAPPDATA", local_app_data.data(), required);
            if (length > 0 && length < required) {
                const std::filesystem::path root(local_app_data.c_str());
                if (root.is_absolute()) return root / L"PaneDock";
            }
        }
    } catch (...) {
        return std::nullopt;
    }
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

namespace panedock::shell_core {
IconImage load_item_icon(std::wstring_view folder, std::wstring_view name, int size) {
    if (size < 16 || size > 512) return {};
    std::wstring path(folder);
    if (!path.empty() && path.back() != L'\\') path += L'\\';
    path += name;
    struct Pidl { PIDLIST_ABSOLUTE value{}; ~Pidl() { CoTaskMemFree(value); } } pidl;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl.value, 0, nullptr))) return {};
    SHFILEINFOW info{};
    // ADDOVERLAYS composes native overlays into the owned icon. No extension
    // filtering, system-image-list mutation or context-menu policy is involved.
    const auto ok = SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl.value), 0, &info,
        sizeof(info), SHGFI_PIDL | SHGFI_ICON | SHGFI_ADDOVERLAYS |
            (size <= 16 ? SHGFI_SMALLICON : SHGFI_LARGEICON));
    struct Icon { HICON value; ~Icon() { if (value) DestroyIcon(value); } } icon{info.hIcon};
    if (!ok || !icon.value) return {};
    struct Dc { HDC value{CreateCompatibleDC(nullptr)}; ~Dc() { if (value) DeleteDC(value); } } dc;
    BITMAPINFO bitmap{};
    bitmap.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void* bits{};
    struct Bitmap { HBITMAP value; ~Bitmap() { if (value) DeleteObject(value); } }
        dib{CreateDIBSection(nullptr, &bitmap, DIB_RGB_COLORS, &bits, nullptr, 0)};
    if (!dc.value || !dib.value || !bits) return {};
    const auto previous = SelectObject(dc.value, dib.value);
    struct Selection { HDC dc; HGDIOBJ old; ~Selection() { SelectObject(dc, old); } } selected{dc.value, previous};
    const auto count = static_cast<std::size_t>(size * size);
    auto* pixels = static_cast<std::uint32_t*>(bits);
    std::fill_n(pixels, count, 0);
    if (!DrawIconEx(dc.value, 0, 0, icon.value, size, size, 0, nullptr, DI_NORMAL)) return {};
    GdiFlush();
    IconImage result;
    result.pixels.assign(pixels, pixels + count);
    std::fill_n(pixels, count, 0);
    if (!DrawIconEx(dc.value, 0, 0, icon.value, size, size, 0, nullptr, DI_MASK)) return {};
    GdiFlush();
    const int stride = ((size + 15) / 16) * 2; // CreateBitmap's WORD aligned scanlines
    result.mask.assign(static_cast<std::size_t>(stride * size), 0);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (pixels[y * size + x] & 0x00ffffff)
                result.mask[y * stride + x / 8] |= static_cast<unsigned char>(0x80 >> (x % 8));
    return result;
}
} // namespace panedock::shell_core

namespace panedock::shell_core {
IconImage load_item_thumbnail(std::wstring_view folder, std::wstring_view name, int size) {
    if (size < 16 || size > 512) return {};
    std::wstring path(folder);
    if (!path.empty() && path.back() != L'\\') path += L'\\';
    path += name;
    Microsoft::WRL::ComPtr<IShellItemImageFactory> factory;
    struct Bitmap { HBITMAP value{}; ~Bitmap() { if (value) DeleteObject(value); } } bitmap;
    // This entire function runs in the image worker's apartment. Thumbnail
    // failure takes the existing native icon path once, with no retry loop.
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->GetImage({size, size}, SIIGBF_THUMBNAILONLY, &bitmap.value)))
        return load_item_icon(folder, name, size);
    BITMAP description{};
    if (!GetObjectW(bitmap.value, sizeof(description), &description) ||
        description.bmWidth <= 0 || description.bmHeight <= 0 ||
        description.bmWidth > size || description.bmHeight > size) return load_item_icon(folder, name, size);
    struct Dc { HDC value{CreateCompatibleDC(nullptr)}; ~Dc() { if (value) DeleteDC(value); } } dc;
    if (!dc.value) return {};
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), description.bmWidth, -description.bmHeight,
        1, 32, BI_RGB, 0, 0, 0, 0, 0};
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(description.bmWidth * description.bmHeight));
    if (GetDIBits(dc.value, bitmap.value, 0, description.bmHeight, pixels.data(), &info, DIB_RGB_COLORS)
        != static_cast<int>(description.bmHeight)) return load_item_icon(folder, name, size);
    IconImage result;
    result.pixels.assign(static_cast<std::size_t>(size * size), 0);
    const int stride = ((size + 15) / 16) * 2;
    result.mask.assign(static_cast<std::size_t>(stride * size), 0xff);
    const int left = (size - description.bmWidth) / 2;
    const int top = (size - description.bmHeight) / 2;
    const bool has_alpha = std::any_of(pixels.begin(), pixels.end(),
        [](auto pixel) { return (pixel & 0xff000000) != 0; });
    for (int y = 0; y < description.bmHeight; ++y)
        for (int x = 0; x < description.bmWidth; ++x) {
            auto pixel = pixels[y * description.bmWidth + x];
            if (!has_alpha) pixel |= 0xff000000;
            result.pixels[(y + top) * size + x + left] = pixel;
            if ((pixel & 0xff000000) == 0) continue;
            result.mask[(y + top) * stride + (x + left) / 8] &=
                static_cast<unsigned char>(~(0x80 >> ((x + left) % 8)));
        }
    return result;
}
} // namespace panedock::shell_core
