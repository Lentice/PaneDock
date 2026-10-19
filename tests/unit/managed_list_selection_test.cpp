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

struct OwnedIdentity final {
    std::wstring location;
    std::wstring name;
    bool operator==(const OwnedIdentity&) const = default;
};
struct OwnedInteraction final {
    std::vector<OwnedIdentity> selected;
    std::optional<OwnedIdentity> focused;
    std::optional<OwnedIdentity> anchor;
    bool operator==(const OwnedInteraction&) const = default;
};

OwnedInteraction own(const app_shell::Pane::ListInteractionSnapshot& snapshot) {
    OwnedInteraction result;
    for (const auto& item : snapshot.selected)
        result.selected.push_back({std::wstring(item.location), std::wstring(item.name)});
    if (snapshot.focused)
        result.focused = OwnedIdentity{std::wstring(snapshot.focused->location),
                                       std::wstring(snapshot.focused->name)};
    if (snapshot.anchor)
        result.anchor = OwnedIdentity{std::wstring(snapshot.anchor->location),
                                      std::wstring(snapshot.anchor->name)};
    return result;
}

struct Fixture final {
    test::TestPaneHost host;
    core::PaneState state{"pane", {
        {"tab-a", {L"C:\\A", {}, {}}, "details", {}, true, {}, 0},
        {"tab-b", {L"C:\\B", {}, {}}, "details", {}, true, {}, 0}}, "tab-a"};
    app_shell::Pane pane;
    Controlled* source{};
    HWND parent{};
    HWND list{};

    Fixture() {
        const auto instance = GetModuleHandleW(nullptr);
        EXPECT(app_shell::Pane::register_window_class(instance));
        parent = CreateWindowExW(0, L"STATIC", L"managed list selection test",
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
    void key(UINT key, WPARAM modifiers = 0) {
        MSG message{};
        message.hwnd = list;
        message.message = WM_KEYDOWN;
        message.wParam = key;
        EXPECT(pane.translate_content_accelerator(&message) == S_FALSE);
        SendMessageW(list, WM_KEYDOWN, key, modifiers);
        SendMessageW(list, WM_KEYUP, key, modifiers);
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

void native_selection_and_keyboard_navigation_report_item_identities() {
    Fixture f;
    f.deliver(L"C:\\A", managed_list::Phase::loading,
        {{L"alpha.txt", 1, false}, {L"beta.txt", 2, false},
         {L"gamma.txt", 3, false}, {L"delta.txt", 4, false}});

    f.select_item(1);
    auto observed = f.pane.list_interaction_snapshot();
    EXPECT(observed.selected.size() == 1);
    if (observed.selected.size() == 1) {
        EXPECT((observed.selected[0] == app_shell::Pane::ListItemIdentity{L"C:\\A", L"beta.txt"}));
    }
    EXPECT((observed.focused == app_shell::Pane::ListItemIdentity{L"C:\\A", L"beta.txt"}));
    EXPECT((observed.anchor == app_shell::Pane::ListItemIdentity{L"C:\\A", L"beta.txt"}));

    f.key(VK_DOWN);
    auto keyboard = f.pane.list_interaction_snapshot();
    EXPECT(keyboard.selected.size() == 1);
    if (keyboard.selected.size() == 1)
        EXPECT((keyboard.selected[0] == app_shell::Pane::ListItemIdentity{L"C:\\A", L"gamma.txt"}));
    EXPECT((keyboard.focused == app_shell::Pane::ListItemIdentity{L"C:\\A", L"gamma.txt"}));

    // Owner-data ListViews ask their parent for the item matching typed text.
    // Sending WM_CHAR exercises that native route (including LVN_ODFINDITEMW).
    SendMessageW(f.list, WM_CHAR, L'd', 0);
    const auto typed = f.pane.list_interaction_snapshot();
    EXPECT(typed.selected.size() == 1);
    if (typed.selected.size() == 1)
        EXPECT((typed.selected[0] == app_shell::Pane::ListItemIdentity{L"C:\\A", L"delta.txt"}));
    EXPECT((typed.focused == app_shell::Pane::ListItemIdentity{L"C:\\A", L"delta.txt"}));

    NMLVFINDITEMW prefix{};
    prefix.hdr.hwndFrom = f.list;
    prefix.hdr.code = LVN_ODFINDITEMW;
    prefix.iStart = 0;
    prefix.lvfi.flags = LVFI_SUBSTRING;
    prefix.lvfi.psz = const_cast<wchar_t*>(L"gam");
    EXPECT(SendMessageW(f.pane.explorer_container(), WM_NOTIFY, 0,
        reinterpret_cast<LPARAM>(&prefix)) == 2);
}

void range_selection_survives_batch_append_by_identity() {
    Fixture f;
    f.deliver(L"C:\\A", managed_list::Phase::loading,
        {{L"one.txt", 1, false}, {L"two.txt", 2, false},
         {L"three.txt", 3, false}, {L"four.txt", 4, false}});
    f.select_range(1, 3);

    const auto before = own(f.pane.list_interaction_snapshot());
    EXPECT(before.selected.size() == 3);
    if (before.selected.size() == 3) {
        EXPECT((before.selected[0] == OwnedIdentity{L"C:\\A", L"two.txt"}));
        EXPECT((before.selected[1] == OwnedIdentity{L"C:\\A", L"three.txt"}));
        EXPECT((before.selected[2] == OwnedIdentity{L"C:\\A", L"four.txt"}));
    }
    EXPECT((before.focused == OwnedIdentity{L"C:\\A", L"four.txt"}));
    EXPECT((before.anchor == OwnedIdentity{L"C:\\A", L"two.txt"}));

    f.deliver(L"C:\\A", managed_list::Phase::loading,
        {{L"five.txt", 5, false}, {L"six.txt", 6, false}});
    const auto after = own(f.pane.list_interaction_snapshot());
    EXPECT(after == before);
    EXPECT(ListView_GetItemCount(f.list) == 6);
}

void switching_tabs_and_reentering_clears_interaction() {
    Fixture f;
    f.deliver(L"C:\\A", managed_list::Phase::complete,
        {{L"alpha.txt", 1, false}, {L"beta.txt", 2, false}});
    f.select_item(1);
    EXPECT(!f.pane.list_interaction_snapshot().selected.empty());

    f.pane.switch_active_tab("tab-b");
    EXPECT(f.pane.active_tab()->id == "tab-b");
    EXPECT(f.pane.list_snapshot().rows.empty());
    EXPECT(f.pane.list_interaction_snapshot() == app_shell::Pane::ListInteractionSnapshot{});
    EXPECT(ListView_GetItemCount(f.list) == 0);
    f.deliver(L"C:\\B", managed_list::Phase::complete,
        {{L"other.txt", 3, false}});
    EXPECT(f.pane.list_interaction_snapshot() == app_shell::Pane::ListInteractionSnapshot{});

    f.pane.navigate_to({L"C:\\A\\child", {}, {}});
    EXPECT(f.pane.list_snapshot().rows.empty());
    EXPECT(f.pane.list_interaction_snapshot() == app_shell::Pane::ListInteractionSnapshot{});
    f.deliver(L"C:\\A\\child", managed_list::Phase::empty);
    f.pane.navigate_to({L"C:\\A", {}, {}});
    EXPECT(f.pane.list_snapshot().rows.empty());
    EXPECT(f.pane.list_interaction_snapshot() == app_shell::Pane::ListInteractionSnapshot{});
    f.deliver(L"C:\\A", managed_list::Phase::complete,
        {{L"alpha.txt", 1, false}, {L"beta.txt", 2, false}});
    EXPECT(f.pane.list_interaction_snapshot() == app_shell::Pane::ListInteractionSnapshot{});
}

void managed_list_yields_global_shortcut_precedence() {
    Fixture f;
    f.deliver(L"C:\\A", managed_list::Phase::complete,
        {{L"folder", 0, true}, {L"file.txt", 1, false}});
    const auto unchanged_location = f.pane.list_snapshot().request.location.parsing_name;

    struct KeyCase final {
        unsigned virtual_key;
        bool control;
        bool alt;
        bool shift;
        core::KeyAction expected;
        core::ShellAccelerator shell;
    };
    const KeyCase cases[] = {
        {core::kVirtualKeyTab, false, false, false,
         core::KeyAction::focus_next_pane, core::ShellAccelerator::never},
        {core::kVirtualKeyTab, false, false, true,
         core::KeyAction::focus_previous_pane, core::ShellAccelerator::never},
        {core::kVirtualKeyTab, true, false, false,
         core::KeyAction::next_tab, core::ShellAccelerator::before_action},
        {'T', true, false, false,
         core::KeyAction::new_tab, core::ShellAccelerator::before_action},
        {'W', true, false, false,
         core::KeyAction::close_tab, core::ShellAccelerator::before_action},
        {core::kVirtualKeyLeft, false, true, false,
         core::KeyAction::history_back, core::ShellAccelerator::before_action},
        {core::kVirtualKeyRight, false, true, false,
         core::KeyAction::history_forward, core::ShellAccelerator::before_action},
    };
    for (const auto& item : cases) {
        core::KeyInput input{};
        input.message = core::KeyMessage::key_down;
        input.virtual_key = item.virtual_key;
        input.control = item.control;
        input.alt = item.alt;
        input.shift = item.shift;
        input.pane_count = 2;
        const auto routing = core::resolve_key(input);
        EXPECT(routing.action == item.expected);
        EXPECT(routing.shell == item.shell);

        MSG message{};
        message.hwnd = f.list;
        message.message = WM_KEYDOWN;
        message.wParam = item.virtual_key;
        EXPECT(f.pane.translate_content_accelerator(&message) == S_FALSE);
    }
    core::KeyInput address_tab{};
    address_tab.message = core::KeyMessage::key_down;
    address_tab.virtual_key = core::kVirtualKeyTab;
    address_tab.address_bar_focused = true;
    address_tab.pane_count = 2;
    EXPECT(core::resolve_key(address_tab).action == core::KeyAction::none);
    EXPECT(core::resolve_key(address_tab).shell == core::ShellAccelerator::never);

    // Ctrl+Backspace is ordinary list input; only unmodified Backspace is
    // the list's navigate-up accelerator.
    BYTE keyboard_state[256]{};
    EXPECT(GetKeyboardState(keyboard_state));
    auto ctrl_state = std::array<BYTE, 256>{};
    std::copy(std::begin(keyboard_state), std::end(keyboard_state), ctrl_state.begin());
    ctrl_state[VK_CONTROL] |= 0x80;
    EXPECT(SetKeyboardState(ctrl_state.data()));
    MSG ctrl_backspace{};
    ctrl_backspace.hwnd = f.list;
    ctrl_backspace.message = WM_KEYDOWN;
    ctrl_backspace.wParam = VK_BACK;
    EXPECT(f.pane.translate_content_accelerator(&ctrl_backspace) == S_FALSE);
    EXPECT(f.pane.list_snapshot().request.location.parsing_name == unchanged_location);
    EXPECT(SetKeyboardState(keyboard_state));
}
} // namespace

int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    native_selection_and_keyboard_navigation_report_item_identities();
    range_selection_survives_batch_append_by_identity();
    switching_tabs_and_reentering_clears_interaction();
    managed_list_yields_global_shortcut_precedence();
    return test::summary("managed_list_selection");
}
