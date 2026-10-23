#pragma once
#include <cstdint>
#include <string_view>
#include <vector>
namespace panedock::shell_core {
// Owned raster values, never an HICON, PIDL or COM pointer across apartments.
struct IconImage final {
    std::vector<std::uint32_t> pixels;
    std::vector<unsigned char> mask;
};
// Caller owns an initialized COM apartment. May block: worker only.
IconImage load_item_icon(std::wstring_view parsing_name, int size);
IconImage load_item_thumbnail(std::wstring_view parsing_name, int size);
// The icon registered for a file type, looked up by extension alone (".txt", or "" for
// no extension) without touching the file system. Worker only.
IconImage load_extension_icon(std::wstring_view extension, bool directory, int size);
// Pure raster helpers for owned square premultiplied BGRA images. Resampling
// area-averages when shrinking and is bilinear when enlarging; an empty result
// means the input did not match source_size. alpha_mask returns the 1-bit
// WORD-aligned mask (1 = transparent) for every pixel whose alpha is zero.
std::vector<std::uint32_t> resample_square(const std::vector<std::uint32_t>& source,
                                           int source_size, int size);
std::vector<unsigned char> alpha_mask(const std::vector<std::uint32_t>& pixels, int size);
} // namespace panedock::shell_core
