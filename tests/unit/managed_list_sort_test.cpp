#include "unit/test_pane_host.h"
#include "unit/test_util.h"

#include <algorithm>
#include <deque>
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
        batches.pop_front();
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
    std::deque<managed_list::Batch> batches;
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
    std::optional<OwnedIdentity> viewport_anchor;
    int viewport_offset{};
    bool viewport_anchor_clamped{};
    bool operator==(const OwnedInteraction&) const = default;
};

OwnedInteraction own(const app_shell::Pane::ListInteractionSnapshot& snapshot) {
    OwnedInteraction result;
    for (const auto& item : snapshot.selected)
        result.selected.push_back({std::wstring(item.location), std::wstring(item.name)});
    std::sort(result.selected.begin(), result.selected.end(),
        [](const auto& left, const auto& right) { return left.name < right.name; });
    if (snapshot.focused)
        result.focused = OwnedIdentity{std::wstring(snapshot.focused->location),
                                       std::wstring(snapshot.focused->name)};
    if (snapshot.anchor)
        result.anchor = OwnedIdentity{std::wstring(snapshot.anchor->location),
                                      std::wstring(snapshot.anchor->name)};
    if (snapshot.viewport_anchor)
        result.viewport_anchor = OwnedIdentity{
            std::wstring(snapshot.viewport_anchor->location),
            std::wstring(snapshot.viewport_anchor->name)};
    result.viewport_offset = snapshot.viewport_offset;
    result.viewport_anchor_clamped = snapshot.viewport_anchor_clamped;
    return result;
}

struct Fixture final {
    test::TestPaneHost host;
    core::PaneState state{"pane", {
        {"tab", {L"C:\\A", {}, {}}, "details", "System.ItemNameDisplay",
         true, {}, 0}}, "tab"};
    app_shell::Pane pane;
    Controlled* source{};
    HWND parent{};
    HWND list{};

    Fixture() {
        const auto instance = GetModuleHandleW(nullptr);
        EXPECT(app_shell::Pane::register_window_class(instance));
        parent = CreateWindowExW(0, L"STATIC", L"managed list sort test",
            0, 0, 0, 800, 600, nullptr, nullptr, instance, nullptr);
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
    void deliver(managed_list::Phase phase, std::vector<managed_list::Row> rows = {}) {
        source->deliver({pane.list_snapshot().request, std::move(rows), phase, 0});
        pane.process_content_results();
    }
    int row(std::wstring_view name) const {
        const auto& rows = pane.list_snapshot().rows;
        const auto found = std::find_if(rows.begin(), rows.end(),
            [&](const auto& item) { return item.name == name; });
        return found == rows.end() ? -1 : static_cast<int>(found - rows.begin());
    }
    void click_column(int column) {
        NMLISTVIEW notification{};
        notification.hdr.hwndFrom = list;
        notification.hdr.code = LVN_COLUMNCLICK;
        notification.iSubItem = column;
        EXPECT(pane.list_notification(&notification.hdr).has_value());
    }
};

void basic_keys_group_directories_and_keep_deterministic_ties() {
    Fixture f;
    f.deliver(managed_list::Phase::complete, {
        {L"beta.txt", 8, false, 20}, {L"alpha.txt", 8, false, 20},
        {L"Alpha.txt", 8, false, 20}, {L"zeta-folder", 0, true, 30},
        {L"alpha-folder", 0, true, 10}, {L"zero.txt", 0, false, 40},
        {L"missing-date.txt", 2, false, 0}, {L"dated.txt", 2, false, 50}});

    f.click_column(1); // Size ascending; empty directory sizes remain last within the folder group.
    const auto& ascending = f.pane.list_snapshot().rows;
    EXPECT(ascending.size() == 8);
    if (ascending.size() == 8) {
        EXPECT(ascending[0].name == L"alpha-folder");
        EXPECT(ascending[1].name == L"zeta-folder");
        EXPECT(ascending[2].name == L"zero.txt"); // zero is a value, not empty
        EXPECT(ascending[3].name == L"dated.txt");
        EXPECT(ascending[4].name == L"missing-date.txt");
        EXPECT(ascending[5].name == L"Alpha.txt");
        EXPECT(ascending[6].name == L"alpha.txt");
        EXPECT(ascending[7].name == L"beta.txt");
    }

    f.click_column(1); // Descending keeps folders first and name tie-break ascending.
    const auto& descending = f.pane.list_snapshot().rows;
    EXPECT(descending.size() == 8);
    if (descending.size() == 8) {
        EXPECT(descending[0].name == L"alpha-folder");
        EXPECT(descending[1].name == L"zeta-folder");
        EXPECT(descending[2].name == L"Alpha.txt");
        EXPECT(descending[3].name == L"alpha.txt");
        EXPECT(descending[4].name == L"beta.txt");
        EXPECT(descending[5].name == L"dated.txt");
        EXPECT(descending[6].name == L"missing-date.txt");
        EXPECT(descending[7].name == L"zero.txt");
    }

    f.click_column(2); // Date Modified ascending; missing timestamps sort last.
    const auto& by_date = f.pane.list_snapshot().rows;
    EXPECT(by_date.size() == 8);
    if (by_date.size() == 8) {
        EXPECT(by_date[0].name == L"alpha-folder");
        EXPECT(by_date[1].name == L"zeta-folder");
        EXPECT(by_date[2].name == L"Alpha.txt");
        EXPECT(by_date[3].name == L"alpha.txt");
        EXPECT(by_date[4].name == L"beta.txt");
        EXPECT(by_date[5].name == L"zero.txt");
        EXPECT(by_date[6].name == L"dated.txt");
        EXPECT(by_date[7].name == L"missing-date.txt");
    }
}

void unicode_name_order_and_unknown_sort_fallback_are_stable() {
    Fixture f;
    f.state.tabs.front().sort_column = "System.ItemNameDisplay";
    f.state.tabs.front().sort_ascending = true;
    f.deliver(managed_list::Phase::complete, {
        {L"å.txt", 1, false, 1}, {L"z.txt", 1, false, 1},
        {L"a.txt", 1, false, 1}, {L"ä.txt", 1, false, 1},
        {L"A.txt", 1, false, 1}});
    const auto& rows = f.pane.list_snapshot().rows;
    EXPECT(rows.size() == 5);
    if (rows.size() == 5) {
        EXPECT(rows[0].name == L"A.txt");
        EXPECT(rows[1].name == L"a.txt");
        EXPECT(rows[2].name == L"z.txt");
        EXPECT(rows[3].name == L"ä.txt");
        EXPECT(rows[4].name == L"å.txt");
    }

    f.state.tabs.front().sort_column = "future.property.key";
    f.state.tabs.front().sort_ascending = false;
    f.pane.apply_sort();
    EXPECT(f.state.tabs.front().sort_column == "future.property.key");
    EXPECT(!f.state.tabs.front().sort_ascending);
    const auto& fallback = f.pane.list_snapshot().rows;
    EXPECT(fallback.size() == 5);
    if (fallback.size() == 5) {
        EXPECT(fallback[0].name == L"å.txt");
        EXPECT(fallback[1].name == L"ä.txt");
        EXPECT(fallback[2].name == L"z.txt");
        EXPECT(fallback[3].name == L"A.txt");
        EXPECT(fallback[4].name == L"a.txt");
    }
    f.pane.capture_sort();
    EXPECT(f.state.tabs.front().sort_column == "future.property.key");
}

void out_of_order_batches_and_sort_changes_preserve_item_and_viewport_identity() {
    Fixture f;
    std::vector<managed_list::Row> first;
    for (int index = 0; index < 48; ++index) {
        wchar_t name[32]{};
        swprintf_s(name, ARRAYSIZE(name), L"item-%02d.txt", index);
        first.push_back({name, static_cast<std::uint64_t>(index + 1), false,
                         static_cast<std::uint64_t>(index + 1)});
    }
    f.deliver(managed_list::Phase::loading, std::move(first));
    const int selected = f.row(L"item-08.txt");
    const int focused = f.row(L"item-37.txt");
    EXPECT(selected >= 0 && focused >= 0);
    if (selected >= 0 && focused >= 0) {
        ListView_SetItemState(f.list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetItemState(f.list, selected, LVIS_SELECTED, LVIS_SELECTED);
        ListView_SetItemState(f.list, focused,
                              LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
        ListView_SetSelectionMark(f.list, selected);
    }
    const int viewport = f.row(L"item-32.txt");
    if (viewport >= 0) ListView_EnsureVisible(f.list, viewport, FALSE);
    const auto before = own(f.pane.list_interaction_snapshot());
    EXPECT(before.selected.size() == 2);
    EXPECT((before.focused == OwnedIdentity{L"C:\\A", L"item-37.txt"}));
    EXPECT((before.anchor == OwnedIdentity{L"C:\\A", L"item-08.txt"}));
    EXPECT(before.viewport_anchor.has_value());

    // The late rows sort at opposite ends of the list, shifting indices when
    // the merged rows are sorted by either direction.
    f.deliver(managed_list::Phase::loading, {
        {L"item-00-new.txt", 0, false, 0}, {L"item-99-new.txt", 99, false, 99}});
    EXPECT(own(f.pane.list_interaction_snapshot()) == before);

    f.click_column(1);
    EXPECT(f.state.tabs.front().sort_column == "System.Size");
    EXPECT(f.state.tabs.front().sort_ascending);
    const auto after_ascending = own(f.pane.list_interaction_snapshot());
    EXPECT(after_ascending.selected == before.selected);
    EXPECT(after_ascending.focused == before.focused);
    EXPECT(after_ascending.anchor == before.anchor);
    EXPECT(after_ascending.viewport_anchor == before.viewport_anchor);
    EXPECT(after_ascending.viewport_offset == before.viewport_offset);
    f.click_column(1);
    EXPECT(!f.state.tabs.front().sort_ascending);
    const auto after_descending = own(f.pane.list_interaction_snapshot());
    EXPECT(after_descending.selected == before.selected);
    EXPECT(after_descending.focused == before.focused);
    EXPECT(after_descending.anchor == before.anchor);
    EXPECT(after_descending.viewport_anchor_clamped);
    EXPECT(after_descending.viewport_offset == before.viewport_offset);
    const auto& rows = f.pane.list_snapshot().rows;
    const int page = std::max(1, ListView_GetCountPerPage(f.list));
    const int max_top = std::max(0, static_cast<int>(rows.size()) - page);
    EXPECT(ListView_GetTopIndex(f.list) == max_top);
    if (max_top >= 0 && static_cast<std::size_t>(max_top) < rows.size()) {
        EXPECT(after_descending.viewport_anchor.has_value());
        EXPECT(after_descending.viewport_anchor->name == rows[max_top].name);
    }
    if (before.viewport_anchor) {
        const int original_anchor = f.row(before.viewport_anchor->name);
        RECT item{};
        RECT client{};
        EXPECT(original_anchor >= 0 &&
            ListView_GetItemRect(f.list, original_anchor, &item, LVIR_BOUNDS) &&
            GetClientRect(f.list, &client) && item.top < client.bottom &&
            item.bottom > client.top);
    }
}
} // namespace

int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    basic_keys_group_directories_and_keep_deterministic_ties();
    unicode_name_order_and_unknown_sort_fallback_are_stable();
    out_of_order_batches_and_sort_changes_preserve_item_and_viewport_identity();
    return test::summary("managed_list_sort");
}
