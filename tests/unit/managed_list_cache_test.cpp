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
    bool submit(ml::Request) noexcept override { ++submissions; return true; }
    std::optional<ml::Batch> take() noexcept override {
        if (batches.empty()) return {};
        auto result = std::move(batches.front()); batches.pop_front(); return result;
    }
    HANDLE event() const noexcept override { return nullptr; }
    void cancel() noexcept override {} // late results deliberately survive
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
        list = FindWindowExW(pane.explorer_container(), nullptr, WC_LISTVIEWW, nullptr);
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
    EXPECT(app_shell::Pane::list_cache_stats().entries == 0);
    EXPECT(SUCCEEDED(f.pane.navigate_to(f.pane.active_tab()->location)));
    EXPECT(f.pane.list_snapshot().rows.empty());
    f.pane.unbind();
    EXPECT(app_shell::Pane::list_cache_stats().entries == 0);
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
} // namespace
int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    warm_refresh_and_notifications(); missing_identity_and_failed_scan();
    global_budget_and_lru(); binding_and_location_invalidation();
    unwatched_cache_refresh_does_not_claim_subscription();
    viewport_and_refresh_failure();
    retained_capacity_includes_property_metadata();
    refresh_index_capacity_failure_is_bounded();
    EXPECT(app_shell::Pane::list_cache_stats().bytes == 0);
    return test::summary("managed_list_cache");
}
