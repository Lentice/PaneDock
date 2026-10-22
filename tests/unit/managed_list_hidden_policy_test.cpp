#include "managed_list/list_worker.h"
#include "unit/test_util.h"

using namespace panedock;
using namespace panedock::managed_list;

int main() {
    constexpr DWORD hidden = FILE_ATTRIBUTE_HIDDEN;
    constexpr DWORD protected_file = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
    constexpr DWORD system_only = FILE_ATTRIBUTE_SYSTEM;

    // Explorer defaults: hidden files hidden, protected files hidden.
    const HiddenFilePolicy defaults{false, false};
    EXPECT(!defaults.hides(FILE_ATTRIBUTE_NORMAL));
    EXPECT(defaults.hides(hidden));
    EXPECT(defaults.hides(protected_file));
    EXPECT(!defaults.hides(system_only)); // the system bit alone never hides a file

    // "Show hidden files" on, protected files still hidden (the common choice).
    const HiddenFilePolicy show_hidden{true, false};
    EXPECT(!show_hidden.hides(hidden));
    EXPECT(show_hidden.hides(protected_file));

    const HiddenFilePolicy show_all{true, true};
    EXPECT(!show_all.hides(protected_file));
    EXPECT(!show_all.hides(hidden));
    return test::summary("managed_list_hidden_policy");
}
