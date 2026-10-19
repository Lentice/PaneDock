#include "managed_list/list_worker.h"
#include "managed_list/list_worker_test.h"
#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace panedock::managed_list {
namespace {
struct State final {
    std::mutex mutex;
    std::condition_variable changed;
    HANDLE ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool stop{};
    bool running{};
    HANDLE thread{};
    std::size_t cancel_requests{}, cancel_accepted{};
    DWORD last_cancel_error{}, first_read_error{};
    bool first_read_returned{};
    std::optional<std::uint64_t> observed_serial;
    bool observed_read_returned{};
    DWORD observed_read_error{};
    std::shared_ptr<testing::ReadGate> gate;
    std::uint64_t serial{};
    std::uint64_t running_serial{};
    std::optional<Request> pending;
    std::deque<Batch> results;
    std::optional<Batch> emergency;
    ~State() { if (thread) CloseHandle(thread); if (ready) CloseHandle(ready); }
};
// UI creates subscriptions on its STA. A blocked abandoned worker retains its
// slot, resources and event; reopening cannot create an unbounded thread leak.
std::array<std::weak_ptr<State>, kMaxSubscriptions> slots;

bool publish(const std::shared_ptr<State>& state, std::uint64_t serial,
             Batch batch) {
    std::unique_lock lock(state->mutex);
    if (state->gate && state->results.size() == kQueuedBatches)
        SetEvent(state->gate->backpressure);
    state->changed.wait(lock, [&] {
        return state->stop || serial != state->serial ||
            state->results.size() < kQueuedBatches;
    });
    if (state->stop || serial != state->serial) return false;
    state->results.push_back(std::move(batch));
    SetEvent(state->ready);
    return true;
}
bool interested(const std::shared_ptr<State>& state, std::uint64_t serial) {
    std::lock_guard lock(state->mutex);
    return !state->stop && state->serial == serial;
}
struct FindHandle final {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~FindHandle() { if (value != INVALID_HANDLE_VALUE) FindClose(value); }
};
void enumerate(const std::shared_ptr<State>& state, std::uint64_t serial,
               const Request& request) {
    Batch batch{request, {}, Phase::loading, 0};
    const auto& path = request.location.parsing_name;
    // All provider/drive calls stay here, including disconnected mappings.
    // Reject device namespaces; this backend accepts ordinary UNC share paths.
    if (!is_filesystem_location(path) || path.size() > kMaxPath ||
        path.starts_with(L"\\\\.\\") || path.find_first_of(L"*?") != std::wstring::npos ||
        (path.starts_with(L"\\\\") && path.find(L'\\', 2) == std::wstring::npos)) {
        batch.phase = Phase::error; batch.error = ERROR_NOT_SUPPORTED;
        publish(state, serial, std::move(batch)); return;
    }
    if (state->gate && path.starts_with(L"\\\\pd226-controlled\\share")) {
        // A controlled OS-read substitute, on the real bounded worker. The
        // gate owns kernel events; no callback can reach Pane or UI state.
        SetEvent(state->gate->entered);
        WaitForSingleObject(state->gate->release, INFINITE);
        for (std::size_t n = 0; n < state->gate->batches; ++n) {
            batch.rows.assign(kBatchRows, {L"controlled", 0, false});
            if (!publish(state, serial, std::move(batch))) return;
            batch = {request, {}, Phase::loading, 0};
        }
        batch.phase = Phase::error; batch.error = ERROR_BAD_NETPATH;
        publish(state, serial, std::move(batch)); return;
    }
    if (!interested(state, serial)) return;
    auto pattern = path;
    if (pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';
    WIN32_FIND_DATAW data{};
    FindHandle find{FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                                    FindExSearchNameMatch, nullptr, 0)};
    const DWORD first_error = find.value == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    {
        std::lock_guard lock(state->mutex);
        state->first_read_returned = true;
        state->first_read_error = first_error;
        if (state->observed_serial == serial) {
            state->observed_read_returned = true;
            state->observed_read_error = first_error;
        }
    }
    if (find.value == INVALID_HANDLE_VALUE) {
        batch.error = first_error;
        if (batch.error == ERROR_FILE_NOT_FOUND) {
            const DWORD attr = GetFileAttributesW(path.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                batch.phase = Phase::empty; batch.error = 0;
            } else batch.phase = Phase::error;
        } else batch.phase = Phase::error;
        publish(state, serial, std::move(batch)); return;
    }
    std::size_t count = 0;
    for (;;) {
        if (!interested(state, serial)) return;
        std::wstring name(data.cFileName);
        if (name != L"." && name != L"..") {
            if (count == kMaxRows) { batch.phase = Phase::limited; break; }
            if (name.size() > kMaxName) {
                batch.phase = Phase::error; batch.error = ERROR_BUFFER_OVERFLOW; break;
            }
            batch.rows.push_back({std::move(name),
                (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow,
                (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
            ++count;
            if (batch.rows.size() == kBatchRows) {
                if (!publish(state, serial, std::move(batch))) return;
                batch = {request, {}, Phase::loading, 0};
            }
        }
        if (!FindNextFileW(find.value, &data)) {
            batch.error = GetLastError();
            if (batch.error == ERROR_NO_MORE_FILES) {
                batch.error = 0; batch.phase = count ? Phase::complete : Phase::empty;
            } else batch.phase = Phase::error;
            break;
        }
    }
    publish(state, serial, std::move(batch));
}
void run(std::shared_ptr<State> state) noexcept {
    for (;;) {
        std::optional<Request> request;
        std::uint64_t serial{};
        {
            std::unique_lock lock(state->mutex);
            state->changed.wait(lock, [&] { return state->stop || state->pending.has_value(); });
            if (state->stop) return;
            request = std::move(state->pending); state->pending.reset();
            serial = state->serial;
            state->running = true;
            state->running_serial = serial;
            state->first_read_returned = false;
            state->first_read_error = 0;
        }
        try { enumerate(state, serial, *request); }
        catch (...) {
            try { publish(state, serial, {*request, {}, Phase::error, ERROR_NOT_ENOUGH_MEMORY}); }
            catch (...) { // No allocation is needed to expose a terminal failure.
                std::lock_guard lock(state->mutex);
                if (!state->stop && state->serial == serial) {
                    state->results.clear();
                    state->emergency.emplace(Batch{std::move(*request), {},
                        Phase::error, ERROR_NOT_ENOUGH_MEMORY});
                    SetEvent(state->ready);
                }
                // Wait for a replacement or shutdown; never repeatedly signal.
            }
        }
        {
            std::lock_guard lock(state->mutex);
            state->running = false;
        }
    }
}
std::shared_ptr<State> acquire(std::shared_ptr<testing::ReadGate> gate = {}) {
    for (auto& slot : slots) {
        if (!slot.expired()) continue;
        auto state = std::make_shared<State>();
        if (!state->ready) return {};
        state->gate = std::move(gate);
        slot = state;
        std::thread worker(run, state);
        if (!DuplicateHandle(GetCurrentProcess(), worker.native_handle(),
            GetCurrentProcess(), &state->thread, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            // Do not admit work without the owned cancellation handle. The
            // new thread has no request and can only be waiting for stop.
            {
                std::lock_guard lock(state->mutex);
                state->stop = true;
                state->changed.notify_all();
            }
            worker.detach();
            return {};
        }
        worker.detach();
        return state;
    }
    return {};
}
// Called under the worker lock. Best effort only: cancellation can race entry
// into a system call or be unsupported. The slot remains occupied until exit.
void invalidate(State& state, bool stop) noexcept {
    ++state.serial;
    state.stop = stop;
    state.pending.reset(); state.results.clear(); state.emergency.reset();
    ResetEvent(state.ready);
    if (state.running && state.thread) {
        ++state.cancel_requests;
        const BOOL accepted = CancelSynchronousIo(state.thread);
        state.last_cancel_error = accepted ? ERROR_SUCCESS : GetLastError();
        state.cancel_accepted += accepted != FALSE;
    }
    state.changed.notify_all();
}
class LocalSource final : public Source {
    std::shared_ptr<State> state_;
    std::shared_ptr<testing::ReadGate> gate_;
  public:
    explicit LocalSource(std::shared_ptr<State> state) : state_(std::move(state)), gate_(state_->gate) {}
    ~LocalSource() override {
        // Never join an OS call. Worker owns only State and FindHandle, and
        // exits after its current OS call returns. State is freed on last use.
        if (state_) { std::lock_guard lock(state_->mutex); invalidate(*state_, true); }
    }
    bool submit(Request request) noexcept override try {
        if (request.location.parsing_name.size() > kMaxPath ||
            request.location.fallback_path.size() > kMaxPath ||
            request.location.known_folder_identity.size() > kMaxPath ||
            request.binding.group_id.size() > 256 || request.binding.tab_id.size() > 256)
            return false;
        bool retire = false;
        if (state_) {
            std::lock_guard lock(state_->mutex);
            // Never queue a successor behind an obsolete OS call. Retiring
            // counts against the SAME four-slot cap, even after Pane dies.
            retire = state_->running;
            if (retire) invalidate(*state_, true);
        }
        if (retire) state_.reset();
        if (!state_) state_ = acquire(gate_);
        if (!state_) return false;
        std::lock_guard lock(state_->mutex);
        if (state_->stop) return false;
        ++state_->serial; state_->results.clear(); state_->emergency.reset(); ResetEvent(state_->ready);
        state_->pending = std::move(request);
        state_->changed.notify_all(); return true;
    } catch (...) { return false; }
    std::optional<Batch> take() noexcept override {
        if (!state_) return {};
        std::lock_guard lock(state_->mutex);
        if (state_->emergency) {
            auto result = std::move(state_->emergency);
            state_->emergency.reset(); ResetEvent(state_->ready);
            return result;
        }
        if (state_->results.empty()) {
            ResetEvent(state_->ready);
            return {};
        }
        auto result = std::move(state_->results.front()); state_->results.pop_front();
        if (state_->results.empty()) ResetEvent(state_->ready);
        state_->changed.notify_all(); return result;
    }
    HANDLE event() const noexcept override { return state_ ? state_->ready : nullptr; }
    void cancel() noexcept override {
        if (state_) { std::lock_guard lock(state_->mutex); invalidate(*state_, false); }
    }
};
std::unique_ptr<Source> make_source(std::shared_ptr<testing::ReadGate> gate = {}) {
    auto state = acquire(std::move(gate));
    if (!state) return {};
    try { return std::make_unique<LocalSource>(state); }
    catch (...) {
        // Allocation failure after launching must not orphan an idle worker.
        std::lock_guard lock(state->mutex);
        invalidate(*state, true);
        throw;
    }
}
} // namespace
std::unique_ptr<Source> make_local_source() noexcept try {
    return make_source();
} catch (...) { return {}; }
namespace testing {
ReadGate::ReadGate(std::size_t result_batches)
    : entered(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      release(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      backpressure(CreateEventW(nullptr, TRUE, FALSE, nullptr)), batches(result_batches) {}
ReadGate::~ReadGate() { CloseHandle(entered); CloseHandle(release); CloseHandle(backpressure); }
std::unique_ptr<Source> make_blocked_source(std::shared_ptr<ReadGate> gate) noexcept try {
    if (!gate || !gate->entered || !gate->release || !gate->backpressure) return {};
    return make_source(std::move(gate));
} catch (...) { return {}; }
Counts counts() noexcept {
    Counts result;
    for (auto& slot : slots) if (auto state = slot.lock()) {
        std::lock_guard lock(state->mutex);
        ++result.workers;
        result.running += state->running;
        result.pending += state->pending.has_value();
        result.batches += state->results.size() + state->emergency.has_value();
    }
    return result;
}
struct ReadObservation final {
    std::array<std::shared_ptr<State>, kMaxSubscriptions> states;
    std::array<std::size_t, kMaxSubscriptions> cancel_requests{}, cancel_accepted{};
    DWORD count{};
};
std::shared_ptr<ReadObservation> observe_running_reads() noexcept try {
    auto observation = std::make_shared<ReadObservation>();
    for (auto& slot : slots) if (auto state = slot.lock()) {
        std::lock_guard lock(state->mutex);
        if (!state->running) continue;
        state->observed_serial = state->running_serial;
        state->observed_read_returned = state->first_read_returned;
        state->observed_read_error = state->first_read_error;
        const auto n = observation->count++;
        observation->cancel_requests[n] = state->cancel_requests;
        observation->cancel_accepted[n] = state->cancel_accepted;
        observation->states[n] = std::move(state);
    }
    return observation;
} catch (...) { return {}; }
RetiredReadReport finish_read_observation(const std::shared_ptr<ReadObservation>& observation,
                                        DWORD timeout) noexcept {
    if (!observation) return {};
    std::array<HANDLE, kMaxSubscriptions> handles{};
    RetiredReadReport report;
    DWORD count = 0;
    for (DWORD n = 0; n < observation->count; ++n) {
        const auto& state = observation->states[n];
        std::lock_guard lock(state->mutex);
        // A read that returned before submit() can reuse the existing worker;
        // never wait for that current source's idle thread to exit.
        if (state->stop && state->thread) handles[count++] = state->thread;
    }
    report.observed = observation->count;
    if (count) (void)WaitForMultipleObjects(count, handles.data(), TRUE, timeout);
    for (DWORD n = 0; n < observation->count; ++n) {
        const auto& state = observation->states[n];
        report.exited += state->thread && WaitForSingleObject(state->thread, 0) == WAIT_OBJECT_0;
        std::lock_guard lock(state->mutex);
        report.cancel_requests += state->cancel_requests - observation->cancel_requests[n];
        report.cancel_accepted += state->cancel_accepted - observation->cancel_accepted[n];
        report.reads_returned += state->observed_read_returned;
        report.reads_aborted += state->observed_read_returned && state->observed_read_error == ERROR_OPERATION_ABORTED;
        report.last_cancel_error = state->last_cancel_error;
        report.last_read_error = state->observed_read_error;
    }
    return report;
}
bool wait_for_workers(DWORD timeout) noexcept {
    // Hold State until the thread signals: weak slots cannot expire before the
    // OS thread itself exits, and no test uses an arbitrary settling sleep.
    std::array<std::shared_ptr<State>, kMaxSubscriptions> states;
    std::array<HANDLE, kMaxSubscriptions> handles{};
    DWORD count = 0;
    for (auto& slot : slots) if (auto state = slot.lock()) {
        if (!state->thread) return false;
        handles[count] = state->thread;
        states[count++] = std::move(state);
    }
    return !count || WaitForMultipleObjects(count, handles.data(), TRUE, timeout) == WAIT_OBJECT_0;
}
} // namespace testing
} // namespace panedock::managed_list
