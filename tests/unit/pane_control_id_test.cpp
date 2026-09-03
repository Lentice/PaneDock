#include "app_shell/pane_control_id.h"
#include "unit/test_util.h"

#include <array>

namespace {

using panedock::app_shell::PaneControl;
using panedock::app_shell::PaneControlId;
using panedock::app_shell::decode_pane_control;
using panedock::app_shell::encode_pane_control;

constexpr std::array kControls{
    PaneControl::tab_strip, PaneControl::back, PaneControl::forward,
    PaneControl::up, PaneControl::address_bar, PaneControl::refresh,
    PaneControl::view_mode, PaneControl::pinned, PaneControl::folder_context};

void test_pane_control_ids_round_trip() {
    for (const auto control : kControls) {
        for (const std::size_t pane : {std::size_t{0}, std::size_t{3}}) {
            const auto decoded =
                decode_pane_control(encode_pane_control(control, pane));
            EXPECT((decoded == PaneControlId{pane, control}));
        }
    }
}

void test_out_of_range_and_non_pane_ids_are_rejected() {
    for (const auto control : kControls) {
        const int base = encode_pane_control(control, 0);
        EXPECT(!decode_pane_control(base - 1).has_value());
        EXPECT(!decode_pane_control(base + 4).has_value());
    }
    for (const int id : {360, 400, 500, 780, 781})
        EXPECT(!decode_pane_control(id).has_value());
}

}  // namespace

int main() {
    test_pane_control_ids_round_trip();
    test_out_of_range_and_non_pane_ids_are_rejected();
    return panedock::test::summary("pane_control_id");
}
