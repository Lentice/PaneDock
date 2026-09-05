#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <optional>
#include <span>
#include <string>

#include "app_shell/tab_overflow.h"
#include "core/model.h"

namespace panedock::app_shell {

class Pane;

struct PinnedLocation final {
    panedock::core::ShellLocation location;
    std::wstring label;
};

class PaneHost {
  public:
    virtual ~PaneHost() = default;

    virtual bool is_shutting_down() const noexcept = 0;
    virtual void shell_call_entered() noexcept = 0;
    virtual void shell_call_left() noexcept = 0;
    virtual void schedule_session_save() noexcept = 0;
    virtual const std::string &active_group_id() const noexcept = 0;
    virtual std::string make_unique_tab_id() const = 0;
    virtual std::optional<TabStripDragLayout>
    tab_drag_layout(const Pane &pane, HWND strip, int min_width, int max_width,
                    int text_reserve) const = 0;
    // Pinned locations belong to application persistence, not one pane.
    virtual std::span<const PinnedLocation> pinned_locations() const noexcept = 0;
    // Pinning updates application persistence, not one pane.
    virtual void pin_location(panedock::core::ShellLocation location) = 0;
    // Singleton coordinator state: Group switching suppresses capture for all panes.
    virtual bool location_capture_suppressed() const noexcept = 0;
    // Transitional coordinator hook: tab-strip refresh moves to Pane in PD-196.
    virtual void tab_strip_needs_refresh(Pane &pane) = 0;
};

class ShellCall final {
  public:
    explicit ShellCall(PaneHost *host) noexcept : host_(host) {
        host_->shell_call_entered();
    }

    ~ShellCall() noexcept { host_->shell_call_left(); }

    ShellCall(const ShellCall &) = delete;
    ShellCall &operator=(const ShellCall &) = delete;

  private:
    PaneHost *host_;
};

} // namespace panedock::app_shell
