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
    void cancel() noexcept override { ++cancels; } // deliberately retain stale deliveries
    int cancels{};
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
        // Case-only rename: the notified old spelling resolves (case-
        // insensitively) to the new file; no ghost row keeps the old one.
        std::wstring upper = name(300);
        CharUpperBuffW(upper.data(), static_cast<DWORD>(upper.size()));
        std::filesystem::rename(root / name(300), root / upper);
        await(f, [&] { return f.index(upper) >= 0 && f.index(name(300)) < 0; });
        EXPECT(f.pane.list_snapshot().rows.size() == 401);
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
        // Repeated loss of notification detail is churn, not failure (a long
        // copy overflows again and again). Each one coalesces into a rescan
        // and the watch keeps running; there is no strike-out.
        for (int scan = 0; scan < 4; ++scan) {
            EXPECT(ml::testing::inject_overflow(*f.source));
            await(f, [&] { return f.pane.list_snapshot().watch == ml::WatchState::recovering; });
            await(f, [&] { return f.pane.list_snapshot().watch == ml::WatchState::watching; });
        }
        // A burst beyond the 128-name detail cap, like extracting an archive.
        for (int i = 0; i < 1200; ++i) std::ofstream(root / (L"burst-" + std::to_wstring(i))) << i;
        await(f, [&] { return f.pane.list_snapshot().rows.size() == 1600 &&
            f.pane.list_snapshot().watch == ml::WatchState::watching; });
        EXPECT(f.index(L"burst-0") >= 0 && f.index(L"burst-1199") >= 0);
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

HWND rename_edit(Fixture& f) { return FindWindowExW(f.list, nullptr, L"EDIT", nullptr); }
void delta(Fixture& f, std::vector<ml::Row> rows, std::vector<std::wstring> removed = {}) {
    ml::Batch batch{f.pane.list_snapshot().request, std::move(rows), ml::Phase::complete, 0, ml::Change::delta};
    batch.removed = std::move(removed);
    static_cast<Delivery&>(*f.source).batches.push_back(std::move(batch));
    f.pane.process_content_results();
}
// A download landing in the folder must not kill an F2 edit: the edit
// follows its row, and only the row's removal (or scrolling away) ends it.
void rename_edit_follows_its_row_through_unrelated_batches() {
    Fixture f(std::make_unique<Delivery>()); f.realize({L"C:\\synthetic", {}, {}});
    // The fixture tab sorts by size, largest first.
    deliver(f, ml::Change::none, ml::Phase::complete, {{L"b", 20, false}, {L"c", 10, false}});
    f.select(L"c");
    EXPECT(f.pane.begin_list_rename());
    const HWND edit = rename_edit(f);
    EXPECT(edit != nullptr);
    const auto edit_top = [&] {
        RECT rect{}; GetWindowRect(edit, &rect);
        MapWindowPoints(nullptr, f.list, reinterpret_cast<POINT*>(&rect), 2);
        return rect.top;
    };
    const auto label_top = [&](int index) {
        RECT rect{}; ListView_GetItemRect(f.list, index, &rect, LVIR_LABEL);
        return rect.top;
    };
    EXPECT(f.index(L"c") == 1 && edit_top() == label_top(1));
    delta(f, {{L"a", 30, false}}); // sorts above the edited row
    EXPECT(rename_edit(f) == edit && f.index(L"c") == 2);
    EXPECT(edit_top() == label_top(2));
    delta(f, {{L"b", 25, false}}); // another row changes
    EXPECT(rename_edit(f) == edit);
    const auto interaction = f.pane.list_interaction_snapshot();
    EXPECT(interaction.selected.size() == 1 && interaction.selected[0].name == L"c");
    delta(f, {}, {L"c"}); // the edited item itself is gone
    EXPECT(rename_edit(f) == nullptr);
}
// Small deltas place changed rows into the existing order (sorted suffix +
// merge) instead of re-sorting; the result must equal a full sort.
void small_deltas_keep_the_list_sorted() {
    Fixture f(std::make_unique<Delivery>()); f.realize({L"C:\\synthetic", {}, {}});
    for (std::size_t begin = 0; begin < 300; begin += 100) {
        std::vector<ml::Row> rows;
        for (std::size_t i = begin; i < begin + 100; ++i) rows.push_back({name(i), 1000 + i * 3, false});
        deliver(f, ml::Change::none, begin == 200 ? ml::Phase::complete : ml::Phase::loading, std::move(rows));
    }
    EXPECT(f.pane.list_snapshot().rows.size() == 300);
    delta(f, {{name(0), 5000, false}, {name(299), 1, false}, {L"new-middle", 1450, false},
              {name(150), 1450, false}}, {name(10), name(20)});
    const auto& list = f.pane.list_snapshot().rows;
    EXPECT(list.size() == 299);
    EXPECT(list.front().identity.key == name(0) && list.back().identity.key == name(299));
    EXPECT(std::is_sorted(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.bytes > b.bytes; }));
    EXPECT(f.index(name(10)) < 0 && f.index(name(20)) < 0 && f.index(L"new-middle") >= 0);
}
// A displayed folder that is gone sends the Pane elsewhere instead of
// showing its stale rows; shutdown drops queued results instead of spinning.
void lost_location_navigates_and_shutdown_drains() {
    Fixture f(std::make_unique<Delivery>()); f.realize({L"C:\\synthetic", {}, {}});
    deliver(f, ml::Change::none, ml::Phase::complete, {{L"stale", 1, false}});
    ml::Batch lost{f.pane.list_snapshot().request, {}, ml::Phase::complete, 0, ml::Change::location_lost};
    lost.parent_location = core::ShellLocation{L"C:\\", {}, {}};
    lost.parent_outcome = ml::ParentOutcome::value;
    static_cast<Delivery&>(*f.source).batches.push_back(std::move(lost));
    f.pane.process_content_results();
    EXPECT(f.pane.list_snapshot().request.location.parsing_name == L"C:\\");
    EXPECT(f.pane.list_snapshot().rows.empty());
    auto& source = static_cast<Delivery&>(*f.source);
    const int cancels = source.cancels;
    f.host.shutting_down = true;
    f.pane.process_content_results();
    EXPECT(source.cancels == cancels + 1);
}
// The real worker: a displayed folder renamed in place is followed; deleted,
// the Pane moves to the nearest existing parent.
void real_folder_loss_follows_rename_or_moves_to_parent() {
    auto temp = std::filesystem::temp_directory_path().wstring();
    std::wstring longer(32768, L'\0');
    const DWORD length = GetLongPathNameW(temp.c_str(), longer.data(), static_cast<DWORD>(longer.size()));
    if (length && length < longer.size()) temp.assign(longer.data(), length);
    const auto root = std::filesystem::path(temp) /
        (L"PaneDock-loss-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove_all(root);
    const auto shown = root / L"shown";
    std::filesystem::create_directories(shown);
    std::ofstream(shown / L"file") << "x";
    {
        Fixture f(ml::make_local_source()); EXPECT(f.source != nullptr);
        f.realize({shown.wstring(), {}, {}});
        await(f, [&] { return f.pane.list_snapshot().watch == ml::WatchState::watching; });
        EXPECT(f.pane.list_snapshot().rows.size() == 1);
        const auto renamed = root / L"shown-renamed";
        // A scanner (Defender, the indexer) can hold the fresh file for a few
        // milliseconds; retry briefly rather than fail on the test machine.
        std::error_code rename_error;
        for (int attempt = 0; attempt < 50; ++attempt) {
            rename_error.clear();
            std::filesystem::rename(shown, renamed, rename_error);
            if (!rename_error) break;
            Sleep(20);
        }
        EXPECT(!rename_error);
        await(f, [&] { return f.pane.list_snapshot().request.location.parsing_name == renamed.wstring() &&
            f.pane.list_snapshot().watch == ml::WatchState::watching; });
        EXPECT(f.pane.list_snapshot().rows.size() == 1);
        std::filesystem::remove_all(renamed);
        await(f, [&] { return f.pane.list_snapshot().request.location.parsing_name == root.wstring() &&
            f.pane.list_snapshot().watch == ml::WatchState::watching; });
        EXPECT(f.pane.list_snapshot().rows.empty());
    }
    EXPECT(ml::testing::wait_for_workers(10000));
    std::filesystem::remove_all(root);
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
    rename_edit_follows_its_row_through_unrelated_batches();
    small_deltas_keep_the_list_sorted();
    lost_location_navigates_and_shutdown_drains();
    real_folder_loss_follows_rename_or_moves_to_parent();
    return test::summary("managed_list_changes");
}
