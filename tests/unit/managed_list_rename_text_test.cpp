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
    // Explorer with "Hide extensions for known file types".
    EXPECT(hidden_extension(L"report", L"report.txt", false) == L".txt");
    EXPECT(hidden_extension(L"report.final", L"report.final.txt", false) == L".txt");
    EXPECT(hidden_extension(L"report.txt", L"report.txt", false).empty()); // shown
    EXPECT(hidden_extension(L"report", L"report.txt", true).empty()); // folder / namespace
    EXPECT(hidden_extension(L"Display", L"other.txt", false).empty());
    EXPECT(hidden_extension(L"a", L"a.tar.gz", false).empty()); // not a single extension
    EXPECT(restore_hidden_extension(L"notes", L".txt") == L"notes.txt");
    EXPECT(restore_hidden_extension(L"notes.TXT", L".txt") == L"notes.TXT"); // typed
    EXPECT(restore_hidden_extension(L"notes.md", L".txt") == L"notes.md.txt");
    EXPECT(restore_hidden_extension(L"notes.md", L"") == L"notes.md");
    EXPECT(restore_hidden_extension(L"", L".txt").empty());
    return test::summary("managed_list_rename_text");
}
