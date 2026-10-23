// Explorer-parity opening and address-bar navigation in the managed list:
// Enter on a selection, shortcut/.zip folder targets, rows restored from a
// snapshot, typed addresses (trim, \\server, files, failures), Escape, Up from
// a share root and rapid Back presses. No Shell provider runs: the presenter
// seam answers every Shell question.
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
    std::vector<managed_list::Batch> batches;
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
};

struct Presenter final : explorer_host::ContextMenuPresenter {
    std::vector<explorer_host::ContextMenuRequest> invoked;
    std::vector<explorer_host::ContextMenuRequest> resolved;
    std::optional<explorer_host::FolderTarget> folder;
    std::vector<std::wstring> messages;
    int answer{IDOK};
    bool show(HWND, const explorer_host::ContextMenuRequest&,
              const explorer_host::MenuTargetCurrent&) noexcept override { return true; }
    bool invoke_default(HWND, const explorer_host::ContextMenuRequest& request,
                        const explorer_host::MenuTargetCurrent& current) noexcept override {
        EXPECT(current());
        invoked.push_back(request);
        return true;
    }
    std::optional<explorer_host::FolderTarget> resolve_folder_target(
        HWND, const explorer_host::ContextMenuRequest& request,
        const explorer_host::MenuTargetCurrent& current) noexcept override {
        EXPECT(current());
        resolved.push_back(request);
        return folder;
    }
    int message_box(HWND, const wchar_t* text, UINT) noexcept override {
        messages.emplace_back(text);
        return answer;
    }
};

struct Fixture final {
    test::TestPaneHost host;
    core::PaneState state{"pane", {
        {"tab-a", {LR"(C:\A)", {}, {}}, "details", {}, true, {}, 0},
        {"tab-b", {LR"(C:\B)", {}, {}}, "details", {}, true, {}, 0}}, "tab-a"};
    app_shell::Pane pane;
    Controlled* source{};
    Presenter* presenter{};
    HWND parent{};
    HWND list{};

    Fixture() {
        const auto instance = GetModuleHandleW(nullptr);
        EXPECT(app_shell::Pane::register_window_class(instance));
        parent = CreateWindowExW(0, L"STATIC", L"managed list open test", WS_OVERLAPPEDWINDOW,
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
        presenter = menu.get();
        pane.use_context_menu_presenter(std::move(menu));
        EXPECT(SUCCEEDED(pane.realize({0, 0, 640, 420}, {LR"(C:\A)", {}, {}})));
        list = FindWindowExW(pane.explorer_container(), nullptr,
                             app_shell::kManagedListClassName, nullptr);
        EXPECT(list != nullptr);
    }
    ~Fixture() {
        pane.destroy();
        if (parent) DestroyWindow(parent);
    }
    void deliver(std::wstring_view location, managed_list::Phase phase,
                 std::vector<managed_list::Row> rows = {}, DWORD error = 0) {
        auto request = pane.list_snapshot().request;
        EXPECT(request.location.parsing_name == location);
        source->deliver({std::move(request), std::move(rows), phase, error});
        pane.process_content_results();
    }
    void pump_pane() {
        MSG message{};
        while (PeekMessageW(&message, pane.window(), 0, 0, PM_REMOVE))
            DispatchMessageW(&message);
    }
    int index(std::wstring_view name) const {
        const auto& rows = pane.list_snapshot().rows;
        const auto found = std::find_if(rows.begin(), rows.end(),
            [&](const auto& row) { return row.name == name; });
        return found == rows.end() ? -1 : static_cast<int>(found - rows.begin());
    }
    void select(std::initializer_list<std::wstring_view> names) {
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        for (const auto name : names) {
            const int row = index(name);
            EXPECT(row >= 0);
            ListView_SetItemState(list, row, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
    void enter() {
        MSG key{};
        key.hwnd = list; key.message = WM_KEYDOWN; key.wParam = VK_RETURN;
        EXPECT(test::press_list_key(pane, key));
    }
    void type_address(const wchar_t* text) {
        SetWindowTextW(pane.address_bar(), text);
        pane.submit_address();
    }
    std::wstring address_text() const {
        wchar_t text[512]{};
        GetWindowTextW(pane.address_bar(), text, 512);
        return text;
    }
};

std::vector<managed_list::Row> sample_rows() {
    managed_list::Row child{L"child", 0, true};
    return {{L"alpha.txt", 1, false}, {L"beta.txt", 2, false}, child,
            {L"link.lnk", 3, false}};
}

void enter_opens_the_whole_selection() {
    Fixture f;
    f.deliver(LR"(C:\A)", managed_list::Phase::complete, sample_rows());
    // Several selected rows: one default-verb invocation over all of them.
    f.select({L"alpha.txt", L"beta.txt"});
    f.enter();
    EXPECT(f.presenter->invoked.size() == 1);
    if (f.presenter->invoked.empty()) return;
    auto items = f.presenter->invoked.back().items;
    std::sort(items.begin(), items.end());
    EXPECT(items == (std::vector<std::wstring>{L"alpha.txt", L"beta.txt"}));
    EXPECT(f.presenter->invoked.back().folder == LR"(C:\A)");
    EXPECT(f.presenter->resolved.empty()); // a multi-selection is never a pane navigation
    // One file: asked whether it is a folder target first, then its verb.
    f.select({L"alpha.txt"});
    f.enter();
    EXPECT(f.presenter->resolved.size() == 1);
    EXPECT(f.presenter->invoked.size() == 2);
    EXPECT(f.presenter->invoked.back().items == std::vector<std::wstring>{L"alpha.txt"});
    // Enter with only a focused, unselected row opens it.
    ListView_SetItemState(f.list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(f.list, f.index(L"beta.txt"), LVIS_FOCUSED, LVIS_FOCUSED);
    f.enter();
    EXPECT(f.presenter->invoked.size() == 3);
    EXPECT(f.presenter->invoked.back().items == std::vector<std::wstring>{L"beta.txt"});
    // One folder navigates the pane.
    f.select({L"child"});
    f.enter();
    EXPECT(f.pane.list_snapshot().request.location.parsing_name == LR"(C:\A\child)");
    EXPECT(f.presenter->invoked.size() == 3);
}

void many_items_ask_first() {
    Fixture f;
    std::vector<managed_list::Row> rows;
    for (int i = 0; i < 16; ++i) rows.push_back({L"f" + std::to_wstring(i) + L".txt", 1, false});
    f.deliver(LR"(C:\A)", managed_list::Phase::complete, std::move(rows));
    ListView_SetItemState(f.list, -1, LVIS_SELECTED, LVIS_SELECTED);
    f.presenter->answer = IDNO;
    f.enter();
    EXPECT(f.presenter->messages.size() == 1);
    EXPECT(f.presenter->invoked.empty());
    f.presenter->answer = IDYES;
    f.enter();
    EXPECT(f.presenter->invoked.size() == 1);
    EXPECT(!f.presenter->invoked.empty() && f.presenter->invoked.back().items.size() == 16);
}

void shortcut_and_zip_targets_stay_in_the_pane() {
    Fixture f;
    f.deliver(LR"(C:\A)", managed_list::Phase::complete, sample_rows());
    f.presenter->folder = explorer_host::FolderTarget{LR"(D:\Target)", false};
    NMITEMACTIVATE click{};
    click.hdr.hwndFrom = f.list; click.hdr.code = NM_DBLCLK; click.iItem = f.index(L"link.lnk");
    (void)f.pane.list_notification(&click.hdr);
    EXPECT(f.presenter->resolved.size() == 1);
    EXPECT(!f.presenter->resolved.empty() &&
           f.presenter->resolved.back().items == std::vector<std::wstring>{L"link.lnk"});
    EXPECT(f.presenter->invoked.empty()); // no separate Explorer window
    const auto& request = f.pane.list_snapshot().request.location;
    EXPECT(request.parsing_name == LR"(D:\Target)");
    EXPECT(!request.namespace_location);
    f.deliver(LR"(D:\Target)", managed_list::Phase::complete, {{L"z.zip", 9, false}});
    // A Shell-folder file is enumerated through the namespace path.
    f.presenter->folder = explorer_host::FolderTarget{LR"(D:\Target\z.zip)", true};
    f.select({L"z.zip"});
    f.enter();
    EXPECT(f.pane.list_snapshot().request.location.parsing_name == LR"(D:\Target\z.zip)");
    EXPECT(f.pane.list_snapshot().request.location.namespace_location);
}

void restored_rows_open_while_revalidated() {
    Fixture f;
    f.deliver(LR"(C:\A)", managed_list::Phase::complete, sample_rows());
    f.pane.refresh_view(); // keeps the snapshot and re-validates it
    EXPECT(f.pane.list_snapshot().updating);
    EXPECT(f.pane.list_snapshot().rows.size() == 4);
    f.select({L"alpha.txt"});
    f.enter();
    EXPECT(f.presenter->invoked.size() == 1);
    app_shell::Pane::FileCommandSnapshot snapshot;
    EXPECT(f.pane.file_command_snapshot(file_operations::Command::copy, snapshot) == S_OK);
    EXPECT(snapshot.items == std::vector<std::wstring>{LR"(C:\A\alpha.txt)"});
    // A different location has no rows of its own yet: nothing is targetable.
    EXPECT(SUCCEEDED(f.pane.navigate_to({LR"(C:\B)", {}, {}})));
    EXPECT(f.pane.list_snapshot().rows.empty());
    EXPECT(f.pane.file_command_snapshot(file_operations::Command::copy, snapshot) == E_PENDING);
    EXPECT(!f.pane.open_list_selection());
    EXPECT(f.presenter->invoked.size() == 1);
}

void typed_address_is_trimmed_and_servers_use_the_namespace() {
    Fixture f;
    f.deliver(LR"(C:\A)", managed_list::Phase::complete, sample_rows());
    f.type_address(LR"(  "C:\B"  )");
    EXPECT(f.pane.list_snapshot().request.location.parsing_name == LR"(C:\B)");
    f.deliver(LR"(C:\B)", managed_list::Phase::empty);
    f.type_address(LR"(\\server\)");
    const auto& location = f.pane.list_snapshot().request.location;
    EXPECT(location.parsing_name == LR"(\\server)");
    EXPECT(location.namespace_location);
    // Escape shows the displayed location again.
    SetWindowTextW(f.pane.address_bar(), L"junk");
    SendMessageW(f.pane.address_bar(), WM_KEYDOWN, VK_ESCAPE, 0);
    EXPECT(f.address_text() == LR"(\\server)");
}

void failed_address_keeps_the_current_folder() {
    Fixture f;
    f.deliver(LR"(C:\A)", managed_list::Phase::complete, sample_rows());
    const auto history = f.state.tabs[0].history;
    const auto cursor = f.state.tabs[0].history_index;
    f.select({L"beta.txt"});
    f.type_address(LR"(C:\nope)");
    f.deliver(LR"(C:\nope)", managed_list::Phase::error, {}, ERROR_PATH_NOT_FOUND);
    f.pump_pane();
    EXPECT(f.presenter->messages.size() == 1);
    EXPECT(!f.presenter->messages.empty() &&
           f.presenter->messages.back().find(LR"(can't find 'C:\nope')") != std::wstring::npos);
    // Back in C:\A with its rows and selection, and no history entry added.
    EXPECT(f.pane.list_snapshot().request.location.parsing_name == LR"(C:\A)");
    EXPECT(f.pane.list_snapshot().rows.size() == 4);
    const auto selected = f.pane.list_interaction_snapshot().selected;
    EXPECT(selected.size() == 1 && selected.front().name == L"beta.txt");
    f.deliver(LR"(C:\A)", managed_list::Phase::complete, sample_rows());
    EXPECT(f.state.tabs[0].history == history);
    EXPECT(f.state.tabs[0].history_index == cursor);
    EXPECT(f.state.tabs[0].location.parsing_name == LR"(C:\A)");
    EXPECT(f.address_text() == LR"(C:\A)");

    // A typed file opens with its default verb; the pane stays put.
    f.type_address(LR"(C:\A\alpha.txt)");
    f.deliver(LR"(C:\A\alpha.txt)", managed_list::Phase::error, {}, ERROR_DIRECTORY);
    f.pump_pane();
    EXPECT(f.presenter->messages.size() == 1);
    EXPECT(f.presenter->invoked.size() == 1);
    if (!f.presenter->invoked.empty()) {
        EXPECT(f.presenter->invoked.back().folder == LR"(C:\A)");
        EXPECT(f.presenter->invoked.back().items == std::vector<std::wstring>{L"alpha.txt"});
    }
    EXPECT(f.pane.list_snapshot().request.location.parsing_name == LR"(C:\A)");

    // A failure superseded by a newer navigation does nothing.
    f.deliver(LR"(C:\A)", managed_list::Phase::complete, sample_rows());
    f.type_address(LR"(C:\gone)");
    f.deliver(LR"(C:\gone)", managed_list::Phase::error, {}, ERROR_PATH_NOT_FOUND);
    EXPECT(SUCCEEDED(f.pane.navigate_to({LR"(C:\B)", {}, {}})));
    f.pump_pane();
    EXPECT(f.presenter->messages.size() == 1);
    EXPECT(f.pane.list_snapshot().request.location.parsing_name == LR"(C:\B)");
}

void up_from_a_share_root_lists_the_server() {
    Fixture f;
    f.deliver(LR"(C:\A)", managed_list::Phase::complete, sample_rows());
    EXPECT(SUCCEEDED(f.pane.navigate_to({LR"(\\server\share)", {}, {}})));
    f.deliver(LR"(\\server\share)", managed_list::Phase::empty);
    f.pane.navigate_up();
    const auto& location = f.pane.list_snapshot().request.location;
    EXPECT(location.parsing_name == LR"(\\server)");
    EXPECT(location.namespace_location);
}

void rapid_back_presses_step_twice() {
    Fixture f;
    f.deliver(LR"(C:\A)", managed_list::Phase::complete, sample_rows());
    for (const auto* location : {LR"(C:\B)", LR"(C:\C)"}) {
        EXPECT(SUCCEEDED(f.pane.navigate_to({location, {}, {}})));
        f.deliver(location, managed_list::Phase::empty);
    }
    EXPECT(f.state.tabs[0].history.size() == 3);
    EXPECT(f.state.tabs[0].history_index == 2);
    f.pane.navigate_history(true);
    EXPECT(f.pane.list_snapshot().request.location.parsing_name == LR"(C:\B)");
    f.pane.navigate_history(true); // before C:\B finished loading
    EXPECT(f.pane.list_snapshot().request.location.parsing_name == LR"(C:\A)");
    f.deliver(LR"(C:\A)", managed_list::Phase::complete, sample_rows());
    EXPECT(f.state.tabs[0].history_index == 0);
    EXPECT(f.state.tabs[0].history.size() == 3);
    f.pane.navigate_history(false);
    f.pane.navigate_history(false);
    EXPECT(f.pane.list_snapshot().request.location.parsing_name == LR"(C:\C)");
    f.pane.navigate_history(false); // already at the end
    EXPECT(f.pane.list_snapshot().request.location.parsing_name == LR"(C:\C)");
}
} // namespace

int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    enter_opens_the_whole_selection();
    many_items_ask_first();
    shortcut_and_zip_targets_stay_in_the_pane();
    restored_rows_open_while_revalidated();
    typed_address_is_trimmed_and_servers_use_the_namespace();
    failed_address_keeps_the_current_folder();
    up_from_a_share_root_lists_the_server();
    rapid_back_presses_step_twice();
    return test::summary("managed_list_open");
}
