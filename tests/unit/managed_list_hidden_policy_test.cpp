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
    // "File name extensions" off: display text drops the extension, never a
    // folder's suffix, and never leaves a dot-leading name empty.
    EXPECT(display_leaf(L"report.docx", false, true) == L"report");
    EXPECT(display_leaf(L"archive.tar.gz", false, true) == L"archive.tar");
    EXPECT(display_leaf(L"report.docx", false, false) == L"report.docx");
    EXPECT(display_leaf(L"v1.2", true, true) == L"v1.2");
    EXPECT(display_leaf(L".gitignore", false, true) == L".gitignore");
    EXPECT(display_leaf(L"README", false, true) == L"README");
    return test::summary("managed_list_hidden_policy");
}
