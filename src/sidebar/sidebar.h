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

namespace panedock::sidebar {

inline constexpr int kSidebarWidth = 226;
inline constexpr int kGroupRowHeight = 54;
inline constexpr UINT kRenameCommitMessage = WM_APP + 1;

struct GroupSummary final {
    std::string id;
    std::wstring name;
    std::size_t pane_count{0};
    std::size_t tab_count{0};
};

class Sidebar final {
public:
    bool create(HWND parent, int control_id) noexcept;
    void set_rect(const RECT& rect, UINT dpi) noexcept;
    void set_groups(const std::vector<GroupSummary>& groups);
    std::optional<std::size_t> selected_index() const noexcept;
    void set_selected_index(std::size_t index) noexcept;
    bool measure_item(MEASUREITEMSTRUCT* item, UINT dpi) const noexcept;
    bool draw_item(const DRAWITEMSTRUCT* item) const noexcept;

    bool begin_rename();
    std::optional<std::wstring> take_rename_text() noexcept;
    HWND window() const noexcept { return list_box_; }

private:
    static LRESULT CALLBACK edit_proc(HWND window, UINT message,
                                      WPARAM wparam, LPARAM lparam);
    void close_editor(bool commit);

    HWND parent_{nullptr};
    HWND list_box_{nullptr};
    HWND editor_{nullptr};
    WNDPROC original_edit_proc_{nullptr};
    int control_id_{};
    std::vector<GroupSummary> groups_;
    std::optional<std::wstring> pending_rename_;
    UINT dpi_{96};
};

}  // namespace panedock::sidebar
