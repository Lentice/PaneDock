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
#include <vector>

#include "app_shell/pane_control_id.h"
#include "app_shell/tab_overflow.h"
#include "core/model.h"
#include "core/navigation.h"
#include "explorer_host/explorer_host.h"

namespace panedock::app_shell {

class PaneHost;

constexpr int kPaneCardOutset = 2;
constexpr int kPaneCardShadowOffset = 2;

inline int pane_card_outset(UINT dpi) noexcept {
    return (std::max)(1, MulDiv(kPaneCardOutset, static_cast<int>(dpi), 96));
}

// Shared by the pane card and the PD-040 explorer-container clip.
inline int pane_card_radius(UINT dpi) noexcept {
    return (std::max)(1, MulDiv(10, static_cast<int>(dpi), 96));
}

inline RECT to_win32_rect(const TabStripRect &rect) noexcept {
    return {rect.left, rect.top, rect.right, rect.bottom};
}

inline int pane_card_shadow_offset(UINT dpi) noexcept {
    return (std::max)(1,
                      MulDiv(kPaneCardShadowOffset, static_cast<int>(dpi), 96));
}

// Dispatch surface for the drag-hover delay timer. `Pane` stores a drag
// target only as `IDropTarget` (RegisterDragDrop/RevokeDragDrop lifetime);
// the coordinator that built the concrete target reaches its extra hover
// behavior via `dynamic_cast<DragHoverTimer*>` on the pointer
// `Pane::drag_hover_target()` returns, so `Pane` never has to know about
// the coordinator's own drag-hover implementation type.
class DragHoverTimer {
  public:
    virtual ~DragHoverTimer() = default;
    virtual void invoke_hover(UINT_PTR generation) noexcept = 0;
    virtual void timer_expired() noexcept = 0;
};

// Pure UI text for one tab strip item. Distinct from the tab domain model:
// a `Pane` never holds tab domain data, only what it needs to paint a label.
struct StripLabel final {
    std::wstring text;
};

// A stable identity slot on the right side that hosts a file view's chrome:
// its own HWNDs, tab-strip geometry, hover/scroll/drag visual state. `Pane`
// never sees the app's coordinator state, the Group domain model, or a tab
// list — the coordinator feeds it labels via set_tabs() and a resolved
// TabStripGeometry via set_geometry(), and reads back hit-test results
// through pure queries.
class Pane final {
  public:
    Pane() noexcept = default;
    ~Pane() { destroy(); }

    Pane(const Pane &) = delete;
    Pane &operator=(const Pane &) = delete;

    static bool register_window_class(HINSTANCE instance) noexcept;
    void set_host(PaneHost *host) noexcept { host_ = host; }
    PaneHost *pane_host() const noexcept { return host_; }
    bool create(HWND parent, int pane_index) noexcept;
    void destroy() noexcept;
    void window_destroyed(HWND window) noexcept;
    // The PaneState this slot currently displays. Not owned: it lives in
    // core::ApplicationState and outlives every rebind (PD-184 guarantees its
    // address is stable for the group's lifetime). Null when this slot is not
    // showing anything (no active Group, or a layout with fewer panes).
    void bind(panedock::core::PaneState *state) noexcept {
        bound_state_ = state;
    }
    void unbind() noexcept { bound_state_ = nullptr; }
    panedock::core::PaneState *pane_state() const noexcept {
        return bound_state_;
    }
    std::size_t index() const noexcept { return index_; }

    // The bound PaneState's active tab, or null when nothing is bound.
    //
    // NEVER store the returned pointer in a member or across a call that can
    // add or remove tabs: PD-184 guarantees the address of a PaneState, but
    // explicitly NOT the address of a TabState — PaneState::tabs is a vector
    // that push_back/insert/erase reallocates. Use it and drop it.
    panedock::core::TabState *active_tab() const noexcept;

    // PD-170 navigation identity for this pane: the generation plus the
    // group/tab it was issued for, so a result arriving after a Group or tab
    // switch can be discarded. The coordinator owns the compare logic
    // (begin_navigation / navigation_request_is_current); Pane only stores it.
    panedock::core::NavigationRequest &pending_navigation() noexcept {
        return pending_navigation_;
    }

    using NavigationGeneration =
        panedock::explorer_host::ExplorerHost::NavigationGeneration;
    NavigationGeneration begin_navigation();
    HRESULT navigate_to(const panedock::core::ShellLocation &location);
    HRESULT navigate_up_one_level();
    bool navigation_request_is_current(
        NavigationGeneration generation);
    void record_navigation_result(
        const panedock::core::ShellLocation &location);
    void navigation_failed(NavigationGeneration generation);
    void navigate_history(bool back);
    void navigate_up();
    void refresh_view();

    // Enable/disable back, forward, up and folder-context from the bound
    // tab's history. Needs no coordinator state, so it lives here.
    void refresh_navigation_buttons() noexcept;

    // (Re)registers this pane's three tab-strip tooltip rects on the shared
    // tooltip control the coordinator owns.
    void update_tab_strip_tooltips(HWND tooltip) noexcept;
    bool set_rect(const RECT &rect) noexcept;
    void set_paint_geometry(const RECT &navigation_background,
                            const RECT &pane_window_rect, UINT dpi) noexcept;
    void paint_background(HDC target) noexcept;
    void set_visible(bool visible) noexcept;
    void apply_font(HFONT font) noexcept;

    HWND window() const noexcept { return window_; }
    HWND explorer_container() const noexcept { return explorer_container_; }
    HWND tab_strip() const noexcept { return tab_strip_; }
    HWND address_bar() const noexcept { return address_bar_; }
    HWND status_bar() const noexcept { return status_bar_; }
    HWND back_button() const noexcept { return back_button_; }
    HWND forward_button() const noexcept { return forward_button_; }
    HWND up_button() const noexcept { return up_button_; }
    HWND refresh_button() const noexcept { return refresh_button_; }
    HWND view_mode_button() const noexcept { return view_mode_button_; }
    HWND pinned_button() const noexcept { return pinned_button_; }

    HWND folder_context_button() const noexcept {
        return folder_context_button_;
    }

    std::optional<RECT> &laid_out_pane_rect() noexcept {
        return laid_out_pane_rect_;
    }
    const std::optional<RECT> &laid_out_pane_rect() const noexcept {
        return laid_out_pane_rect_;
    }
    bool &tab_tooltips_registered() noexcept {
        return tab_tooltips_registered_;
    }
    bool tab_tooltips_registered() const noexcept {
        return tab_tooltips_registered_;
    }

    // --- Tab strip UI state (PD-182) ---------------------------------

    // Replaces the tab labels. The coordinator computes them fresh from
    // core::PaneState each time (refresh_tab_strip); Pane never holds a
    // copy of the tab list itself, only these display strings.
    void set_tabs(std::span<const std::wstring> labels) noexcept;
    const std::vector<StripLabel> &tab_visuals() const noexcept {
        return tab_visuals_;
    }

    // The already-resolved tab strip geometry. The coordinator computes
    // this (tab_overflow.h's layout_tab_strip, fed by its own font/drag
    // context) and stores the result here; Pane only holds and serves it.
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

    // RegisterDragDrop/RevokeDragDrop bookkeeping for this pane's own tab
    // strip HWND. The caller still wraps the RegisterDragDrop call in its
    // own ShellCallScope — Pane has no coordinator state to do that itself.
    bool register_drag_hover_target(IDropTarget *target) noexcept;
    void revoke_drag_hover_target() noexcept;
    IDropTarget *drag_hover_target() const noexcept {
        return tab_drag_target_.Get();
    }

    // --- Shell view lifetime (PD-183) --------------------------------
    //
    // Pane holds the ExplorerHost and its parent container HWND, so
    // realize()/derealize() can only be called through Pane's own
    // destroy() ordering (see pane.cpp) — the coordinator decides *when*
    // to realize/derealize via core::plan_realization, Pane just carries
    // that decision out. Pane never calls plan_realization itself.

    // Wraps ExplorerHost::initialize() against this pane's own
    // explorer_container(). Returns the initialize() HRESULT (not `bool`
    // per the ticket's literal signature) because callers need the exact
    // HRESULT to report first-failure diagnostics; see PD-183 交接區.
    HRESULT realize(const RECT &local_rect,
                    const panedock::core::ShellLocation &location) noexcept;
    void derealize() noexcept;
    bool realized() const noexcept { return realized_; }
    void set_suppress_history(bool suppress) noexcept {
        suppress_history_record_ = suppress;
    }
    bool suppress_history() const noexcept { return suppress_history_record_; }

    // Transitional direct access for ExplorerHost members that PD-183
    // does not promote to a Pane-level command/query (navigation
    // notification setup, view mode, sort, item counts, focus, ...). See
    // PD-183 交接區 for the call-site list still using this.
    panedock::explorer_host::ExplorerHost &host() noexcept {
        return explorer_host_;
    }
    const panedock::explorer_host::ExplorerHost &host() const noexcept {
        return explorer_host_;
    }

  private:
    PaneHost *host_{nullptr};
    panedock::core::PaneState *bound_state_{nullptr};
    std::size_t index_{};
    panedock::core::NavigationRequest pending_navigation_{};
    HWND window_{nullptr};
    HWND explorer_container_{nullptr};
    HWND tab_strip_{nullptr};
    HWND address_bar_{nullptr};
    HWND status_bar_{nullptr};
    HWND back_button_{nullptr};
    HWND forward_button_{nullptr};
    HWND up_button_{nullptr};
    HWND refresh_button_{nullptr};
    HWND view_mode_button_{nullptr};
    HWND pinned_button_{nullptr};
    HWND folder_context_button_{nullptr};
    std::optional<RECT> laid_out_pane_rect_;
    bool tab_tooltips_registered_{};

    std::vector<StripLabel> tab_visuals_;
    TabStripGeometry tab_strip_geometry_;
    std::optional<std::size_t> tab_hover_index_;
    std::optional<std::size_t> tab_scroll_hover_index_;
    Microsoft::WRL::ComPtr<IDropTarget> tab_drag_target_;

    panedock::explorer_host::ExplorerHost explorer_host_;
    bool realized_{};
    bool suppress_history_record_{};
    RECT navigation_background_{};
    UINT paint_dpi_{96};
    HDC paint_dc_{nullptr};
    HBITMAP paint_bitmap_{nullptr};
    HBITMAP paint_old_bitmap_{nullptr};
    SIZE paint_size_{};
    HPEN card_border_pen_{nullptr};
    UINT card_border_pen_dpi_{};
};

} // namespace panedock::app_shell
