#include "unit/test_pane_host.h"
#include "unit/test_util.h"
#include "managed_list/list_worker_test.h"
#include <array>
#include <iostream>
#include <shellapi.h>

using namespace panedock;
namespace ml = managed_list;
namespace {
struct Fixture {
    test::TestPaneHost host;
    core::PaneState state{"pane", {{"tab", {L"C:\\", {}, {}}, {}, {}, true, {}, 0}}, "tab"};
    app_shell::Pane pane;
    HWND parent{};
    bool realized{};
    explicit Fixture(std::unique_ptr<ml::Source> source = {}) {
        pane.set_host(&host); pane.set_reentry_guard(&host.guard);
        pane.bind(&state);
        EXPECT(app_shell::Pane::register_window_class(GetModuleHandleW(nullptr)));
        parent = CreateWindowExW(0, L"STATIC", L"capacity test", 0,
            0, 0, 800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        EXPECT(parent != nullptr);
        EXPECT(pane.create(parent, 0));
        pane.bind(&state);
        pane.use_managed_list(std::move(source));
    }
    ~Fixture() { pane.destroy(); if (parent) DestroyWindow(parent); }
    void navigate(core::ShellLocation location) {
        const auto result = realized
            ? pane.navigate_to(location)
            : pane.realize({0, 0, 640, 420}, location);
        EXPECT(SUCCEEDED(result));
        realized = SUCCEEDED(result);
    }
};
constexpr std::size_t kWorkers = ml::kMaxSubscriptions + ml::kMaxRetiredWorkers;
const core::ShellLocation blocked{L"\\\\pd226-controlled\\share\\blocked", L"known-folder-identity", L"Z:\\fallback"};
void complete(app_shell::Pane& pane) {
    const auto deadline = GetTickCount64() + 10000;
    while (pane.list_snapshot().phase == ml::Phase::loading && GetTickCount64() < deadline) {
        EXPECT(WaitForSingleObject(pane.content_event(), 10000) == WAIT_OBJECT_0);
        pane.process_content_results();
    }
    EXPECT(pane.list_snapshot().phase != ml::Phase::loading);
}
void saturation_and_shutdown() {
    std::array<std::shared_ptr<ml::testing::ReadGate>, 1> gates;
    Fixture other;
    {
        gates[0] = std::make_shared<ml::testing::ReadGate>();
        Fixture slow(ml::testing::make_blocked_source(gates[0]));
        slow.navigate(blocked);
        EXPECT(WaitForSingleObject(gates[0]->entered, 10000) == WAIT_OBJECT_0);
        // An unrelated pane finishes real local work while a real worker is held.
        wchar_t directory[MAX_PATH]{}; GetCurrentDirectoryW(MAX_PATH, directory);
        const core::ShellLocation local{directory, {}, {}};
        other.navigate(local); complete(other.pane);
        EXPECT(other.pane.list_snapshot().phase == ml::Phase::complete);
        // This pane can leave the blocked read for a different local destination.
        slow.navigate(local); complete(slow.pane);
        EXPECT(slow.pane.list_snapshot().request.location == local);
        // Drop the other idle subscription, then stop/reclaim every unblocked
        // worker before the saturation scenario. No timing-based settling sleep.
        other.pane.destroy(); slow.pane.destroy();
    }
    SetEvent(gates[0]->release);
    EXPECT(ml::testing::wait_for_workers(10000));
    {
        gates[0] = std::make_shared<ml::testing::ReadGate>();
        Fixture slow(ml::testing::make_blocked_source(gates[0]));
        slow.navigate(blocked);
        EXPECT(WaitForSingleObject(gates[0]->entered, 10000) == WAIT_OBJECT_0);
        // Each obsolete read remains genuinely blocked, first filling the
        // retired reserve, then live slots. Successors use the same production
        // acquisition/cancellation path; saturation needs live+retired exhaustion.
        for (std::size_t i = 1; i < kWorkers; ++i) {
            // A shared gate can acknowledge successive worker entries by reset.
            ResetEvent(gates[0]->entered);
            slow.pane.refresh_view();
            EXPECT(WaitForSingleObject(gates[0]->entered, 10000) == WAIT_OBJECT_0);
            EXPECT(slow.pane.list_snapshot().phase == ml::Phase::loading);
        }
        const auto before = GetTickCount64();
        for (int i = 0; i < 1000; ++i) {
            slow.pane.refresh_view();
            EXPECT(slow.pane.list_snapshot().phase == ml::Phase::error);
            EXPECT(slow.pane.list_snapshot().error == ERROR_NO_SYSTEM_RESOURCES);
            EXPECT(slow.pane.list_snapshot().request.location == blocked);
        }
        auto counts = ml::testing::counts();
        EXPECT(counts.workers == kWorkers && counts.running == kWorkers);
        EXPECT(counts.pending == 0 && counts.batches == 0);
        std::cout << "saturation workers=" << counts.workers << " running=" << counts.running
                  << " pending=" << counts.pending << " batches=" << counts.batches << " retries=1000\n";
        EXPECT(GetTickCount64() - before < 2000);
        Fixture exhausted;
        exhausted.navigate({L"C:\\", {}, {}});
        EXPECT(exhausted.pane.list_snapshot().error == ERROR_NO_SYSTEM_RESOURCES);
        // Capacity recovery requires an explicit retry; no idle timer/spin.
        SetEvent(gates[0]->release);
        EXPECT(ml::testing::wait_for_workers(10000));
        slow.pane.refresh_view(); complete(slow.pane);
        EXPECT(slow.pane.list_snapshot().error == ERROR_BAD_NETPATH);
        EXPECT(slow.pane.list_snapshot().request.location == blocked);
        EXPECT(slow.pane.active_tab()->location.parsing_name == L"C:\\");
        // Late failures from retired workers never replace the new destination.
        slow.navigate({L"C:\\", {}, {}}); complete(slow.pane);
        EXPECT(slow.pane.list_snapshot().request.location.parsing_name == L"C:\\");
    }
    EXPECT(ml::testing::wait_for_workers(10000));
    // Destruction must finish with all eight OS-read substitutes still blocked.
    auto gate = std::make_shared<ml::testing::ReadGate>();
    {
        Fixture slow(ml::testing::make_blocked_source(gate)); slow.navigate(blocked);
        EXPECT(WaitForSingleObject(gate->entered, 10000) == WAIT_OBJECT_0);
        for (std::size_t i = 1; i < kWorkers; ++i) {
            ResetEvent(gate->entered); slow.pane.refresh_view();
            EXPECT(WaitForSingleObject(gate->entered, 10000) == WAIT_OBJECT_0);
        }
        auto observation = ml::testing::observe_running_reads();
        slow.host.shutting_down = true;
        const auto before = GetTickCount64(); slow.pane.destroy();
        EXPECT(GetTickCount64() - before < 1000);
        EXPECT(slow.pane.content_event() == nullptr);
        EXPECT(slow.pane.list_snapshot().phase == ml::Phase::idle);
        EXPECT(ml::testing::counts().running == kWorkers);
        const auto retained = ml::testing::finish_read_observation(observation, 0);
        EXPECT(retained.observed == kWorkers && retained.exited == 0);
        // Older reads were already cancelled before capture; only the
        // current source receives new requests during destroy().
        EXPECT(retained.cancel_requests >= 1 && retained.reads_returned == 0);
        observation.reset();
        std::cout << "shutdown retained_workers=" << ml::testing::counts().running
                  << " destroy_ms=" << GetTickCount64()-before << " ui_event=null\n";
    }
    SetEvent(gate->release); EXPECT(ml::testing::wait_for_workers(10000));
}
// Quad layout: three idle subscriptions and one blocked read hold every live
// slot. Leaving the blocked read retires it into the reserve, so the successor
// still acquires the freed live slot instead of failing on capacity.
void retired_read_frees_live_slot() {
    auto gate = std::make_shared<ml::testing::ReadGate>();
    wchar_t directory[MAX_PATH]{}; GetCurrentDirectoryW(MAX_PATH, directory);
    const core::ShellLocation local{directory, {}, {}};
    {
        std::array<Fixture, ml::kMaxSubscriptions - 1> others;
        for (auto& other : others) {
            other.navigate(local); complete(other.pane);
            EXPECT(other.pane.list_snapshot().phase == ml::Phase::complete);
        }
        Fixture slow(ml::testing::make_blocked_source(gate));
        slow.navigate(blocked);
        EXPECT(WaitForSingleObject(gate->entered, 10000) == WAIT_OBJECT_0);
        EXPECT(ml::testing::counts().workers == ml::kMaxSubscriptions);
        slow.navigate(local); complete(slow.pane);
        EXPECT(slow.pane.list_snapshot().phase == ml::Phase::complete);
        EXPECT(slow.pane.list_snapshot().error == 0);
        EXPECT(slow.pane.list_snapshot().request.location == local);
        const auto counts = ml::testing::counts();
        EXPECT(counts.workers == ml::kMaxSubscriptions + 1 && counts.running >= 1);
        std::cout << "retired_reserve workers=" << counts.workers << " running=" << counts.running << '\n';
    }
    SetEvent(gate->release);
    EXPECT(ml::testing::wait_for_workers(10000));
}
void restored_identity_and_unc_parent() {
    auto gate = std::make_shared<ml::testing::ReadGate>();
    SetEvent(gate->release);
    {
        Fixture f(ml::testing::make_blocked_source(gate));
        f.state.tabs[0].location = blocked;
        f.state.tabs[0].history = {{L"C:\\", {}, {}}, blocked};
        f.state.tabs[0].history_index = 1;
        const auto original = f.state.tabs[0];
        f.navigate(blocked); complete(f.pane);
        EXPECT(f.pane.list_snapshot().error == ERROR_BAD_NETPATH);
        f.pane.refresh_view(); complete(f.pane);
        EXPECT(f.pane.list_snapshot().request.location == blocked);
        EXPECT(f.state.tabs[0].location == original.location);
        EXPECT(f.state.tabs[0].history == original.history);
        EXPECT(f.state.tabs[0].history_index == original.history_index);
        EXPECT(SUCCEEDED(f.pane.navigate_up_one_level()));
        EXPECT(f.pane.list_snapshot().request.location.parsing_name == L"\\\\pd226-controlled\\share");
        // Up from a share root now lists its server through the Shell
        // namespace (managed_list_open_test), which would resolve this fake
        // server name over the network on the real worker; not followed here.
    }
    EXPECT(ml::testing::wait_for_workers(10000));
}
void backpressure_and_late_failure() {
    auto gate = std::make_shared<ml::testing::ReadGate>(20);
    {
        Fixture f(ml::testing::make_blocked_source(gate)); f.navigate(blocked);
        EXPECT(WaitForSingleObject(gate->entered, 10000) == WAIT_OBJECT_0);
        SetEvent(gate->release);
        EXPECT(WaitForSingleObject(gate->backpressure, 10000) == WAIT_OBJECT_0);
        const auto counts = ml::testing::counts();
        EXPECT(counts.batches == ml::kQueuedBatches && counts.pending == 0);
        std::cout << "backpressure queued_batches=" << counts.batches << " pending=" << counts.pending << '\n';
        EXPECT(f.pane.list_snapshot().rows.empty());
        f.pane.process_content_results();
        EXPECT(f.pane.list_snapshot().rows.size() <= ml::kBatchRows * ml::kQueuedBatches);
        f.navigate({L"C:\\", {}, {}}); complete(f.pane);
        EXPECT(f.pane.list_snapshot().request.location.parsing_name == L"C:\\");
        EXPECT(f.pane.list_snapshot().error == 0);
    }
    EXPECT(ml::testing::wait_for_workers(10000));
}
// Separate opt-in integration probe: never labels a fast failure a slow PASS.
int network_probe(const wchar_t* path) {
    Fixture remote, other;
    const core::ShellLocation target{path, L"probe-identity", L"Z:\\probe-fallback"};
    const auto start = GetTickCount64(); remote.navigate(target);
    const auto acknowledgement = GetTickCount64() - start;
    std::cout << "request_ack_ms=" << acknowledgement << '\n';
    // Observe a pending read for up to 5 seconds. This wait belongs to the
    // measurement executable, not the product's UI or worker idle path.
    const DWORD wait = remote.pane.content_event()
        ? WaitForSingleObject(remote.pane.content_event(), 5000) : WAIT_FAILED;
    std::cout << "observation_ms=" << GetTickCount64()-start << " wait=" << wait << '\n';
    if (wait == WAIT_OBJECT_0) remote.pane.process_content_results();
    std::cout << "phase=" << static_cast<int>(remote.pane.list_snapshot().phase)
              << " error=" << remote.pane.list_snapshot().error << '\n';
    const bool slow = wait == WAIT_TIMEOUT;
    wchar_t local[MAX_PATH]{}; GetCurrentDirectoryW(MAX_PATH, local);
    for (int i = 0; i < 30; ++i) {
        const auto before = GetTickCount64(); other.navigate({local, {}, {}});
        std::cout << "other_ack_ms[" << i << "]=" << GetTickCount64()-before << '\n';
        complete(other.pane);
    }
    auto observation = ml::testing::observe_running_reads();
    const auto leave = GetTickCount64(); remote.navigate({local, {}, {}});
    std::cout << "leave_ack_ms=" << GetTickCount64()-leave << '\n';
    complete(remote.pane);
    // Only retired workers are waited on; the two current local sources stay
    // alive. This is a bounded harness observation, never a product UI wait.
    const auto cancelled = GetTickCount64();
    const auto retired = ml::testing::finish_read_observation(observation, 10000);
    std::cout << "retired_observation_ms=" << GetTickCount64()-cancelled
              << " observed=" << retired.observed << " exited=" << retired.exited
              << " cancel_requests=" << retired.cancel_requests
              << " cancel_accepted=" << retired.cancel_accepted
              << " last_cancel_error=" << retired.last_cancel_error
              << " reads_returned=" << retired.reads_returned
              << " reads_aborted=" << retired.reads_aborted
              << " last_read_error=" << retired.last_read_error << '\n';
    observation.reset();
    remote.navigate(target); remote.pane.refresh_view();
    EXPECT(remote.pane.list_snapshot().request.location == target);
    const auto close = GetTickCount64(); remote.pane.destroy(); other.pane.destroy();
    std::cout << "shutdown_ms=" << GetTickCount64()-close << '\n';
    const auto cleanup = GetTickCount64();
    const bool workers_exited = ml::testing::wait_for_workers(10000);
    std::cout << "cleanup_wait_ms=" << GetTickCount64()-cleanup
              << " workers_exited=" << workers_exited
              << " retained_workers=" << ml::testing::counts().workers << '\n';
    std::cout << (slow ? "INCOMPLETE: pending UNC observed; desktop/mapping/cancellation evidence still required.\n"
                      : "INCOMPLETE: no sustained slow read observed; fast completion is not slow-network PASS.\n");
    return 2;
}
}
int main() {
    int argc{};
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc == 2) {
        const int result = network_probe(argv[1]); LocalFree(argv); return result;
    }
    if (argv) LocalFree(argv);
    saturation_and_shutdown(); retired_read_frees_live_slot(); restored_identity_and_unc_parent(); backpressure_and_late_failure();
    EXPECT(ml::testing::counts().workers == 0);
    std::cout << "cleanup workers=" << ml::testing::counts().workers << '\n';
    return test::summary("managed_list_capacity");
}
