#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "core/model.h"

struct IDropTarget;

namespace panedock::sidebar {

inline constexpr int kSidebarWidth = panedock::core::kDefaultSidebarWidth;
inline constexpr int kGroupRowHeight = 52;
inline constexpr COLORREF kPlaceholderContent = RGB(148, 163, 184);
inline constexpr UINT kRenameCommitMessage = WM_APP + 1;

struct GroupSummary final {
    std::string id;
    std::wstring name;
    std::size_t pane_count{0};
    std::size_t tab_count{0};
};

// A completed reorder gesture, waiting for the coordinator to apply it.
// Sidebar owns the gesture; committing it to the model is coordinator work
// (core::reorder_group plus the session save), so it is reported, not done.
struct GroupReorder final {
    std::string group_id;
    std::size_t target_index{};
};

class Sidebar final {
public:
    bool create(HWND parent, int control_id, ::IDropTarget* drop_target) noexcept;
    void revoke_drag_drop() noexcept;
    void set_rect(const RECT& rect, UINT dpi, HDWP* deferred = nullptr) noexcept;
    void set_groups(const std::vector<GroupSummary>& groups);
    std::optional<std::size_t> selected_index() const noexcept;
    void set_selected_index(std::size_t index) noexcept;
    void set_hover_index(std::optional<std::size_t> index) noexcept;
    bool measure_item(MEASUREITEMSTRUCT* item, UINT dpi) const noexcept;
    // Resolves which group each row shows on its own: while a reorder is in
    // progress the rows are drawn in their projected order, which the caller
    // used to compute. It has no reason to know a drag exists.
    bool draw_item(const DRAWITEMSTRUCT* item) const noexcept;

    // The list's own mouse behaviour: hover tracking and the reorder drag.
    // Returns a result when handled (calling DefSubclassProc itself where the
    // control must see the message first), or nullopt for the caller to pass
    // the message on. The caller keeps the shutdown and Shell-reentry gates:
    // those are coordinator services, not list state.
    std::optional<LRESULT> handle_list_message(HWND window, UINT message,
                                               WPARAM wparam,
                                               LPARAM lparam) noexcept;
    // Non-null once a drag finished on a new position. Cleared by the read.
    std::optional<GroupReorder> take_reorder_request() noexcept;
    void cancel_drag() noexcept;

    bool begin_rename();
    std::optional<std::wstring> take_rename_text() noexcept;
    HWND window() const noexcept { return list_box_; }

private:
    static LRESULT CALLBACK edit_proc(HWND window, UINT message,
                                      WPARAM wparam, LPARAM lparam);
    void close_editor(bool commit);
    std::optional<std::size_t> item_at_point(HWND list,
                                             POINT point) const noexcept;
    void update_drag(HWND list, WPARAM wparam, LPARAM lparam) noexcept;
    void finish_drag(HWND list) noexcept;

    struct Drag final {
        HWND list{nullptr};
        std::size_t source_index{};
        std::string group_id;
        POINT start{};
        bool dragging{};
        std::optional<std::size_t> target_index;
    };

    HWND parent_{nullptr};
    HWND list_box_{nullptr};
    HWND editor_{nullptr};
    WNDPROC original_edit_proc_{nullptr};
    int control_id_{};
    bool drag_drop_registered_{false};
    std::vector<GroupSummary> groups_;
    std::optional<std::size_t> hover_index_;
    std::optional<std::wstring> pending_rename_;
    std::optional<Drag> drag_;
    std::optional<GroupReorder> pending_reorder_;
    UINT dpi_{};
};

}  // namespace panedock::sidebar
