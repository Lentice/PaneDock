#include "managed_list/list_worker.h"
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
    std::uint64_t serial{};
    std::optional<Request> pending;
    std::deque<Batch> results;
    std::optional<Batch> emergency;
    ~State() { if (ready) CloseHandle(ready); }
};
// UI creates subscriptions on its STA. A blocked abandoned worker retains its
// slot, resources and event; reopening cannot create an unbounded thread leak.
std::array<std::weak_ptr<State>, kMaxSubscriptions> slots;

bool publish(const std::shared_ptr<State>& state, std::uint64_t serial,
             Batch batch) {
    std::unique_lock lock(state->mutex);
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
    // PD-224 is intentionally local-only. No UI-thread path probing or Shell
    // resolution. Later namespace work has a separate backend, not a fallback
    // to display names or borrowed PIDLs.
    if (path.size() < 3 || path.size() > kMaxPath || path[1] != L':' ||
        path[2] != L'\\' || path.find_first_of(L"*?") != std::wstring::npos) {
        batch.phase = Phase::error; batch.error = ERROR_NOT_SUPPORTED;
        publish(state, serial, std::move(batch)); return;
    }
    const wchar_t root[]{path[0], L':', L'\\', 0};
    const UINT drive = GetDriveTypeW(root);
    if (drive != DRIVE_FIXED && drive != DRIVE_REMOVABLE && drive != DRIVE_RAMDISK) {
        batch.phase = Phase::error; batch.error = ERROR_NOT_SUPPORTED;
        publish(state, serial, std::move(batch)); return;
    }
    auto pattern = path;
    if (pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';
    WIN32_FIND_DATAW data{};
    FindHandle find{FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                                    FindExSearchNameMatch, nullptr, 0)};
    if (find.value == INVALID_HANDLE_VALUE) {
        batch.error = GetLastError();
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
    }
}
class LocalSource final : public Source {
    std::shared_ptr<State> state_;
  public:
    explicit LocalSource(std::shared_ptr<State> state) : state_(std::move(state)) {}
    ~LocalSource() override {
        // Never join an OS call. Worker owns only State and FindHandle, and
        // exits after its current OS call returns. State is freed on last use.
        std::lock_guard lock(state_->mutex);
        state_->stop = true; ++state_->serial;
        state_->pending.reset(); state_->results.clear(); state_->emergency.reset();
        state_->changed.notify_all();
    }
    bool submit(Request request) noexcept override {
        if (request.location.parsing_name.size() > kMaxPath ||
            request.location.fallback_path.size() > kMaxPath ||
            request.location.known_folder_identity.size() > kMaxPath ||
            request.binding.group_id.size() > 256 || request.binding.tab_id.size() > 256)
            return false;
        std::lock_guard lock(state_->mutex);
        if (state_->stop) return false;
        ++state_->serial; state_->results.clear(); state_->emergency.reset(); ResetEvent(state_->ready);
        state_->pending = std::move(request);
        state_->changed.notify_all(); return true;
    }
    std::optional<Batch> take() noexcept override {
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
    HANDLE event() const noexcept override { return state_->ready; }
    void cancel() noexcept override {
        std::lock_guard lock(state_->mutex);
        ++state_->serial; state_->pending.reset(); state_->results.clear(); state_->emergency.reset();
        ResetEvent(state_->ready); state_->changed.notify_all();
    }
};
} // namespace
std::unique_ptr<Source> make_local_source() noexcept try {
    for (auto& slot : slots) {
        if (!slot.expired()) continue;
        auto state = std::make_shared<State>();
        if (!state->ready) return {};
        auto source = std::make_unique<LocalSource>(state);
        slot = state;
        std::thread(run, state).detach();
        return source;
    }
    return {};
} catch (...) { return {}; }
} // namespace panedock::managed_list
