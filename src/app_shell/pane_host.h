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
#include <string_view>

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
    // Shell name resolution uses the coordinator's re-entry gate.
    virtual std::wstring tab_display_text(std::wstring_view parsing_name) = 0;
    virtual HFONT chrome_font() const noexcept = 0;
    virtual HWND tooltip() const noexcept = 0;
    // PD-189: a pane's own proc handles the notifications its child controls
    // raise. Acting on them needs the coordinator (global commands, the
    // hovered owner-draw button, the Shell re-entry deferral), so the pane
    // asks its host and returns what it gets back; nullopt means the message
    // is none of the pane's business.
    //
    // This is a PaneHost method rather than a free function declared in a
    // header and defined in main.cpp: that arrangement left panedock_pane
    // with an undefined symbol only the executable or a test stub could
    // resolve, which is a circular dependency in everything but the include
    // graph.
    virtual std::optional<LRESULT>
    handle_pane_control_message(Pane &pane, UINT message, WPARAM wparam,
                                LPARAM lparam) = 0;
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
