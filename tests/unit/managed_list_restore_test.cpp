#include "unit/test_pane_host.h"
#include "unit/test_util.h"
#include "core/session.h"
#include <deque>
#include <filesystem>
#include <functional>

using namespace panedock;
namespace {
class Delivery final : public managed_list::Source {
  public:
    std::deque<managed_list::Batch> batches;
    bool submit(managed_list::Request) noexcept override { return true; }
    std::optional<managed_list::Batch> take() noexcept override {
        if (batches.empty()) return {};
        auto batch = std::move(batches.front()); batches.pop_front(); return batch;
    }
    HANDLE event() const noexcept override { return nullptr; }
    void cancel() noexcept override {} // deliberately retain late deliveries
};
core::ApplicationState sample() {
    core::ApplicationState app;
    const std::array modes{"FVM_ICON:256", "FVM_ICON:96", "FVM_ICON:48", "FVM_ICON:16",
                           "FVM_LIST", "FVM_DETAILS", "FVM_TILE", "FVM_CONTENT"};
    for (int g = 0; g < 2; ++g) {
        core::GroupState group;
        group.id = "group-" + std::to_string(g); group.name = L"Work";
        group.layout_template = g == 0 ? core::LayoutTemplate::four_pane_grid : core::LayoutTemplate::single;
        group.divider_ratios = core::default_divider_ratios(group.layout_template);
        core::reserve_panes(group);
        for (int p = 0; p < 4; ++p) {
            core::PaneState pane; pane.id = "pane-" + std::to_string(p);
            for (int t = 0; t < 2; ++t) {
                const auto id = "tab-" + std::to_string(p * 2 + t);
                core::ShellLocation location{L"C:\\group" + std::to_wstring(g) +
                    L"\\pane" + std::to_wstring(p) + L"\\tab" + std::to_wstring(t),
                    L"preserved-known-folder", L"C:\\fallback"};
                pane.tabs.push_back({id, location, modes[p * 2 + t], "future-property-key", t == 0,
                                     {location}, 0});
            }
            pane.active_tab_id = pane.tabs[1].id;
            group.panes.push_back(std::move(pane));
        }
        group.active_pane_id = group.panes[0].id;
        app.groups.push_back(std::move(group));
    }
    app.active_group_id = app.groups[0].id;
    // This hidden namespace tab must survive without being resolved or
    // rewritten as its fallback filesystem path by any managed-list restore.
    app.groups[1].panes[3].tabs[0].location = {
        L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", L"ThisPC", L"C:\\fallback"};
    app.groups[1].panes[3].tabs[0].history[0] = app.groups[1].panes[3].tabs[0].location;
    EXPECT(core::is_valid(app));
    return app;
}
struct ReentrantHost final : test::TestPaneHost {
    std::function<void()> on_save;
    void schedule_session_save() noexcept override {
        ++session_saves;
        if (auto action = std::exchange(on_save, {})) action();
    }
};
struct HiddenControls final {
    ReentrantHost host;
    core::ApplicationState app;
    std::array<app_shell::Pane, 4> panes;
    std::array<Delivery*, 4> sources{};
    HWND parent{};
    explicit HiddenControls(core::ApplicationState state = sample()) : app(std::move(state)) {
        EXPECT(app_shell::Pane::register_window_class(GetModuleHandleW(nullptr)));
        parent = CreateWindowExW(0, L"STATIC", L"PD225 test", 0, 0, 0, 800, 600,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        EXPECT(parent != nullptr);
        for (int i = 0; i < 4; ++i) {
            auto& pane = panes[i];
            pane.set_host(&host); pane.set_reentry_guard(&host.guard);
            EXPECT(pane.create(parent, i));
            auto source = std::make_unique<Delivery>(); sources[i] = source.get();
            pane.use_managed_list(std::move(source));
        }
        activate(0);
    }
    ~HiddenControls() { for (auto& pane : panes) pane.destroy(); DestroyWindow(parent); }
    void activate(int group_index) {
        auto& group = app.groups[group_index];
        app.active_group_id = host.group_id = group.id;
        const auto visible = core::pane_count(group.layout_template);
        for (std::size_t i = 0; i < panes.size(); ++i) {
            auto& pane = panes[i]; pane.bind(&group.panes[i]);
            if (i >= visible) { pane.derealize(); continue; }
            const auto location = pane.active_tab()->location;
            if (pane.realized()) EXPECT(SUCCEEDED(pane.navigate_to(location)));
            else EXPECT(SUCCEEDED(pane.realize({0, 0, 400, 300}, location)));
        }
    }
    void deliver(int i, managed_list::Request request, std::wstring name,
                 managed_list::Phase phase = managed_list::Phase::complete) {
        sources[i]->batches.push_back({std::move(request), {{std::move(name), 4, false}}, phase, 0});
        panes[i].process_content_results();
    }
    void complete(int i, std::wstring name = L"current") {
        deliver(i, panes[i].list_snapshot().request, std::move(name));
    }
    HWND list(int i) { return FindWindowExW(panes[i].explorer_container(), nullptr, panedock::app_shell::kManagedListClassName, nullptr); }
};
void four_panes_tabs_groups_and_move() {
    HiddenControls f;
    std::array<HWND, 4> windows{}, lists{};
    std::array<managed_list::Request, 4> old{};
    for (int i = 0; i < 4; ++i) {
        windows[i] = f.panes[i].window(); lists[i] = f.list(i);
        old[i] = f.panes[i].list_snapshot().request;
        EXPECT(old[i].location == f.panes[i].active_tab()->location);
        f.complete(i, L"pane" + std::to_wstring(i));
    }
    auto& pane = f.panes[0];
    const auto a = pane.active_tab()->id;
    const auto b = pane.pane_state()->tabs[0].id;
    pane.switch_active_tab(b);
    const auto obsolete_b = pane.list_snapshot().request;
    pane.cycle_active_tab(false); // B -> A, without waiting for B
    EXPECT(pane.active_tab()->id == a);
    EXPECT(pane.list_snapshot().updating);
    EXPECT(pane.list_snapshot().rows.size() == 1);
    EXPECT(pane.list_snapshot().rows[0].name == L"pane0");
    f.deliver(0, old[0], L"old A"); f.deliver(0, obsolete_b, L"old B");
    EXPECT(pane.list_snapshot().rows[0].name == L"pane0");
    f.complete(0);
    pane.add_tab({L"C:\\added", {}, {}});
    const auto closed = pane.list_snapshot().request;
    pane.close_tab(pane.active_tab()->id);
    f.deliver(0, closed, L"closed");
    EXPECT(pane.list_snapshot().updating);
    EXPECT(pane.list_snapshot().rows.size() == 1);
    EXPECT(pane.list_snapshot().rows[0].name == L"current");
    f.complete(0);

    // Same value-model move and public content entry used by the coordinator.
    const auto moved_id = pane.active_tab()->id;
    const auto moved_request = pane.list_snapshot().request;
    EXPECT(core::move_tab(*pane.pane_state(), *f.panes[1].pane_state(), moved_id, 0,
                         {L"C:\\default", {}, {}}, "retained"));
    EXPECT(SUCCEEDED(pane.navigate_to(pane.active_tab()->location)));
    EXPECT(SUCCEEDED(f.panes[1].navigate_to(f.panes[1].active_tab()->location)));
    f.deliver(0, moved_request, L"moved late");
    f.deliver(1, old[1], L"previous destination late");
    EXPECT(pane.list_snapshot().rows.empty());
    EXPECT(f.panes[1].list_snapshot().rows.empty());
    f.complete(0); f.complete(1);
    const auto retained = f.panes[1].list_snapshot().request;
    const auto inactive_id = f.panes[1].pane_state()->tabs.back().id;
    f.panes[1].close_tab(inactive_id);
    EXPECT(f.panes[1].list_snapshot().request == retained);
    EXPECT(f.panes[1].list_snapshot().rows.size() == 1);

    for (int i = 0; i < 4; ++i) old[i] = f.panes[i].list_snapshot().request;
    f.activate(1); // Single layout releases hidden pane lists.
    for (int i = 0; i < 4; ++i) {
        f.deliver(i, old[i], L"previous group");
        EXPECT(f.panes[i].list_snapshot().rows.empty());
        EXPECT(f.panes[i].window() == windows[i]);
        if (i != 0) EXPECT(!f.panes[i].realized() && f.list(i) == nullptr);
    }
    f.complete(0);
    const auto group_b = pane.list_snapshot().request;
    f.activate(0);
    f.deliver(0, group_b, L"late group B");
    for (int i = 0; i < 4; ++i) {
        // PD-261: the still-realized pane restores A immediately; hidden panes
        // underwent real teardown and have no retained owner snapshots.
        if (i == 0) {
            EXPECT(f.panes[i].list_snapshot().updating);
            EXPECT(f.panes[i].list_snapshot().rows.size() == 1);
            EXPECT(f.panes[i].list_snapshot().rows[0].name == L"current");
        } else {
            EXPECT(f.panes[i].list_snapshot().rows.empty());
        }
        f.complete(i);
        EXPECT(f.panes[i].window() == windows[i]);
    }
    EXPECT(f.list(0) == lists[0]); // still-visible content HWND also survives
}
void failed_navigation_and_history() {
    HiddenControls f;
    auto& pane = f.panes[0]; f.complete(0);
    auto previous = *pane.active_tab();
    SetWindowTextW(pane.address_bar(), L"C:\\missing"); pane.submit_address();
    EXPECT(*pane.active_tab() == previous); // a save while loading is safe
    wchar_t address[128]{};
    GetWindowTextW(pane.address_bar(), address, 128);
    EXPECT(std::wstring(address) == L"C:\\missing");
    auto request = pane.list_snapshot().request;
    f.sources[0]->batches.push_back({request, {}, managed_list::Phase::error, ERROR_PATH_NOT_FOUND});
    pane.process_content_results();
    EXPECT(*pane.active_tab() == previous);
    pane.refresh_view(); EXPECT(pane.list_snapshot().request.location == request.location);
    f.complete(0); EXPECT(pane.active_tab()->location == request.location);
    const auto at_missing = *pane.active_tab();
    pane.navigate_history(true);
    EXPECT(*pane.active_tab() == at_missing);
    f.sources[0]->batches.push_back({pane.list_snapshot().request, {}, managed_list::Phase::error, ERROR_ACCESS_DENIED});
    pane.process_content_results();
    EXPECT(*pane.active_tab() == at_missing);
    pane.navigate_history(true); f.complete(0);
    EXPECT(pane.active_tab()->location == previous.location);
    EXPECT(pane.active_tab()->history_index == previous.history_index);
    pane.navigate_history(false); f.complete(0);
    EXPECT(*pane.active_tab() == at_missing);
    EXPECT(SUCCEEDED(pane.navigate_up_one_level())); f.complete(0);
    EXPECT(pane.active_tab()->location.parsing_name == L"C:\\");
}
void isolated_session_restore() {
    const auto root = std::filesystem::current_path() /
        (L"pd225-session-" + std::to_wstring(GetCurrentProcessId()));
    EXPECT(!std::filesystem::exists(root));
    auto app = sample();
    auto json = core::serialize_session({app, {}, true});
    json.insert(json.find('{') + 1, "\"future-root\":{\"keep\":225},");
    json.insert(json.find("\"view_mode\""), "\"future-tab\":[1,2,3],");
    auto document = core::deserialize_session(json); EXPECT(document.has_value());
    if (!document) return;
    {
        HiddenControls f(document->application);
        for (int i = 0; i < 4; ++i) f.complete(i);
        f.panes[0].switch_active_tab("tab-0"); f.complete(0);
        f.activate(1); f.complete(0); f.activate(0);
        for (int i = 0; i < 4; ++i) f.complete(i);
        EXPECT(SUCCEEDED(f.panes[2].navigate_to({L"C:\\unconfirmed", {}, {}})));
        // Persistence never records a pending list as success.
        app_shell::SessionWriter writer; writer.set_directory(root); writer.adopt(*document);
        EXPECT(writer.write(f.app, true, nullptr));
        app = f.app;
        EXPECT(writer.write(f.app, true, nullptr)); // atomic backup path too
    }
    const auto read = core::read_session(root);
    EXPECT(read.source == core::SessionSource::primary);
    EXPECT(read.document.application == app);
    const auto preserved = core::serialize_session(read.document);
    EXPECT(preserved.find("future-root") != std::string::npos);
    EXPECT(preserved.find("future-tab") != std::string::npos);
    EXPECT(std::filesystem::exists(root / core::kSessionBackupFileName));
    {
        HiddenControls reopened(read.document.application);
        for (int i = 0; i < 4; ++i) {
            EXPECT(reopened.panes[i].list_snapshot().request.location == reopened.panes[i].active_tab()->location);
            reopened.complete(i);
        }
        EXPECT(reopened.app == app); // required modes, sort, identities, active tabs/layout
    }
    std::filesystem::remove_all(root);
}
void reentrant_completion_binding_changes() {
    for (int action = 0; action < 4; ++action) {
        HiddenControls f;
        const auto original = f.panes[0].list_snapshot().request;
        f.host.on_save = [&] {
            if (action == 0) f.panes[0].switch_active_tab("tab-0");
            if (action == 1) f.activate(1);
            if (action == 2) f.panes[0].close_tab("tab-1");
            if (action == 3) f.host.shutting_down = true;
        };
        f.complete(0, L"completion before reentry");
        f.deliver(0, original, L"late enrichment", managed_list::Phase::loading);
        EXPECT(f.panes[0].list_snapshot().rows.empty());
        if (action != 3) {
            f.complete(0, L"correct binding");
            EXPECT(f.panes[0].list_snapshot().rows.size() == 1);
            EXPECT(f.panes[0].list_snapshot().rows[0].name == L"correct binding");
        }
    }
}
} // namespace
int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    four_panes_tabs_groups_and_move(); failed_navigation_and_history(); isolated_session_restore();
    reentrant_completion_binding_changes();
    return test::summary("managed_list_restore");
}
