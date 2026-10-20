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
// Local parent+leaf compatibility entries; never pass display text as name.
IconImage load_item_icon(std::wstring_view folder, std::wstring_view name, int size);
IconImage load_item_thumbnail(std::wstring_view folder, std::wstring_view name, int size);
} // namespace panedock::shell_core
