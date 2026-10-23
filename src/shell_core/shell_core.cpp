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
#include <commoncontrols.h>
#include <cmath>
#include <utility>
#include <wrl/client.h>

namespace panedock::shell_core {
namespace {

constexpr DWORD kDisplayNameLookupTimeoutMs = 1000;

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
namespace {
// Draws the icon into an owned 32-bit colour plus 1-bit mask raster.
IconImage rasterize_icon(HICON icon_handle, int size) {
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
    if (!DrawIconEx(dc.value, 0, 0, icon_handle, size, size, 0, nullptr, DI_NORMAL)) return {};
    GdiFlush();
    IconImage result;
    result.pixels.assign(pixels, pixels + count);
    std::fill_n(pixels, count, 0);
    if (!DrawIconEx(dc.value, 0, 0, icon_handle, size, size, 0, nullptr, DI_MASK)) return {};
    GdiFlush();
    const int stride = ((size + 15) / 16) * 2; // CreateBitmap's WORD aligned scanlines
    result.mask.assign(static_cast<std::size_t>(stride * size), 0);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (pixels[y * size + x] & 0x00ffffff)
                result.mask[y * stride + x / 8] |= static_cast<unsigned char>(0x80 >> (x % 8));
    return result;
}
// SHGFI_LARGEICON is 32 px; above it the system image list's 48 px and 256 px
// images are drawn at their own size and resampled, so Medium/Large/Extra large
// icons are not a stretched 32 px icon.
constexpr int kShellLargeIconSize = 32;
constexpr int kSystemExtraLargeIconSize = 48; // SHIL_EXTRALARGE, not the 256 px view mode
// Concurrent first lookups of one item's icon fail on every thread except the
// one that wins: measured on a cold process with four STA threads asking for
// the C: drive at once (four panes opening This PC), 3 of 4 SHGetFileInfo calls failed,
// with or without ADDOVERLAYS, and each loser succeeded on an immediate second
// call. The Pane keeps no negative cache but does not re-request a failed image
// until its interest changes, so the loser showed the placeholder for good.
// Exactly one retry: bounded, no wait, no loop.
DWORD_PTR file_info(LPCWSTR path, DWORD attributes, SHFILEINFOW& info, UINT flags) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        info = {};
        const auto result = SHGetFileInfoW(path, attributes, &info, sizeof(info), flags);
        if (result && (!(flags & SHGFI_ICON) || info.hIcon)) return result;
        if (info.hIcon) DestroyIcon(info.hIcon);
    }
    info = {};
    return 0;
}
// A legacy GDI icon draws no alpha: give its opaque mask pixels full alpha so
// resampling and the alpha-derived mask treat every icon the same way.
void ensure_alpha(IconImage& image, int size) {
    if (std::any_of(image.pixels.begin(), image.pixels.end(),
                    [](std::uint32_t pixel) { return (pixel & 0xff000000) != 0; })) return;
    const int stride = ((size + 15) / 16) * 2;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            auto& pixel = image.pixels[y * size + x];
            const bool transparent = image.mask[y * stride + x / 8] & (0x80 >> (x % 8));
            pixel = transparent ? 0 : (pixel | 0xff000000);
        }
}
// An icon without a 256 px image sits in the jumbo list as its small image in
// the top-left corner of an otherwise empty 256 px cell.
bool small_image_in_jumbo_cell(const IconImage& image, int size) {
    bool content = false;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            if ((image.pixels[y * size + x] & 0xff000000) == 0) continue;
            if (x >= kSystemExtraLargeIconSize || y >= kSystemExtraLargeIconSize) return false;
            content = true;
        }
    return content;
}
// Draws a system image list entry (with its Shell overlay) at the list's own
// size, then resamples it to the requested size. Worker apartment only.
IconImage system_image(int index, int overlay, int size) {
    const auto draw = [&](int list, int& native) -> IconImage {
        Microsoft::WRL::ComPtr<IImageList> images;
        int width{}, height{};
        if (FAILED(SHGetImageList(list, IID_PPV_ARGS(&images))) ||
            FAILED(images->GetIconSize(&width, &height)) || width < 16 || width != height ||
            width > 512) return {};
        HICON handle{};
        const UINT flags = ILD_TRANSPARENT |
            (overlay > 0 && overlay < 16 ? INDEXTOOVERLAYMASK(overlay) : 0);
        if (FAILED(images->GetIcon(index, flags, &handle)) || !handle) return {};
        struct Icon { HICON value; ~Icon() { DestroyIcon(value); } } icon{handle};
        auto image = rasterize_icon(icon.value, width);
        if (image.pixels.empty()) return {};
        ensure_alpha(image, width);
        native = width;
        return image;
    };
    int native = 0;
    IconImage image;
    if (size > kSystemExtraLargeIconSize) {
        image = draw(SHIL_JUMBO, native);
        if (!image.pixels.empty() && native > kSystemExtraLargeIconSize &&
            small_image_in_jumbo_cell(image, native)) image = {};
    }
    if (image.pixels.empty()) image = draw(SHIL_EXTRALARGE, native);
    if (image.pixels.empty()) return {};
    IconImage result;
    result.pixels = native == size ? std::move(image.pixels)
                                   : resample_square(image.pixels, native, size);
    result.mask = alpha_mask(result.pixels, size);
    return result;
}
} // namespace
std::vector<std::uint32_t> resample_square(const std::vector<std::uint32_t>& source,
                                           int source_size, int size) {
    if (source_size <= 0 || size <= 0 ||
        source.size() != static_cast<std::size_t>(source_size) * source_size) return {};
    std::vector<std::uint32_t> result(static_cast<std::size_t>(size) * size);
    const auto channel = [](std::uint32_t pixel, int shift) {
        return static_cast<double>((pixel >> shift) & 0xff);
    };
    const auto pack = [](const double (&sum)[4]) {
        std::uint32_t pixel = 0;
        for (int c = 0; c < 4; ++c)
            pixel |= static_cast<std::uint32_t>(std::clamp(std::lround(sum[c]), 0L, 255L)) << (c * 8);
        return pixel;
    };
    const double scale = static_cast<double>(source_size) / size;
    if (size <= source_size) {
        // Area average over premultiplied channels: each destination pixel is
        // the coverage-weighted mean of the source pixels under it.
        for (int y = 0; y < size; ++y) {
            const double top = y * scale, bottom = (y + 1) * scale;
            for (int x = 0; x < size; ++x) {
                const double left = x * scale, right = (x + 1) * scale;
                double sum[4]{}, total = 0;
                for (int sy = static_cast<int>(top); sy < source_size && sy < bottom; ++sy) {
                    const double wy = std::min<double>(sy + 1, bottom) - std::max<double>(sy, top);
                    for (int sx = static_cast<int>(left); sx < source_size && sx < right; ++sx) {
                        const double weight = wy *
                            (std::min<double>(sx + 1, right) - std::max<double>(sx, left));
                        if (weight <= 0) continue;
                        const auto pixel = source[static_cast<std::size_t>(sy) * source_size + sx];
                        for (int c = 0; c < 4; ++c) sum[c] += weight * channel(pixel, c * 8);
                        total += weight;
                    }
                }
                if (total > 0) for (auto& value : sum) value /= total;
                result[static_cast<std::size_t>(y) * size + x] = pack(sum);
            }
        }
        return result;
    }
    // Bilinear when enlarging past the largest system image (e.g. 512 px).
    const auto at = [&](int sx, int sy) {
        return source[static_cast<std::size_t>(sy) * source_size + sx];
    };
    for (int y = 0; y < size; ++y) {
        const double fy = std::clamp((y + 0.5) * scale - 0.5, 0.0, source_size - 1.0);
        const int y0 = static_cast<int>(fy), y1 = std::min(y0 + 1, source_size - 1);
        const double ty = fy - y0;
        for (int x = 0; x < size; ++x) {
            const double fx = std::clamp((x + 0.5) * scale - 0.5, 0.0, source_size - 1.0);
            const int x0 = static_cast<int>(fx), x1 = std::min(x0 + 1, source_size - 1);
            const double tx = fx - x0;
            double sum[4]{};
            for (int c = 0; c < 4; ++c)
                sum[c] = (1 - ty) * ((1 - tx) * channel(at(x0, y0), c * 8) + tx * channel(at(x1, y0), c * 8)) +
                    ty * ((1 - tx) * channel(at(x0, y1), c * 8) + tx * channel(at(x1, y1), c * 8));
            result[static_cast<std::size_t>(y) * size + x] = pack(sum);
        }
    }
    return result;
}
std::vector<unsigned char> alpha_mask(const std::vector<std::uint32_t>& pixels, int size) {
    if (size <= 0 || pixels.size() != static_cast<std::size_t>(size) * size) return {};
    const int stride = ((size + 15) / 16) * 2; // CreateBitmap's WORD aligned scanlines
    std::vector<unsigned char> mask(static_cast<std::size_t>(stride) * size, 0xff);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (pixels[static_cast<std::size_t>(y) * size + x] & 0xff000000)
                mask[y * stride + x / 8] &= static_cast<unsigned char>(~(0x80 >> (x % 8)));
    return mask;
}
IconImage load_item_icon(std::wstring_view parsing_name, int size) {
    if (size < 16 || size > 512 || parsing_name.empty()) return {};
    const std::wstring path(parsing_name);
    struct Pidl { PIDLIST_ABSOLUTE value{}; ~Pidl() { CoTaskMemFree(value); } } pidl;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &pidl.value, 0, nullptr))) return {};
    if (size > kShellLargeIconSize) {
        // OVERLAYINDEX requires ICON; the returned handle is unused and freed.
        SHFILEINFOW large{};
        const auto found = file_info(reinterpret_cast<LPCWSTR>(pidl.value), 0, large,
            SHGFI_PIDL | SHGFI_ICON | SHGFI_SYSICONINDEX | SHGFI_OVERLAYINDEX);
        if (large.hIcon) DestroyIcon(large.hIcon);
        if (found) {
            const auto packed = static_cast<unsigned>(large.iIcon);
            auto image = system_image(static_cast<int>(packed & 0x00ffffffu),
                                      static_cast<int>(packed >> 24), size);
            if (!image.pixels.empty()) return image;
        }
    }
    SHFILEINFOW info{};
    // ADDOVERLAYS composes native overlays into the owned icon. No extension
    // filtering, system-image-list mutation or context-menu policy is involved.
    const auto ok = file_info(reinterpret_cast<LPCWSTR>(pidl.value), 0, info,
        SHGFI_PIDL | SHGFI_ICON | SHGFI_ADDOVERLAYS |
            (size <= 16 ? SHGFI_SMALLICON : SHGFI_LARGEICON));
    struct Icon { HICON value; ~Icon() { if (value) DestroyIcon(value); } } icon{info.hIcon};
    if (!ok || !icon.value) return {};
    return rasterize_icon(icon.value, size);
}
IconImage load_extension_icon(std::wstring_view extension, bool directory, int size) {
    if (size < 16 || size > 512) return {};
    std::wstring name = L"file";
    name.append(extension);
    if (size > kShellLargeIconSize) {
        SHFILEINFOW large{};
        if (file_info(name.c_str(), directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
                      large, SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES)) {
            auto image = system_image(large.iIcon, 0, size);
            if (!image.pixels.empty()) return image;
        }
    }
    SHFILEINFOW info{};
    const auto ok = file_info(name.c_str(),
        directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, info,
        SHGFI_ICON | SHGFI_USEFILEATTRIBUTES | (size <= 16 ? SHGFI_SMALLICON : SHGFI_LARGEICON));
    struct Icon { HICON value; ~Icon() { if (value) DestroyIcon(value); } } icon{info.hIcon};
    if (!ok || !icon.value) return {};
    return rasterize_icon(icon.value, size);
}
} // namespace panedock::shell_core

namespace panedock::shell_core {
IconImage load_item_thumbnail(std::wstring_view parsing_name, int size) {
    if (size < 16 || size > 512 || parsing_name.empty()) return {};
    const std::wstring path(parsing_name);
    Microsoft::WRL::ComPtr<IShellItemImageFactory> factory;
    struct Bitmap { HBITMAP value{}; ~Bitmap() { if (value) DeleteObject(value); } } bitmap;
    // This entire function runs in the image worker's apartment. Thumbnail
    // failure takes the existing native icon path once, with no retry loop.
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->GetImage({size, size}, SIIGBF_THUMBNAILONLY, &bitmap.value)))
        return load_item_icon(parsing_name, size);
    BITMAP description{};
    if (!GetObjectW(bitmap.value, sizeof(description), &description) ||
        description.bmWidth <= 0 || description.bmHeight <= 0 ||
        description.bmWidth > size || description.bmHeight > size) return load_item_icon(parsing_name, size);
    struct Dc { HDC value{CreateCompatibleDC(nullptr)}; ~Dc() { if (value) DeleteDC(value); } } dc;
    if (!dc.value) return {};
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), description.bmWidth, -description.bmHeight,
        1, 32, BI_RGB, 0, 0, 0, 0, 0};
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(description.bmWidth * description.bmHeight));
    if (GetDIBits(dc.value, bitmap.value, 0, description.bmHeight, pixels.data(), &info, DIB_RGB_COLORS)
        != static_cast<int>(description.bmHeight)) return load_item_icon(parsing_name, size);
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
