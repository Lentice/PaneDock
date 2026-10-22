#include "managed_list/rename_text.h"
#include "unit/test_util.h"

using namespace panedock;
using namespace panedock::managed_list;

int main() {
    EXPECT(rename_selection_length(L"report.txt", false) == 6);
    EXPECT(rename_selection_length(L"report.final.txt", false) == 12);
    EXPECT(rename_selection_length(L"README", false) == 6);
    EXPECT(rename_selection_length(L".gitignore", false) == 10);
    EXPECT(rename_selection_length(L"v1.2 release", true) == 12); // folders select everything
    EXPECT(rename_selection_length(L"", false) == 0);
    EXPECT(rename_selection_length(L"測試檔案.txt", false) == 4); // CJK stem
    EXPECT(rename_selection_length(L"\U0001F600.png", false) == 2); // surrogate pair counts as two units
    return test::summary("managed_list_rename_text");
}
