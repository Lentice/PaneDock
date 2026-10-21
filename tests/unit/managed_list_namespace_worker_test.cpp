#include "managed_list/list_worker_test.h"
#include "unit/test_util.h"
#include <array>
#include <iostream>

using namespace panedock;
namespace ml = managed_list;
namespace {
ml::Request request(std::uint64_t generation = 1) {
    ml::Request value;
    value.binding.generation = generation;
    value.binding.group_id = "group";
    value.binding.tab_id = "tab";
    value.location = {L"::PD245-original", L"original-known-folder", L"C:\\never-use-fallback"};
    return value;
}
ml::Batch values(bool root = false) {
    ml::Batch batch;
    batch.phase = ml::Phase::complete;
    batch.resolved_location = core::ShellLocation{L"::canonical", L"canonical-known-folder", {}};
    batch.parent_outcome = root ? ml::ParentOutcome::root : ml::ParentOutcome::value;
    if (!root) batch.parent_location = core::ShellLocation{L"::parent", {}, {}};
    ml::Row child{L"Display name", 0, true};
    child.identity = {L"::canonical-child", true};
    child.child_location = core::ShellLocation{child.identity.key, L"child-known-folder", {}};
    batch.rows.push_back(std::move(child));
    return batch;
}
ml::Batch take(ml::Source& source) {
    EXPECT(WaitForSingleObject(source.event(), 10000) == WAIT_OBJECT_0);
    auto batch = source.take();
    EXPECT(batch.has_value());
    return batch ? std::move(*batch) : ml::Batch{};
}
void owned_metadata() {
    for (const bool root : {false, true}) {
        const auto provider = values(root);
        auto gate = std::make_shared<ml::testing::ReadGate>(3, provider);
        auto source = ml::testing::make_blocked_source(gate);
        EXPECT(source != nullptr);
        const auto original = request();
        EXPECT(source->submit(original));
        EXPECT(WaitForSingleObject(gate->entered, 10000) == WAIT_OBJECT_0);
        EXPECT(!source->take());
        SetEvent(gate->release);
        EXPECT(WaitForSingleObject(gate->backpressure, 10000) == WAIT_OBJECT_0);
        EXPECT(ml::testing::counts().batches == ml::kQueuedBatches);
        for (int n = 0; n < 4; ++n) {
            auto batch = take(*source);
            EXPECT(batch.request == original);
            auto changed = original; ++changed.binding.generation;
            EXPECT(!(batch.request == changed));
            changed = original; changed.binding.tab_id = "other";
            EXPECT(!(batch.request == changed));
            changed = original; changed.location = *provider.resolved_location;
            EXPECT(!(batch.request == changed));
            EXPECT(batch.resolved_location == provider.resolved_location);
            EXPECT(batch.parent_location == provider.parent_location);
            EXPECT(batch.parent_outcome == provider.parent_outcome);
            EXPECT(batch.notification == ml::NotificationCapability::manual_refresh);
            EXPECT(batch.change == ml::Change::none);
            EXPECT(ml::owned_batch_bytes(batch) <= ml::kMaxBatchOwnedBytes);
            EXPECT(batch.phase == (n == 3 ? ml::Phase::complete : ml::Phase::loading));
            if (n < 3) {
                EXPECT(batch.rows.size() == 1);
                EXPECT(batch.rows[0].identity == provider.rows[0].identity);
                EXPECT(batch.rows[0].name != batch.rows[0].identity.key);
                EXPECT(batch.rows[0].child_location == provider.rows[0].child_location);
                EXPECT(ml::prepare_row(batch.rows[0]));
            }
        }
        EXPECT(!source->take());
        EXPECT(WaitForSingleObject(source->event(), 0) == WAIT_TIMEOUT);
        source.reset(); EXPECT(ml::testing::wait_for_workers(10000));
    }
    // A failed bind is retryable without replacing the original identity or
    // reporting a filesystem notification failure. No actual bind occurs here.
    ml::Batch failure;
    failure.phase = ml::Phase::error; failure.error = ERROR_PATH_NOT_FOUND;
    auto gate = std::make_shared<ml::testing::ReadGate>(0, failure);
    SetEvent(gate->release);
    auto source = ml::testing::make_blocked_source(gate);
    for (int n = 0; n < 2; ++n) {
        const auto original = request(n + 1);
        EXPECT(source->submit(original));
        auto batch = take(*source);
        EXPECT(batch.request == original);
        EXPECT(batch.phase == ml::Phase::error && batch.error == ERROR_PATH_NOT_FOUND);
        EXPECT(!batch.resolved_location && !batch.parent_location);
        EXPECT(batch.parent_outcome == ml::ParentOutcome::unresolved);
        EXPECT(batch.notification == ml::NotificationCapability::manual_refresh);
        EXPECT(batch.change == ml::Change::none);
    }
    source.reset(); EXPECT(ml::testing::wait_for_workers(10000));
}
void bounds() {
    // Names and each persisted location component are independently bounded.
    for (int kind = 0; kind < 6; ++kind) {
        auto provider = values();
        if (kind == 0) provider.rows[0].name.assign(ml::kMaxName + 1, L'x');
        if (kind == 1) provider.rows[0].identity.key.assign(ml::kMaxPath + 1, L'x');
        if (kind == 2) provider.rows[0].child_location->known_folder_identity.assign(ml::kMaxPath + 1, L'x');
        if (kind == 3) provider.rows[0].child_location->fallback_path.assign(ml::kMaxPath + 1, L'x');
        if (kind == 4) provider.resolved_location->parsing_name.assign(ml::kMaxPath + 1, L'x');
        if (kind == 5) provider.parent_location->fallback_path.assign(ml::kMaxPath + 1, L'x');
        auto gate = std::make_shared<ml::testing::ReadGate>(1, provider);
        SetEvent(gate->release);
        auto source = ml::testing::make_blocked_source(gate);
        EXPECT(source->submit(request()));
        const auto batch = take(*source);
        EXPECT(batch.phase == ml::Phase::error && batch.error == ERROR_BUFFER_OVERFLOW);
        EXPECT(batch.rows.empty());
        source.reset(); EXPECT(ml::testing::wait_for_workers(10000));
    }
    auto provider = values();
    auto& row = provider.rows[0];
    row.identity.key.assign(ml::kMaxPath / 4, L'x');
    row.child_location = core::ShellLocation{row.identity.key, row.identity.key, row.identity.key};
    provider.rows.resize(ml::kBatchRows, row);
    auto gate = std::make_shared<ml::testing::ReadGate>(1, std::move(provider));
    SetEvent(gate->release);
    auto source = ml::testing::make_blocked_source(gate);
    EXPECT(source->submit(request()));
    auto limited = take(*source);
    EXPECT(limited.phase == ml::Phase::limited && limited.rows.empty());
    EXPECT(ml::owned_batch_bytes(limited) <= ml::kMaxBatchOwnedBytes);
    auto oversized = request(); oversized.location.parsing_name.assign(ml::kMaxPath + 1, L'x');
    EXPECT(!source->submit(oversized));
    oversized = request(); oversized.location.known_folder_identity.assign(ml::kMaxPath + 1, L'x');
    EXPECT(!source->submit(oversized));
    oversized = request(); oversized.location.fallback_path.assign(ml::kMaxPath + 1, L'x');
    EXPECT(!source->submit(oversized));
    source.reset(); EXPECT(ml::testing::wait_for_workers(10000));
    // Metadata and Row structural storage are part of owned-byte accounting.
    auto small = values(); const auto before = ml::owned_batch_bytes(small);
    small.parent_location->fallback_path.assign(1000, L'x');
    EXPECT(ml::owned_batch_bytes(small) > before + 1000);
}
void cancellation_and_capacity() {
    auto gate = std::make_shared<ml::testing::ReadGate>(20, values());
    auto source = ml::testing::make_blocked_source(gate);
    EXPECT(source->submit(request()));
    EXPECT(WaitForSingleObject(gate->entered, 10000) == WAIT_OBJECT_0);
    // Each navigation abandons a delayed provider in the existing four slots.
    for (std::size_t n = 1; n < ml::kMaxSubscriptions; ++n) {
        ResetEvent(gate->entered);
        EXPECT(source->submit(request(n + 1)));
        EXPECT(WaitForSingleObject(gate->entered, 10000) == WAIT_OBJECT_0);
    }
    for (int n = 0; n < 1000; ++n) EXPECT(!source->submit(request(10 + n)));
    auto counts = ml::testing::counts();
    EXPECT(counts.workers == 4 && counts.running == 4);
    EXPECT(counts.pending == 0 && counts.batches == 0);
    EXPECT(!ml::make_local_source()); // same pool, no separate namespace capacity
    const auto start = GetTickCount64(); source.reset();
    EXPECT(GetTickCount64() - start < 1000);
    EXPECT(ml::testing::counts().workers == 4);
    SetEvent(gate->release); EXPECT(ml::testing::wait_for_workers(10000));
    EXPECT(ml::testing::counts().workers == 0);
    // Cancel while publishing is backpressured; no late batch survives cancel.
    gate = std::make_shared<ml::testing::ReadGate>(20, values());
    source = ml::testing::make_blocked_source(gate);
    EXPECT(source->submit(request())); SetEvent(gate->release);
    EXPECT(WaitForSingleObject(gate->backpressure, 10000) == WAIT_OBJECT_0);
    source->cancel(); EXPECT(!source->take());
    // Destroying the old source never waits on its provider or backpressure.
    source.reset(); EXPECT(ml::testing::wait_for_workers(10000));
    // A cancelled delayed provider returns only to its invalidated serial.
    gate = std::make_shared<ml::testing::ReadGate>(1, values());
    source = ml::testing::make_blocked_source(gate);
    EXPECT(source->submit(request()));
    EXPECT(WaitForSingleObject(gate->entered, 10000) == WAIT_OBJECT_0);
    source->cancel(); SetEvent(gate->release);
    auto successor = request(99);
    EXPECT(source->submit(successor));
    auto current = take(*source);
    EXPECT(current.request == successor);
    EXPECT(current.phase == ml::Phase::loading);
    EXPECT(take(*source).phase == ml::Phase::complete);
    source.reset(); EXPECT(ml::testing::wait_for_workers(10000));
}
}
int main() {
    EXPECT(ml::uses_shell_namespace(core::ShellLocation{L"shell:Desktop", {}, {}}));
    auto physical_desktop = core::ShellLocation{
        L"D:\\Documents\\Desktop", L"{B4BFCC3A-DB2C-424C-B029-7FE99A87C641}",
        L"D:\\Documents\\Desktop"};
    EXPECT(!ml::uses_shell_namespace(physical_desktop));
    physical_desktop.namespace_location = true;
    EXPECT(ml::uses_shell_namespace(physical_desktop));
    owned_metadata(); bounds(); cancellation_and_capacity();
    EXPECT(ml::testing::counts().workers == 0);
    std::cout << "owned values only; four shared slots; two queued batches; "
              << "2 MiB per batch; controlled teardown only; real Shell UNVERIFIED\n";
    return test::summary("managed_list_namespace_worker");
}
