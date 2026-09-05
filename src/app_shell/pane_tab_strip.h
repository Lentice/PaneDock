#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app_shell/tab_overflow.h"

namespace panedock::app_shell {

class Pane;

inline constexpr UINT kTabStripSelectionMessage = WM_APP + 49;

// Dispatch surface for the drag-hover delay timer. `PaneTabStrip` stores a drag
// target only as `IDropTarget` (RegisterDragDrop/RevokeDragDrop lifetime);
// the coordinator that built the concrete target reaches its extra hover
// behavior via `dynamic_cast<DragHoverTimer*>` on the pointer
// `PaneTabStrip::drag_hover_target()` returns, so `PaneTabStrip` never has to know about
// the coordinator's own drag-hover implementation type.
class DragHoverTimer {
  public:
    virtual ~DragHoverTimer() = default;
    virtual void invoke_hover(UINT_PTR generation) noexcept = 0;
    virtual void timer_expired() noexcept = 0;
};

// Pure UI text for one tab strip item. Distinct from the tab domain model:
// the strip never holds tab domain data, only what it needs to paint a label.
struct StripLabel final {
    std::wstring text;
};

// Values resolved by the coordinator; no borrowed tab/model pointers.
struct TabStripPaintState final {
    bool active_pane{};
    std::optional<std::size_t> dragged_index;
    std::optional<std::wstring_view> placeholder_text;
};

class PaneTabStrip final {
  public:
    explicit PaneTabStrip(Pane *owner) noexcept : owner_(owner) {}
    ~PaneTabStrip() { destroy(); }
    PaneTabStrip(const PaneTabStrip &) = delete;
    PaneTabStrip &operator=(const PaneTabStrip &) = delete;

    bool create(HWND parent) noexcept;
    void destroy() noexcept;
    HWND window() const noexcept { return tab_strip_; }

    void refresh();
    void apply_item_size(bool reveal_active = false);
    void scroll(bool forward);
    RECT viewport_rect() const noexcept;
    std::optional<std::size_t> scroll_button_at(POINT point) const noexcept;
    int scroll_step(bool forward) const noexcept;
    void update_tab_strip_tooltips(HWND tooltip) noexcept;
    void paint(const TabStripPaintState &state) noexcept;
    void mouse_move(POINT point) noexcept;
    void mouse_leave() noexcept;
    // Only local cases; the subclass retains drag and re-entry coordination.
    std::optional<LRESULT> handle_message(UINT message, WPARAM wparam,
                                          LPARAM lparam);

    void set_tabs(std::span<const std::wstring> labels) noexcept;
    const std::vector<StripLabel> &tab_visuals() const noexcept {
        return tab_visuals_;
    }

    const TabStripGeometry &tab_geometry() const noexcept {
        return tab_strip_geometry_;
    }
    void set_geometry(TabStripGeometry geometry) noexcept {
        tab_strip_geometry_ = std::move(geometry);
    }

    std::optional<std::size_t> tab_at(POINT client) const noexcept {
        return tab_strip_hit_test(tab_strip_geometry_, client.x, client.y);
    }
    // ScreenToClient against this pane's own tab strip, then tab_at(). Used
    // by cross-pane drag hit-testing so the coordinator never has to reach
    // into another pane's HWND client-rect math directly.
    std::optional<std::size_t> tab_at_screen(POINT screen) const noexcept;

    std::optional<std::size_t> tab_hover_index() const noexcept {
        return tab_hover_index_;
    }
    void set_tab_hover(std::optional<std::size_t> index) noexcept {
        tab_hover_index_ = index;
    }
    std::optional<std::size_t> scroll_hover_index() const noexcept {
        return tab_scroll_hover_index_;
    }
    void set_scroll_hover(std::optional<std::size_t> index) noexcept {
        tab_scroll_hover_index_ = index;
    }

    // The coordinator wraps registration in its ShellCallScope.
    bool register_drag_hover_target(IDropTarget *target) noexcept;
    void revoke_drag_hover_target() noexcept;
    IDropTarget *drag_hover_target() const noexcept {
        return tab_drag_target_.Get();
    }

  private:
    void paint_contents(HDC dc, const TabStripPaintState &state) noexcept;
    Pane *owner_;
    HWND tab_strip_{nullptr};
    bool tab_tooltips_registered_{};
    std::vector<StripLabel> tab_visuals_;
    TabStripGeometry tab_strip_geometry_;
    std::optional<std::size_t> tab_hover_index_;
    std::optional<std::size_t> tab_scroll_hover_index_;
    Microsoft::WRL::ComPtr<IDropTarget> tab_drag_target_;
};

} // namespace panedock::app_shell
