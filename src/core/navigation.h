#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace panedock::core {

struct NavigationRequest final {
    std::uint64_t generation{};
    std::string group_id;
    std::string tab_id;
};

inline bool navigation_request_matches(
    const NavigationRequest& request, std::uint64_t generation,
    std::string_view group_id, std::string_view tab_id) noexcept {
    return request.generation == generation && request.group_id == group_id &&
           request.tab_id == tab_id;
}

// The generation bookkeeping behind IExplorerBrowserEvents. The Shell gives a
// completion no way to say which navigation it answers, so start order is the
// only identity available: requests queue in the order they were issued and a
// completion takes the front one. The three counters then say what is in
// flight -- `completed` reaching `latest` is what makes the live view's view
// mode, sort and item counts trustworthy again.
//
// This lives in core because it is the part of that machinery with no COM in
// it, which is the part worth testing (see docs/testing.md).
class NavigationLedger final {
public:
    using Generation = std::uint64_t;

    // A fresh generation, not yet queued. Used for the Shell's own navigations
    // (a double-click inside the view), which we learn about after the fact.
    Generation begin() noexcept { return ++next_; }

    // Queues a request we issued. Returns the generation actually recorded, or
    // 0 if the record could not be allocated -- the caller must then treat the
    // navigation as unidentifiable rather than pretend it is queued.
    Generation enqueue(Generation generation) noexcept {
        if (generation == 0) generation = begin();
        if (generation > next_) next_ = generation;
        try {
            requests_.push_back({generation, false});
        } catch (...) {
            return 0;
        }
        latest_ = std::max(latest_, generation);
        return generation;
    }

    // Withdraws a request that failed before the Shell ever accepted it.
    // Removes that record only -- never the front one, which belongs to
    // whichever navigation is still in flight -- and hands `latest` back to the
    // newest surviving request, so that navigation's completion still counts
    // (PD-210). With nothing left in flight `latest` stays where it is: no
    // navigation succeeded, and the caller's error reporting depends on that.
    void withdraw(Generation generation) noexcept {
        for (auto request = requests_.rbegin(); request != requests_.rend();
             ++request) {
            if (request->generation != generation) continue;
            requests_.erase(std::next(request).base());
            break;
        }
        if (!requests_.empty()) latest_ = requests_.back().generation;
    }

    // The generation a just-arrived completion answers to. An empty queue means
    // the Shell navigated on its own, so mint one and adopt it as latest.
    Generation take() noexcept {
        if (requests_.empty()) {
            const Generation generation = begin();
            latest_ = std::max(latest_, generation);
            return generation;
        }
        const Generation generation = requests_.front().generation;
        requests_.pop_front();
        return generation;
    }

    // Attributes an OnNavigationPending to the oldest request that has not seen
    // one yet. Returns true when it was attributed; false means the Shell
    // started a navigation we never asked for, and the caller should
    // adopt_pending() a fresh generation for it.
    bool mark_pending() noexcept {
        for (auto& request : requests_) {
            if (request.pending_notified) continue;
            request.pending_notified = true;
            return true;
        }
        return false;
    }

    // Records a Shell-initiated navigation that mark_pending() could not place.
    // Returns false if the record could not be allocated.
    bool adopt_pending(Generation generation) noexcept {
        try {
            requests_.push_back({generation, true});
        } catch (...) {
            return false;
        }
        latest_ = generation;
        return true;
    }

    // A completion only advances `completed` when it is the newest request.
    // An older one landing after a newer was issued is stale by definition.
    void complete(Generation generation) noexcept {
        if (generation == latest_) completed_ = generation;
    }

    bool is_latest(Generation generation) const noexcept {
        return generation == latest_;
    }

    // True while a navigation has been issued but not yet answered: the live
    // view still belongs to the previous folder, so its settings and counts
    // must not be read or written.
    bool in_flight() const noexcept { return completed_ != latest_; }

    void clear() noexcept { requests_.clear(); }

private:
    struct Record final {
        Generation generation{};
        bool pending_notified{};
    };

    std::deque<Record> requests_;
    Generation next_{};
    Generation latest_{};
    Generation completed_{};
};

}  // namespace panedock::core
