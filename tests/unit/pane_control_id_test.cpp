#include "app_shell/pane_control_id.h"
#include "unit/test_util.h"

#include <array>
#include <set>

namespace {

using namespace panedock::app_shell;

constexpr std::array kControls{
    PaneControl::tab_strip, PaneControl::back, PaneControl::forward,
    PaneControl::up, PaneControl::address_bar, PaneControl::refresh,
    PaneControl::view_mode, PaneControl::pinned, PaneControl::folder_context};

void test_pane_control_ids_round_trip() {
    for (const auto control : kControls) {
        const auto decoded = decode_pane_control(encode_pane_control(control));
        EXPECT((decoded == control));
    }
}

void test_ids_are_distinct() {
    for (const auto control : kControls)
        for (const auto other : kControls)
            if (control != other)
                EXPECT(encode_pane_control(control) !=
                       encode_pane_control(other));
}

void test_non_pane_ids_are_rejected() {
    for (const auto control : kControls) {
        const int base = encode_pane_control(control);
        EXPECT(!decode_pane_control(base - 1).has_value());
        EXPECT(!decode_pane_control(base + 4).has_value());
    }
    for (const int id : {360, 400, 500, 780, 781})
        EXPECT(!decode_pane_control(id).has_value());
}

// The four routing/painting sites in main.cpp used to re-derive these
// ranges independently. They now all go through decode_command, so a wrong
// constant fails here instead of misrouting a command silently.
void test_every_block_decodes_to_its_own_kind() {
    EXPECT(decode_command(kGroupListId).kind == CommandKind::group_list);

    for (int offset = 0; offset < kGroupActionCount; ++offset) {
        const auto command = decode_command(kGroupActionIdBase + offset);
        EXPECT(command.kind == CommandKind::group_action);
        EXPECT(command.index == static_cast<std::size_t>(offset));
        EXPECT(command.action == static_cast<GroupAction>(offset));
    }
    EXPECT(decode_command(kNewGroupId).action == GroupAction::create);
    EXPECT(decode_command(kMoveDownId).action == GroupAction::move_down);

    for (const auto control : kControls) {
        const auto command = decode_command(encode_pane_control(control));
        EXPECT(command.kind == CommandKind::pane_control);
        EXPECT(command.control == control);
    }

    for (std::size_t layout = 0; layout < kLayoutButtonCount; ++layout) {
        const auto command =
            decode_command(kLayoutButtonIdBase + static_cast<int>(layout));
        EXPECT(command.kind == CommandKind::layout_template);
        EXPECT(command.index == layout);
    }

    for (int offset = 0; offset < 4; ++offset) {
        const auto command = decode_command(kCloseTabId + offset);
        EXPECT(command.kind == CommandKind::tab_close);
        EXPECT(command.index == static_cast<std::size_t>(offset));
    }
}

void test_view_mode_ids_split_into_per_pane_blocks() {
    for (std::size_t pane = 0; pane < kCommandPaneCount; ++pane) {
        for (std::size_t option = 0; option < kViewModeOptionCount; ++option) {
            const int id =
                kViewModeMenuIdBase +
                static_cast<int>(pane * kViewModeOptionCount + option);
            const auto command = decode_command(id);
            EXPECT(command.kind == CommandKind::view_mode);
            EXPECT(command.pane == pane);
            EXPECT(command.index == option);
        }
    }
}

// The manage slot is the one that must not be handed to Pane::handle_command.
void test_pinned_manage_is_the_only_action_slot() {
    for (std::size_t pane = 0; pane < kCommandPaneCount; ++pane) {
        const int base =
            kPinnedMenuIdBase + static_cast<int>(pane) * kPinnedMenuSlotsPerPane;
        for (int slot = 0; slot < kPinnedMenuSlotsPerPane; ++slot) {
            const auto command = decode_command(base + slot);
            EXPECT(command.pane == pane);
            EXPECT(command.index == static_cast<std::size_t>(slot));
            EXPECT(command.kind == (slot == kPinnedMenuManageOffset
                                        ? CommandKind::pinned_manage
                                        : CommandKind::pinned_location));
        }
    }
    // The last pane's manage slot is the last id in the block; one past it is
    // outside every range.
    const int past = kPinnedMenuIdBase + kPinnedMenuIdCount;
    EXPECT(decode_command(past - 1).kind == CommandKind::pinned_manage);
    EXPECT(decode_command(past).kind == CommandKind::unknown);
}

// A block that started one id early or late would show up as an overlap.
void test_the_blocks_do_not_overlap() {
    std::set<int> seen;
    for (int id = 0; id < 1200; ++id) {
        if (decode_command(id).kind == CommandKind::unknown) continue;
        EXPECT(seen.insert(id).second);
    }
    // Spot-check the gaps between blocks stay unclaimed.
    for (const int id : {0, 99, 107, 359, 392 + 1, 408, 499, 779, 784, 791})
        if (!decode_pane_control(id).has_value())
            EXPECT(decode_command(id).kind == CommandKind::unknown);
}

}  // namespace

int main() {
    test_pane_control_ids_round_trip();
    test_ids_are_distinct();
    test_non_pane_ids_are_rejected();
    test_every_block_decodes_to_its_own_kind();
    test_view_mode_ids_split_into_per_pane_blocks();
    test_pinned_manage_is_the_only_action_slot();
    test_the_blocks_do_not_overlap();
    return panedock::test::summary("pane_control_id");
}
