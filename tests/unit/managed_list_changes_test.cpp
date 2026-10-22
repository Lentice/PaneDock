#include "unit/test_pane_host.h"
#include "unit/test_util.h"
#include "managed_list/list_worker_test.h"
#include <algorithm>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>

using namespace panedock;
namespace ml = managed_list;
namespace {
class Delivery final : public ml::Source {
  public:
    std::deque<ml::Batch> batches;
    bool submit(ml::Request) noexcept override { return true; }
    std::optional<ml::Batch> take() noexcept override {
        if (batches.empty()) return {};
        auto batch = std::move(batches.front()); batches.pop_front(); return batch;
    }
    HANDLE event() const noexcept override { return nullptr; }
    void cancel() noexcept override {} // deliberately retain stale deliveries
};
struct Fixture final {
    test::TestPaneHost host;
    core::PaneState state{"pane", {{"tab", {L"C:\\synthetic", {}, {}}, "FVM_DETAILS",
        "System.Size", false, {}, 0}}, "tab"};
    app_shell::Pane pane;
    ml::Source* source{};
    HWND parent{}, list{};
    explicit Fixture(std::unique_ptr<ml::Source> input, std::size_t index_limit = ml::kMaxIndexBytes) {
        EXPECT(app_shell::Pane::register_window_class(GetModuleHandleW(nullptr)));
        parent = CreateWindowExW(0, L"STATIC", L"change test", 0, 0, 0, 800, 600,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        EXPECT(parent != nullptr);
        pane.set_host(&host); pane.set_reentry_guard(&host.guard);
        pane.bind(&state); EXPECT(pane.create(parent, 0)); pane.bind(&state);
        source = input.get(); pane.use_managed_list(std::move(input));
        pane.limit_list_index_bytes(index_limit);
    }
    void realize(const core::ShellLocation& location) {
        state.tabs[0].location = location;
        EXPECT(SUCCEEDED(pane.realize({0, 0, 640, 420}, location)));
        list = FindWindowExW(pane.explorer_container(), nullptr, panedock::app_shell::kManagedListClassName, nullptr);
        EXPECT(list != nullptr);
    }
    ~Fixture() { pane.destroy(); if (parent) DestroyWindow(parent); }
    int index(std::wstring_view name) const {
        const auto& rows = pane.list_snapshot().rows;
        const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.name == name; });
        return it == rows.end() ? -1 : static_cast<int>(it - rows.begin());
    }
    void select(std::wstring_view name) {
        const int i = index(name); EXPECT(i >= 0);
        if (i >= 0) {
            ListView_SetItemState(list, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_SetSelectionMark(list, i);
        }
    }
};
void await(Fixture& f, const std::function<bool()>& done) {
    const auto deadline = GetTickCount64() + 10000;
    while (!done() && GetTickCount64() < deadline) {
        const auto event = f.pane.content_event();
        if (!event || WaitForSingleObject(event, 10000) != WAIT_OBJECT_0) break;
        f.pane.process_content_results();
    }
    EXPECT(done());
}
void deliver(Fixture& f, ml::Change change, ml::Phase phase = ml::Phase::complete,
             std::vector<ml::Row> rows = {}, DWORD error = 0) {
    auto& source = static_cast<Delivery&>(*f.source);
    source.batches.push_back({f.pane.list_snapshot().request, std::move(rows), phase, error, change});
    f.pane.process_content_results();
}
std::wstring name(std::size_t i) { return L"item-" + std::to_wstring(100000 + i); }
void large_recovery_has_log_linear_work_and_one_snapshot() {
    Fixture f(std::make_unique<Delivery>()); f.realize({L"C:\\synthetic", {}, {}});
    deliver(f, ml::Change::none, ml::Phase::empty);
    constexpr std::size_t count = 32768;
    const auto scan = [&](std::size_t first, std::size_t last) {
        deliver(f, ml::Change::rescan_begin, ml::Phase::loading);
        for (auto begin = first; begin < last; begin += ml::kBatchRows) {
            std::vector<ml::Row> rows;
            for (auto i = begin; i < std::min(begin + ml::kBatchRows, last); ++i)
                rows.push_back({name(i), i, false, i + 1});
            deliver(f, ml::Change::rescan, ml::Phase::loading, std::move(rows));
        }
        deliver(f, ml::Change::rescan);
    };
    scan(0, count);
    EXPECT(f.pane.list_snapshot().rows.size() == count);
    f.select(name(2048));
    const int viewport = f.index(name(16000));
    ListView_EnsureVisible(f.list, viewport, FALSE);
    const auto initial = f.pane.list_interaction_snapshot();
    const std::wstring anchor = initial.viewport_anchor ? std::wstring(initial.viewport_anchor->name) : L"";
    const int offset = initial.viewport_offset;
    // Deletes 128 old identities and adds 256, retaining the sole row vector.
    scan(128, count + 256);
    const auto work = f.pane.list_recovery_work();
    EXPECT(f.pane.list_index_bytes() == 0);
    EXPECT(f.pane.list_index_peak_bytes() > 0 && f.pane.list_index_peak_bytes() <= ml::kMaxIndexBytes);
    EXPECT(work.indexed_rows == count);
    EXPECT(work.lookups == count + 128);
    // One accounting visit per input row, plus a single terminal sweep.
    EXPECT(work.accounted_rows <= 2 * (count + 256));
    // Red-black tree construction + all lookups/inserts; a deterministic bound,
    // independent of scheduling/CPU speed, far below quadratic row comparisons.
    EXPECT(work.comparisons < 128 * (count + 256));
    EXPECT(f.pane.list_snapshot().rows.size() == count + 128);
    EXPECT(f.index(name(0)) == -1 && f.index(name(count + 255)) >= 0);
    const auto after = f.pane.list_interaction_snapshot();
    EXPECT(after.selected.size() == 1 && after.selected[0].name == name(2048));
    EXPECT(after.focused && after.focused->name == name(2048));
    EXPECT(after.anchor && after.anchor->name == name(2048));
    EXPECT(after.viewport_anchor && after.viewport_anchor->name == anchor);
    EXPECT(after.viewport_offset == offset);
    const auto& rows = f.pane.list_snapshot().rows;
    EXPECT(std::is_sorted(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.bytes > b.bytes; }));
    // An incomplete recovery must keep unseen identities and expose retry.
    deliver(f, ml::Change::rescan_begin, ml::Phase::loading);
    deliver(f, ml::Change::rescan, ml::Phase::error, {}, ERROR_ACCESS_DENIED);
    EXPECT(f.pane.list_snapshot().watch == ml::WatchState::failed);
    EXPECT(f.pane.list_snapshot().rows.size() == count + 128);

    // Same large snapshot with a deterministically small index budget. Initial
    // enumeration does not need a recovery index; beginning recovery must fail
    // BEFORE allocating past the budget, release all nodes and retain rows.
    Fixture limited(std::make_unique<Delivery>(), 100 * 1024);
    limited.realize({L"C:\\synthetic", {}, {}});
    for (std::size_t begin = 0; begin < count; begin += ml::kBatchRows) {
        std::vector<ml::Row> input;
        for (auto i = begin; i < begin + ml::kBatchRows; ++i) input.push_back({name(i), i, false});
        deliver(limited, ml::Change::none, begin + ml::kBatchRows == count ? ml::Phase::complete : ml::Phase::loading,
            std::move(input));
    }
    EXPECT(limited.pane.list_snapshot().rows.size() == count);
    deliver(limited, ml::Change::rescan_begin, ml::Phase::loading);
    EXPECT(limited.pane.list_snapshot().watch == ml::WatchState::failed);
    EXPECT(limited.pane.list_snapshot().watch_error == ERROR_NOT_ENOUGH_MEMORY);
    EXPECT(limited.pane.list_index_peak_bytes() > ml::kMaxRows && limited.pane.list_index_peak_bytes() <= 100 * 1024);
    EXPECT(limited.pane.list_index_bytes() == 0);
    EXPECT(limited.pane.list_snapshot().rows.size() == count);
}
void stale_events_and_reentry_cannot_change_a_new_binding() {
    Fixture f(std::make_unique<Delivery>()); f.realize({L"C:\\synthetic", {}, {}});
    deliver(f, ml::Change::none, ml::Phase::complete, {{L"old", 1, false}});
    const auto old = f.pane.list_snapshot().request;
    auto& source = static_cast<Delivery&>(*f.source);
    // Shell-call reentry holds results until the outer call returns.
    source.batches.push_back({old, {{L"deferred", 2, false}}, ml::Phase::complete, 0, ml::Change::delta});
    {
        app_shell::ShellCallScope call(f.host.guard);
        f.pane.process_content_results(); EXPECT(f.index(L"deferred") == -1);
    }
    f.pane.process_content_results(); EXPECT(f.index(L"deferred") >= 0);
    f.state.tabs.push_back({"other", {L"C:\\other", {}, {}}, "FVM_DETAILS", "System.Size", false, {}, 0});
    f.pane.switch_active_tab("other");
    source.batches.push_back({old, {{L"late-tab", 1, false}}, ml::Phase::complete, 0, ml::Change::delta});
    deliver(f, ml::Change::none, ml::Phase::empty);
    EXPECT(f.index(L"late-tab") == -1 && f.pane.list_snapshot().rows.empty());
    const auto tab = f.pane.list_snapshot().request;
    f.host.group_id = "group-b";
    source.batches.push_back({tab, {{L"late-group", 1, false}}, ml::Phase::complete, 0, ml::Change::delta});
    f.pane.process_content_results(); EXPECT(f.index(L"late-group") == -1);
    f.pane.derealize();
    source.batches.push_back({tab, {{L"late-destroy", 1, false}}, ml::Phase::complete, 0, ml::Change::delta});
    f.pane.process_content_results(); EXPECT(f.pane.list_snapshot().rows.empty());
}
void real_local_changes_overflow_failure_retry_and_departure() {
    const auto root = std::filesystem::temp_directory_path() /
        (L"PaneDock-PD232-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directory(root);
    const auto other = root / L"other";
    std::filesystem::create_directory(other);
    for (int i = 0; i < 400; ++i) std::ofstream(root / name(i)) << i;
    {
        Fixture f(ml::make_local_source()); EXPECT(f.source != nullptr);
        f.realize({root.wstring(), {}, {}});
        await(f, [&] { return f.pane.list_snapshot().watch == ml::WatchState::watching; });
        EXPECT(f.pane.list_snapshot().rows.size() == 401);
        f.select(name(200));
        const std::wstring renamed = L"renamed";
        std::filesystem::rename(root / name(200), root / renamed);
        await(f, [&] { return f.index(renamed) >= 0 && f.index(name(200)) < 0; });
        auto interaction = f.pane.list_interaction_snapshot();
        EXPECT(interaction.selected.size() == 1 && interaction.selected[0].name == renamed);
        EXPECT(interaction.focused && interaction.focused->name == renamed);
        EXPECT(interaction.anchor && interaction.anchor->name == renamed);
        std::ofstream(root / L"added") << "external";
        std::filesystem::remove(root / name(100));
        await(f, [&] { return f.index(L"added") >= 0 && f.index(name(100)) < 0; });
        std::filesystem::remove(root / renamed);
        await(f, [&] { return f.index(renamed) < 0; });
        interaction = f.pane.list_interaction_snapshot();
        EXPECT(interaction.selected.empty());
        EXPECT(interaction.focused && interaction.focused->name != renamed);
        // Start recovery, wait for its public recovering state; the production
        // two-batch queue then backpressures the 400-row scan. Repeated loss
        // injection coalesces into the in-progress scan rather than queuing work.
        EXPECT(ml::testing::inject_overflow(*f.source));
        await(f, [&] { return f.pane.list_snapshot().watch == ml::WatchState::recovering; });
        for (int i = 0; i < 1000; ++i) EXPECT(ml::testing::inject_overflow(*f.source));
        await(f, [&] { return f.pane.list_snapshot().watch == ml::WatchState::watching; });
        EXPECT(f.pane.list_snapshot().rows.size() == 400);
        EXPECT(f.index(L"added") >= 0 && f.index(renamed) < 0);
        EXPECT(ml::testing::inject_watch_failure(*f.source, ERROR_ACCESS_DENIED));
        await(f, [&] { return f.pane.list_snapshot().watch == ml::WatchState::failed; });
        EXPECT(f.pane.list_snapshot().watch_error == ERROR_ACCESS_DENIED);
        EXPECT(f.pane.list_snapshot().rows.size() == 400);
        // No automatic retry or idle UI event after failure.
        EXPECT(WaitForSingleObject(f.pane.content_event(), 0) == WAIT_TIMEOUT);
        f.pane.refresh_view();
        await(f, [&] { return f.pane.list_snapshot().watch == ml::WatchState::watching; });
        // Repeated loss with no useful notification detail has a fixed retry
        // budget. It must stop visibly instead of rescanning indefinitely.
        for (int scan = 0; scan < 2; ++scan) {
            EXPECT(ml::testing::inject_overflow(*f.source));
            await(f, [&] { return f.pane.list_snapshot().watch == ml::WatchState::recovering; });
            await(f, [&] { return f.pane.list_snapshot().watch == ml::WatchState::watching; });
        }
        EXPECT(ml::testing::inject_overflow(*f.source));
        await(f, [&] { return f.pane.list_snapshot().watch == ml::WatchState::failed; });
        EXPECT(f.pane.list_snapshot().watch_error == ERROR_NOTIFY_ENUM_DIR);
        EXPECT(WaitForSingleObject(f.pane.content_event(), 0) == WAIT_TIMEOUT);
        f.pane.refresh_view();
        await(f, [&] { return f.pane.list_snapshot().watch == ml::WatchState::watching; });
        EXPECT(SUCCEEDED(f.pane.navigate_to({other.wstring(), {}, {}})));
        await(f, [&] { return f.pane.list_snapshot().watch == ml::WatchState::watching; });
        std::ofstream(root / L"old-location-change") << "ignored";
        EXPECT(f.pane.list_snapshot().request.location.parsing_name == other.wstring());
        EXPECT(f.pane.list_snapshot().rows.empty());
        const auto before = GetTickCount64(); f.pane.derealize();
        EXPECT(GetTickCount64() - before < 1000);
        EXPECT(WaitForSingleObject(f.pane.content_event(), 0) == WAIT_TIMEOUT);
    }
    EXPECT(ml::testing::wait_for_workers(10000));
    EXPECT(ml::testing::counts().workers == 0);
    std::filesystem::remove_all(root);
}

void local_deltas_use_leaf_identity_instead_of_display_text() {
    Fixture f(std::make_unique<Delivery>()); f.realize({L"C:\\synthetic", {}, {}});
    ml::Row local{L"Friendly name", 1, false}; local.identity = {L"actual-leaf", false};
    ml::Row shell{L"Friendly name", 2, false}; shell.identity = {L"actual-leaf", true};
    deliver(f, ml::Change::none, ml::Phase::complete, {local, shell});
    f.select(L"Friendly name");
    // Pick local explicitly; both the display text and raw key collide.
    const auto& rows = f.pane.list_snapshot().rows;
    const int selected = rows[0].identity.shell_item ? 1 : 0;
    ListView_SetItemState(f.list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(f.list, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(f.list, selected);
    ml::Row renamed{L"Still friendly", 1, false}; renamed.identity = {L"new-leaf", false};
    ml::Batch delta{f.pane.list_snapshot().request, {renamed}, ml::Phase::complete, 0, ml::Change::delta};
    delta.renamed.push_back({L"actual-leaf", L"new-leaf"});
    delta.removed.push_back(L"actual-leaf");
    auto& source = static_cast<Delivery&>(*f.source);
    source.batches.push_back(std::move(delta)); f.pane.process_content_results();
    auto interaction = f.pane.list_interaction_snapshot();
    EXPECT(interaction.selected.size() == 1 && interaction.selected[0].name == L"new-leaf");
    EXPECT(interaction.focused && interaction.focused->name == L"new-leaf");
    EXPECT(interaction.anchor && interaction.anchor->name == L"new-leaf");
    EXPECT(f.pane.list_snapshot().rows.size() == 2); // Shell key is not a leaf delta
    ml::Batch removed{f.pane.list_snapshot().request, {}, ml::Phase::complete, 0, ml::Change::delta};
    removed.removed.push_back(L"new-leaf"); source.batches.push_back(std::move(removed));
    f.pane.process_content_results();
    EXPECT(f.pane.list_snapshot().rows.size() == 1);
    EXPECT(f.pane.list_snapshot().rows[0].identity.shell_item);
    EXPECT(f.pane.list_interaction_snapshot().selected.empty());
    EXPECT(!f.pane.list_interaction_snapshot().anchor);
}

void local_recovery_and_notifications_preserve_exact_case_comparisons() {
    Fixture f(std::make_unique<Delivery>()); f.realize({L"C:\\synthetic", {}, {}});
    ml::Row lower{L"Same label", 1, false}; lower.identity = {L"leaf", false};
    ml::Row upper{L"Same label", 2, false}; upper.identity = {L"LEAF", false};
    deliver(f, ml::Change::none, ml::Phase::complete, {lower, upper});
    const int selected = f.pane.list_snapshot().rows[0].identity.key == L"leaf" ? 0 : 1;
    ListView_SetItemState(f.list, selected, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(f.list, selected);
    deliver(f, ml::Change::rescan_begin, ml::Phase::loading);
    lower.bytes = 3; upper.bytes = 4;
    deliver(f, ml::Change::rescan, ml::Phase::complete, {lower, upper});
    EXPECT(f.pane.list_snapshot().rows.size() == 2);
    EXPECT(f.pane.list_snapshot().rows[0].identity.key == L"LEAF");
    EXPECT(f.pane.list_snapshot().rows[0].bytes == 4);
    EXPECT(f.pane.list_snapshot().rows[1].identity.key == L"leaf");
    EXPECT(f.pane.list_snapshot().rows[1].bytes == 3);
    EXPECT(f.pane.list_interaction_snapshot().selected.size() == 1);
    EXPECT(f.pane.list_interaction_snapshot().anchor &&
        f.pane.list_interaction_snapshot().anchor->name == L"leaf");
    auto& source = static_cast<Delivery&>(*f.source);
    ml::Batch removed{f.pane.list_snapshot().request, {}, ml::Phase::complete, 0, ml::Change::delta};
    removed.removed.push_back(L"LEAF"); source.batches.push_back(std::move(removed));
    f.pane.process_content_results();
    EXPECT(f.pane.list_snapshot().rows.size() == 1);
    EXPECT(f.pane.list_interaction_snapshot().selected.size() == 1);
    EXPECT(f.pane.list_interaction_snapshot().selected[0].name == L"leaf");
    lower.identity.key = L"Leaf";
    ml::Batch renamed{f.pane.list_snapshot().request, {lower}, ml::Phase::complete, 0, ml::Change::delta};
    renamed.renamed.push_back({L"leaf", L"Leaf"}); source.batches.push_back(std::move(renamed));
    f.pane.process_content_results();
    EXPECT(f.pane.list_snapshot().rows.size() == 1);
    const auto interaction = f.pane.list_interaction_snapshot();
    EXPECT(interaction.selected.size() == 1 && interaction.selected[0].name == L"Leaf");
    EXPECT(interaction.focused && interaction.focused->name == L"Leaf");
    EXPECT(interaction.anchor && interaction.anchor->name == L"Leaf");
    EXPECT(f.pane.list_snapshot().rows[0].name == L"Same label");
}
} // namespace
int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    local_recovery_and_notifications_preserve_exact_case_comparisons();
    local_deltas_use_leaf_identity_instead_of_display_text();
    large_recovery_has_log_linear_work_and_one_snapshot();
    stale_events_and_reentry_cannot_change_a_new_binding();
    real_local_changes_overflow_failure_retry_and_departure();
    return test::summary("managed_list_changes");
}
