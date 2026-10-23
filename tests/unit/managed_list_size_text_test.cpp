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

    // Status bar sizes follow StrFormatByteSize: three significant digits, truncated.
    EXPECT(byte_size_text(0) == L"0 bytes");
    EXPECT(byte_size_text(1023) == L"1023 bytes");
    EXPECT(byte_size_text(1024) == L"1.00 KB");
    EXPECT(byte_size_text(1536) == L"1.50 KB");
    EXPECT(byte_size_text(1535) == L"1.49 KB");
    EXPECT(byte_size_text(10 * 1024 + 512) == L"10.5 KB");
    EXPECT(byte_size_text(123 * 1024 + 900) == L"123 KB");
    EXPECT(byte_size_text(1000 * 1024) == L"0.97 MB");
    EXPECT(byte_size_text(2600000) == L"2.47 MB");
    EXPECT(byte_size_text(5ull << 30) == L"5.00 GB");
    EXPECT(byte_size_text(~0ull) == L"15.9 EB");

    // Explorer's Length column is hh:mm:ss.
    EXPECT(duration_text(0) == L"00:00:00");
    EXPECT(duration_text(205ull * 10000000 + 9999999) == L"00:03:25");
    EXPECT(duration_text((3600ull + 330) * 10000000) == L"01:05:30");
    EXPECT(duration_text(100ull * 3600 * 10000000) == L"100:00:00");

    EXPECT(item_count_text(0) == L"0 items");
    EXPECT(item_count_text(1) == L"1 item");
    EXPECT(item_count_text(12345) == L"12,345 items");
    EXPECT(selection_status_text(1, 1536) == L"1 item selected  1.50 KB");
    EXPECT(selection_status_text(3, std::nullopt) == L"3 items selected");
    return test::summary("managed_list_size_text");
}
