#include "unit/test_util.h"

#include "app_shell/layout_state.h"
#include "core/prototype_location_persistence.h"

#include <array>
#include <string>

namespace {

using panedock::core::PrototypeLayout;
using panedock::core::PrototypeLocationState;

const std::array<std::wstring, PrototypeLocationState::kPaneCount> kDefaults{
    L"default-0", L"default-1", L"default-2", L"default-3"};

void check_common(const PrototypeLocationState& state) {
    EXPECT(state.locations.size() == PrototypeLocationState::kPaneCount);
    EXPECT(state.active_pane < PrototypeLocationState::kPaneCount);
    EXPECT(state.layout == PrototypeLayout::two_pane ||
           state.layout == PrototypeLayout::four_pane);
}

void check_input(std::wstring_view contents) {
    const auto state =
        panedock::core::parse_prototype_location_state(contents, kDefaults);
    check_common(state);
}

}  // namespace

int main() {
    check_input(L"two\n1\nC:\\One\nC:\\Two\nC:\\Three\nC:\\Four\n");
    check_input(L"");
    check_input(L"four\n");
    check_input(L"four\n2\n\\\\offline\\share\nC:\\Two\nC:\\Three\nC:\\Four\n");
    check_input(L"\n\n\nC:\\Two\n\nC:\\Four\n");

    const auto invalid = panedock::core::parse_prototype_location_state(
        L"four\n0\n\\\\offline\\share\nC:\\Two\nC:\\Three\nC:\\Four\n",
        kDefaults);
    EXPECT(invalid.locations[0] == L"\\\\offline\\share");

    const PrototypeLocationState original{
        {L"C:\\One", L"\\\\offline\\share", L"C:\\Three", L"C:\\Four"},
        PrototypeLayout::two_pane,
        1};
    const auto round_trip = panedock::core::parse_prototype_location_state(
        panedock::core::serialize_prototype_location_state(original), kDefaults);
    EXPECT(round_trip.locations == original.locations);
    EXPECT(round_trip.layout == original.layout);
    EXPECT(round_trip.active_pane == original.active_pane);

    panedock::app_shell::LayoutState layout;
    layout.restore(panedock::app_shell::LayoutTemplate::two_pane, 1);
    EXPECT(layout.layout() == panedock::app_shell::LayoutTemplate::two_pane);
    EXPECT(layout.active_pane() == 1);
    layout.restore(panedock::app_shell::LayoutTemplate::two_pane, 3);
    EXPECT(layout.active_pane() == 0);

    return panedock::test::summary("prototype_location_persistence_check");
}
