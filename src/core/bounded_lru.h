#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <list>
#include <optional>
#include <utility>

namespace panedock::core {

struct LruStats final {
    std::size_t bytes{}, entries{}, hits{}, evictions{}, limit{};
};

// Values held under a byte limit the caller measures, evicting the oldest
// admission first. A hit removes the value: the caller owns it again.
template <class T>
class BoundedLru final {
public:
    explicit BoundedLru(std::size_t limit) noexcept : limit_(limit) {}

    // Admits `value` measured at `bytes`, evicting from the oldest end until
    // it fits. False, and nothing evicted, when it alone exceeds the limit.
    bool admit(T value, std::size_t bytes) {
        if (bytes > limit_) return false;
        evict_until(limit_ - bytes);
        entries_.push_back({std::move(value), bytes});
        bytes_ += bytes;
        return true;
    }

    // Removes and returns the oldest value matching `predicate`; a hit.
    template <class Predicate>
    std::optional<T> take(Predicate predicate) {
        const auto found = std::find_if(entries_.begin(), entries_.end(),
            [&](const Slot& slot) { return predicate(std::as_const(slot.value)); });
        if (found == entries_.end()) return std::nullopt;
        std::optional<T> value{std::move(found->value)};
        bytes_ -= found->bytes;
        entries_.erase(found);
        ++hits_;
        return value;
    }

    // Drops every value matching `predicate`; not counted as evictions.
    template <class Predicate>
    void erase_if(Predicate predicate) {
        std::erase_if(entries_, [&](const Slot& slot) {
            if (!predicate(std::as_const(slot.value))) return false;
            bytes_ -= slot.bytes;
            return true;
        });
    }

    // The newest value, if any.
    const T* newest() const noexcept {
        return entries_.empty() ? nullptr : &entries_.back().value;
    }

    // A new limit evicts at once until the held bytes fit it.
    void set_limit(std::size_t limit) {
        limit_ = limit;
        evict_until(limit_);
    }

    LruStats stats() const noexcept {
        return {bytes_, entries_.size(), hits_, evictions_, limit_};
    }

private:
    struct Slot final {
        T value;
        std::size_t bytes{};
    };
    void evict_until(std::size_t bytes) {
        while (bytes_ > bytes && !entries_.empty()) {
            bytes_ -= entries_.front().bytes;
            entries_.pop_front();
            ++evictions_;
        }
    }

    std::list<Slot> entries_;
    std::size_t bytes_{}, hits_{}, evictions_{}, limit_{};
};

}  // namespace panedock::core
