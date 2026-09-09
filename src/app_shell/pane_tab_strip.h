#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app_shell/tab_overflow.h"

namespace panedock::app_shell {

class Pane;


// Dispatch surface for the drag-hover delay timer. `PaneTabStrip` stores a drag
// target as `IDropTarget` (RegisterDragDrop/RevokeDragDrop lifetime) plus this
// non-owning view of the same object, so the coordinator can reach the hover
// behavior without naming its own implementation type -- which lives in
// main.cpp's anonymous namespace and cannot be named here at all. Registering
// both at once is what keeps a `dynamic_cast` (and RTTI) out of the timer path.
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

// How many tabs the strip may draw. The cached visuals, the laid-out rects
// and the live model are three lengths kept in sync by hand, and a Shell call
// can pump a WM_PAINT in between: a tab is erased from `PaneState::tabs` and
// `navigate_to` re-enters the message loop before `refresh()` rebuilds the
// visuals. Drawing past the shortest of the three reads out of bounds.
constexpr std::size_t drawable_tab_count(std::size_t visual_count,
                                         std::size_t model_tab_count,
                                         std::size_t rect_count) noexcept {
    return (std::min)({visual_count, model_tab_count, rect_count});
}

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

    // The coordinator wraps registration in its ShellCallScope. `timer` is the
    // same object as `target`, viewed through its hover interface; it is not
    // owned and is cleared with the target.
    bool register_drag_hover_target(IDropTarget *target,
                                    DragHoverTimer *timer) noexcept;
    void revoke_drag_hover_target() noexcept;
    IDropTarget *drag_hover_target() const noexcept {
        return tab_drag_target_.Get();
    }
    DragHoverTimer *drag_hover_timer() const noexcept {
        return tab_drag_timer_;
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
    DragHoverTimer *tab_drag_timer_{nullptr};
};

} // namespace panedock::app_shell
