#include "shell_core/shell_core.h"
#include "unit/test_util.h"

#include <array>

namespace {
using panedock::shell_core::ViewModeSelection;

void test_all_persisted_modes_round_trip() {
    const std::array<ViewModeSelection, 8> modes{{
        {FVM_ICON, panedock::shell_core::kExtraLargeIconSize},
        {FVM_ICON, panedock::shell_core::kLargeIconSize},
        {FVM_ICON, panedock::shell_core::kMediumIconSize},
        {FVM_ICON, panedock::shell_core::kSmallIconSize},
        {FVM_LIST, -1},
        {FVM_DETAILS, -1},
        {FVM_TILE, -1},
        {FVM_CONTENT, -1},
    }};
    for (const auto& mode : modes) {
        const auto encoded = panedock::shell_core::view_mode_name(
            mode.mode, mode.image_size);
        const auto decoded = panedock::shell_core::parse_view_mode(encoded);
        EXPECT(decoded.has_value());
        EXPECT(decoded.value() == mode);
    }
}

void test_unknown_modes_are_rejected() {
    EXPECT(!panedock::shell_core::parse_view_mode("nonsense").has_value());
    EXPECT(!panedock::shell_core::parse_view_mode("").has_value());
}

void test_icon_sizes_are_distinguishable() {
    EXPECT(panedock::shell_core::view_mode_name(FVM_ICON, 48) !=
           panedock::shell_core::view_mode_name(FVM_ICON, 96));
}
}  // namespace

int main() {
    test_all_persisted_modes_round_trip();
    test_unknown_modes_are_rejected();
    test_icon_sizes_are_distinguishable();
    return panedock::test::summary("shell_core_view_mode");
}
