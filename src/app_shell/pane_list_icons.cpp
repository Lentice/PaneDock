#include "app_shell/pane.h"
#include <algorithm>

namespace panedock::app_shell {
namespace {
constexpr UINT kViewportMessage = WM_APP + 229;
struct ViewportAnchor final {
    int index{-1};
    POINT position{};
    DWORD view{LV_VIEW_DETAILS};
};
ViewportAnchor capture_viewport(HWND window) noexcept {
    // The style also works with comctl32 v5 (hidden test controls), where
    // LVM_GETVIEW returns zero even for a report list. Managed cards use the
    // same report/icon styles rather than native tile mode.
    const auto view = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE) & LVS_TYPEMASK);
    const int index = view == LV_VIEW_DETAILS || view == LV_VIEW_LIST
        ? ListView_GetTopIndex(window) : ListView_GetNextItem(window, -1, LVNI_VISIBLEONLY);
    RECT rect{};
    if (index < 0 || !ListView_GetItemRect(window, index, &rect, LVIR_BOUNDS)) return {};
    return {index, {rect.left, rect.top}, view};
}
bool restore_viewport(HWND window, const ViewportAnchor& anchor) noexcept {
    if (anchor.index < 0) return false;
    // The scope verified this index still holds its saved identity. Restore only
    // the viewport; never reset selection/focus for a live-resize frame.
    ListView_EnsureVisible(window, anchor.index, FALSE);
    RECT rect{};
    if (!ListView_GetItemRect(window, anchor.index, &rect, LVIR_BOUNDS)) return true;
    if (anchor.view == LV_VIEW_DETAILS) {
        // Report scrolling is quantized to rows. Header/font/image changes can
        // change the first row's y coordinate; rounding a raw pixel delta would
        // move the anchor to its neighbour. Preserve the identity first.
        const int height = rect.bottom - rect.top;
        ListView_Scroll(window, 0,
            (anchor.index - ListView_GetTopIndex(window)) * height);
    } else {
        ListView_Scroll(window, anchor.view == LV_VIEW_LIST ? rect.left - anchor.position.x : 0,
                       anchor.view == LV_VIEW_LIST ? 0 : rect.top - anchor.position.y);
    }
    if (!ListView_GetItemRect(window, anchor.index, &rect, LVIR_BOUNDS)) return true;
    return rect.left != anchor.position.x || rect.top != anchor.position.y;
}
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
Pane::ListGeometryScope::ListGeometryScope(Pane& pane) noexcept
    : pane_(pane), window_(pane.list_window_), generation_(pane.list_.request.binding.generation) {
    if (!window_) return;
    const auto anchor = capture_viewport(window_);
    index_ = anchor.index; position_ = anchor.position; view_ = anchor.view;
    if (index_ >= 0 && static_cast<std::size_t>(index_) < pane.list_.rows.size()) {
        try { identity_ = pane.list_.rows[index_].identity; }
        catch (...) { index_ = -1; }
    }
}
Pane::ListGeometryScope::~ListGeometryScope() {
    if (pane_.list_window_ != window_ || !window_ || !IsWindow(window_) ||
        pane_.list_.request.binding.generation != generation_ || !pane_.active() ||
        !pane_.content_request_is_current(pane_.list_.request.binding)) return;
    // Geometry never reorders rows. If nested work did, its own interaction
    // restoration wins. recovery_index_ is row-backed and may be invalidated
    // by sorting/cache replacement: never use it as a general identity map.
    if (!identity_ || index_ < 0 || static_cast<std::size_t>(index_) >= pane_.list_.rows.size() ||
        pane_.list_.rows[index_].identity != *identity_) return;
    pane_.last_viewport_anchor_clamped_ = restore_viewport(window_, {index_, position_, view_});
}
void Pane::use_list_icons(std::unique_ptr<managed_list::IconSource> source) {
    cancel_list_icons();
    icon_source_ = std::move(source);
    icon_seam_ = true;
}
managed_list::IconKey Pane::list_icon_key(std::size_t row) const {
    const auto& item = list_.rows[row];
    return {list_.request.location, item.identity, item.bytes, item.date_modified,
        item.directory, icon_size_, icon_dpi_, icon_thumbnail_ && !item.directory, item.child_location};
}
void Pane::cancel_list_icons() noexcept {
    ++icon_viewport_;
    icon_interest_.clear();
    enrichment_interest_.clear();
    image_attempted_.fill(false);
    property_sort_cursor_ = 0;
    property_order_.clear();
    if (icon_source_) icon_source_->cancel();
}
void Pane::destroy_list_icons() noexcept {
    cancel_list_icons();
    icon_source_.reset();
    if (list_window_) {
        ListView_SetImageList(list_window_, nullptr, LVSIL_SMALL);
        ListView_SetImageList(list_window_, nullptr, LVSIL_NORMAL);
    }
    if (icon_images_) ImageList_Destroy(icon_images_);
    icon_images_ = nullptr;
    icon_cache_.clear();
}
bool Pane::ensure_list_icon_images(int size) noexcept try {
    if (icon_images_ && size == icon_size_) return true;
    cancel_list_icons();
    if (list_window_) {
        ListView_SetImageList(list_window_, nullptr, LVSIL_SMALL);
        ListView_SetImageList(list_window_, nullptr, LVSIL_NORMAL);
    }
    if (icon_images_) ImageList_Destroy(icon_images_);
    icon_images_ = nullptr;
    icon_cache_.clear();
    icon_size_ = size;
    icon_images_ = ImageList_Create(size, size, ILC_COLOR32 | ILC_MASK,
        static_cast<int>(managed_list::image_capacity(size) + 2), 1);
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
    if (list_window_) {
        ListView_SetImageList(list_window_, icon_images_, LVSIL_SMALL);
        ListView_SetImageList(list_window_, icon_images_, LVSIL_NORMAL);
    }
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
            return key.location == list_.request.location && key.matches(item) && key.size == icon_size_ &&
                key.dpi == icon_dpi_ && key.thumbnail == (icon_thumbnail_ && !item.directory);
        });
    return found == icon_cache_.end() ? (item.directory ? 1 : 0) : found->slot;
}
void Pane::set_list_icon_viewport(std::size_t first, std::size_t count) noexcept try {
    first = std::min(first, list_.rows.size());
    const auto end = first + std::min({count, managed_list::kIconInterest, list_.rows.size() - first});
    std::vector<std::size_t> rows;
    for (auto i = first; i < end; ++i) rows.push_back(i);
    request_list_images(std::move(rows), paint_dpi_);
} catch (...) { cancel_list_icons(); }
void Pane::request_list_images(std::vector<std::size_t> rows, UINT dpi) noexcept try {
    if (!managed_list_active_ || !active() ||
        !content_request_is_current(list_.request.binding)) { cancel_list_icons(); return; }
    ListGeometryScope geometry(*this);
    const auto* tab = active_tab();
    const auto mode = shell_core::parse_view_mode(tab ? tab->view_mode : "")
        .value_or(shell_core::ViewModeSelection{FVM_DETAILS, -1});
    const int logical = mode.mode == FVM_ICON ? mode.image_size :
        (mode.mode == FVM_TILE ? 48 : mode.mode == FVM_CONTENT ? 64 : 16);
    const int size = std::clamp(MulDiv(std::clamp(logical, 16, 512), static_cast<int>(dpi), 96),
        16, managed_list::kMaxImageSize);
    const bool thumbnail = logical > 16;
    if (icon_dpi_ != dpi || icon_thumbnail_ != thumbnail) {
        cancel_list_icons();
        if (list_window_) {
            ListView_SetImageList(list_window_, nullptr, LVSIL_SMALL);
            ListView_SetImageList(list_window_, nullptr, LVSIL_NORMAL);
        }
        if (icon_images_) ImageList_Destroy(icon_images_);
        icon_images_ = nullptr; icon_cache_.clear();
        icon_dpi_ = dpi; icon_thumbnail_ = thumbnail;
    }
    if (!ensure_list_icon_images(size)) {
        property_capacity_failed_ = true;
        property_sort_pending_ = false;
        update_managed_list();
        return;
    }
    std::vector<managed_list::IconKey> interest;
    std::size_t interest_bytes{};
    for (auto i : rows) {
        if (i >= list_.rows.size() || interest.size() >= managed_list::kIconInterest) continue;
        auto key = list_icon_key(i);
        const auto bytes = sizeof(key) + managed_list::owned_icon_key_bytes(key);
        if (interest_bytes + bytes > managed_list::kIconValueBytes) break;
        interest_bytes += bytes;
        interest.push_back(std::move(key));
    }
    const bool changed = interest != icon_interest_;
    if (!changed && !enrichment_interest_.empty()) return;
    if (changed) {
        ++icon_viewport_;
        enrichment_interest_.clear();
        image_attempted_.fill(false);
        // Requests discarded by replace remain unresolved and are retried only
        // in the next bounded demand window. No per-completion whole-list scan.
        property_sort_cursor_ = 0;
        property_order_.clear();
    }
    icon_interest_ = std::move(interest);
    std::vector<managed_list::IconRequest> requests;
    append_property_requests(requests, rows);
    if (property_sort_pending_ && requests.empty() &&
        list_.phase != managed_list::Phase::loading && property_sort_cursor_ >= list_.rows.size()) {
        finish_property_batch();
        return;
    }
    if (!property_sort_pending_) for (std::size_t index = 0; index < icon_interest_.size(); ++index) {
        const auto& key = icon_interest_[index];
        if (image_attempted_[index]) continue;
        const auto found = std::find_if(icon_cache_.begin(), icon_cache_.end(),
            [&](const auto& entry) { return entry.key == key; });
        if (found != icon_cache_.end()) { found->used = ++icon_clock_; continue; }
        if (std::any_of(requests.begin(), requests.end(), [&](const auto& r) { return r.key == key && r.property.empty(); })) continue;
        if (requests.size() < managed_list::kIconInterest) {
            if (!managed_list::append_icon_request(requests,
                    {list_.request, icon_viewport_, key, {}, 0, 0})) break;
            image_attempted_[index] = true;
        }
    }
    if (requests.empty()) { if (icon_source_) icon_source_->cancel(); finish_property_batch(); return; }
    if (!icon_source_ && !icon_seam_) icon_source_ = managed_list::make_icon_source();
    enrichment_interest_ = requests;
    if (!icon_source_ || !icon_source_->replace(std::move(requests))) {
        if (icon_source_) icon_source_->cancel();
        enrichment_interest_.clear();
        property_capacity_failed_ = true;
        finish_property_batch();
    }
} catch (...) {
    cancel_list_icons();
    property_capacity_failed_ = true;
    property_sort_pending_ = false;
    update_managed_list();
}
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
            request.key.size != icon_size_ || !icon_images_) continue;
        const auto pending = std::find(enrichment_interest_.begin(), enrichment_interest_.end(), request);
        if (pending == enrichment_interest_.end()) continue;
        enrichment_interest_.erase(pending);
        if (!request.property.empty()) {
            accept_property_result(*result);
            continue;
        }
        if (std::find(icon_interest_.begin(), icon_interest_.end(), request.key) == icon_interest_.end()) continue;
        struct OwnedIcon { HICON value; ~OwnedIcon() { if (value) DestroyIcon(value); } };
        const OwnedIcon icon{raster_icon(result->image, icon_size_)};
        if (!icon.value) continue; // retry on renewed interest/Refresh, never an idle loop
        auto found = std::find_if(icon_cache_.begin(), icon_cache_.end(),
            [&](const auto& entry) { return entry.key == request.key; });
        if (found != icon_cache_.end()) continue;
        auto key = request.key; // allocate before mutating any occupied slot
        const auto key_bytes = sizeof(CachedIcon) + managed_list::owned_icon_key_bytes(key);
        // Include owned identity capacity in the existing image-cache budget.
        // Removing an LRU image shifts native slot indices; repair every key's
        // slot before exposing the cache again. Placeholders always stay 0/1.
        if (key_bytes + 3 * managed_list::image_bytes(icon_size_) > managed_list::kImageCacheBytes) continue;
        while (icon_cache_.size() == managed_list::image_capacity(icon_size_) ||
            list_image_bytes() + managed_list::image_bytes(icon_size_) + key_bytes > managed_list::kImageCacheBytes) {
            found = std::min_element(icon_cache_.begin(), icon_cache_.end(),
                [](const auto& a, const auto& b) { return a.used < b.used; });
            const int removed = found->slot;
            if (!ImageList_Remove(icon_images_, removed)) break;
            icon_cache_.erase(found);
            for (auto& entry : icon_cache_) if (entry.slot > removed) --entry.slot;
        }
        if (list_image_bytes() + managed_list::image_bytes(icon_size_) + key_bytes > managed_list::kImageCacheBytes)
            continue;
        // Track the key before adding the image so allocation failure cannot
        // orphan a native slot. Image count stays <= the existing raster cap.
        icon_cache_.push_back({std::move(key), -1, ++icon_clock_});
        const int slot = ImageList_AddIcon(icon_images_, icon.value);
        if (slot < 0) icon_cache_.pop_back(); else icon_cache_.back().slot = slot;
        if (slot >= 0 && list_window_) InvalidateRect(list_window_, nullptr, FALSE);
    }
    if (enrichment_interest_.empty()) {
        finish_property_batch();
        queue_list_icon_viewport();
    }
} catch (...) { cancel_list_icons(); }
void Pane::queue_list_icon_viewport() noexcept {
    if (list_window_ && !icon_viewport_posted_)
        icon_viewport_posted_ = PostMessageW(list_window_, kViewportMessage, 0, 0) != FALSE;
}
void Pane::refresh_list_icon_viewport() noexcept try {
    if (!list_window_ || !IsWindowVisible(list_window_)) { cancel_list_icons(); return; }
    // Visible-only traversal handles horizontal List and icon grids as well
    // as report rows. Never enumerate the whole folder to discover a viewport.
    std::vector<std::size_t> rows;
    int item = -1;
    while (rows.size() < managed_list::kIconInterest) {
        item = ListView_GetNextItem(list_window_, item, LVNI_VISIBLEONLY);
        if (item < 0) break;
        rows.push_back(static_cast<std::size_t>(item));
    }
    request_list_images(std::move(rows), paint_dpi_);
} catch (...) { cancel_list_icons(); }
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
    // An edit is positioned over one label. Cancel before geometry or scrolling
    // can move a different row underneath it; never keep an editor by ordinal.
    if (message == WM_VSCROLL || message == WM_HSCROLL || message == WM_MOUSEWHEEL ||
        message == WM_SIZE || message == LVM_SCROLL || message == LVM_SETVIEW ||
        message == WM_SETFONT || message == LVM_SETIMAGELIST)
        pane->cancel_list_rename();
    const bool geometry = message == WM_SETFONT ||
        message == LVM_SETIMAGELIST || message == LVM_SETICONSPACING || message == LVM_SETVIEW;
    std::optional<ListGeometryScope> viewport;
    if (geometry) viewport.emplace(*pane);
    const auto result = DefSubclassProc(window, message, wp, lp);
    if (pane->list_window_ != window || !IsWindow(window)) return result;
    switch (message) {
    case WM_NOTIFY:
        if (lp && reinterpret_cast<NMHDR*>(lp)->hwndFrom == ListView_GetHeader(window))
            pane->queue_list_icon_viewport();
        break;
    case WM_VSCROLL: case WM_HSCROLL: case WM_MOUSEWHEEL: case WM_KEYDOWN:
    case WM_SIZE: case WM_SHOWWINDOW: case LVM_SCROLL: case LVM_ENSUREVISIBLE:
        pane->queue_list_icon_viewport(); break;
    default: break;
    }
    return result;
}

void Pane::apply_managed_list_view_mode() noexcept {
    if (!active() || !managed_list_active_) return;
    ListGeometryScope geometry(*this);
    const auto* tab = active_tab();
    if (!tab) return;
    const auto mode = shell_core::parse_view_mode(tab->view_mode)
        .value_or(shell_core::ViewModeSelection{FVM_DETAILS, -1});
    if (mode != list_view_selection_) {
        cancel_list_icons();
        if (list_window_) {
            ListView_SetImageList(list_window_, nullptr, LVSIL_SMALL);
            ListView_SetImageList(list_window_, nullptr, LVSIL_NORMAL);
        }
        if (icon_images_) ImageList_Destroy(icon_images_);
        icon_images_ = nullptr; icon_cache_.clear();
        list_view_selection_ = mode;
    }
    list_mode_ = mode.mode;
    cancel_list_icons();
    if (!list_window_) return;
    // Attach size-dependent images before spacing: comctl32 may recompute
    // icon metrics when an image list changes. This never calls the Shell.
    set_list_icon_viewport(0, 0);
    const auto scale = [&](int n) { return MulDiv(n, static_cast<int>(paint_dpi_), 96); };
    // LVS_OWNERDATA cannot use native tile item data. Cards use the virtual
    // icon grid (Tiles) or headerless report rows (Content), drawn below.
    const DWORD view = mode.mode == FVM_LIST ? LV_VIEW_LIST :
        mode.mode == FVM_DETAILS || mode.mode == FVM_CONTENT ? LV_VIEW_DETAILS :
        mode.mode == FVM_ICON && mode.image_size == 16 ? LV_VIEW_SMALLICON : LV_VIEW_ICON;
    auto style = GetWindowLongPtrW(list_window_, GWL_STYLE);
    style = mode.mode == FVM_CONTENT ? style | LVS_NOCOLUMNHEADER : style & ~LVS_NOCOLUMNHEADER;
    style = (style & ~LVS_TYPEMASK) | view;
    SetWindowLongPtrW(list_window_, GWL_STYLE, style);
    ListView_SetView(list_window_, view);
    if (view == LV_VIEW_ICON) {
        const int size = mode.mode == FVM_TILE ? 48 : std::clamp(mode.image_size, 16, 512);
        ListView_SetIconSpacing(list_window_, scale(mode.mode == FVM_TILE ? 300 : size + 40),
            scale(mode.mode == FVM_TILE ? 80 : size + 52));
    }
    queue_list_icon_viewport();
    InvalidateRect(list_window_, nullptr, TRUE);
}
std::optional<LRESULT> Pane::draw_list_card(NMHDR* notification) noexcept try {
    if (notification->code != NM_CUSTOMDRAW ||
        (list_mode_ != FVM_TILE && list_mode_ != FVM_CONTENT)) return {};
    auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(notification);
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
    if (draw->nmcd.dwDrawStage != CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
    const auto index = static_cast<std::size_t>(draw->nmcd.dwItemSpec);
    if (index >= list_.rows.size()) return CDRF_DODEFAULT;
    RECT rect{};
    if (!ListView_GetItemRect(list_window_, static_cast<int>(index), &rect, LVIR_BOUNDS))
        return CDRF_DODEFAULT;
    const auto& row = list_.rows[index];
    const bool selected = (ListView_GetItemState(list_window_, static_cast<int>(index), LVIS_SELECTED) != 0);
    const HDC dc = draw->nmcd.hdc;
    const int saved = SaveDC(dc);
    struct DcState { HDC dc; int saved; ~DcState() { if (saved) RestoreDC(dc, saved); } } restore{dc, saved};
    FillRect(dc, &rect, GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
    SetTextColor(dc, GetSysColor(selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
    SetBkMode(dc, TRANSPARENT);
    const int pad = MulDiv(6, static_cast<int>(paint_dpi_), 96);
    if (icon_images_) ImageList_Draw(icon_images_, list_item_image(index), dc,
        rect.left + pad, rect.top + std::max(0L, (rect.bottom - rect.top - icon_size_) / 2), ILD_TRANSPARENT);
    rect.left += icon_size_ + pad * 2; rect.top += pad; rect.right -= pad;
    RECT title = rect;
    DrawTextW(dc, row.name.c_str(), -1, &title, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
    rect.top += metrics.tmHeight + pad;
    auto detail = row.directory ? std::wstring(L"Folder") : std::to_wstring(row.bytes) + L" bytes";
    if (list_mode_ == FVM_CONTENT && row.date_modified != 0) {
        FILETIME utc{static_cast<DWORD>(row.date_modified), static_cast<DWORD>(row.date_modified >> 32)};
        FILETIME local{}; SYSTEMTIME time{}; wchar_t date[64]{};
        if (FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &time) &&
            GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &time, nullptr, date, ARRAYSIZE(date)))
            detail += std::wstring(L"    ") + date;
    }
    DrawTextW(dc, detail.c_str(), -1, &rect, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (draw->nmcd.uItemState & CDIS_FOCUS) {
        RECT focus{}; ListView_GetItemRect(list_window_, static_cast<int>(index), &focus, LVIR_BOUNDS);
        DrawFocusRect(dc, &focus);
    }
    return CDRF_SKIPDEFAULT;
} catch (...) { return CDRF_DODEFAULT; }
} // namespace panedock::app_shell
