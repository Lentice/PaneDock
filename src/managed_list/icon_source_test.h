#pragma once
#include "managed_list/icon_source.h"
namespace panedock::managed_list::testing {
// Controlled provider pause inside the production icon worker/queues. No Shell
// call, HWND, callback or UI-owned data enters the worker.
struct IconGate final {
    HANDLE entered{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HANDLE release{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HANDLE backpressure{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    ~IconGate();
};
struct IconCounts final { std::size_t workers{}, running{}, pending{}, results{}; };
std::unique_ptr<IconSource> make_gated_icons(std::shared_ptr<IconGate> gate) noexcept;
IconCounts icon_counts() noexcept;
bool wait_for_icon_workers(DWORD timeout) noexcept;
}
