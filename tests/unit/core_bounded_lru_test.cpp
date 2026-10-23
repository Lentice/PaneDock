#include "core/bounded_lru.h"
#include "unit/test_util.h"

#include <string>

using panedock::core::BoundedLru;

namespace {

void admission_evicts_the_oldest_first() {
    BoundedLru<std::string> cache{10};
    EXPECT(cache.admit("a", 4));
    EXPECT(cache.admit("b", 4));
    EXPECT(cache.admit("c", 4)); // "a" goes
    auto stats = cache.stats();
    EXPECT(stats.bytes == 8 && stats.entries == 2 && stats.evictions == 1);
    EXPECT(!cache.take([](const std::string& value) { return value == "a"; }));
    EXPECT(cache.newest() && *cache.newest() == "c");
}

void an_oversized_value_is_refused_without_evicting() {
    BoundedLru<std::string> cache{10};
    EXPECT(cache.admit("a", 6));
    EXPECT(!cache.admit("huge", 11));
    const auto stats = cache.stats();
    EXPECT(stats.entries == 1 && stats.bytes == 6 && stats.evictions == 0);
}

void take_is_a_hit_and_releases_its_bytes() {
    BoundedLru<std::string> cache{10};
    EXPECT(cache.admit("a", 3));
    EXPECT(cache.admit("b", 3));
    const auto taken = cache.take([](const std::string& value) { return value == "a"; });
    EXPECT(taken == std::optional<std::string>{"a"});
    const auto stats = cache.stats();
    EXPECT(stats.hits == 1 && stats.bytes == 3 && stats.entries == 1);
}

void erase_if_is_not_an_eviction() {
    BoundedLru<std::string> cache{10};
    EXPECT(cache.admit("keep", 2));
    EXPECT(cache.admit("drop", 2));
    cache.erase_if([](const std::string& value) { return value == "drop"; });
    const auto stats = cache.stats();
    EXPECT(stats.entries == 1 && stats.bytes == 2 && stats.evictions == 0);
}

void a_lower_limit_evicts_at_once() {
    BoundedLru<std::string> cache{10};
    EXPECT(cache.admit("a", 4));
    EXPECT(cache.admit("b", 4));
    cache.set_limit(5);
    auto stats = cache.stats();
    EXPECT(stats.entries == 1 && stats.bytes == 4 && stats.limit == 5 && stats.evictions == 1);
    cache.set_limit(0);
    stats = cache.stats();
    EXPECT(stats.entries == 0 && stats.bytes == 0 && !cache.newest());
}

}  // namespace

int main() {
    admission_evicts_the_oldest_first();
    an_oversized_value_is_refused_without_evicting();
    take_is_a_hit_and_releases_its_bytes();
    erase_if_is_not_an_eviction();
    a_lower_limit_evicts_at_once();
    return panedock::test::summary("core_bounded_lru");
}
