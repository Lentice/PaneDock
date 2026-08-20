#pragma once

#include <cstddef>

namespace panedock::app_shell {

enum class LayoutTemplate { two_pane, four_pane };

class LayoutState final {
public:
    static constexpr std::size_t kPaneCount = 4;

    LayoutTemplate layout() const noexcept { return layout_; }
    std::size_t active_pane() const noexcept { return active_pane_; }

    bool is_visible(std::size_t pane) const noexcept {
        return pane < kPaneCount &&
               (layout_ == LayoutTemplate::four_pane || pane < 2);
    }

    bool set_active_pane(std::size_t pane) noexcept {
        if (!is_visible(pane)) {
            return false;
        }
        active_pane_ = pane;
        return true;
    }

    void toggle_layout() noexcept {
        if (layout_ == LayoutTemplate::four_pane) {
            layout_ = LayoutTemplate::two_pane;
            if (active_pane_ >= 2) {
                active_pane_ = 0;
            }
        } else {
            layout_ = LayoutTemplate::four_pane;
        }
    }

private:
    LayoutTemplate layout_{LayoutTemplate::four_pane};
    std::size_t active_pane_{0};
};

}  // namespace panedock::app_shell
