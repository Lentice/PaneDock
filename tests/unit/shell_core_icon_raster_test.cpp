#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "shell_core/icon_image.h"
#include "unit/test_util.h"

using namespace panedock::shell_core;

namespace {
int stride(int size) { return ((size + 15) / 16) * 2; }
bool transparent(const std::vector<unsigned char>& mask, int size, int x, int y) {
    return (mask[y * stride(size) + x / 8] & (0x80 >> (x % 8))) != 0;
}
std::uint32_t alpha(std::uint32_t pixel) { return pixel >> 24; }

// Four panes opening This PC ask four icon workers for the system drive's icon
// at once. On a cold process every racing SHGetFileInfo but one used to fail,
// leaving the drive card on its placeholder. Runs first, while still cold.
void concurrent_first_drive_icon() {
    wchar_t windows[MAX_PATH]{};
    if (GetWindowsDirectoryW(windows, MAX_PATH) < 3) return;
    const std::wstring root(windows, 3); // "C:\"
    for (const int size : {16, 72}) {
        std::array<bool, 4> loaded{};
        std::vector<std::thread> workers;
        for (std::size_t t = 0; t < loaded.size(); ++t)
            workers.emplace_back([&, t] {
                if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return;
                loaded[t] = load_item_icon(root, size).pixels.size() ==
                    static_cast<std::size_t>(size * size);
                CoUninitialize();
            });
        for (auto& worker : workers) worker.join();
        for (const bool ok : loaded) EXPECT(ok);
    }
}
}  // namespace

int main() {
    concurrent_first_drive_icon();
    // Shrinking area-averages premultiplied channels.
    const std::vector<std::uint32_t> quad{0xff000000, 0xff0000ff, 0x00000000, 0xff0000ff};
    const auto one = resample_square(quad, 2, 1);
    EXPECT(one.size() == 1);
    EXPECT(alpha(one[0]) == 191);          // (255 + 255 + 0 + 255) / 4
    EXPECT((one[0] & 0xff) == 128);         // (0 + 255 + 0 + 255) / 4

    // A uniform opaque image stays exactly uniform at every size (no edge loss).
    const std::vector<std::uint32_t> solid(256 * 256, 0xff336699);
    for (const int size : {96, 48, 40, 33}) {
        const auto scaled = resample_square(solid, 256, size);
        EXPECT(scaled.size() == static_cast<std::size_t>(size * size));
        EXPECT(std::all_of(scaled.begin(), scaled.end(),
                           [](std::uint32_t pixel) { return pixel == 0xff336699; }));
    }
    const auto enlarged = resample_square(std::vector<std::uint32_t>(48 * 48, 0xff336699), 48, 512);
    EXPECT(enlarged.size() == 512u * 512u && enlarged.front() == 0xff336699 &&
           enlarged.back() == 0xff336699);

    // A mismatched source is rejected rather than read out of bounds.
    EXPECT(resample_square(solid, 255, 32).empty());

    // A transparent left half stays transparent; the mask follows alpha.
    std::vector<std::uint32_t> half(64 * 64, 0);
    for (int y = 0; y < 64; ++y)
        for (int x = 32; x < 64; ++x) half[y * 64 + x] = 0xff00ff00;
    const auto small = resample_square(half, 64, 32);
    const auto mask = alpha_mask(small, 32);
    EXPECT(mask.size() == static_cast<std::size_t>(stride(32) * 32));
    EXPECT(transparent(mask, 32, 0, 0) && transparent(mask, 32, 15, 31));
    EXPECT(!transparent(mask, 32, 16, 0) && !transparent(mask, 32, 31, 31));
    EXPECT(alpha_mask(small, 31).empty());

    // Live Shell: a type icon above 32 px comes back at the requested size with
    // visible content (the image-list path, not a 32 px stretch).
    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        for (const int size : {48, 96, 256}) {
            const auto image = load_extension_icon(L".txt", false, size);
            EXPECT(image.pixels.size() == static_cast<std::size_t>(size * size));
            EXPECT(image.mask.size() == static_cast<std::size_t>(stride(size) * size));
            EXPECT(std::any_of(image.pixels.begin(), image.pixels.end(),
                               [](std::uint32_t pixel) { return alpha(pixel) != 0; }));
        }
        const auto folder = load_extension_icon(L"", true, 96);
        EXPECT(folder.pixels.size() == 96u * 96u);
        CoUninitialize();
    }
    return panedock::test::summary("shell_core_icon_raster");
}
