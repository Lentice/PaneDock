#include "managed_list/size_text.h"
#include "unit/test_util.h"

using namespace panedock;
using namespace panedock::managed_list;

int main() {
    EXPECT(file_size_text(0) == L"0 KB");
    EXPECT(file_size_text(1) == L"1 KB");
    EXPECT(file_size_text(1024) == L"1 KB");
    EXPECT(file_size_text(1025) == L"2 KB");
    EXPECT(file_size_text(2473) == L"3 KB");
    EXPECT(file_size_text(1000ull * 1024) == L"1,000 KB");
    EXPECT(file_size_text(19254688ull * 1024) == L"19,254,688 KB");
    EXPECT(file_size_text(1ull << 60) == L"1,125,899,906,842,624 KB");
    return test::summary("managed_list_size_text");
}
