#pragma once

#include <atomic>

namespace panedock {

template <typename T, typename... Interfaces>
class ComRefCounted : public Interfaces... {
public:
    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining =
            references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) delete static_cast<T*>(this);
        return remaining;
    }

protected:
    ~ComRefCounted() = default;

private:
    std::atomic<ULONG> references_{1};
};

}  // namespace panedock
