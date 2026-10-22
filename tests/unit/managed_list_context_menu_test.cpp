#include "unit/test_pane_host.h"
#include "unit/test_util.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace panedock;
namespace {
class Controlled final : public managed_list::Source {
    HANDLE ready_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  public:
    ~Controlled() override { if (ready_) CloseHandle(ready_); }
    bool submit(managed_list::Request) noexcept override { return true; }
    std::optional<managed_list::Batch> take() noexcept override {
        if (batches.empty()) { ResetEvent(ready_); return {}; }
        auto batch = std::move(batches.front());
        batches.erase(batches.begin());
        if (batches.empty()) ResetEvent(ready_);
        return batch;
    }
    HANDLE event() const noexcept override { return ready_; }
    void cancel() noexcept override {}
    void deliver(managed_list::Batch batch) {
        batches.push_back(std::move(batch));
        SetEvent(ready_);
    }
  private:
    std::vector<managed_list::Batch> batches;
};

struct Fixture final {
    test::TestPaneHost host;
    core::PaneState state{"pane", {
        {"tab-a", {L"C:\\A", {}, {}}, "details", {}, true, {}, 0},
        {"tab-b", {L"C:\\B", {}, {}}, "details", {}, true, {}, 0}}, "tab-a"};
    app_shell::Pane pane;
    Controlled* source{};
    struct Presenter final : explorer_host::ContextMenuPresenter {
        Fixture* fixture{};
        std::vector<explorer_host::ContextMenuRequest> requests;
        std::function<void()> pump;
        bool current_after{};
        bool guarded{};
        bool show(HWND, const explorer_host::ContextMenuRequest& request,
                  const explorer_host::MenuTargetCurrent& current) noexcept override {
            requests.push_back(request);
            guarded = fixture->host.guard.in_shell_call();
            EXPECT(current());
            if (pump) pump();
            current_after = current();
            // Provider sees the same owned target after a sort/navigation pump.
            EXPECT(request.items == requests.back().items);
            return true;
        }
    };
    Presenter* presenter{};
    HWND parent{};
    HWND list{};

    Fixture() {
        const auto instance = GetModuleHandleW(nullptr);
        EXPECT(app_shell::Pane::register_window_class(instance));
        parent = CreateWindowExW(0, L"STATIC", L"managed list context menu test",
            WS_OVERLAPPEDWINDOW,
            0, 0, 800, 600, nullptr, nullptr, instance, nullptr);
        EXPECT(parent != nullptr);
        pane.set_host(&host);
        pane.set_reentry_guard(&host.guard);
        pane.bind(&state);
        EXPECT(pane.create(parent, 0));
        pane.bind(&state);
        auto controlled = std::make_unique<Controlled>();
        source = controlled.get();
        pane.use_managed_list(std::move(controlled));
        auto menu = std::make_unique<Presenter>();
        presenter = menu.get(); presenter->fixture = this;
        pane.use_context_menu_presenter(std::move(menu));
        EXPECT(SUCCEEDED(pane.realize({0, 0, 640, 420}, {L"C:\\A", {}, {}})));
        list = FindWindowExW(pane.explorer_container(), nullptr, panedock::app_shell::kManagedListClassName, nullptr);
        EXPECT(list != nullptr);
    }
    ~Fixture() {
        pane.destroy();
        if (parent) DestroyWindow(parent);
    }
    void deliver(const wchar_t* location, managed_list::Phase phase,
                 std::vector<managed_list::Row> rows = {}) {
        auto request = pane.list_snapshot().request;
        EXPECT(request.location.parsing_name == location);
        source->deliver({std::move(request), std::move(rows), phase, 0});
        pane.process_content_results();
    }
    void select_item(int index) {
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetSelectionMark(list, index);
    }
    void select_range(int anchor, int focused) {
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        for (int index = std::min(anchor, focused);
             index <= std::max(anchor, focused); ++index) {
            ListView_SetItemState(list, index, LVIS_SELECTED, LVIS_SELECTED);
        }
        ListView_SetItemState(list, focused, LVIS_FOCUSED, LVIS_FOCUSED);
        ListView_SetSelectionMark(list, anchor);
    }
};

POINT point_at(Fixture& f, int row) {
    RECT rect{};
    EXPECT(ListView_GetItemRect(f.list, row, &rect, LVIR_LABEL));
    POINT point{rect.left + 2, (rect.top + rect.bottom) / 2};
    ClientToScreen(f.list, &point);
    return point;
}
void targets_and_routing() {
    Fixture f;
    f.deliver(L"C:\\A", managed_list::Phase::loading,
        {{L"alpha.txt", 1, false}, {L"beta.txt", 2, false}});
    f.select_range(0, 1);
    const auto point = point_at(f, 0);
    app_shell::Pane::ContextMenuSnapshot snapshot;
    EXPECT(f.pane.context_menu_snapshot(point, false, snapshot) == S_OK);
    EXPECT(snapshot.request.items == (std::vector<std::wstring>{L"alpha.txt", L"beta.txt"}));
    EXPECT(snapshot.target.items == (std::vector<std::wstring>{L"C:\\A\\alpha.txt", L"C:\\A\\beta.txt"}));
    EXPECT(snapshot.request.folder == L"C:\\A");
    NMLISTVIEW sort{};
    sort.hdr.hwndFrom = f.list; sort.hdr.code = LVN_COLUMNCLICK; sort.iSubItem = 0;
    (void)f.pane.list_notification(&sort.hdr);
    EXPECT(snapshot.request.items[0] == L"alpha.txt");
    EXPECT(f.pane.file_command_is_current(snapshot.target));
    EXPECT(!f.pane.show_content_context_menu(f.pane.tab_strip(), point));
    // Exercise real child WM_CONTEXTMENU forwarding through the Pane gate.
    SendMessageW(f.list, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(f.list),
                 MAKELPARAM(point.x, point.y));
    EXPECT(f.presenter->requests.size() == 1);
    if (f.presenter->requests.empty()) return;
    EXPECT(f.presenter->requests.back().items.size() == 2);
    EXPECT(f.presenter->guarded && f.presenter->current_after);
    f.select_item(0);
    const auto unselected = point_at(f, 1);
    EXPECT(f.pane.show_content_context_menu(f.list, unselected));
    EXPECT(f.presenter->requests.back().items.size() == 1);
    EXPECT(f.presenter->requests.back().items[0] ==
        f.pane.list_snapshot().rows[1].identity.key);
    MSG key{}; key.hwnd = f.list; key.message = WM_KEYDOWN; key.wParam = VK_APPS;
    EXPECT(f.pane.translate_content_accelerator(&key) == S_FALSE);
    SendMessageW(f.list, WM_KEYUP, VK_APPS, 0);
    EXPECT(f.presenter->requests.size() == 3);
    EXPECT(f.presenter->requests.back().screen.x != -1);
    // SetKeyboardState affects this test thread only; no desktop input is sent.
    BYTE previous[256]{};
    EXPECT(GetKeyboardState(previous));
    BYTE shifted[256]{};
    shifted[VK_SHIFT] = 0x80;
    EXPECT(SetKeyboardState(shifted));
    SendMessageW(f.list, WM_KEYDOWN, VK_F10, 0);
    EXPECT(SetKeyboardState(previous));
    EXPECT(f.presenter->requests.size() == 4);
    EXPECT(f.pane.show_folder_context_menu(f.parent, {40, 50}));
    EXPECT(f.presenter->requests.back().items.empty());
    EXPECT(f.presenter->requests.back().align_above);
    f.select_item(0);
    RECT client{}; GetClientRect(f.list, &client);
    POINT empty{client.right - 4, client.bottom - 4}; ClientToScreen(f.list, &empty);
    EXPECT(f.pane.show_content_context_menu(f.list, empty));
    EXPECT(f.presenter->requests.back().items.empty());
    EXPECT(!f.presenter->requests.back().align_above);
    EXPECT(f.pane.context_menu_snapshot({-1, -1}, false, snapshot) == S_OK);
    EXPECT(snapshot.request.items.empty()); // keyboard with no selection is background
}
void modal_selection_and_sort_keep_owned_targets() {
    Fixture f;
    managed_list::Row row{L"Not an identity", 1, false}; row.identity = {L"leaf.txt", false};
    f.deliver(L"C:\\A", managed_list::Phase::complete, {row, {L"other.txt", 2, false}});
    f.select_item(0);
    f.presenter->pump = [&] {
        f.select_item(1);
        NMLISTVIEW sort{};
        sort.hdr.hwndFrom = f.list; sort.hdr.code = LVN_COLUMNCLICK; sort.iSubItem = 0;
        (void)f.pane.list_notification(&sort.hdr);
    };
    EXPECT(f.pane.show_content_context_menu(f.list, {-1, -1}));
    EXPECT(f.presenter->current_after);
    EXPECT(f.presenter->requests.size() == 1);
    EXPECT(f.presenter->requests[0].items == std::vector<std::wstring>{L"leaf.txt"});
    // Binding changes invalidate a modal target even without navigation.
    f.presenter->pump = [&] { f.state.active_tab_id = "tab-b"; };
    EXPECT(f.pane.show_content_context_menu(f.list, {-1, -1}));
    EXPECT(!f.presenter->current_after);
}
void namespace_and_modal_invalidation() {
    Fixture f;
    managed_list::Row row{L"Display only", 1, false};
    row.identity = {L"::{virtual}\\absolute-child", true};
    f.deliver(L"C:\\A", managed_list::Phase::complete, {row});
    f.select_item(0);
    app_shell::Pane::ContextMenuSnapshot snapshot;
    EXPECT(f.pane.context_menu_snapshot({-1, -1}, false, snapshot) == S_OK);
    EXPECT(snapshot.request.items == std::vector<std::wstring>{row.identity.key});
    f.presenter->pump = [&] {
        // A nested request cannot replace the outer menu/selection.
        EXPECT(f.pane.show_content_context_menu(f.list, {-1, -1}));
        EXPECT(f.presenter->requests.size() == 1);
        EXPECT(SUCCEEDED(f.pane.navigate_to({L"C:\\B", {}, {}})));
    };
    EXPECT(f.pane.show_content_context_menu(f.list, {-1, -1}));
    EXPECT(!f.presenter->current_after);
    EXPECT(!f.pane.file_command_is_current(snapshot.target));
    EXPECT(f.presenter->requests[0].items == snapshot.request.items);
    EXPECT(f.pane.context_menu_snapshot({-1, -1}, false, snapshot) == E_PENDING);
    f.deliver(L"C:\\B", managed_list::Phase::empty);
    f.presenter->pump = [&] { f.host.shutting_down = true; };
    EXPECT(f.pane.show_content_context_menu(f.list, {-1, -1}));
    EXPECT(!f.presenter->current_after);
    EXPECT(f.pane.context_menu_snapshot({-1, -1}, false, snapshot) == E_ABORT);
}
} // namespace
int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    targets_and_routing();
    modal_selection_and_sort_keep_owned_targets();
    namespace_and_modal_invalidation();
    return test::summary("managed_list_context_menu");
}
