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
// PD-210: the whole point of the ledger is that `completed` catches up to
// `latest`, because that is what makes the live view's settings and counts
// readable again. A withdrawn request must not leave `latest` stranded.
void test_a_withdrawn_request_hands_latest_back_to_the_one_in_flight() {
    NavigationLedger ledger;
    // Our slow navigation: issued, accepted by the Shell, still in flight.
    const auto slow = ledger.enqueue(0);
    EXPECT(slow != 0);
    EXPECT(ledger.in_flight());

    // The user switches again; this one fails synchronously before the Shell
    // ever accepts it, so we withdraw it.
    const auto failed = ledger.enqueue(0);
    EXPECT(failed > slow);
    EXPECT(ledger.is_latest(failed));
    ledger.withdraw(failed);

    // The withdrawn generation must no longer look like the newest request --
    // that is what stops its error overlay from covering a view which is about
    // to load successfully.
    EXPECT(!ledger.is_latest(failed));
    EXPECT(ledger.is_latest(slow));

    // And the slow navigation's completion still counts.
    EXPECT(ledger.take() == slow);
    ledger.complete(slow);
    EXPECT(!ledger.in_flight());
}

void test_a_withdrawal_with_nothing_in_flight_keeps_latest() {
    NavigationLedger ledger;
    const auto only = ledger.enqueue(0);
    ledger.withdraw(only);
    // Nothing succeeded, so the failure is still the newest thing that
    // happened: the error overlay is correct and in_flight stays true.
    EXPECT(ledger.is_latest(only));
    EXPECT(ledger.in_flight());
}

void test_withdrawing_never_consumes_the_request_in_flight() {
    NavigationLedger ledger;
    const auto first = ledger.enqueue(0);
    const auto second = ledger.enqueue(0);
    // Withdrawing the older one must not pop the queue front out from under
    // the newer one.
    ledger.withdraw(first);
    EXPECT(ledger.take() == second);
}

void test_a_shell_initiated_navigation_gets_its_own_generation() {
    NavigationLedger ledger;
    // Nothing of ours is queued, so this pending belongs to the Shell.
    EXPECT(!ledger.mark_pending());
    EXPECT(ledger.adopt_pending(ledger.begin()));
    const auto adopted = ledger.take();
    ledger.complete(adopted);
    EXPECT(!ledger.in_flight());

    // With one of ours queued, the next pending is attributed to it instead.
    (void)ledger.enqueue(0);
    EXPECT(ledger.mark_pending());
    EXPECT(!ledger.mark_pending());
}

void test_a_stale_completion_does_not_clear_in_flight() {
    NavigationLedger ledger;
    const auto first = ledger.enqueue(0);
    const auto second = ledger.enqueue(0);
    EXPECT(ledger.take() == first);
    ledger.complete(first);
    // `second` was issued after `first`, so first's completion says nothing
    // about what the view is showing now.
    EXPECT(ledger.in_flight());
    EXPECT(ledger.take() == second);
    ledger.complete(second);
    EXPECT(!ledger.in_flight());
}
}  // namespace

int main() {
    test_navigation_request_identity();
    test_a_withdrawn_request_hands_latest_back_to_the_one_in_flight();
    test_a_withdrawal_with_nothing_in_flight_keeps_latest();
    test_withdrawing_never_consumes_the_request_in_flight();
    test_a_shell_initiated_navigation_gets_its_own_generation();
    test_a_stale_completion_does_not_clear_in_flight();
    return panedock::test::summary("core_navigation");
}
