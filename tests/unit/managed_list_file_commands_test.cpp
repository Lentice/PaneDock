#include "unit/test_pane_host.h"
#include "unit/test_util.h"
#include "core/key_routing.h"

#include <algorithm>
#include <array>
#include <iterator>
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
    HWND parent{};
    HWND list{};

    explicit Fixture(int index = 0) {
        const auto instance = GetModuleHandleW(nullptr);
        EXPECT(app_shell::Pane::register_window_class(instance));
        parent = CreateWindowExW(0, L"STATIC", L"managed list selection test",
            WS_OVERLAPPEDWINDOW,
            0, 0, 800, 600, nullptr, nullptr, instance, nullptr);
        EXPECT(parent != nullptr);
        pane.set_host(&host);
        pane.set_reentry_guard(&host.guard);
        pane.bind(&state);
        EXPECT(pane.create(parent, index));
        pane.bind(&state);
        auto controlled = std::make_unique<Controlled>();
        source = controlled.get();
        pane.use_managed_list(std::move(controlled));
        EXPECT(SUCCEEDED(pane.realize({0, 0, 640, 420}, {L"C:\\A", {}, {}})));
        list = FindWindowExW(pane.explorer_container(), nullptr, WC_LISTVIEWW, nullptr);
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
    int row(std::wstring_view name) const {
        const auto& rows = pane.list_snapshot().rows;
        const auto found = std::find_if(rows.begin(), rows.end(),
            [&](const auto& item) { return item.name == name; });
        return found == rows.end() ? -1 : static_cast<int>(found - rows.begin());
    }
    void select(int index) {
        ListView_SetItemState(list, index, LVIS_SELECTED, LVIS_SELECTED);
    }
};
using Command = file_operations::Command;
using Snapshot = app_shell::Pane::FileCommandSnapshot;

void absolute_selection_and_owned_snapshot() {
    Fixture f;
    f.pane.use_list_icons(nullptr);
    managed_list::Row a{L"Same display", 0, false};
    a.identity = {L"::{provider-a}\\item", true};
    a.child_location = core::ShellLocation{a.identity.key, L"known-a", L"C:\\wrong"};
    managed_list::Row b{L"Same display", 0, false};
    b.identity = {L"::{provider-b}\\item", true}; // different parent, no child location
    f.deliver(L"C:\\A", managed_list::Phase::complete, {a, b});
    f.select(0); f.select(1);
    Snapshot snapshot;
    EXPECT(f.pane.file_command_snapshot(Command::copy, snapshot) == S_OK);
    EXPECT(snapshot.items == std::vector<std::wstring>({a.identity.key, b.identity.key}));
    EXPECT(f.pane.file_command_is_current(snapshot));
    Snapshot cut;
    EXPECT(f.pane.file_command_snapshot(Command::cut, cut) == S_OK);
    EXPECT(cut.items == snapshot.items && cut.folder == snapshot.folder);
    ListView_SetItemState(f.list, -1, 0, LVIS_SELECTED);
    Snapshot empty;
    EXPECT(f.pane.file_command_snapshot(Command::cut, empty) == S_FALSE);
    EXPECT(snapshot.items == std::vector<std::wstring>({a.identity.key, b.identity.key}));
    EXPECT(f.pane.file_command_snapshot(Command::paste, empty) == S_OK);
    EXPECT(empty.items.empty());
    EXPECT(empty.folder.parsing_name == L"C:\\A");
    f.state.active_tab_id = "tab-b";
    EXPECT(!f.pane.file_command_is_current(snapshot));
    f.state.active_tab_id = "tab-a";
    f.host.group_id = "group-b";
    EXPECT(!f.pane.file_command_is_current(snapshot));
    f.host.group_id = "group-a";
    f.pane.begin_navigation();
    EXPECT(!f.pane.file_command_is_current(snapshot));
    f.host.shutting_down = true;
    EXPECT(f.pane.file_command_snapshot(Command::delete_items, empty) == E_ABORT);
    EXPECT(!f.pane.file_command_is_current(snapshot));
}

void local_leaf_and_paste_folder() {
    Fixture f;
    f.pane.use_list_icons(nullptr);
    Snapshot snapshot;
    EXPECT(f.pane.file_command_snapshot(Command::paste, snapshot) == E_PENDING);
    managed_list::Row row{L"Display only", 0, true};
    row.identity = {L"actual-leaf", false};
    f.deliver(L"C:\\A", managed_list::Phase::complete, {row});
    f.select(0);
    EXPECT(f.pane.file_command_snapshot(Command::delete_items, snapshot) == S_OK);
    EXPECT(snapshot.items == std::vector<std::wstring>{L"C:\\A\\actual-leaf"});
    EXPECT(f.pane.file_command_snapshot(Command::paste, snapshot) == S_OK);
    EXPECT(snapshot.folder.parsing_name == L"C:\\A"); // selected folder is not the target
    f.pane.unbind();
    EXPECT(!f.pane.file_command_is_current(snapshot));
}

bool stale_request(void* context) noexcept {
    const auto& value = *static_cast<std::pair<app_shell::Pane*, Snapshot>*>(context);
    return !value.first->file_command_is_current(value.second);
}

void stale_execution_is_rejected_and_right_pane_keeps_its_target() {
    Fixture left;
    Fixture right(1);
    left.pane.use_list_icons(nullptr); right.pane.use_list_icons(nullptr);
    left.deliver(L"C:\\A", managed_list::Phase::complete, {{L"left", 0, false}});
    const core::ShellLocation virtual_folder{L"::{provider}\\folder", L"known-folder", L"C:\\wrong"};
    EXPECT(SUCCEEDED(right.pane.navigate_to(virtual_folder)));
    auto request = right.pane.list_snapshot().request;
    managed_list::Row row{L"Shown name", 0, false};
    row.identity = {L"::{other-parent}\\selected-item", true};
    managed_list::Batch batch{request, {row}, managed_list::Phase::complete};
    batch.resolved_location = virtual_folder;
    right.source->deliver(std::move(batch)); right.pane.process_content_results();
    left.select(0); right.select(0);
    Snapshot snapshot;
    EXPECT(right.pane.file_command_snapshot(Command::paste, snapshot) == S_OK);
    EXPECT(snapshot.folder == virtual_folder && snapshot.items.empty());
    EXPECT(right.pane.file_command_snapshot(Command::delete_items, snapshot) == S_OK);
    EXPECT(snapshot.items == std::vector<std::wstring>{row.identity.key});
    std::pair<app_shell::Pane*, Snapshot> context{&right.pane, snapshot};
    file_operations::Callbacks callbacks{};
    callbacks.context = &context; callbacks.abort_setup = stale_request;
    right.pane.unbind();
    right.host.group_id = "replacement-group";
    right.pane.bind(&left.state);
    // Aborted before any parsing/clipboard access or destructive Shell operation.
    EXPECT(file_operations::delete_selection(nullptr, snapshot.items, callbacks).result == E_ABORT);
    EXPECT(file_operations::selection_to_clipboard(snapshot.items, false, callbacks) == E_ABORT);
    EXPECT(left.pane.file_command_snapshot(Command::copy, snapshot) == S_OK);
    EXPECT(snapshot.items == std::vector<std::wstring>{L"C:\\A\\left"});
}

void keyboard_routing_stays_in_content() {
    Fixture f;
    MSG message{};
    message.hwnd = f.list; message.message = WM_KEYDOWN;
    for (const auto& [key, command] : std::array<std::pair<UINT, Command>, 3>{{
            {'C', Command::copy}, {'X', Command::cut}, {'V', Command::paste}}}) {
        message.wParam = key;
        EXPECT(f.pane.file_command_for_key(message, true, false, false) == command);
        EXPECT(!f.pane.file_command_for_key(message, false, false, false));
        EXPECT(!f.pane.file_command_for_key(message, true, true, false));
        EXPECT(!f.pane.file_command_for_key(message, true, false, true));
        message.hwnd = f.pane.address_bar();
        EXPECT(!f.pane.file_command_for_key(message, true, false, false));
        message.hwnd = f.list;
    }
    message.wParam = VK_DELETE;
    EXPECT(f.pane.file_command_for_key(message, false, false, false) == Command::delete_items);
    EXPECT(!f.pane.file_command_for_key(message, true, false, false));
    EXPECT(!f.pane.file_command_for_key(message, false, false, true));
    message.message = WM_KEYUP;
    EXPECT(!f.pane.file_command_for_key(message, false, false, false));
}
} // namespace

int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    absolute_selection_and_owned_snapshot();
    local_leaf_and_paste_folder();
    keyboard_routing_stays_in_content();
    stale_execution_is_rejected_and_right_pane_keeps_its_target();
    return test::summary("managed_list_file_commands");
}
