#include "unit/test_util.h"

#include "explorer_host/live_view_count.h"

int main() {
    using panedock::explorer_host::LiveViewRegistration;
    using panedock::explorer_host::live_view_count;

    EXPECT(live_view_count() == 0);

    {
        LiveViewRegistration view;
        view.mark_initialized();
        EXPECT(live_view_count() == 1);
        view.reset();
        EXPECT(live_view_count() == 0);
    }

    {
        LiveViewRegistration view;
        view.mark_initialized();
        view.reset();
        view.reset();
        EXPECT(live_view_count() == 0);
    }

    return panedock::test::summary("explorer_host_lifetime_check");
}
