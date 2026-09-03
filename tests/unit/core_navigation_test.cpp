#include "core/navigation.h"
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
}  // namespace

int main() {
    test_navigation_request_identity();
    return panedock::test::summary("core_navigation");
}
