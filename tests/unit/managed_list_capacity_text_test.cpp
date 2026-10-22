#include "managed_list/capacity_text.h"
#include "unit/test_util.h"

#include <cstdint>

using namespace panedock;
using namespace panedock::managed_list;

int main() {
    constexpr std::uint64_t kGB = 1024ull * 1024 * 1024;
    // Three significant digits, matching the Explorer drive cards.
    EXPECT(capacity_size_text(0) == L"0 bytes");
    EXPECT(capacity_size_text(512) == L"512 bytes");
    EXPECT(capacity_size_text(kGB * 304) == L"304 GB");
    EXPECT(capacity_size_text(kGB * 175) == L"175 GB");
    EXPECT(capacity_size_text(static_cast<std::uint64_t>(28.8 * kGB)) == L"28.8 GB");
    EXPECT(capacity_size_text(kGB * 1024 * 3 / 2) == L"1.50 TB");
    EXPECT(capacity_free_text(kGB * 608, kGB * 931) == L"608 GB free of 931 GB");

    // Red from 90% used; unknown or inconsistent values never turn red.
    EXPECT(!capacity_nearly_full(kGB * 100, kGB * 304));
    EXPECT(capacity_nearly_full(static_cast<std::uint64_t>(28.8 * kGB), kGB * 304));
    EXPECT(capacity_nearly_full(kGB * 10, kGB * 100));
    EXPECT(!capacity_nearly_full(kGB * 11, kGB * 100));
    EXPECT(!capacity_nearly_full(0, 0));
    EXPECT(!capacity_nearly_full(kGB * 2, kGB));
    return test::summary("managed_list_capacity_text");
}
