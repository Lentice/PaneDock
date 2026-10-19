#include "app_shell/pane.h"
#include <algorithm>

namespace panedock::app_shell {
namespace {
constexpr UINT kViewportMessage = WM_APP + 229;
HICON raster_icon(const shell_core::IconImage& image, int size) noexcept {
    const int stride = ((size + 15) / 16) * 2;
    if (image.pixels.size() != static_cast<std::size_t>(size * size) ||
        image.mask.size() != static_cast<std::size_t>(stride * size)) return nullptr;
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void* bits{};
    const HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    const HBITMAP mask = CreateBitmap(size, size, 1, 1, image.mask.data());
    HICON icon{};
    if (color && mask && bits) {
        std::copy(image.pixels.begin(), image.pixels.end(), static_cast<std::uint32_t*>(bits));
        ICONINFO definition{TRUE, 0, 0, mask, color};
        icon = CreateIconIndirect(&definition);
    }
    if (mask) DeleteObject(mask);
    if (color) DeleteObject(color);
    return icon;
}
} // namespace
void Pane::use_list_icons(std::unique_ptr<managed_list::IconSource> source) {
    cancel_list_icons();
    icon_source_ = std::move(source);
    icon_seam_ = true;
}
managed_list::IconKey Pane::list_icon_key(std::size_t row) const {
    const auto& item = list_.rows[row];
    return {list_.request.location, item.name, item.bytes, item.date_modified,
        item.directory, icon_size_};
}
void Pane::cancel_list_icons() noexcept {
    ++icon_viewport_;
    icon_interest_.clear();
    if (icon_source_) icon_source_->cancel();
}
void Pane::destroy_list_icons() noexcept {
    cancel_list_icons();
    icon_source_.reset();
    if (list_window_) ListView_SetImageList(list_window_, nullptr, LVSIL_SMALL);
    if (icon_images_) ImageList_Destroy(icon_images_);
    icon_images_ = nullptr;
    icon_cache_.clear();
}
bool Pane::ensure_list_icon_images(int size) noexcept try {
    if (icon_images_ && size == icon_size_) return true;
    cancel_list_icons();
    if (list_window_) ListView_SetImageList(list_window_, nullptr, LVSIL_SMALL);
    if (icon_images_) ImageList_Destroy(icon_images_);
    icon_images_ = nullptr;
    icon_cache_.clear();
    icon_size_ = size;
    icon_images_ = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK,
        static_cast<int>(managed_list::kIconCache + 2), 1);
    if (!icon_images_) return false;
    // Cheap local placeholders: no Shell/property call on paint or selection.
    for (int folder = 0; folder < 2; ++folder) {
        shell_core::IconImage image;
        image.pixels.resize(static_cast<std::size_t>(size * size));
        image.mask.assign(static_cast<std::size_t>(((size + 15) / 16) * 2 * size), 0xff);
        const int stride = ((size + 15) / 16) * 2;
        for (int y = size / 4; y < size * 3 / 4; ++y)
            for (int x = size / 4; x < size * 3 / 4; ++x) {
                image.pixels[y * size + x] = folder ? 0xffffcf60 : 0xffb8c7d8;
                image.mask[y * stride + x / 8] &= static_cast<unsigned char>(~(0x80 >> (x % 8)));
            }
        const HICON icon = raster_icon(image, size);
        const int index = icon ? ImageList_AddIcon(icon_images_, icon) : -1;
        if (icon) DestroyIcon(icon);
        if (index != folder) {
            ImageList_Destroy(icon_images_); icon_images_ = nullptr; return false;
        }
    }
    if (list_window_) ListView_SetImageList(list_window_, icon_images_, LVSIL_SMALL);
    return true;
} catch (...) {
    if (icon_images_) ImageList_Destroy(icon_images_);
    icon_images_ = nullptr;
    return false;
}
int Pane::list_item_image(std::size_t row) const noexcept {
    if (row >= list_.rows.size()) return 0;
    const auto& item = list_.rows[row];
    const auto found = std::find_if(icon_cache_.begin(), icon_cache_.end(),
        [&](const auto& entry) {
            const auto& key = entry.key;
            return key.location == list_.request.location && key.name == item.name &&
                key.bytes == item.bytes && key.modified == item.date_modified &&
                key.directory == item.directory && key.size == icon_size_;
        });
    return found == icon_cache_.end() ? (item.directory ? 1 : 0) : found->slot;
}
void Pane::set_list_icon_viewport(std::size_t first, std::size_t count) noexcept try {
    if (!managed_list_active_ || !active() ||
        !content_request_is_current(list_.request.binding)) { cancel_list_icons(); return; }
    const int size = std::clamp(MulDiv(16, static_cast<int>(paint_dpi_), 96), 16, 64);
    if (!ensure_list_icon_images(size)) return;
    std::vector<managed_list::IconKey> interest;
    first = std::min(first, list_.rows.size());
    const auto end = first + std::min({count, managed_list::kIconInterest, list_.rows.size() - first});
    for (auto i = first; i < end; ++i) interest.push_back(list_icon_key(i));
    if (interest == icon_interest_) return;
    ++icon_viewport_;
    icon_interest_ = std::move(interest);
    std::vector<managed_list::IconRequest> requests;
    for (const auto& key : icon_interest_) {
        const auto found = std::find_if(icon_cache_.begin(), icon_cache_.end(),
            [&](const auto& entry) { return entry.key == key; });
        if (found != icon_cache_.end()) { found->used = ++icon_clock_; continue; }
        if (std::any_of(requests.begin(), requests.end(), [&](const auto& r) { return r.key == key; })) continue;
        requests.push_back({list_.request, icon_viewport_, key});
    }
    if (requests.empty()) { if (icon_source_) icon_source_->cancel(); return; }
    if (!icon_source_ && !icon_seam_) icon_source_ = managed_list::make_icon_source();
    if (icon_source_ && !icon_source_->replace(std::move(requests))) icon_source_->cancel();
} catch (...) { cancel_list_icons(); }
HANDLE Pane::icon_event() const noexcept { return icon_source_ ? icon_source_->event() : nullptr; }
void Pane::process_icon_results() noexcept try {
    if (!icon_source_) return;
    for (std::size_t n = 0; n < managed_list::kIconResults; ++n) {
        auto result = icon_source_->take();
        if (!result) break;
        const auto& request = result->request;
        if (!active() || !managed_list_active_ ||
            !content_request_is_current(request.request.binding) ||
            !(request.request == list_.request) || request.viewport != icon_viewport_ ||
            std::find(icon_interest_.begin(), icon_interest_.end(), request.key) == icon_interest_.end() ||
            request.key.size != icon_size_ || !icon_images_) continue;
        struct OwnedIcon { HICON value; ~OwnedIcon() { if (value) DestroyIcon(value); } };
        const OwnedIcon icon{raster_icon(result->image, icon_size_)};
        if (!icon.value) continue; // retry on renewed interest/Refresh, never an idle loop
        auto found = std::find_if(icon_cache_.begin(), icon_cache_.end(),
            [&](const auto& entry) { return entry.key == request.key; });
        if (found != icon_cache_.end()) continue;
        int slot = -1;
        if (icon_cache_.size() == managed_list::kIconCache) {
            found = std::min_element(icon_cache_.begin(), icon_cache_.end(),
                [](const auto& a, const auto& b) { return a.used < b.used; });
            auto key = request.key; // allocate before mutating the occupied slot
            slot = ImageList_ReplaceIcon(icon_images_, found->slot, icon.value);
            if (slot >= 0) *found = {std::move(key), slot, ++icon_clock_};
        } else {
            // Allocate the key before adding an image, so allocation failure
            // cannot orphan an untracked slot and grow the image list.
            icon_cache_.push_back({request.key, -1, ++icon_clock_});
            slot = ImageList_AddIcon(icon_images_, icon.value);
            if (slot < 0) icon_cache_.pop_back(); else icon_cache_.back().slot = slot;
        }
        if (slot >= 0 && list_window_) InvalidateRect(list_window_, nullptr, FALSE);
    }
} catch (...) { cancel_list_icons(); }
void Pane::queue_list_icon_viewport() noexcept {
    if (list_window_ && !icon_viewport_posted_)
        icon_viewport_posted_ = PostMessageW(list_window_, kViewportMessage, 0, 0) != FALSE;
}
void Pane::refresh_list_icon_viewport() noexcept {
    if (!list_window_ || !IsWindowVisible(list_window_)) { cancel_list_icons(); return; }
    set_list_icon_viewport(static_cast<std::size_t>(std::max(0, ListView_GetTopIndex(list_window_))),
        static_cast<std::size_t>(std::max(0, ListView_GetCountPerPage(list_window_))) + 1);
}
LRESULT CALLBACK Pane::list_icon_subclass(HWND window, UINT message, WPARAM wp, LPARAM lp,
                                         UINT_PTR id, DWORD_PTR data) {
    auto* pane = reinterpret_cast<Pane*>(data);
    if (message == kViewportMessage) {
        pane->icon_viewport_posted_ = false;
        pane->refresh_list_icon_viewport(); return 0;
    }
    if (message == WM_NCDESTROY) {
        pane->icon_viewport_posted_ = false;
        RemoveWindowSubclass(window, list_icon_subclass, id);
        return DefSubclassProc(window, message, wp, lp);
    }
    const auto result = DefSubclassProc(window, message, wp, lp);
    if (pane->list_window_ != window || !IsWindow(window)) return result;
    switch (message) {
    case WM_VSCROLL: case WM_HSCROLL: case WM_MOUSEWHEEL: case WM_KEYDOWN:
    case WM_SIZE: case WM_SHOWWINDOW: case LVM_SCROLL: case LVM_ENSUREVISIBLE:
        pane->queue_list_icon_viewport(); break;
    default: break;
    }
    return result;
}
} // namespace panedock::app_shell
