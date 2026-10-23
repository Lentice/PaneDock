#include "unit/test_pane_host.h"
#include "unit/test_util.h"
#include <deque>

using namespace panedock;
namespace ml = managed_list;
namespace {
class Delivery final : public ml::Source {
  public:
    std::deque<ml::Batch> batches;
    std::size_t submissions{};
    std::size_t cancellations{};
    bool interested{};
    bool submit(ml::Request) noexcept override { ++submissions; interested = true; return true; }
    std::optional<ml::Batch> take() noexcept override {
        if (batches.empty()) return {};
        auto result = std::move(batches.front()); batches.pop_front(); return result;
    }
    HANDLE event() const noexcept override { return nullptr; }
    void cancel() noexcept override { ++cancellations; interested = false; } // late results deliberately survive
};
class Enrichment final : public ml::IconSource {
  public:
    std::vector<ml::IconRequest> interest;
    std::size_t cancellations{};
    bool replace(std::vector<ml::IconRequest> requests) noexcept override {
        interest = std::move(requests); return true;
    }
    std::optional<ml::IconResult> take() noexcept override { return {}; }
    HANDLE event() const noexcept override { return nullptr; }
    void cancel() noexcept override { ++cancellations; interest.clear(); }
};
struct Fixture final {
    test::TestPaneHost host;
    core::PaneState state;
    app_shell::Pane pane;
    Delivery* source{};
    HWND parent{}, list{};
    explicit Fixture(int count = 3, std::wstring prefix = L"C:\\cache",
                     std::size_t index_limit = ml::kMaxIndexBytes) {
        state.id = "pane";
        for (int i = 0; i < count; ++i)
            state.tabs.push_back({std::to_string(i), {prefix + std::to_wstring(i), {}, {}},
                "FVM_DETAILS", "System.ItemNameDisplay", true, {}, 0});
        state.active_tab_id = "0";
        EXPECT(app_shell::Pane::register_window_class(GetModuleHandleW(nullptr)));
        parent = CreateWindowExW(0, L"STATIC", L"cache test", 0, 0, 0, 800, 600,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        EXPECT(parent != nullptr);
        pane.set_host(&host); pane.set_reentry_guard(&host.guard);
        EXPECT(pane.create(parent, 0)); pane.bind(&state);
        auto input = std::make_unique<Delivery>(); source = input.get();
        pane.use_managed_list(std::move(input));
        pane.limit_list_index_bytes(index_limit);
        EXPECT(SUCCEEDED(pane.realize({0, 0, 640, 420}, state.tabs[0].location)));
        list = FindWindowExW(pane.explorer_container(), nullptr, panedock::app_shell::kManagedListClassName, nullptr);
        EXPECT(list != nullptr);
    }
    ~Fixture() { pane.destroy(); DestroyWindow(parent); }
    void deliver(std::vector<ml::Row> rows, ml::Phase phase = ml::Phase::complete,
                 ml::Change change = ml::Change::none, DWORD error = 0) {
        source->batches.push_back({pane.list_snapshot().request, std::move(rows), phase, error, change});
        pane.process_content_results();
    }
    void select(int index) {
        ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetSelectionMark(list, index);
    }
};
void warm_refresh_and_notifications() {
    Fixture f;
    f.deliver({{L"deleted", 1, true}, {L"keep", 2, false}, {L"renamed", 3, false}});
    f.select(1);
    const auto old = f.pane.list_snapshot().request;
    f.pane.switch_active_tab("1");
    const auto retained = app_shell::Pane::list_cache_stats();
    EXPECT(retained.entries == 1 && retained.bytes > 3 * sizeof(ml::Row));
    f.deliver({{L"other", 9, false}});
    f.pane.switch_active_tab("0");
    EXPECT(f.pane.list_snapshot().rows.size() == 3);
    EXPECT(f.pane.list_snapshot().updating);
    EXPECT(f.source->submissions == 3);
    EXPECT(app_shell::Pane::list_cache_stats().hits == retained.hits + 1);
    const auto restored = f.pane.list_interaction_snapshot();
    EXPECT(restored.selected.size() == 1 && restored.selected[0].name == L"keep");
    EXPECT(restored.focused && restored.focused->name == L"keep");
    wchar_t status[256]{}; GetWindowTextW(f.pane.status_bar(), status, 256);
    EXPECT(std::wstring(status).find(L"Updating") != std::wstring::npos);
    f.source->batches.push_back({old, {{L"obsolete", 0, false}}, ml::Phase::complete, 0});
    f.pane.process_content_results();
    EXPECT(f.pane.list_snapshot().rows.size() == 3);
    f.deliver({{L"keep", 20, false}}, ml::Phase::loading);
    EXPECT(f.pane.list_snapshot().updating);
    f.deliver({{L"new-name", 3, false}});
    EXPECT(!f.pane.list_snapshot().updating && f.pane.list_snapshot().rows.size() == 2);
    const auto after = f.pane.list_interaction_snapshot();
    EXPECT(after.selected.size() == 1 && after.selected[0].name == L"keep");
    EXPECT(after.focused && after.focused->name == L"keep");
    EXPECT(after.anchor && after.anchor->name == L"keep");
    // PD-232 deltas still apply after the current-generation refresh commits.
    ml::Batch delta{f.pane.list_snapshot().request, {{L"final-name", 20, false}}, ml::Phase::complete, 0,
                    ml::Change::delta};
    delta.renamed.push_back({L"keep", L"final-name"});
    f.source->batches.push_back(std::move(delta)); f.pane.process_content_results();
    EXPECT(f.pane.list_interaction_snapshot().selected[0].name == L"final-name");
}
void missing_identity_and_failed_scan() {
    Fixture f;
    f.deliver({{L"gone", 0, true}, {L"survivor", 0, false}}); f.select(0);
    f.pane.switch_active_tab("1"); f.deliver({}, ml::Phase::empty);
    f.pane.switch_active_tab("0");
    // Opening a stale directory issues a fresh exact-identity scan. A cache
    // hit never commits the target/history or substitutes the adjacent row.
    const auto saved = *f.pane.active_tab();
    EXPECT(f.pane.open_list_row(0));
    EXPECT(f.pane.list_snapshot().request.location.parsing_name == L"C:\\cache0\\gone");
    EXPECT(*f.pane.active_tab() == saved);
    f.deliver({}, ml::Phase::error, ml::Change::none, ERROR_PATH_NOT_FOUND);
    EXPECT(*f.pane.active_tab() == saved);
    EXPECT(SUCCEEDED(f.pane.navigate_to(saved.location)));
    f.deliver({{L"gone", 0, true}, {L"survivor", 0, false}}); f.select(0);
    f.pane.switch_active_tab("1"); f.deliver({}, ml::Phase::empty);
    f.pane.switch_active_tab("0");
    f.deliver({{L"survivor", 0, false}});
    const auto after = f.pane.list_interaction_snapshot();
    EXPECT(after.selected.empty());
    EXPECT(after.focused && after.focused->name == L"survivor");
    EXPECT(!after.anchor); // selection mark never transfers on deletion
    EXPECT(!f.pane.open_list_row(0)); // replacement file cannot inherit directory action
}
void global_budget_and_lru() {
    Fixture first(101), second;
    first.deliver({{L"row", 1, false}});
    first.pane.switch_active_tab("1");
    const auto one = app_shell::Pane::list_cache_stats().bytes;
    EXPECT(one > 0);
    app_shell::Pane::limit_list_cache_bytes(one * 2 + 512);
    const auto initial = app_shell::Pane::list_cache_stats();
    second.deliver({{L"row", 1, false}}); second.pane.switch_active_tab("1");
    EXPECT(app_shell::Pane::list_cache_stats().entries == 2);
    // Touch first's tab 0 so second's entry becomes least recently used.
    first.deliver({{L"row", 1, false}}); first.pane.switch_active_tab("0");
    EXPECT(first.pane.list_snapshot().updating); first.deliver({{L"row", 1, false}});
    for (int i = 2; i < 101; ++i) {
        first.pane.switch_active_tab(std::to_string(i));
        first.deliver({{L"row", 1, false}});
        const auto stats = app_shell::Pane::list_cache_stats();
        EXPECT(stats.bytes <= stats.limit && stats.entries <= 2);
    }
    EXPECT(app_shell::Pane::list_cache_stats().evictions > initial.evictions);
    second.pane.switch_active_tab("0");
    EXPECT(second.pane.list_snapshot().rows.empty());
    EXPECT(!second.pane.list_snapshot().updating);
    app_shell::Pane::limit_list_cache_bytes(0);
    EXPECT(app_shell::Pane::list_cache_stats().bytes == 0);
    EXPECT(app_shell::Pane::list_cache_stats().entries == 0);
    app_shell::Pane::limit_list_cache_bytes(32 * 1024 * 1024);
}
void binding_and_location_invalidation() {
    Fixture f;
    f.deliver({{L"old", 1, false}}); f.pane.switch_active_tab("1");
    f.state.tabs[0].location = {L"C:\\different", {}, {}};
    f.pane.switch_active_tab("0");
    EXPECT(f.pane.list_snapshot().rows.empty());
    f.deliver({{L"different", 1, false}}); f.pane.switch_active_tab("1");
    f.pane.close_tab("0");
    EXPECT(app_shell::Pane::list_cache_stats().entries == 0);
    f.deliver({{L"group-a", 1, false}}); f.pane.switch_active_tab("2");
    auto other = f.state;
    f.host.group_id = "other-group"; f.pane.bind(&other);
    EXPECT(app_shell::Pane::list_cache_stats().entries == 1);
    EXPECT(SUCCEEDED(f.pane.navigate_to(f.pane.active_tab()->location)));
    EXPECT(f.pane.list_snapshot().rows.empty());
    f.pane.unbind(); // hiding keeps snapshots; an incomplete list is not admitted
    EXPECT(app_shell::Pane::list_cache_stats().entries == 1);
    app_shell::Pane::discard_group_list_cache("other-group");
    EXPECT(app_shell::Pane::list_cache_stats().entries == 1);
    app_shell::Pane::discard_group_list_cache("group-a");
    EXPECT(app_shell::Pane::list_cache_stats().entries == 0);
}
void group_round_trip_refresh_and_teardown() {
    Fixture f;
    auto enrichment = std::make_unique<Enrichment>();
    auto* icons = enrichment.get();
    f.pane.use_list_icons(std::move(enrichment));
    std::vector<ml::Row> rows;
    for (int i = 0; i < 100; ++i)
        rows.push_back({L"item-" + std::to_wstring(100 + i), 1, false});
    f.deliver(std::move(rows)); f.select(40);
    ListView_EnsureVisible(f.list, 65, FALSE);
    const auto before = f.pane.list_interaction_snapshot();
    const auto top = std::wstring(before.viewport_anchor->name);
    const auto old = f.pane.list_snapshot().request;
    const auto cancelled = f.source->cancellations;
    const auto icon_cancelled = icons->cancellations;
    f.deliver({}, ml::Phase::complete, ml::Change::watching);
    EXPECT(f.pane.list_snapshot().watch == ml::WatchState::watching);
    auto other = f.state; // deliberately identical tab and full location identity
    f.host.group_id = "group-b"; f.pane.bind(&other);
    EXPECT(!f.source->interested && f.source->cancellations > cancelled);
    EXPECT(icons->cancellations > icon_cancelled && icons->interest.empty());
    EXPECT(f.pane.list_snapshot().rows.empty());
    EXPECT(ListView_GetItemCount(f.list) == 0);
    EXPECT(app_shell::Pane::list_cache_stats().entries == 1);
    EXPECT(SUCCEEDED(f.pane.navigate_to(other.tabs[0].location)));
    EXPECT(f.pane.list_snapshot().rows.empty()); // Group identity isolates A
    f.deliver({{L"only-b", 2, false}});
    f.host.group_id = "group-a"; f.pane.bind(&f.state);
    EXPECT(!f.source->interested);
    EXPECT(f.pane.list_snapshot().rows.empty());
    const auto submissions = f.source->submissions;
    EXPECT(SUCCEEDED(f.pane.navigate_to(f.state.tabs[0].location)));
    EXPECT(f.source->submissions == submissions + 1);
    EXPECT(f.pane.list_snapshot().rows.size() == 100);
    EXPECT(f.pane.list_snapshot().updating);
    EXPECT(f.pane.list_snapshot().request.binding.generation != old.binding.generation);
    EXPECT(f.pane.list_snapshot().watch != ml::WatchState::watching);
    wchar_t status[256]{}; GetWindowTextW(f.pane.status_bar(), status, 256);
    EXPECT(std::wstring(status).find(L"Updating") != std::wstring::npos);
    const auto restored = f.pane.list_interaction_snapshot();
    EXPECT(restored.selected.size() == 1 && restored.selected[0].name == L"item-140");
    EXPECT(restored.focused && restored.focused->name == L"item-140");
    EXPECT(restored.anchor && restored.anchor->name == L"item-140");
    EXPECT(restored.viewport_anchor && restored.viewport_anchor->name == top);
    EXPECT(restored.viewport_offset == before.viewport_offset);
    f.source->batches.push_back({old, {{L"stale-a", 0, false}}, ml::Phase::complete, 0});
    f.pane.process_content_results();
    EXPECT(f.pane.list_snapshot().rows.size() == 100);
    f.deliver({{L"item-140", 20, false}, {L"new-a", 3, false}});
    EXPECT(!f.pane.list_snapshot().updating);
    EXPECT(f.pane.list_snapshot().rows.size() == 2);
    EXPECT(f.pane.list_interaction_snapshot().selected[0].name == L"item-140");
    // Unbinding hides the pane: it keeps B's snapshot and admits A's completed
    // list. Deleting B discards B's snapshots, because numeric Group IDs can be
    // reused and a reused ID must never hit the deleted Group's list.
    f.pane.unbind();
    EXPECT(app_shell::Pane::list_cache_stats().entries == 2);
    EXPECT(!f.source->interested);
    app_shell::Pane::discard_group_list_cache("group-b");
    EXPECT(app_shell::Pane::list_cache_stats().entries == 1);
    f.host.group_id = "group-b"; f.pane.bind(&other);
    EXPECT(SUCCEEDED(f.pane.navigate_to(other.tabs[0].location)));
    EXPECT(f.pane.list_snapshot().rows.empty());
    f.deliver({}, ml::Phase::empty);
    f.host.group_id = "group-a"; f.pane.bind(&f.state);
    EXPECT(app_shell::Pane::list_cache_stats().entries == 2); // completed empty is admitted
    f.host.shutting_down = true;
    f.pane.destroy();
    EXPECT(app_shell::Pane::list_cache_stats().entries == 0);
    EXPECT(app_shell::Pane::list_cache_stats().bytes == 0);
}
void hidden_pane_rebind_hits_cache_and_teardown_clears_owner() {
    Fixture f;
    f.deliver({{L"kept", 1, false}});
    // A layout or Group with fewer panes hides this one; that is not teardown.
    f.pane.unbind();
    EXPECT(app_shell::Pane::list_cache_stats().entries == 1);
    EXPECT(!f.source->interested);
    const auto hits = app_shell::Pane::list_cache_stats().hits;
    f.pane.bind(&f.state);
    EXPECT(SUCCEEDED(f.pane.navigate_to(f.state.tabs[0].location)));
    EXPECT(app_shell::Pane::list_cache_stats().hits == hits + 1);
    EXPECT(f.pane.list_snapshot().updating);
    EXPECT(f.pane.list_snapshot().rows.size() == 1 && f.pane.list_snapshot().rows[0].name == L"kept");
    wchar_t status[256]{}; GetWindowTextW(f.pane.status_bar(), status, 256);
    EXPECT(std::wstring(status).find(L"Updating") != std::wstring::npos);
    f.deliver({{L"kept", 1, false}});
    f.pane.switch_active_tab("1"); f.deliver({}, ml::Phase::empty);
    EXPECT(app_shell::Pane::list_cache_stats().entries == 1);
    f.host.shutting_down = true;
    f.pane.destroy();
    EXPECT(app_shell::Pane::list_cache_stats().entries == 0);
    EXPECT(app_shell::Pane::list_cache_stats().bytes == 0);
}
void cache_hit_adopts_worker_type_cells() {
    Fixture f;
    f.deliver({{L"a.txt", 1, false}});
    f.pane.switch_active_tab("1"); f.deliver({}, ml::Phase::empty);
    f.pane.switch_active_tab("0");
    EXPECT(f.pane.list_snapshot().updating);
    EXPECT(f.pane.list_snapshot().rows[0].properties[0].state == ml::PropertyState::unresolved);
    // The worker fills Type and Duration's missing state while enumerating.
    ml::Row fresh{L"a.txt", 1, false};
    fresh.properties[0].state = ml::PropertyState::value;
    fresh.properties[0].text = L"Text Document";
    fresh.properties[1].state = ml::PropertyState::missing;
    std::vector<ml::Row> rows; rows.push_back(std::move(fresh));
    f.deliver(std::move(rows));
    const auto& row = f.pane.list_snapshot().rows;
    EXPECT(!f.pane.list_snapshot().updating && row.size() == 1);
    EXPECT(row[0].properties[0].state == ml::PropertyState::value);
    EXPECT(row[0].properties[0].text == L"Text Document");
    EXPECT(row[0].properties[1].state == ml::PropertyState::missing);
    EXPECT(row[0].properties[2].state == ml::PropertyState::unresolved);
}
void group_snapshots_share_global_lru() {
    Fixture f;
    auto other = f.state;
    f.deliver({{L"a", 1, false}});
    f.host.group_id = "group-b"; f.pane.bind(&other);
    const auto one = app_shell::Pane::list_cache_stats().bytes;
    EXPECT(one > 0);
    app_shell::Pane::limit_list_cache_bytes(one);
    const auto evictions = app_shell::Pane::list_cache_stats().evictions;
    EXPECT(SUCCEEDED(f.pane.navigate_to(other.tabs[0].location)));
    f.deliver({{L"b", 1, false}});
    f.host.group_id = "group-a"; f.pane.bind(&f.state);
    const auto stats = app_shell::Pane::list_cache_stats();
    EXPECT(stats.bytes <= one && stats.entries == 1);
    EXPECT(stats.evictions == evictions + 1);
    EXPECT(SUCCEEDED(f.pane.navigate_to(f.state.tabs[0].location)));
    EXPECT(f.pane.list_snapshot().rows.empty()); // A was least recently used
    app_shell::Pane::limit_list_cache_bytes(32 * 1024 * 1024);
}
void group_partial_and_full_location_identity() {
    Fixture f;
    f.deliver({{L"partial", 1, false}}, ml::Phase::loading);
    auto other = f.state;
    f.host.group_id = "group-b"; f.pane.bind(&other);
    EXPECT(app_shell::Pane::list_cache_stats().entries == 0);
    EXPECT(SUCCEEDED(f.pane.navigate_to(other.tabs[0].location)));
    f.deliver({{L"complete-b", 1, false}});
    f.host.group_id = "group-a"; f.pane.bind(&f.state);
    EXPECT(SUCCEEDED(f.pane.navigate_to(f.state.tabs[0].location)));
    EXPECT(f.pane.list_snapshot().rows.empty());
    f.deliver({{L"complete-a", 1, false}});
    f.host.group_id = "group-b"; f.pane.bind(&other);
    other.tabs[0].location.known_folder_identity = L"changed-known-folder";
    EXPECT(SUCCEEDED(f.pane.navigate_to(other.tabs[0].location)));
    EXPECT(f.pane.list_snapshot().rows.empty());
    f.host.group_id = "group-a"; f.pane.bind(&f.state);
    f.state.tabs[0].location.fallback_path = L"C:\\changed-fallback";
    EXPECT(SUCCEEDED(f.pane.navigate_to(f.state.tabs[0].location)));
    EXPECT(f.pane.list_snapshot().rows.empty());
}
void unwatched_cache_refresh_does_not_claim_subscription() {
    Fixture f(3, L"\\\\server\\share\\cache");
    f.deliver({{L"before", 1, false}});
    EXPECT(f.pane.list_snapshot().watch == ml::WatchState::inactive);
    f.pane.switch_active_tab("1"); f.deliver({}, ml::Phase::empty);
    f.pane.switch_active_tab("0");
    EXPECT(f.pane.list_snapshot().updating);
    f.deliver({{L"after", 2, false}});
    EXPECT(!f.pane.list_snapshot().updating);
    EXPECT(f.pane.list_snapshot().rows.size() == 1);
    EXPECT(f.pane.list_snapshot().rows[0].name == L"after");
    EXPECT(f.pane.list_snapshot().watch == ml::WatchState::inactive);
    // Only an actual worker subscription may change that state.
    f.deliver({}, ml::Phase::complete, ml::Change::watching);
    EXPECT(f.pane.list_snapshot().watch == ml::WatchState::watching);
}
void viewport_and_refresh_failure() {
    Fixture f;
    std::vector<ml::Row> rows;
    for (int i = 0; i < 100; ++i)
        rows.push_back({L"item-" + std::to_wstring(100 + i), 1, false});
    f.deliver(std::move(rows));
    f.select(40);
    ListView_EnsureVisible(f.list, 65, FALSE);
    const auto before = f.pane.list_interaction_snapshot();
    const auto top = std::wstring(before.viewport_anchor->name);
    f.pane.switch_active_tab("1"); f.deliver({}, ml::Phase::empty);
    f.pane.switch_active_tab("0");
    auto restored = f.pane.list_interaction_snapshot();
    EXPECT(restored.viewport_anchor && restored.viewport_anchor->name == top);
    EXPECT(restored.viewport_offset == before.viewport_offset);
    f.deliver({}, ml::Phase::error, ml::Change::none, ERROR_ACCESS_DENIED);
    EXPECT(f.pane.list_snapshot().phase == ml::Phase::error);
    EXPECT(f.pane.list_snapshot().watch == ml::WatchState::failed);
    EXPECT(!f.pane.list_snapshot().updating);
    EXPECT(f.pane.list_snapshot().rows.size() == 100); // incomplete scan cannot delete unseen rows
    EXPECT(f.pane.list_index_bytes() == 0);
    f.pane.refresh_view();
    EXPECT(f.pane.list_snapshot().rows.empty());
    f.deliver({}, ml::Phase::empty);
    EXPECT(f.pane.list_snapshot().rows.empty());
}
void retained_capacity_includes_property_metadata() {
    std::size_t plain{};
    {
        Fixture f;
        f.deliver({{L"row", 1, false}}); f.pane.switch_active_tab("1");
        plain = app_shell::Pane::list_cache_stats().bytes;
    }
    {
        Fixture f;
        ml::Row row{L"row", 1, false};
        row.name.reserve(1024);
        row.properties[0].state = ml::PropertyState::value;
        row.properties[0].text.assign(128, L'x');
        row.properties[0].text.reserve(4096);
        const auto allocated = (row.name.capacity() + row.properties[0].text.capacity()) * sizeof(wchar_t);
        std::vector<ml::Row> rows; rows.push_back(std::move(row));
        f.deliver(std::move(rows));
        f.pane.switch_active_tab("1");
        const auto bytes = app_shell::Pane::list_cache_stats().bytes;
        EXPECT(bytes > plain + allocated - 256);
        EXPECT(bytes <= app_shell::Pane::list_cache_stats().limit);
        f.pane.switch_active_tab("0");
        EXPECT(f.pane.list_snapshot().updating);
        EXPECT(f.pane.list_snapshot().rows[0].properties[0].state == ml::PropertyState::unresolved);
    }
    // A single snapshot that cannot fit must be dropped, not exempted.
    app_shell::Pane::limit_list_cache_bytes(plain - 1);
    {
        Fixture f;
        f.deliver({{L"row", 1, false}}); f.pane.switch_active_tab("1");
        EXPECT(app_shell::Pane::list_cache_stats().bytes == 0);
        f.pane.switch_active_tab("0");
        EXPECT(f.pane.list_snapshot().rows.empty());
    }
    app_shell::Pane::limit_list_cache_bytes(32 * 1024 * 1024);
}
void refresh_index_capacity_failure_is_bounded() {
    Fixture f(3, L"C:\\cache", 1);
    f.deliver({{L"row", 1, false}}); f.pane.switch_active_tab("1");
    f.deliver({}, ml::Phase::empty); f.pane.switch_active_tab("0");
    EXPECT(f.pane.list_snapshot().phase == ml::Phase::error);
    EXPECT(f.pane.list_snapshot().watch == ml::WatchState::failed);
    EXPECT(!f.pane.list_snapshot().updating);
    EXPECT(f.pane.list_snapshot().rows.size() == 1);
    EXPECT(f.pane.list_index_bytes() == 0);
    EXPECT(f.pane.list_index_peak_bytes() <= 1);
}

ml::Row shell_row(std::wstring key, std::uint64_t size = 1) {
    ml::Row row{L"Same display: name", size, true, size};
    row.identity = {key, true};
    row.child_location = core::ShellLocation{std::move(key), L"KnownFolder", L"C:\\fallback"};
    return row;
}
void duplicate_display_names_keep_identity_through_sort_cache_and_recovery() {
    Fixture f;
    f.deliver({shell_row(L"::{provider}\\one", 2), shell_row(L"::{provider}\\two", 1)});
    const auto index = [&](std::wstring_view key) {
        const auto& rows = f.pane.list_snapshot().rows;
        const auto it = std::find_if(rows.begin(), rows.end(), [&](const auto& row) {
            return row.identity.key == key;
        });
        return static_cast<int>(it - rows.begin());
    };
    f.select(index(L"::{provider}\\one"));
    const auto check = [&] {
        const auto interaction = f.pane.list_interaction_snapshot();
        EXPECT(interaction.selected.size() == 1);
        if (!interaction.selected.empty()) {
            EXPECT(interaction.selected[0].name == L"::{provider}\\one");
            EXPECT(interaction.selected[0].shell_item);
        }
        EXPECT(interaction.focused && interaction.focused->name == L"::{provider}\\one");
        EXPECT(interaction.anchor && interaction.anchor->name == L"::{provider}\\one");
    };
    // Date sort moves the selected identity to the opposite end.
    f.state.tabs[0].sort_column = "System.DateModified";
    f.state.tabs[0].sort_ascending = false; f.pane.apply_sort(); check();
    EXPECT(index(L"::{provider}\\one") == 0);
    f.pane.switch_active_tab("1"); f.deliver({}, ml::Phase::empty);
    f.pane.switch_active_tab("0"); check();
    EXPECT(f.pane.list_snapshot().rows[index(L"::{provider}\\one")].child_location ==
        shell_row(L"::{provider}\\one").child_location);
    // Cached scan is a fresh request. Both same-text identities must survive,
    // including a display-only rename with unchanged size/time metadata.
    auto renamed_display = shell_row(L"::{provider}\\one", 2);
    renamed_display.name = L"New display";
    f.deliver({shell_row(L"::{provider}\\two", 1)}, ml::Phase::loading);
    f.deliver({std::move(renamed_display)}); check();
    EXPECT(f.pane.list_snapshot().rows.size() == 2);
    EXPECT(f.pane.list_snapshot().rows[index(L"::{provider}\\one")].name == L"New display");
    // Overflow recovery uses the same key index. Losing one item must not select
    // its same-text replacement or leave a Shift-select mark on that replacement.
    f.deliver({}, ml::Phase::loading, ml::Change::rescan_begin);
    auto same_text_replacement = shell_row(L"::{provider}\\two", 1);
    same_text_replacement.name = L"New display";
    f.deliver({std::move(same_text_replacement)}, ml::Phase::complete, ml::Change::rescan);
    EXPECT(f.pane.list_snapshot().rows.size() == 1);
    EXPECT(f.pane.list_interaction_snapshot().selected.empty());
    EXPECT(!f.pane.list_interaction_snapshot().anchor);
    EXPECT(f.pane.list_index_bytes() == 0);
}
void identity_capacities_obey_active_and_cache_budgets() {
    // Each independent owned identity string can exhaust admission even when
    // its text is short. A retry must discard the rejected storage.
    for (int field = 0; field < 4; ++field) {
        Fixture f;
        auto row = shell_row(L"::{item}");
        auto* text = field == 0 ? &row.identity.key : field == 1 ? &row.child_location->parsing_name :
            field == 2 ? &row.child_location->known_folder_identity : &row.child_location->fallback_path;
        text->reserve(ml::kMaxRowBytes / sizeof(wchar_t));
        std::vector<ml::Row> rows; rows.push_back(std::move(row));
        f.deliver(std::move(rows));
        EXPECT(f.pane.list_snapshot().phase == ml::Phase::limited);
        EXPECT(f.pane.list_snapshot().rows.empty());
        f.pane.refresh_view(); f.deliver({shell_row(L"::{retry}")});
        EXPECT(f.pane.list_snapshot().phase == ml::Phase::complete);
        // Recovery replacement admission uses the same capacity accounting.
        auto huge = shell_row(L"::{retry}", 2);
        huge.child_location->fallback_path.reserve(ml::kMaxRowBytes / sizeof(wchar_t));
        f.deliver({}, ml::Phase::loading, ml::Change::rescan_begin);
        std::vector<ml::Row> replacement; replacement.push_back(std::move(huge));
        f.deliver(std::move(replacement), ml::Phase::complete, ml::Change::rescan);
        EXPECT(f.pane.list_snapshot().watch == ml::WatchState::failed);
        EXPECT(f.pane.list_snapshot().rows[0].bytes == 1);
        EXPECT(f.pane.list_index_bytes() == 0);
    }
    std::size_t ordinary{};
    {
        Fixture f; f.deliver({shell_row(L"::{item}")}); f.select(0);
        f.pane.switch_active_tab("1"); ordinary = app_shell::Pane::list_cache_stats().bytes;
    }
    {
        Fixture f;
        auto row = shell_row(L"::{item}");
        row.identity.key.reserve(4096);
        row.child_location->parsing_name.reserve(4096);
        row.child_location->known_folder_identity.reserve(4096);
        row.child_location->fallback_path.reserve(4096);
        std::vector<ml::Row> rows; rows.push_back(std::move(row));
        f.deliver(std::move(rows)); f.select(0); f.pane.switch_active_tab("1");
        EXPECT(app_shell::Pane::list_cache_stats().bytes > ordinary + 4 * 4000 * sizeof(wchar_t));
        app_shell::Pane::limit_list_cache_bytes(ordinary);
        EXPECT(app_shell::Pane::list_cache_stats().bytes == 0);
        f.pane.switch_active_tab("0"); EXPECT(f.pane.list_snapshot().rows.empty());
    }
    app_shell::Pane::limit_list_cache_bytes(32 * 1024 * 1024);
}

void contradictory_or_missing_shell_keys_are_rejected_without_display_fallback() {
    for (int kind = 0; kind < 4; ++kind) {
        Fixture f;
        auto row = shell_row(L"::{item}");
        if (kind == 0) row.child_location->parsing_name = L"::{different}";
        if (kind == 1) row.identity.key.clear();
        if (kind == 2) { row.identity.key.clear(); row.child_location.reset(); }
        if (kind == 3) row.identity.shell_item = false;
        // No shell/local reinterpretation and no fallback to Row::name.
        f.deliver({row});
        EXPECT(f.pane.list_snapshot().phase == ml::Phase::error);
        EXPECT(f.pane.list_snapshot().error == ERROR_INVALID_DATA);
        EXPECT(f.pane.list_snapshot().rows.empty());
        f.pane.refresh_view(); f.deliver({shell_row(L"::{valid}")});
        EXPECT(f.pane.list_snapshot().rows.size() == 1);
        // The delta/recovery admission must enforce the same invariant.
        f.deliver({}, ml::Phase::loading, ml::Change::rescan_begin);
        f.deliver({row}, ml::Phase::complete, ml::Change::rescan);
        EXPECT(f.pane.list_snapshot().watch == ml::WatchState::failed);
        EXPECT(f.pane.list_snapshot().watch_error == ERROR_INVALID_DATA);
        EXPECT(f.pane.list_snapshot().rows.size() == 1);
        EXPECT(f.pane.list_snapshot().rows[0].identity.key == L"::{valid}");
        EXPECT(f.pane.list_index_bytes() == 0);
    }
}
} // namespace
int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    contradictory_or_missing_shell_keys_are_rejected_without_display_fallback();
    duplicate_display_names_keep_identity_through_sort_cache_and_recovery();
    identity_capacities_obey_active_and_cache_budgets();
    warm_refresh_and_notifications(); missing_identity_and_failed_scan();
    global_budget_and_lru(); binding_and_location_invalidation();
    group_round_trip_refresh_and_teardown(); group_partial_and_full_location_identity();
    group_snapshots_share_global_lru();
    hidden_pane_rebind_hits_cache_and_teardown_clears_owner();
    cache_hit_adopts_worker_type_cells();
    unwatched_cache_refresh_does_not_claim_subscription();
    viewport_and_refresh_failure();
    retained_capacity_includes_property_metadata();
    refresh_index_capacity_failure_is_bounded();
    EXPECT(app_shell::Pane::list_cache_stats().bytes == 0);
    return test::summary("managed_list_cache");
}
