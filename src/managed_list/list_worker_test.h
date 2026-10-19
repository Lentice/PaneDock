#pragma once
#include "managed_list/list_worker.h"

namespace panedock::managed_list::testing {
// Opt-in harness seam. Production never supplies a gate. All waits, locks and
// worker code remain in list_worker.cpp, with the production capacity/queue.
struct ReadGate final {
    HANDLE entered;
    HANDLE release;
    HANDLE backpressure;
    const std::size_t batches;
    explicit ReadGate(std::size_t result_batches = 0);
    ~ReadGate();
    ReadGate(const ReadGate&) = delete;
    ReadGate& operator=(const ReadGate&) = delete;
};
struct Counts final {
    std::size_t workers{}, running{}, pending{}, batches{};
};
std::unique_ptr<Source> make_blocked_source(std::shared_ptr<ReadGate> gate) noexcept;
bool inject_overflow(Source& source) noexcept;
bool inject_watch_failure(Source& source, DWORD error) noexcept;
Counts counts() noexcept; // UI/test caller only, like subscription creation
struct RetiredReadReport final {
    std::size_t observed{}, exited{}, cancel_requests{}, cancel_accepted{};
    std::size_t reads_returned{}, reads_aborted{};
    DWORD last_cancel_error{}, last_read_error{};
};
struct ReadObservation;
// Capture before navigation so a promptly exiting worker cannot erase evidence.
// Release the observation before retrying: it retains the captured bounded slots.
std::shared_ptr<ReadObservation> observe_running_reads() noexcept;
RetiredReadReport finish_read_observation(const std::shared_ptr<ReadObservation>& observation,
                                        DWORD timeout) noexcept;
// Only for test cleanup, after destroying all sources and releasing gates.
bool wait_for_workers(DWORD timeout) noexcept;
}
