#include "unit/test_pane_host.h"
#include "unit/test_util.h"
#include "core/session.h"
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>

using namespace panedock;
namespace ml = managed_list;
namespace {
const core::ShellLocation pc{L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", {}, {}};
const core::ShellLocation desktop{L"::{00021400-0000-0000-C000-000000000046}", L"Desktop-id", {}};
const core::ShellLocation child{L"::{provider}\\absolute-child", L"Child-id", L"C:\\fallback"};
const core::ShellLocation local{L"C:\\namespace-test", {}, {}};
class Delivery final : public ml::Source {
public:
    std::deque<ml::Batch> batches;
    std::vector<ml::Request> submitted;
    bool submit(ml::Request request) noexcept override {
        submitted.push_back(std::move(request)); return true;
    }
    std::optional<ml::Batch> take() noexcept override {
        if (batches.empty()) return {};
        auto result = std::move(batches.front()); batches.pop_front(); return result;
    }
    HANDLE event() const noexcept override { return nullptr; }
    void cancel() noexcept override {} // deliberately deliver obsolete results
};
struct Host final : test::TestPaneHost {
    std::function<void()> on_save;
    void schedule_session_save() noexcept override {
        ++session_saves;
        if (auto action = std::exchange(on_save, {})) action();
    }
};
core::ApplicationState sample() {
    core::ApplicationState app;
    core::GroupState group;
    group.id = "group-a"; group.name = L"Namespace";
    group.layout_template = core::LayoutTemplate::left_right;
    group.divider_ratios = core::default_divider_ratios(group.layout_template);
    core::reserve_panes(group);
    group.panes.push_back({"pane-a", {{"tab-a", pc, "FVM_DETAILS", "System.ItemNameDisplay", true, {}, 0}}, "tab-a"});
    group.panes.push_back({"pane-b", {{"tab-b", desktop, "FVM_DETAILS", "System.ItemNameDisplay", true, {}, 0}}, "tab-b"});
    group.active_pane_id = "pane-a";
    app.groups.push_back(std::move(group)); app.active_group_id = "group-a";
    EXPECT(core::is_valid(app));
    return app;
}
ml::Row folder(const core::ShellLocation& target, std::wstring display = L"Same display") {
    ml::Row row{std::move(display), 0, true};
    row.identity = {target.parsing_name, true}; row.child_location = target;
    return row;
}
struct Fixture final {
    Host host;
    core::ApplicationState app;
    app_shell::Pane pane;
    Delivery* source{};
    HWND parent{}, list{};
    explicit Fixture(core::ApplicationState state = sample()) : app(std::move(state)) {
        EXPECT(app_shell::Pane::register_window_class(GetModuleHandleW(nullptr)));
        parent = CreateWindowExW(0, L"STATIC", L"namespace test", 0, 0, 0, 800, 600,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        EXPECT(parent != nullptr);
        pane.set_host(&host); pane.set_reentry_guard(&host.guard);
        EXPECT(pane.create(parent, 0)); pane.bind(&app.groups[0].panes[0]);
        auto input = std::make_unique<Delivery>(); source = input.get();
        pane.use_managed_list(std::move(input));
        pane.use_list_icons(nullptr); // owned-value test only, no provider demand
        EXPECT(SUCCEEDED(pane.realize({0, 0, 640, 420}, pane.active_tab()->location)));
        list = FindWindowExW(pane.explorer_container(), nullptr, panedock::app_shell::kManagedListClassName, nullptr);
        EXPECT(list != nullptr);
    }
    ~Fixture() { pane.destroy(); DestroyWindow(parent); }
    ml::Batch batch(core::ShellLocation resolved, std::optional<core::ShellLocation> parent_location = {},
                    std::vector<ml::Row> rows = {}, ml::Phase phase = ml::Phase::complete) {
        ml::Batch result{pane.list_snapshot().request, std::move(rows), phase};
        result.resolved_location = std::move(resolved);
        result.parent_outcome = parent_location ? ml::ParentOutcome::value : ml::ParentOutcome::root;
        result.parent_location = std::move(parent_location);
        result.notification = ml::NotificationCapability::manual_refresh;
        return result;
    }
    void deliver(ml::Batch result) {
        source->batches.push_back(std::move(result)); pane.process_content_results();
    }
    void complete(core::ShellLocation resolved, std::optional<core::ShellLocation> parent_location = {},
                  std::vector<ml::Row> rows = {}) {
        deliver(batch(std::move(resolved), std::move(parent_location), std::move(rows)));
    }
    void fail() {
        ml::Batch result{pane.list_snapshot().request, {}, ml::Phase::error, ERROR_ACCESS_DENIED};
        result.notification = ml::NotificationCapability::manual_refresh;
        deliver(std::move(result));
    }
    void select(int index) {
        ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetSelectionMark(list, index);
    }
};
void virtual_navigation_and_history() {
    Fixture f;
    EXPECT(f.source->submitted.back().location == pc);
    EXPECT(f.pane.navigate_up_one_level() == E_PENDING);
    auto canonical_pc = pc; canonical_pc.known_folder_identity = L"Computer-id";
    f.complete(canonical_pc, desktop, {folder(desktop)});
    EXPECT(f.pane.active_tab()->location == canonical_pc);
    EXPECT(f.pane.active_tab()->history.empty()); // canonical restore is not a new visit
    EXPECT(f.pane.list_snapshot().request.location == pc); // original batch identity
    EXPECT(f.pane.content_location() == canonical_pc);
    EXPECT(f.pane.open_list_row(0));
    EXPECT(f.source->submitted.back().location == desktop);
    auto second = child; second.parsing_name += L"-two";
    f.complete(desktop, {}, {folder(child), folder(second)});
    EXPECT(f.pane.navigate_up_one_level() == S_FALSE);
    EXPECT(f.pane.list_snapshot().notification == ml::NotificationCapability::manual_refresh);
    EXPECT(f.pane.list_snapshot().watch != ml::WatchState::watching);
    wchar_t status[256]{}; GetWindowTextW(f.pane.status_bar(), status, 256);
    EXPECT(std::wstring(status).find(L"Automatic refresh unavailable. Use Refresh to update.") != std::wstring::npos);
    EXPECT(f.pane.open_list_row(1));
    EXPECT(f.source->submitted.back().location == second); // same display, distinct identity
    f.complete(second, desktop);
    EXPECT(SUCCEEDED(f.pane.navigate_up_one_level()));
    EXPECT(f.source->submitted.back().location == desktop);
    f.complete(desktop);
    EXPECT(SUCCEEDED(f.pane.navigate_to(local))); f.complete(local);
    const auto before = *f.pane.active_tab();
    f.pane.navigate_history(true);
    EXPECT(f.source->submitted.back().location == desktop);
    EXPECT(*f.pane.active_tab() == before);
    f.fail(); EXPECT(*f.pane.active_tab() == before);
    f.pane.refresh_view();
    EXPECT(f.source->submitted.back().location == desktop);
    f.complete(desktop);
    EXPECT(f.pane.active_tab()->history.size() == before.history.size());
    EXPECT(f.pane.active_tab()->history_index + 1 == before.history_index);
    f.pane.navigate_history(false); f.complete(local);
    EXPECT(*f.pane.active_tab() == before);
    EXPECT(SUCCEEDED(f.pane.navigate_to(child))); f.fail();
    EXPECT(*f.pane.active_tab() == before);
    EXPECT(f.pane.navigate_up_one_level() == E_PENDING);
    f.pane.refresh_view(); EXPECT(f.source->submitted.back().location == child);
    f.complete(child, desktop);
    const auto history = f.pane.active_tab()->history.size();
    f.pane.refresh_view(); f.complete(child, desktop);
    EXPECT(f.pane.active_tab()->history.size() == history);
    const auto first_tab = *f.pane.active_tab();
    f.pane.add_tab(desktop); f.complete(desktop);
    EXPECT(f.pane.active_tab()->history.empty());
    f.pane.switch_active_tab("tab-a"); f.complete(child, desktop);
    EXPECT(*f.pane.active_tab() == first_tab); // each tab keeps its own history
    EXPECT(f.pane.window() != nullptr && f.list == FindWindowExW(f.pane.explorer_container(), nullptr, panedock::app_shell::kManagedListClassName, nullptr));
}
void namespace_child_keeps_enumerating_parent() {
    Fixture f;
    auto desktop_root = core::ShellLocation{
        L"D:\\Documents\\Desktop", L"{B4BFCC3A-DB2C-424C-B029-7FE99A87C641}",
        L"D:\\Documents\\Desktop"};
    desktop_root.namespace_location = true;
    const core::ShellLocationParent desktop_identity{
        desktop_root.parsing_name, desktop_root.known_folder_identity,
        desktop_root.fallback_path, desktop_root.namespace_location};
    auto child_location = child;
    child_location.navigation_parent = desktop_identity;

    EXPECT(SUCCEEDED(f.pane.navigate_to(desktop_root)));
    f.complete(desktop_root, {}, {folder(child_location, L"CpuDieId")});
    EXPECT(f.pane.open_list_row(0));
    EXPECT(f.source->submitted.back().location.navigation_parent == desktop_identity);
    f.complete(child_location);
    EXPECT(f.pane.list_snapshot().parent_outcome == ml::ParentOutcome::root);
    EXPECT(SUCCEEDED(f.pane.navigate_up_one_level()));
    EXPECT(f.source->submitted.back().location == desktop_root);
    f.complete(desktop_root);
    f.pane.navigate_history(true);
    EXPECT(f.source->submitted.back().location == child_location);
    f.complete(child_location);
    EXPECT(SUCCEEDED(f.pane.navigate_up_one_level()));
    EXPECT(f.source->submitted.back().location == desktop_root);
}
void defaults_restore_and_stale_parent() {
    Fixture f;
    f.complete(pc, desktop);
    f.pane.add_default_tab(); EXPECT(f.source->submitted.back().location == pc);
    auto late = f.batch(child, child, {folder(child)});
    f.pane.switch_active_tab("tab-a"); f.deliver(late);
    EXPECT(f.pane.active_tab()->location == pc);
    f.complete(pc, desktop);
    f.pane.close_tab("tab-new"); f.pane.close_tab("tab-a");
    EXPECT(f.pane.active_tab()->location == pc);
    EXPECT(f.source->submitted.back().location == pc);
    f.complete(pc, desktop);
    // New Group and layout restore use the same public binding/navigation seam.
    auto group = sample().groups[0]; group.id = "group-new";
    f.app.groups.push_back(std::move(group));
    const auto old = f.batch(child, child, {folder(child)});
    f.host.group_id = f.app.active_group_id = "group-new";
    f.pane.bind(&f.app.groups[1].panes[0]);
    EXPECT(SUCCEEDED(f.pane.navigate_to(f.pane.active_tab()->location)));
    EXPECT(f.source->submitted.back().location == pc);
    f.deliver(old); EXPECT(f.pane.list_snapshot().rows.empty());
    EXPECT(f.pane.navigate_up_one_level() == E_PENDING);
    f.complete(pc, desktop);
    EXPECT(SUCCEEDED(f.pane.navigate_up_one_level()));
    EXPECT(f.source->submitted.back().location == desktop);
    f.complete(desktop);
    const auto shutdown_late = f.batch(child, child, {folder(child)});
    const auto saved = *f.pane.active_tab(); f.host.shutting_down = true;
    f.deliver(shutdown_late); EXPECT(*f.pane.active_tab() == saved);
    EXPECT(f.pane.navigate_up_one_level() == E_ABORT);
    f.host.shutting_down = false;
    f.pane.derealize(); f.deliver(shutdown_late);
    EXPECT(f.pane.list_snapshot().rows.empty());
    EXPECT(SUCCEEDED(f.pane.realize({0, 0, 640, 420}, desktop)));
    EXPECT(f.source->submitted.back().location == desktop);
}
void reentrant_completion() {
    for (int action = 0; action < 3; ++action) {
        Fixture f;
        f.host.on_save = [&] {
            if (action == 0) f.pane.add_default_tab();
            if (action == 1) { f.host.group_id = "group-other"; f.pane.unbind(); }
            if (action == 2) f.host.shutting_down = true;
        };
        f.complete(pc, child, {folder(child)});
        EXPECT(f.pane.list_snapshot().rows.empty());
        EXPECT(f.pane.navigate_up_one_level() != S_OK);
    }
}
void canonical_cache_and_refresh_interaction() {
    Fixture f;
    auto canonical = pc; canonical.known_folder_identity = L"Computer-id";
    std::vector<ml::Row> rows;
    for (int i = 0; i < 40; ++i) {
        auto target = child; target.parsing_name += std::to_wstring(i);
        rows.push_back(folder(target, L"Item " + std::to_wstring(i)));
    }
    f.complete(canonical, desktop, rows); f.select(12);
    ListView_EnsureVisible(f.list, 25, FALSE);
    const auto selected = std::wstring(f.pane.list_interaction_snapshot().selected[0].name);
    const auto viewport = std::wstring(f.pane.list_interaction_snapshot().viewport_anchor->name);
    const auto hits = app_shell::Pane::list_cache_stats().hits;
    f.pane.add_tab(desktop); f.complete(desktop);
    f.pane.switch_active_tab("tab-a");
    EXPECT(f.pane.list_snapshot().updating);
    EXPECT(app_shell::Pane::list_cache_stats().hits == hits + 1);
    EXPECT(f.source->submitted.back().location == canonical);
    f.complete(canonical, desktop, rows);
    f.pane.refresh_view();
    EXPECT(f.pane.list_snapshot().updating && f.pane.list_snapshot().rows.size() == 40);
    EXPECT(f.pane.list_interaction_snapshot().selected[0].name == selected);
    std::reverse(rows.begin(), rows.end());
    f.complete(canonical, desktop, rows);
    const auto after = f.pane.list_interaction_snapshot();
    EXPECT(after.selected.size() == 1 && after.selected[0].name == selected);
    EXPECT(after.anchor && after.anchor->name == selected);
    EXPECT(after.viewport_anchor && after.viewport_anchor->name == viewport);
    EXPECT(f.pane.list_snapshot().watch == ml::WatchState::inactive);
    const auto count = f.source->submitted.size();
    for (int i = 0; i < 10; ++i) f.pane.process_content_results();
    EXPECT(f.source->submitted.size() == count); // no work from idle dispatch
}
void namespace_metadata_bounds() {
    for (int field = 0; field < 3; ++field) {
        Fixture f;
        auto result = f.batch(pc, desktop);
        if (field < 2) {
            auto& location = field == 0 ? *result.resolved_location : *result.parent_location;
            location.fallback_path.reserve(ml::kMaxRowBytes / sizeof(wchar_t));
        } else {
            result.resolved_location->navigation_parent = core::ShellLocationParent{
                L"::parent", {}, {}};
            result.resolved_location->navigation_parent->fallback_path.reserve(
                ml::kMaxRowBytes / sizeof(wchar_t));
        }
        f.deliver(std::move(result));
        EXPECT(f.pane.list_snapshot().phase == ml::Phase::limited);
        EXPECT(f.pane.active_tab()->location == pc);
        f.pane.refresh_view(); f.complete(pc, desktop);
        EXPECT(f.pane.list_snapshot().phase == ml::Phase::complete);
    }
    std::size_t small{};
    for (int large = 0; large < 2; ++large) {
        Fixture f;
        auto result = f.batch(pc, desktop);
        if (large) result.parent_location->fallback_path.reserve(8192);
        f.deliver(std::move(result)); f.pane.add_tab(desktop);
        const auto bytes = app_shell::Pane::list_cache_stats().bytes;
        if (!large) small = bytes;
        else EXPECT(bytes > small + 8000 * sizeof(wchar_t));
    }
    Fixture f;
    auto invalid = f.batch(pc, desktop); invalid.parent_outcome = ml::ParentOutcome::root;
    f.deliver(std::move(invalid));
    EXPECT(f.pane.list_snapshot().phase == ml::Phase::error);
    EXPECT(f.pane.active_tab()->location == pc);
}
void namespace_route_session_round_trip() {
    auto app = sample();
    core::ShellLocation desktop_root{
        L"D:\\Documents\\Desktop", L"{B4BFCC3A-DB2C-424C-B029-7FE99A87C641}",
        L"D:\\Documents\\Desktop"};
    desktop_root.namespace_location = true;
    app.groups[0].panes[0].tabs[0].location = desktop_root;
    auto document = core::deserialize_session(core::serialize_session({app, {}, true}));
    EXPECT(document.has_value());
    if (!document) return;
    EXPECT(document->application.groups[0].panes[0].tabs[0].location == desktop_root);
}
void session_identity_and_unknown_fields() {
    auto app = sample();
    auto child_with_parent = child;
    child_with_parent.navigation_parent = core::ShellLocationParent{
        L"::{Desktop}", L"Desktop-id", L"C:\\Desktop", true};
    app.groups[0].panes[0].tabs[0].location = child_with_parent;
    auto json = core::serialize_session({app, {}, true});
    json.insert(json.find('{') + 1, "\"future-root\":{\"keep\":246},");
    json.insert(json.find("\"view_mode\""), "\"future-tab\":[246],");
    const auto nested = json.find('{', json.find("\"shell_location\""));
    json.insert(nested + 1, "\"future-location\":{\"keep\":true},");
    const auto navigation_parent = json.find('{', json.find("\"navigation_parent\""));
    json.insert(navigation_parent + 1, "\"future-parent\":{\"keep\":true},");
    auto document = core::deserialize_session(json); EXPECT(document.has_value());
    if (!document) return;
    EXPECT(core::serialize_session(*document).find(
        "\"future-parent\":{\"keep\":true}") != std::string::npos);
    Fixture f(document->application);
    EXPECT(f.source->submitted.back().location == child_with_parent);
    f.complete(child_with_parent, desktop);
    auto& group = f.app.groups[0];
    EXPECT(core::move_tab(group.panes[0], group.panes[1], "tab-a", 0, pc, "replacement"));
    EXPECT(group.panes[0].tabs[0].location == pc);
    f.pane.bind(&group.panes[1]);
    EXPECT(SUCCEEDED(f.pane.navigate_to(f.pane.active_tab()->location)));
    f.complete(child_with_parent, desktop);
    auto root = std::filesystem::current_path() / ("pd246-session-" + std::to_string(GetCurrentProcessId()));
    EXPECT(!std::filesystem::exists(root));
    app_shell::SessionWriter writer; writer.set_directory(root); writer.adopt(*document);
    const auto verify = [&](core::ShellLocation expected) {
        EXPECT(writer.write(f.app, true, nullptr));
        const auto restored = core::read_session(root);
        EXPECT(restored.source == core::SessionSource::primary);
        const auto& tab = restored.document.application.groups[0].panes[1].tabs[0];
        EXPECT(tab.id == "tab-a" && tab.location == expected);
        const auto saved = core::serialize_session(restored.document);
        EXPECT(saved.find("\"future-root\":{\"keep\":246}") != std::string::npos);
        EXPECT(saved.find("\"future-tab\":[246]") != std::string::npos);
        EXPECT(saved.find("\"future-location\":{\"keep\":true}") != std::string::npos);
        EXPECT(saved.find("\"future-parent\":{\"keep\":true}") != std::string::npos);
        EXPECT(restored.document.application.schema_version == 1);
    };
    EXPECT(SUCCEEDED(f.pane.navigate_to(desktop))); verify(child_with_parent); // pending save
    f.fail(); verify(child_with_parent); // error save preserves good identity
    f.pane.refresh_view(); EXPECT(f.source->submitted.back().location == desktop);
    f.complete(desktop); verify(desktop); // empty fallback is retained
    EXPECT(std::filesystem::exists(root / core::kSessionBackupFileName));
    const auto blocker = root / "blocker";
    { std::ofstream file(blocker); file << "block directory creation"; }
    writer.set_directory(blocker); EXPECT(!writer.write(f.app, true, nullptr));
    writer.set_directory(root); verify(desktop);
    std::filesystem::remove_all(root);
}
} // namespace
int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    virtual_navigation_and_history(); namespace_child_keeps_enumerating_parent();
    defaults_restore_and_stale_parent();
    reentrant_completion(); canonical_cache_and_refresh_interaction();
    namespace_metadata_bounds(); namespace_route_session_round_trip();
    session_identity_and_unknown_fields();
    EXPECT(app_shell::Pane::list_cache_stats().bytes == 0);
    return test::summary("managed_list_namespace");
}
