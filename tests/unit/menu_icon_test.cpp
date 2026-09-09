#define NOMINMAX
#include "app_shell/menu_icon.h"
#include "unit/test_util.h"

int main() {
    for (const UINT dpi : {96u, 144u, 192u}) {
        for (const auto glyph : {L'\uE70F', L'\uE8C8', L'\uE74D',
                                  L'\uE74A', L'\uE74B'}) {
            const COLORREF color = RGB(40, 80, 120);
            HBITMAP bitmap = panedock::app_shell::create_menu_icon(glyph, dpi, color);
            EXPECT(bitmap != nullptr);
            DIBSECTION dib{};
            EXPECT(GetObjectW(bitmap, sizeof(dib), &dib) == sizeof(dib));
            EXPECT(dib.dsBm.bmWidth == MulDiv(16, dpi, 96));
            EXPECT(dib.dsBm.bmHeight == dib.dsBm.bmWidth);
            const auto* pixels = static_cast<const std::uint32_t*>(dib.dsBm.bmBits);
            bool visible = false;
            bool transparent = false;
            for (int i = 0; i < dib.dsBm.bmWidth * dib.dsBm.bmHeight; ++i) {
                const auto alpha = pixels[i] >> 24;
                visible |= alpha != 0;
                transparent |= alpha == 0;
                EXPECT(((pixels[i] >> 16) & 255) == 40 * alpha / 255);
                EXPECT(((pixels[i] >> 8) & 255) == 80 * alpha / 255);
                EXPECT((pixels[i] & 255) == 120 * alpha / 255);
            }
            EXPECT(visible && transparent);
            EXPECT(DeleteObject(bitmap));
        }
    }
    return 0;
}
