#pragma once

namespace panedock::explorer_host {

// A plain scalar, not an atomic: this is our own bookkeeping, and every writer
// is ExplorerHost::initialize or ExplorerHost::destroy, both of which can only
// run on the STA thread that owns the views. The readers -- the --diagnostic
// line and the teardown asserts -- are on that same thread. See the
// single-threaded rule in AGENTS.md; the atomic in src/com_ref_counted.h stays
// because that one is released by shell32, not by us.
inline unsigned g_live_view_count{0};

inline unsigned live_view_count() noexcept { return g_live_view_count; }

class LiveViewRegistration final {
public:
    LiveViewRegistration() noexcept = default;
    LiveViewRegistration(const LiveViewRegistration&) = delete;
    LiveViewRegistration& operator=(const LiveViewRegistration&) = delete;

    ~LiveViewRegistration() noexcept { reset(); }

    void mark_initialized() noexcept {
        if (!registered_) {
            registered_ = true;
            ++g_live_view_count;
        }
    }

    void reset() noexcept {
        if (registered_) {
            registered_ = false;
            --g_live_view_count;
        }
    }

private:
    bool registered_{false};
};

}  // namespace panedock::explorer_host
