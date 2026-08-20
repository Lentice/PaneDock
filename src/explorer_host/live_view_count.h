#pragma once

#include <atomic>

namespace panedock::explorer_host {

inline std::atomic<unsigned> g_live_view_count{0};

inline unsigned live_view_count() noexcept {
    return g_live_view_count.load(std::memory_order_relaxed);
}

class LiveViewRegistration final {
public:
    LiveViewRegistration() noexcept = default;
    LiveViewRegistration(const LiveViewRegistration&) = delete;
    LiveViewRegistration& operator=(const LiveViewRegistration&) = delete;

    ~LiveViewRegistration() noexcept { reset(); }

    void mark_initialized() noexcept {
        if (!registered_) {
            registered_ = true;
            g_live_view_count.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void reset() noexcept {
        if (registered_) {
            registered_ = false;
            g_live_view_count.fetch_sub(1, std::memory_order_relaxed);
        }
    }

private:
    bool registered_{false};
};

}  // namespace panedock::explorer_host
