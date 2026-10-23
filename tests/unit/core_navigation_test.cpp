#include "core/navigation.h"
#include "core/address_text.h"
#include "core/model.h"
#include "unit/test_util.h"

namespace {
using namespace panedock::core;

void test_navigation_request_identity() {
    NavigationRequest request{42, "group-a", "tab-a"};
    EXPECT(navigation_request_matches(request, 42, "group-a", "tab-a"));
    EXPECT(!navigation_request_matches(request, 41, "group-a", "tab-a"));
    EXPECT(!navigation_request_matches(request, 42, "group-b", "tab-a"));
    EXPECT(!navigation_request_matches(request, 42, "group-a", "tab-b"));

    request = NavigationRequest{43, "group-b", "tab-c"};
    EXPECT(!navigation_request_matches(request, 42, "group-a", "tab-a"));
    EXPECT(navigation_request_matches(request, 43, "group-b", "tab-c"));
}

void test_history_steps_from_a_pending_target() {
    TabState tab;
    tab.history = {{LR"(C:\A)", {}, {}}, {LR"(C:\B)", {}, {}}, {LR"(C:\C)", {}, {}},
                   {LR"(C:\D)", {}, {}}};
    tab.history_index = 3;
    EXPECT(history_step_target(tab, std::nullopt, true) == std::optional<std::size_t>(2));
    // A second Back while the first is still loading goes one further.
    EXPECT(history_step_target(tab, 2, true) == std::optional<std::size_t>(1));
    EXPECT(history_step_target(tab, 0, true) == std::nullopt);
    EXPECT(history_step_target(tab, 1, false) == std::optional<std::size_t>(2));
    EXPECT(history_step_target(tab, std::nullopt, false) == std::nullopt);
    // A stale pending index past the end is ignored.
    EXPECT(history_step_target(tab, 9, true) == std::optional<std::size_t>(2));
    TabState empty;
    EXPECT(history_step_target(empty, std::nullopt, true) == std::nullopt);
}

void test_typed_address_text() {
    EXPECT(trim_typed_address(L"  C:\\Temp \t") == LR"(C:\Temp)");
    EXPECT(trim_typed_address(LR"("C:\Program Files")") == LR"(C:\Program Files)");
    EXPECT(trim_typed_address(LR"( " C:\X " )") == LR"(C:\X)");
    EXPECT(trim_typed_address(LR"(")") == LR"(")");
    EXPECT(trim_typed_address(L"").empty());

    EXPECT(is_unc_server_root(LR"(\\server)"));
    EXPECT(is_unc_server_root(LR"(\\server\)"));
    EXPECT(!is_unc_server_root(LR"(\\server\share)"));
    EXPECT(!is_unc_server_root(LR"(\\)"));
    EXPECT(!is_unc_server_root(LR"(\\.\pipe)"));
    EXPECT(!is_unc_server_root(LR"(\\?\C:)"));
    EXPECT(!is_unc_server_root(LR"(C:\)"));

    EXPECT(unc_server_of_share_root(LR"(\\server\share)") == LR"(\\server)");
    EXPECT(unc_server_of_share_root(LR"(\\server\share\)") == LR"(\\server)");
    EXPECT(unc_server_of_share_root(LR"(\\server\share\dir)").empty());
    EXPECT(unc_server_of_share_root(LR"(\\server)").empty());
    EXPECT(unc_server_of_share_root(LR"(C:\share)").empty());
    EXPECT(unc_server_of_share_root(LR"(\\?\UNC)").empty());
}

void test_filesystem_parent_and_leaf() {
    EXPECT(filesystem_parent(LR"(C:\a\b)") == LR"(C:\a)");
    EXPECT(filesystem_parent(LR"(C:\a\b\\)") == LR"(C:\a)");
    EXPECT(filesystem_parent(LR"(C:\a)") == LR"(C:\)");
    EXPECT(filesystem_parent(LR"(C:\)").empty());
    EXPECT(filesystem_parent(LR"(C:)").empty());
    EXPECT(filesystem_parent(LR"(\\srv\share\a\b)") == LR"(\\srv\share\a)");
    EXPECT(filesystem_parent(LR"(\\srv\share\a)") == LR"(\\srv\share)");
    // A share root's server is not a filesystem directory.
    EXPECT(filesystem_parent(LR"(\\srv\share)").empty());
    EXPECT(filesystem_parent(LR"(\\srv\share\)").empty());
    EXPECT(filesystem_parent(LR"(\\srv)").empty());
    EXPECT(filesystem_parent(L"relative").empty());

    EXPECT(filesystem_leaf(LR"(C:\a\b)") == L"b");
    EXPECT(filesystem_leaf(LR"(C:\a\b\\)") == L"b");
    EXPECT(filesystem_leaf(LR"(C:\file.txt)") == L"file.txt");
    EXPECT(filesystem_leaf(LR"(C:\)").empty());
    EXPECT(filesystem_leaf(LR"(\\srv\share\a)") == L"a");
}
} // namespace
int main() {
    test_navigation_request_identity();
    test_history_steps_from_a_pending_target();
    test_typed_address_text();
    test_filesystem_parent_and_leaf();
    return panedock::test::summary("core_navigation_test");
}
