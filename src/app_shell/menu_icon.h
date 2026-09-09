#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdint>

namespace panedock::app_shell {

// The caller owns the bitmap and must retain it until DestroyMenu.
inline HBITMAP create_menu_icon(wchar_t glyph, UINT dpi, COLORREF color) {
    const int size = std::max(1, MulDiv(16, static_cast<int>(dpi), 96));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS,
                                     &bits, nullptr, 0);
    if (bitmap == nullptr) return nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontW(-size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                            DEFAULT_PITCH, L"Segoe MDL2 Assets");
    if (dc == nullptr || font == nullptr) {
        if (dc != nullptr) DeleteDC(dc);
        if (font != nullptr) DeleteObject(font);
        DeleteObject(bitmap);
        return nullptr;
    }
    auto* pixels = static_cast<std::uint32_t*>(bits);
    std::fill_n(pixels, size * size, 0);
    const auto old_bitmap = SelectObject(dc, bitmap);
    const auto old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    RECT rect{0, 0, size, size};
    DrawTextW(dc, &glyph, 1, &rect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    GdiFlush();
    // GDI text supplies coverage, not alpha. Native menus need premultiplied BGRA.
    for (int i = 0; i < size * size; ++i) {
        const auto alpha = pixels[i] & 0xff;
        pixels[i] = (alpha << 24) |
                    ((GetRValue(color) * alpha / 255) << 16) |
                    ((GetGValue(color) * alpha / 255) << 8) |
                    (GetBValue(color) * alpha / 255);
    }
    SelectObject(dc, old_font);
    SelectObject(dc, old_bitmap);
    DeleteObject(font);
    DeleteDC(dc);
    return bitmap;
}

}  // namespace panedock::app_shell
