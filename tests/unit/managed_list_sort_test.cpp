#include "unit/test_pane_host.h"
#include "core/columns.h"
#include "unit/test_util.h"
#include "managed_list/icon_source_test.h"

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

class Properties final : public managed_list::IconSource {
  public:
    std::vector<managed_list::IconRequest> requests;
    std::deque<managed_list::IconResult> results;
    bool accept{true};
    bool replace(std::vector<managed_list::IconRequest> value) noexcept override {
        requests = std::move(value); return accept;
    }
    std::optional<managed_list::IconResult> take() noexcept override {
        if (results.empty()) return {};
        auto result = std::move(results.front()); results.pop_front(); return result;
    }
    HANDLE event() const noexcept override { return nullptr; }
    void cancel() noexcept override { requests.clear(); }
    void deliver(const managed_list::IconRequest& request, managed_list::PropertyValue value) {
        results.push_back({request, {}, std::move(value)});
    }
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
        list = FindWindowExW(pane.explorer_container(), nullptr, panedock::app_shell::kManagedListClassName, nullptr);
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

    f.click_column(1); // Descending reverses the folder grouping (Explorer); name ties stay ascending.
    const auto& descending = f.pane.list_snapshot().rows;
    EXPECT(descending.size() == 8);
    if (descending.size() == 8) {
        EXPECT(descending[0].name == L"Alpha.txt");
        EXPECT(descending[1].name == L"alpha.txt");
        EXPECT(descending[2].name == L"beta.txt");
        EXPECT(descending[3].name == L"dated.txt");
        EXPECT(descending[4].name == L"missing-date.txt");
        EXPECT(descending[5].name == L"zero.txt");
        EXPECT(descending[6].name == L"alpha-folder");
        EXPECT(descending[7].name == L"zeta-folder");
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
        EXPECT(rows[2].name == L"ä.txt");
        EXPECT(rows[3].name == L"å.txt");
        EXPECT(rows[4].name == L"z.txt");
    }

    f.state.tabs.front().sort_column = "future.property.key";
    f.state.tabs.front().sort_ascending = false;
    f.pane.apply_sort();
    EXPECT(f.state.tabs.front().sort_column == "future.property.key");
    EXPECT(!f.state.tabs.front().sort_ascending);
    const auto& fallback = f.pane.list_snapshot().rows;
    EXPECT(fallback.size() == 5);
    if (fallback.size() == 5) {
        EXPECT(fallback[0].name == L"z.txt");
        EXPECT(fallback[1].name == L"å.txt");
        EXPECT(fallback[2].name == L"ä.txt");
        EXPECT(fallback[3].name == L"A.txt");
        EXPECT(fallback[4].name == L"a.txt");
    }
    EXPECT(f.state.tabs.front().sort_column == "future.property.key");
}

void names_sort_naturally_and_descending_puts_folders_last() {
    Fixture f;
    f.state.tabs.front().sort_column = "System.ItemNameDisplay";
    f.state.tabs.front().sort_ascending = true;
    f.deliver(managed_list::Phase::complete, {
        {L"file10.txt", 1, false, 1}, {L"file2.txt", 1, false, 1}, {L"file1.txt", 1, false, 1},
        {L"dir10", 0, true, 1}, {L"dir2", 0, true, 1}});
    const auto& rows = f.pane.list_snapshot().rows;
    EXPECT(rows.size() == 5);
    if (rows.size() == 5) { // Explorer: folders first, digits compare as numbers
        EXPECT(rows[0].name == L"dir2");
        EXPECT(rows[1].name == L"dir10");
        EXPECT(rows[2].name == L"file1.txt");
        EXPECT(rows[3].name == L"file2.txt");
        EXPECT(rows[4].name == L"file10.txt");
    }
    f.click_column(0); // same column again: descending reverses everything, folders last
    const auto& reversed = f.pane.list_snapshot().rows;
    EXPECT(reversed.size() == 5);
    if (reversed.size() == 5) {
        EXPECT(reversed[0].name == L"file10.txt");
        EXPECT(reversed[1].name == L"file2.txt");
        EXPECT(reversed[2].name == L"file1.txt");
        EXPECT(reversed[3].name == L"dir10");
        EXPECT(reversed[4].name == L"dir2");
    }
}

void columns_can_be_hidden_and_keep_clicks_and_cell_text_correct() {
    Fixture f;
    f.deliver(managed_list::Phase::complete, {{L"a.txt", 5, false, 10}, {L"b.txt", 9, false, 20}});
    const HWND header = ListView_GetHeader(f.list);
    EXPECT(Header_GetItemCount(header) == 6 && f.pane.list_column_ids().size() == 6);
    const auto text = [&](int column) {
        wchar_t buffer[64]{};
        LVITEMW item{};
        item.iSubItem = column;
        item.pszText = buffer;
        item.cchTextMax = 64;
        SendMessageW(f.list, LVM_GETITEMTEXTW, 0, reinterpret_cast<LPARAM>(&item));
        return std::wstring(buffer);
    };
    EXPECT(text(0) == L"a.txt" && text(1) == L"1 KB");
    f.pane.toggle_list_column("size");
    EXPECT(Header_GetItemCount(header) == 5 && !core::column_visible(core::parse_columns(f.state.tabs.front().columns), "size"));
    // Cell data follows the header position: position 1 is Date Modified now, not Size.
    EXPECT(text(1) != L"1 KB" && !text(1).empty());
    // Header position 3 is now Duration: a click there must sort by Duration, not Type.
    f.click_column(3);
    EXPECT(f.state.tabs.front().sort_column == "System.Media.Duration");
    f.click_column(2); // Type sits at position 2 now
    EXPECT(f.state.tabs.front().sort_column == "System.ItemTypeText");
    // Dragging a header reorders; the layout is stored in display order, Name stays first.
    std::array<int, 5> order{0, 2, 1, 3, 4}; // Type before Date Modified
    ListView_SetColumnOrderArray(f.list, 5, order.data());
    f.pane.capture_list_columns();
    auto layout = core::parse_columns(f.state.tabs.front().columns);
    EXPECT(layout.size() == 5 && layout[0].id == "name" && layout[1].id == "type" && layout[2].id == "date");
    order = {1, 0, 2, 3, 4}; // attempt to move Name away from the first position
    ListView_SetColumnOrderArray(f.list, 5, order.data());
    f.pane.capture_list_columns();
    layout = core::parse_columns(f.state.tabs.front().columns);
    EXPECT(layout[0].id == "name");
    // Showing a column again appends it with its default width.
    f.pane.toggle_list_column("size");
    layout = core::parse_columns(f.state.tabs.front().columns);
    EXPECT(layout.size() == 6 && layout.back().id == "size" && Header_GetItemCount(header) == 6);
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

managed_list::PropertyValue number(long double value) {
    return {managed_list::PropertyState::value, {}, value, true};
}
std::vector<std::wstring> order(const Fixture& f) {
    std::vector<std::wstring> names;
    for (const auto& row : f.pane.list_snapshot().rows) names.push_back(row.name);
    return names;
}
Properties* use_properties(Fixture& f) {
    auto source = std::make_unique<Properties>();
    auto* result = source.get(); f.pane.use_list_icons(std::move(source)); return result;
}
void expensive_sort_commits_once_and_preserves_interaction() {
    Fixture f;
    auto* source = use_properties(f);
    std::vector<managed_list::Row> rows;
    for (int i = 0; i < 50; ++i) rows.push_back({L"item-" + std::to_wstring(i), 1, false, 1});
    f.deliver(managed_list::Phase::complete, std::move(rows));
    ListView_SetItemState(f.list, 17, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(f.list, 17);
    ListView_EnsureVisible(f.list, 35, FALSE);
    const auto interaction = own(f.pane.list_interaction_snapshot());
    const auto before = order(f);
    f.click_column(4);
    EXPECT(f.pane.list_property_sort_pending());
    f.pane.set_list_icon_viewport(0, 3);
    auto requests = source->requests;
    EXPECT(requests.size() <= managed_list::kIconInterest);
    std::vector<managed_list::IconRequest> properties;
    for (const auto& r : requests) if (!r.property.empty()) properties.push_back(r);
    EXPECT(properties.size() == 50); // all keys, including offscreen rows
    std::reverse(properties.begin(), properties.end());
    for (std::size_t i = 0; i < properties.size(); ++i) {
        auto value = number(100 - static_cast<long double>(properties[i].row_index));
        if (properties[i].row_index == 0) value.state = managed_list::PropertyState::missing;
        if (properties[i].row_index == 1) value.state = managed_list::PropertyState::failed;
        source->deliver(properties[i], std::move(value));
        f.pane.process_icon_results();
        if (i + 1 < properties.size()) {
            EXPECT(order(f) == before);
            EXPECT(f.pane.list_property_sort_pending());
        }
    }
    for (const auto& r : requests) if (r.property.empty()) source->deliver(r, {});
    f.pane.process_icon_results(); f.pane.process_icon_results();
    EXPECT(!f.pane.list_property_sort_pending());
    const auto after = order(f);
    EXPECT(after != before);
    EXPECT(after.front() == before.back());
    EXPECT(after[48] == before[0]); EXPECT(after[49] == before[1]);
    const auto restored = own(f.pane.list_interaction_snapshot());
    EXPECT(restored.selected == interaction.selected);
    EXPECT(restored.focused == interaction.focused);
    EXPECT(restored.anchor == interaction.anchor);
    if (!restored.viewport_anchor_clamped)
        EXPECT(restored.viewport_anchor == interaction.viewport_anchor);
    else if (interaction.viewport_anchor) {
        RECT item{}, client{};
        EXPECT(ListView_GetItemRect(f.list, f.row(interaction.viewport_anchor->name), &item, LVIR_BOUNDS));
        EXPECT(GetClientRect(f.list, &client));
        EXPECT(item.top < client.bottom && item.bottom > client.top);
    }
    EXPECT(restored.viewport_offset == interaction.viewport_offset);
    f.click_column(4); // cached values: no extraction, same missing-last policy
    f.pane.set_list_icon_viewport(0, 3);
    EXPECT(!f.pane.list_property_sort_pending());
    EXPECT(order(f).front() == before[2]);
    for (const auto& r : source->requests) EXPECT(r.property.empty());
}
void property_demand_is_bounded_and_stale_results_are_rejected() {
    Fixture f;
    auto* source = use_properties(f);
    for (int batch = 0; batch < 3; ++batch) {
        std::vector<managed_list::Row> rows;
        for (int i = 0; i < 120; ++i)
            rows.push_back({L"item-" + std::to_wstring(batch * 120 + i), 1, false, 1});
        f.deliver(batch == 2 ? managed_list::Phase::complete : managed_list::Phase::loading, std::move(rows));
    }
    f.click_column(4);
    f.pane.set_list_icon_viewport(0, 3);
    auto stale = source->requests.front();
    EXPECT(!stale.property.empty());
    f.pane.set_list_icon_viewport(100, 3);
    source->deliver(stale, number(999)); f.pane.process_icon_results();
    EXPECT(f.pane.list_snapshot().rows[stale.row_index].properties[1].state == managed_list::PropertyState::unresolved);
    std::size_t completed = 0;
    for (int round = 0; round < 8 && f.pane.list_property_sort_pending(); ++round) {
        auto requests = source->requests;
        EXPECT(requests.size() <= managed_list::kIconInterest);
        for (const auto& r : requests) {
            source->deliver(r, number(1));
            if (!r.property.empty()) ++completed;
        }
        while (!source->results.empty()) f.pane.process_icon_results();
        f.pane.set_list_icon_viewport(100, 3);
    }
    EXPECT(completed == 360);
    EXPECT(!f.pane.list_property_sort_pending());
    const auto before = order(f);
    source->deliver(stale, number(0)); f.pane.process_icon_results();
    EXPECT(order(f) == before);
    EXPECT(SUCCEEDED(f.pane.navigate_to({L"C:\\B", {}, {}})));
    f.deliver(managed_list::Phase::complete, {{L"new", 2, false, 2}});
    source->deliver(stale, number(100)); f.pane.process_icon_results();
    EXPECT(f.pane.list_snapshot().rows.size() == 1);
    EXPECT(f.pane.list_snapshot().rows.front().properties[1].state == managed_list::PropertyState::unresolved);
}
void display_demand_and_custom_keys_use_the_same_bounded_boundary() {
    Fixture f;
    auto* source = use_properties(f);
    f.deliver(managed_list::Phase::complete, {{L"a", 1, false}, {L"b", 2, false}, {L"c", 3, false}});
    for (int column = 0; column < 5; ++column) ListView_SetColumnWidth(f.list, column, 100);
    f.pane.set_list_icon_viewport(0, 1);
    std::size_t properties = 0;
    for (const auto& r : source->requests) if (!r.property.empty()) {
        ++properties; EXPECT(r.row_index == 0);
    }
    EXPECT(properties == 2); // display-only never extracts offscreen keys
    auto old = source->requests;
    f.state.tabs.front().sort_column = "{9B174B34-40FF-11D2-A27E-00C04FC30871} 5";
    f.pane.apply_sort();
    f.pane.set_list_icon_viewport(1, 1);
    EXPECT(f.pane.list_property_sort_pending());
    auto requests = source->requests;
    std::size_t custom = 0;
    for (const auto& r : requests) {
        if (r.property_slot == 2) { ++custom; EXPECT(r.property == f.state.tabs.front().sort_column); }
        source->deliver(r, r.property_slot == 2 ?
            managed_list::PropertyValue{managed_list::PropertyState::value, r.row_index == 0 ? L"Z" : L"A", 0, false} : number(1));
    }
    EXPECT(custom == 3);
    while (!source->results.empty()) f.pane.process_icon_results();
    EXPECT(!f.pane.list_property_sort_pending());
    EXPECT(order(f) == std::vector<std::wstring>({L"b", L"c", L"a"}));
    for (const auto& r : old) source->deliver(r, number(99));
    while (!source->results.empty()) f.pane.process_icon_results();
    EXPECT(f.pane.list_snapshot().rows[0].properties[0].state == managed_list::PropertyState::unresolved);
    // Basic canonical keys reuse cached enumeration fields instead of property reads.
    f.state.tabs.front().sort_column = "{B725F130-47EF-101A-A5F1-02608C9EEBAC} 12";
    f.pane.apply_sort(); EXPECT(!f.pane.list_property_sort_pending());
    EXPECT(f.state.tabs.front().sort_column == "{B725F130-47EF-101A-A5F1-02608C9EEBAC} 12");
}

std::wstring cell(Fixture& f, int row, int column) {
    wchar_t buffer[256]{};
    NMLVDISPINFOW info{};
    info.hdr.hwndFrom = f.list; info.hdr.code = LVN_GETDISPINFOW;
    info.item.mask = LVIF_TEXT; info.item.iItem = row; info.item.iSubItem = column;
    info.item.pszText = buffer; info.item.cchTextMax = ARRAYSIZE(buffer);
    EXPECT(f.pane.list_notification(&info.hdr).has_value()); return buffer;
}
std::wstring status(Fixture& f) {
    wchar_t buffer[512]{}; GetWindowTextW(f.pane.status_bar(), buffer, ARRAYSIZE(buffer)); return buffer;
}
void property_cells_status_and_explicit_retry() {
    Fixture f;
    auto* source = use_properties(f);
    for (int column = 0; column < 5; ++column) ListView_SetColumnWidth(f.list, column, 100);
    f.deliver(managed_list::Phase::complete, {{L"a", 1, false}, {L"b", 2, false}});
    EXPECT(cell(f, 0, 3) == L"Loading..."); EXPECT(cell(f, 0, 4) == L"Loading...");
    f.pane.set_list_icon_viewport(0, 2);
    auto requests = source->requests;
    for (const auto& r : requests) {
        auto value = number(650000000);
        if (r.row_index == 0 && r.property_slot == 0)
            value = {managed_list::PropertyState::value, L"Audio", 0, false};
        if (r.row_index == 1) value = {managed_list::PropertyState::failed, {}, 0, false};
        source->deliver(r, std::move(value));
    }
    while (!source->results.empty()) f.pane.process_icon_results();
    EXPECT(cell(f, 0, 3) == L"Audio"); EXPECT(cell(f, 0, 4) == L"1:05");
    EXPECT(cell(f, 1, 3) == L"Unavailable"); EXPECT(cell(f, 1, 4) == L"Unavailable");
    f.pane.set_list_icon_viewport(0, 2);
    EXPECT(source->requests.empty()); // failures are terminal until explicit Refresh
    f.pane.refresh_view();
    f.deliver(managed_list::Phase::complete, {{L"a", 1, false}, {L"b", 2, false}});
    f.click_column(4); f.pane.set_list_icon_viewport(0, 2);
    EXPECT(status(f).find(L"Sorting properties...") != std::wstring::npos);
    EXPECT(cell(f, 1, 4) == L"Loading...");
    auto retry = source->requests; EXPECT(retry.size() == 2);
    for (const auto& r : retry) source->deliver(r, number(10000000));
    while (!source->results.empty()) f.pane.process_icon_results();
    EXPECT(status(f).find(L"Sorting properties...") == std::wstring::npos);
    EXPECT(cell(f, 1, 4) == L"0:01");
    // A delayed key does not trap the old tab, even with the same folder/name.
    f.pane.refresh_view();
    f.deliver(managed_list::Phase::complete, {{L"a", 1, false}});
    f.pane.set_list_icon_viewport(0, 1);
    const auto old = source->requests.front();
    f.state.tabs.push_back({"second", {L"C:\\A", {}, {}}, "details", "System.Media.Duration", true, {}, 0});
    f.pane.activate_tab_at(1);
    EXPECT(f.state.active_tab_id == "second");
    f.deliver(managed_list::Phase::complete, {{L"a", 1, false}});
    source->deliver(old, number(990000000)); f.pane.process_icon_results();
    EXPECT(cell(f, 0, 4) == L"Loading...");
}
void exhausted_properties_degrade_without_blocking_navigation() {
    Fixture f;
    auto* source = use_properties(f);
    f.deliver(managed_list::Phase::complete, {{L"a", 1, false}});
    source->accept = false;
    f.click_column(4); f.pane.set_list_icon_viewport(0, 1);
    EXPECT(f.pane.list_property_capacity_failed());
    EXPECT(!f.pane.list_property_sort_pending());
    EXPECT(f.pane.list_snapshot().rows.size() == 1);
    EXPECT(status(f).find(L"Properties unavailable") != std::wstring::npos);
    source->accept = true;
    f.pane.refresh_view();
    f.deliver(managed_list::Phase::complete, {{L"a", 1, false}});
    f.pane.set_list_icon_viewport(0, 1);
    EXPECT(!f.pane.list_property_capacity_failed());
    EXPECT(f.pane.list_property_sort_pending());
    auto retry = source->requests; EXPECT(retry.size() == 1);
    for (const auto& r : retry) source->deliver(r, number(10000000));
    while (!source->results.empty()) f.pane.process_icon_results();
    EXPECT(cell(f, 0, 4) == L"0:01");
    EXPECT(!f.pane.list_property_sort_pending());
    EXPECT(SUCCEEDED(f.pane.navigate_to({L"C:\\B", {}, {}})));
    EXPECT(!f.pane.list_property_capacity_failed());
}

void bounded_order_yields_without_mutation_and_finishes_stably() {
    struct Item final {
        std::size_t key{}, original{};
        bool operator==(const Item&) const = default;
    };
    std::vector<Item> rows;
    for (std::size_t index = 0; index < 5000; ++index)
        rows.push_back({(5000 - index) % 17, index});
    const auto original = rows;
    core::BoundedOrder order;
    order.start(rows.size());
    const auto less = [&](auto a, auto b) { return rows[a].key < rows[b].key; };
    bool complete = order.step(2048, less);
    EXPECT(!complete);
    EXPECT(rows == original);
    std::size_t turns = 1;
    while (!complete && turns < 64) {
        complete = order.step(2048, less);
        ++turns;
        EXPECT(rows == original); // no partial permutation is ever presented
    }
    EXPECT(complete); EXPECT(turns > 1);
    order.apply(rows);
    for (std::size_t index = 1; index < rows.size(); ++index) {
        EXPECT(rows[index - 1].key <= rows[index].key);
        if (rows[index - 1].key == rows[index].key)
            EXPECT(rows[index - 1].original < rows[index].original);
    }
}
void property_owned_bytes_limit_keeps_rows_usable() {
    Fixture f;
    auto* source = use_properties(f);
    f.deliver(managed_list::Phase::complete, {{L"a", 1, false}});
    f.click_column(3); f.pane.set_list_icon_viewport(0, 1);
    auto request = source->requests.front();
    managed_list::PropertyValue oversized{managed_list::PropertyState::value, L"Type", 0, false};
    oversized.text.reserve(managed_list::kMaxRowBytes);
    source->deliver(request, std::move(oversized)); f.pane.process_icon_results();
    EXPECT(f.pane.list_property_capacity_failed()); EXPECT(!f.pane.list_property_sort_pending());
    EXPECT(f.pane.list_snapshot().rows.size() == 1);
    EXPECT(cell(f, 0, 3) == L"Unavailable");
    EXPECT(f.pane.list_snapshot().rows.front().properties[0].text.empty());
    source->deliver(request, {managed_list::PropertyState::value, L"late", 0, false});
    f.pane.process_icon_results(); EXPECT(cell(f, 0, 3) == L"Unavailable");
}
void paused_worker_property_keeps_navigation_available() {
    auto gate = std::make_shared<managed_list::testing::IconGate>();
    {
        Fixture f;
        auto worker = managed_list::testing::make_gated_icons(gate);
        EXPECT(worker != nullptr); f.pane.use_list_icons(std::move(worker));
        f.deliver(managed_list::Phase::complete, {{L"a", 1, false}});
        f.click_column(4); f.pane.set_list_icon_viewport(0, 1);
        EXPECT(WaitForSingleObject(gate->entered, 2000) == WAIT_OBJECT_0);
        EXPECT(f.pane.list_property_sort_pending());
        EXPECT(status(f).find(L"Sorting properties...") != std::wstring::npos);
        const auto counts = managed_list::testing::icon_counts();
        EXPECT(counts.workers <= managed_list::kIconWorkers);
        EXPECT(counts.running == 1); EXPECT(counts.pending <= managed_list::kIconInterest);
        // The provider remains gated during departure and destruction. Neither
        // operation joins it or needs its COM objects to return to the UI.
        EXPECT(SUCCEEDED(f.pane.navigate_to({L"C:\\B", {}, {}})));
        f.deliver(managed_list::Phase::complete, {{L"new", 2, false}});
        EXPECT(f.pane.list_snapshot().request.location.parsing_name == L"C:\\B");
        EXPECT(f.pane.list_snapshot().rows.front().name == L"new");
    }
    SetEvent(gate->release);
    EXPECT(managed_list::testing::wait_for_icon_workers(3000));
}

void duplicate_display_names_preserve_viewport_and_interaction_on_reorder() {
    Fixture f;
    std::vector<managed_list::Row> rows;
    for (int i = 0; i < 60; ++i) {
        managed_list::Row row{L"Same label", 1, false, static_cast<std::uint64_t>(i + 1)};
        row.identity = {L"::{provider}\\" + std::to_wstring(i), true};
        rows.push_back(std::move(row));
    }
    f.deliver(managed_list::Phase::complete, std::move(rows));
    ListView_SetItemState(f.list, 20, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(f.list, 20);
    ListView_EnsureVisible(f.list, 44, FALSE);
    const auto before = own(f.pane.list_interaction_snapshot());
    EXPECT(before.viewport_anchor.has_value());
    f.state.tabs[0].sort_column = "System.DateModified";
    f.state.tabs[0].sort_ascending = false;
    f.pane.apply_sort();
    const auto after = own(f.pane.list_interaction_snapshot());
    EXPECT(after.selected == before.selected);
    EXPECT(after.focused == before.focused);
    EXPECT(after.anchor == before.anchor);
    EXPECT(after.viewport_anchor == before.viewport_anchor);
    EXPECT(after.viewport_offset == before.viewport_offset);
    EXPECT(!after.viewport_anchor_clamped);
}
void shell_property_identity_reorder_and_completion() {
    Fixture f;
    auto* source = use_properties(f);
    managed_list::Row first{L"Same label", 1, false, 1};
    first.identity = {L"::{provider}\\one", true};
    first.child_location = core::ShellLocation{first.identity.key, L"KnownFolder", L"C:\\fallback"};
    auto second = first;
    second.identity.key = L"::{provider}\\two";
    second.child_location->parsing_name = second.identity.key;
    f.deliver(managed_list::Phase::complete, {first, second});
    f.click_column(4); f.pane.set_list_icon_viewport(0, 2);
    const auto requests = source->requests;
    EXPECT(requests.size() == 2);
    EXPECT(requests[0].key.identity != requests[1].key.identity);
    for (const auto& request : requests) {
        const auto& row = f.pane.list_snapshot().rows[request.row_index];
        EXPECT(request.key.identity == row.identity);
        EXPECT(request.key.child_location == row.child_location);
        EXPECT(request.key.parsing_name() == row.identity.key);
        EXPECT(request.property == "System.Media.Duration");
    }
    for (int field = 0; field < 6; ++field) {
        auto wrong = requests.front();
        if (field == 0) wrong.row_index = requests.back().row_index;
        if (field == 1) wrong.key.identity = requests.back().key.identity;
        if (field == 2) wrong.key.child_location->known_folder_identity = L"Other";
        if (field == 3) wrong.key.child_location->fallback_path = L"C:\\other";
        if (field == 4) ++wrong.viewport;
        if (field == 5) ++wrong.request.binding.generation;
        source->deliver(wrong, number(999)); f.pane.process_icon_results();
        for (const auto& row : f.pane.list_snapshot().rows)
            EXPECT(row.properties[1].state == managed_list::PropertyState::unresolved);
        EXPECT(f.pane.list_property_sort_pending());
    }
    source->deliver(requests[1], number(1)); f.pane.process_icon_results();
    EXPECT(f.pane.list_property_sort_pending());
    EXPECT(f.pane.list_snapshot().rows[requests[1].row_index].properties[1].number == 1);
    EXPECT(f.pane.list_snapshot().rows[requests[0].row_index].properties[1].state ==
        managed_list::PropertyState::unresolved);
    source->deliver(requests[0], number(2)); f.pane.process_icon_results();
    EXPECT(!f.pane.list_property_sort_pending());
    EXPECT(f.pane.list_snapshot().rows[0].identity == requests[1].key.identity);
    EXPECT(f.pane.list_snapshot().rows[1].identity == requests[0].key.identity);
    // The old ordinal now names the other identical label. It cannot receive
    // the old completion, including after a fresh tab or Group binding.
    for (const auto& request : requests) source->deliver(request, number(999));
    f.pane.process_icon_results();
    EXPECT(f.pane.list_snapshot().rows[0].properties[1].number == 1);
    EXPECT(f.pane.list_snapshot().rows[1].properties[1].number == 2);
    f.state.tabs.push_back({"second", {L"C:\\A", {}, {}}, "details", "System.Media.Duration", true, {}, 0});
    f.pane.activate_tab_at(1);
    f.deliver(managed_list::Phase::complete, {first, second});
    f.pane.set_list_icon_viewport(0, 2);
    for (const auto& request : requests) source->deliver(request, number(999));
    f.pane.process_icon_results();
    for (const auto& row : f.pane.list_snapshot().rows)
        EXPECT(row.properties[1].state == managed_list::PropertyState::unresolved);
    const auto old_group = source->requests;
    auto other = f.state;
    f.host.group_id = "other-group"; f.pane.bind(&other);
    EXPECT(SUCCEEDED(f.pane.navigate_to({L"C:\\A", {}, {}})));
    f.deliver(managed_list::Phase::complete, {first, second});
    f.pane.set_list_icon_viewport(0, 2);
    for (const auto& request : old_group) source->deliver(request, number(999));
    f.pane.process_icon_results();
    for (const auto& row : f.pane.list_snapshot().rows)
        EXPECT(row.properties[1].state == managed_list::PropertyState::unresolved);
    f.pane.unbind(); // other must outlive its binding
}
void long_shell_property_batches_finish_within_byte_budget() {
    Fixture f;
    auto* source = use_properties(f);
    std::vector<managed_list::Row> rows;
    for (int n = 0; n < 30; ++n) {
        managed_list::Row row{L"Same label", 1, false, 1};
        row.identity = {L"::{provider}\\" + std::to_wstring(n) + std::wstring(10000, L'x'), true};
        row.child_location = core::ShellLocation{row.identity.key, std::wstring(10000, L'k'), std::wstring(10000, L'f')};
        rows.push_back(std::move(row));
    }
    f.deliver(managed_list::Phase::complete, std::move(rows));
    f.click_column(4); f.pane.set_list_icon_viewport(0, 2);
    EXPECT(source->requests.size() < 30);
    std::size_t completed{};
    for (int round = 0; round < 30 && f.pane.list_property_sort_pending(); ++round) {
        auto requests = source->requests;
        std::size_t bytes{};
        for (const auto& request : requests) {
            bytes += managed_list::owned_icon_request_bytes(request);
            EXPECT(!request.property.empty());
            source->deliver(request, number(1)); ++completed;
        }
        EXPECT(bytes <= managed_list::kIconValueBytes);
        while (!source->results.empty()) f.pane.process_icon_results();
        f.pane.set_list_icon_viewport(0, 2);
    }
    EXPECT(completed == 30);
    EXPECT(!f.pane.list_property_sort_pending());
    EXPECT(!f.pane.list_property_capacity_failed());
}
} // namespace

void quick_repeat_on_a_column_title_still_flips_the_sort() {
    Fixture f;
    f.deliver(managed_list::Phase::complete, {{L"a.txt", 1, false, 10}, {L"b.txt", 2, false, 20}});
    const HWND header = ListView_GetHeader(f.list);
    EXPECT(header != nullptr);
    const LPARAM title = MAKELPARAM(40, 8);
    const auto press = [&](UINT down) {
        SendMessageW(header, down, MK_LBUTTON, title);
        SendMessageW(header, WM_LBUTTONUP, 0, title);
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    };
    press(WM_LBUTTONDOWN);
    const bool first = f.state.tabs.front().sort_ascending;
    press(WM_LBUTTONDBLCLK); // the second click of a quick repeat arrives as a double-click
    EXPECT(f.state.tabs.front().sort_ascending != first);
    press(WM_LBUTTONDOWN);
    EXPECT(f.state.tabs.front().sort_ascending == first);
}

int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    shell_property_identity_reorder_and_completion();
    long_shell_property_batches_finish_within_byte_budget();
    duplicate_display_names_preserve_viewport_and_interaction_on_reorder();
    basic_keys_group_directories_and_keep_deterministic_ties();
    quick_repeat_on_a_column_title_still_flips_the_sort();
    unicode_name_order_and_unknown_sort_fallback_are_stable();
    names_sort_naturally_and_descending_puts_folders_last();
    columns_can_be_hidden_and_keep_clicks_and_cell_text_correct();
    out_of_order_batches_and_sort_changes_preserve_item_and_viewport_identity();
    expensive_sort_commits_once_and_preserves_interaction();
    property_demand_is_bounded_and_stale_results_are_rejected();
    display_demand_and_custom_keys_use_the_same_bounded_boundary();
    exhausted_properties_degrade_without_blocking_navigation();
    property_cells_status_and_explicit_retry();
    bounded_order_yields_without_mutation_and_finishes_stably();
    property_owned_bytes_limit_keeps_rows_usable();
    paused_worker_property_keeps_navigation_available();
    return test::summary("managed_list_sort");
}
