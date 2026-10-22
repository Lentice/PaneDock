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

class RenameHost final : public test::TestPaneHost {
  public:
    file_operations::RenameResult answer;
    app_shell::Pane::FileCommandSnapshot captured;
    std::wstring new_name;
    int calls{};
    bool change_tab{}, shutdown_during_call{};
    core::PaneState* state{};
    file_operations::RenameResult rename_item(app_shell::Pane&,
        const app_shell::Pane::FileCommandSnapshot& snapshot, std::wstring_view name) noexcept override {
        ++calls; captured = snapshot; new_name = name;
        EXPECT(guard.in_shell_call());
        if (change_tab) state->active_tab_id = "tab-b";
        if (shutdown_during_call) shutting_down = true;
        return answer;
    }
};
struct Fixture final {
    RenameHost host;
    core::PaneState state{"pane", {
        {"tab-a", {L"C:\\A", {}, {}}, "details", {}, true, {}, 0},
        {"tab-b", {L"C:\\B", {}, {}}, "details", {}, true, {}, 0}}, "tab-a"};
    app_shell::Pane pane;
    Controlled* source{};
    HWND parent{};
    HWND list{};

    explicit Fixture(int index = 0) {
        host.state = &state;
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
        list = FindWindowExW(pane.explorer_container(), nullptr, panedock::app_shell::kManagedListClassName, nullptr);
        EXPECT(list != nullptr);
        pane.use_list_icons(nullptr);
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
        ListView_SetItemState(list, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
};

void end_edit(Fixture& f, const wchar_t* text, UINT key = VK_RETURN) {
    const HWND edit = FindWindowExW(f.list, nullptr, L"EDIT", nullptr);
    EXPECT(edit != nullptr);
    SetWindowTextW(edit, text);
    SendMessageW(edit, WM_KEYDOWN, key, 0);
}
void setup(Fixture& f) {
    f.deliver(LR"(C:\A)", managed_list::Phase::complete,
              {{L"old.txt", 7, false}, {L"other.txt", 9, false}});
    f.select(f.row(L"old.txt"));
    f.host.answer.result = S_OK;
    f.host.answer.parsing_name = LR"(C:\A\new.txt)";
    f.host.answer.leaf_name = L"new.txt";
    f.host.answer.display_name = L"New display";
}
void enter_and_notification_converge() {
    Fixture f; setup(f);
    ListView_SetItemState(f.list, f.row(L"other.txt"), LVIS_FOCUSED, LVIS_FOCUSED);
    MSG key{}; key.hwnd = f.list; key.message = WM_KEYDOWN; key.wParam = VK_F2;
    EXPECT(f.pane.handle_list_rename_key(key, false, false, false));
    EXPECT(FindWindowExW(f.list, nullptr, L"EDIT", nullptr) != nullptr);
    end_edit(f, L"requested.txt");
    EXPECT(f.host.calls == 1);
    EXPECT(f.host.new_name == L"requested.txt");
    EXPECT(f.host.captured.items == std::vector<std::wstring>{LR"(C:\A\old.txt)"});
    EXPECT(f.pane.list_rename_result() == S_OK);
    EXPECT(f.row(L"old.txt") == -1);
    const auto interaction = f.pane.list_interaction_snapshot();
    EXPECT(interaction.selected.size() == 1);
    EXPECT(interaction.selected[0].name == L"new.txt");
    EXPECT(interaction.focused && interaction.focused->name == L"new.txt");
    managed_list::Batch batch{f.pane.list_snapshot().request,
        {{L"new.txt", 7, false}}, managed_list::Phase::complete};
    batch.change = managed_list::Change::delta;
    batch.renamed.emplace_back(L"old.txt", L"new.txt");
    f.source->deliver(std::move(batch)); f.pane.process_content_results();
    EXPECT(f.pane.list_snapshot().rows.size() == 2);
    EXPECT(f.pane.list_interaction_snapshot().selected[0].name == L"new.txt");
}
void cancel_focus_batch_sort_and_navigation() {
    Fixture f; setup(f);
    EXPECT(f.pane.begin_list_rename()); end_edit(f, L"ignored", VK_ESCAPE);
    EXPECT(f.host.calls == 0 && f.row(L"old.txt") >= 0);
    EXPECT(f.pane.begin_list_rename());
    const HWND edit = FindWindowExW(f.list, nullptr, L"EDIT", nullptr);
    SetWindowTextW(edit, L"ignored");
    SendMessageW(edit, WM_KILLFOCUS, reinterpret_cast<WPARAM>(f.list), 0);
    EXPECT(f.host.calls == 0);
    f.pane.cancel_list_rename();
    EXPECT(f.pane.begin_list_rename());
    managed_list::Batch batch{f.pane.list_snapshot().request, {}, managed_list::Phase::complete};
    batch.change = managed_list::Change::delta; batch.removed = {L"old.txt"};
    f.source->deliver(std::move(batch)); f.pane.process_content_results();
    EXPECT(FindWindowExW(f.list, nullptr, L"EDIT", nullptr) == nullptr);
    EXPECT(f.row(L"old.txt") == -1 && f.host.calls == 0);
    f.select(f.row(L"other.txt"));
    EXPECT(f.pane.begin_list_rename());
    NMLISTVIEW click{}; click.hdr.hwndFrom = f.list; click.hdr.code = LVN_COLUMNCLICK;
    f.pane.list_notification(&click.hdr);
    EXPECT(FindWindowExW(f.list, nullptr, L"EDIT", nullptr) == nullptr && f.host.calls == 0);
    EXPECT(f.pane.begin_list_rename());
    SendMessageW(f.list, WM_VSCROLL, SB_LINEDOWN, 0);
    EXPECT(FindWindowExW(f.list, nullptr, L"EDIT", nullptr) == nullptr);
    EXPECT(f.pane.begin_list_rename());
    f.pane.switch_active_tab("tab-b");
    EXPECT(FindWindowExW(f.list, nullptr, L"EDIT", nullptr) == nullptr && f.host.calls == 0);
}
void errors_and_reentry_keep_target_safe() {
    {
        Fixture f; setup(f);
        EXPECT(f.pane.begin_list_rename()); end_edit(f, L"");
        EXPECT(f.host.calls == 0 && f.pane.list_rename_result() == E_INVALIDARG);
        EXPECT(f.pane.begin_list_rename());
        f.host.answer.aborted = true; end_edit(f, L"new.txt");
        EXPECT(f.pane.list_rename_result() == HRESULT_FROM_WIN32(ERROR_CANCELLED));
        EXPECT(f.row(L"old.txt") >= 0);
    }
    for (HRESULT failure : {E_ACCESSDENIED, HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS),
                           HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)}) {
        Fixture f; setup(f); f.host.answer.result = failure;
        EXPECT(f.pane.begin_list_rename()); end_edit(f, L"new.txt");
        EXPECT(f.pane.list_rename_result() == failure);
        EXPECT(f.row(L"old.txt") >= 0 && f.pane.list_snapshot().rows.size() == 2);
        EXPECT(f.pane.begin_list_rename()); f.pane.cancel_list_rename();
    }
    for (bool shutdown : {false, true}) {
        Fixture f; setup(f);
        f.host.change_tab = !shutdown; f.host.shutdown_during_call = shutdown;
        EXPECT(f.pane.begin_list_rename()); end_edit(f, L"new.txt");
        EXPECT(f.host.calls == 1 && f.row(L"old.txt") >= 0 && f.row(L"New display") < 0);
    }
}
void focus_routing_and_virtual_identity() {
    Fixture f; setup(f);
    MSG message{}; message.hwnd = f.pane.address_bar();
    message.message = WM_KEYDOWN; message.wParam = VK_F2;
    EXPECT(!f.pane.handle_list_rename_key(message, false, false, false));
    message.hwnd = f.parent; // sidebar/other controls never rename a file
    EXPECT(!f.pane.handle_list_rename_key(message, false, false, false));
    message.hwnd = f.list;
    EXPECT(!f.pane.handle_list_rename_key(message, true, false, false));
    EXPECT(!f.pane.handle_list_rename_key(message, false, true, false));
    EXPECT(!f.pane.handle_list_rename_key(message, false, false, true));
    {
        app_shell::ShellCallScope nested(f.host.guard);
        EXPECT(f.pane.handle_list_rename_key(message, false, false, false));
        EXPECT(FindWindowExW(f.list, nullptr, L"EDIT", nullptr) == nullptr);
    }
    core::KeyInput input{}; input.message = core::KeyMessage::key_down;
    input.virtual_key = VK_F2; input.sidebar_focused = true;
    EXPECT(core::resolve_key(input).action == core::KeyAction::begin_group_rename);
    EXPECT(f.pane.begin_list_rename());
    message.hwnd = FindWindowExW(f.list, nullptr, L"EDIT", nullptr);
    message.wParam = 'V';
    EXPECT(!f.pane.file_command_for_key(message, true, false, false));
    input.sidebar_focused = false; input.address_bar_focused = true;
    input.control = true; input.virtual_key = 'V';
    EXPECT(core::resolve_key(input).action == core::KeyAction::none);
    input.virtual_key = VK_TAB;
    EXPECT(core::resolve_key(input).action == core::KeyAction::next_tab);
    f.pane.cancel_list_rename();
    f.select(f.row(L"other.txt"));
    EXPECT(!f.pane.begin_list_rename()); // exactly one target required
    ListView_SetItemState(f.list, -1, 0, LVIS_SELECTED);
    managed_list::Row row{L"Display", 0, false};
    row.identity = {L"::{provider}\\old", true};
    managed_list::Batch batch{f.pane.list_snapshot().request, {row}, managed_list::Phase::complete};
    batch.change = managed_list::Change::delta;
    f.source->deliver(std::move(batch)); f.pane.process_content_results();
    f.select(f.row(L"Display"));
    f.host.answer.parsing_name = L"::{provider}\\actual";
    f.host.answer.leaf_name = L"actual"; f.host.answer.display_name = L"Actual";
    EXPECT(f.pane.begin_list_rename()); end_edit(f, L"requested");
    EXPECT(f.host.captured.items == std::vector<std::wstring>{row.identity.key});
    EXPECT(f.pane.list_interaction_snapshot().selected[0].name == L"::{provider}\\actual");
    EXPECT(f.pane.list_interaction_snapshot().selected[0].shell_item);
}
void binding_changes_and_batches_cancel_without_retargeting() {
    for (int action = 0; action < 4; ++action) {
        Fixture f; setup(f);
        EXPECT(f.pane.begin_list_rename());
        if (action == 0) {
            // A progressive/notification batch cancels before rows reorder.
            managed_list::Batch batch{f.pane.list_snapshot().request,
                {{L"aaa", 0, false}}, managed_list::Phase::complete};
            batch.change = managed_list::Change::delta;
            f.source->deliver(std::move(batch)); f.pane.process_content_results();
        } else if (action == 1) {
            f.host.group_id = "replacement";
            end_edit(f, L"new.txt");
        } else if (action == 2) {
            f.pane.unbind();
        } else {
            f.host.shutting_down = true;
            end_edit(f, L"new.txt");
        }
        EXPECT(f.host.calls == 0);
        EXPECT(FindWindowExW(f.list, nullptr, L"EDIT", nullptr) == nullptr);
    }
    Fixture loading;
    loading.deliver(LR"(C:\A)", managed_list::Phase::loading, {{L"partial", 0, false}});
    loading.select(0);
    EXPECT(!loading.pane.begin_list_rename()); // no stale enumeration echo after commit
}
bool abort_setup(void*) noexcept { return true; }
void rejected_shell_setup_is_non_destructive() {
    file_operations::Callbacks callbacks{}; callbacks.abort_setup = abort_setup;
    EXPECT(file_operations::rename_item(nullptr, LR"(C:\unused)", L"unused", callbacks).result == E_ABORT);
}
} // namespace
int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    enter_and_notification_converge();
    cancel_focus_batch_sort_and_navigation();
    errors_and_reentry_keep_target_safe();
    focus_routing_and_virtual_identity();
    binding_changes_and_batches_cancel_without_retargeting();
    rejected_shell_setup_is_non_destructive();
    return test::summary("managed_list_rename");
}
