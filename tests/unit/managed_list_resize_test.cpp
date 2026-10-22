#include "unit/test_pane_host.h"
#include "unit/test_util.h"
#include "app_shell/window_helpers.h"
#include "core/layout.h"
#include <deque>

using namespace panedock;
namespace {
class Rows final : public managed_list::Source {
public:
    int submissions{};
    std::deque<managed_list::Batch> batches;
    bool submit(managed_list::Request) noexcept override { ++submissions; return true; }
    std::optional<managed_list::Batch> take() noexcept override {
        if (batches.empty()) return {};
        auto result = std::move(batches.front()); batches.pop_front(); return result;
    }
    HANDLE event() const noexcept override { return nullptr; }
    void cancel() noexcept override {}
};
class Images final : public managed_list::IconSource {
public:
    std::vector<managed_list::IconRequest> requests;
    bool replace(std::vector<managed_list::IconRequest> next) noexcept override {
        requests = std::move(next); return true;
    }
    std::optional<managed_list::IconResult> take() noexcept override { return {}; }
    HANDLE event() const noexcept override { return nullptr; }
    void cancel() noexcept override { requests.clear(); }
};
RECT child_rect(HWND child) {
    RECT rect{}; GetWindowRect(child, &rect);
    MapWindowPoints(HWND_DESKTOP, GetParent(child), reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}
void test_parent_batches_dpi_font_and_anchor() {
    test::TestPaneHost host;
    core::PaneState state{"pane", {{"tab", {L"C:\\A", {}, {}}, "details", {}, true, {}, 0}}, "tab"};
    app_shell::Pane pane;
    const auto instance = GetModuleHandleW(nullptr);
    EXPECT(app_shell::Pane::register_window_class(instance));
    // Hidden real controls, no desktop input or PaneDock launch.
    HWND parent = CreateWindowExW(0, L"STATIC", L"resize test", WS_OVERLAPPEDWINDOW,
        0, 0, 1600, 1200, nullptr, nullptr, instance, nullptr);
    EXPECT(parent != nullptr);
    pane.set_host(&host); pane.set_reentry_guard(&host.guard);
    EXPECT(pane.create(parent, 0)); pane.bind(&state);
    auto source = std::make_unique<Rows>(); auto* rows = source.get();
    pane.use_managed_list(std::move(source));
    auto images = std::make_unique<Images>(); auto* demand = images.get();
    pane.use_list_icons(std::move(images));
    EXPECT(SUCCEEDED(pane.realize({0, 0, 640, 420}, state.tabs[0].location)));
    HWND list = FindWindowExW(pane.explorer_container(), nullptr, panedock::app_shell::kManagedListClassName, nullptr);
    EXPECT(list != nullptr);
    std::vector<managed_list::Row> items;
    for (int i = 0; i < 300; ++i) {
        auto key = L"item-" + std::to_wstring(1000 + i);
        managed_list::Row row{L"Same display", static_cast<std::uint64_t>(i), false, 1};
        row.identity = {key, true};
        row.child_location = core::ShellLocation{key, {}, {}};
        items.push_back(std::move(row));
    }
    for (int batch = 0; batch < 3; ++batch) {
        std::vector<managed_list::Row> part;
        for (int i = batch * 100; i < (batch + 1) * 100; ++i)
            part.push_back(std::move(items[i]));
        rows->batches.push_back({pane.list_snapshot().request, std::move(part),
            batch == 2 ? managed_list::Phase::complete : managed_list::Phase::loading, 0});
        pane.process_content_results();
    }
    EXPECT(pane.list_snapshot().rows.size() == 300);
    const auto request = pane.list_snapshot().request;
    const auto submissions = rows->submissions;
    ListView_SetItemState(list, 100, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(list, 100);
    ListView_EnsureVisible(list, 130, FALSE);
    const auto initial = pane.list_interaction_snapshot();
    EXPECT(initial.viewport_anchor.has_value());
    const std::wstring anchor(initial.viewport_anchor->name);
    const auto check_identity = [&] {
        const auto current = pane.list_interaction_snapshot();
        EXPECT(current.selected.size() == 1 && current.selected[0].name == L"item-1100");
        EXPECT(current.focused && current.focused->name == L"item-1100");
        EXPECT(current.anchor && current.anchor->name == L"item-1100");
        EXPECT(current.viewport_anchor && current.viewport_anchor->name == anchor);
        EXPECT(pane.list_snapshot().request == request);
        EXPECT(rows->submissions == submissions);
    };
    UINT previous_dpi = 96;
    for (const UINT dpi : {96u, 144u, 192u, 96u}) {
        for (const RECT target : {RECT{20, 30, 700, 500}, RECT{20, 30, 530, 400}, RECT{20, 30, 700, 500}}) {
            app_shell::WindowPositionBatch outer, children, content;
            const RECT before = child_rect(list);
            const int offset = pane.list_interaction_snapshot().viewport_offset;
            const auto layout = pane.stage_layout(target, dpi, outer, children);
            pane.set_shell_rect(layout.shell_rect, &content);
            const RECT staged = child_rect(list);
            EXPECT(EqualRect(&before, &staged)); // no immediate child placement
            EXPECT(outer.commit()); EXPECT(children.commit()); EXPECT(content.commit());
            pane.finish_layout(layout, dpi);
            const RECT actual = child_rect(list);
            EXPECT(EqualRect(&actual, &layout.shell_rect));
            RECT container{}; GetClientRect(pane.explorer_container(), &container);
            EXPECT(EqualRect(&actual, &container));
            const auto chrome = app_shell::pane_chrome_rects(target, dpi);
            RECT tab = chrome.tab_strip;
            OffsetRect(&tab, -chrome.pane_window.left, -chrome.pane_window.top);
            const RECT actual_tab = child_rect(pane.tab_strip());
            EXPECT(EqualRect(&tab, &actual_tab));
            check_identity();
            if (dpi == previous_dpi) EXPECT(pane.list_interaction_snapshot().viewport_offset == offset);
            previous_dpi = dpi;
            pane.set_list_icon_viewport(100, 2);
            EXPECT(!demand->requests.empty());
            for (const auto& image : demand->requests) {
                EXPECT(image.key.dpi == dpi);
                EXPECT(image.key.size == MulDiv(16, static_cast<int>(dpi), 96));
            }
        }
        HFONT font = CreateFontW(-MulDiv(12, static_cast<int>(dpi), 96), 0, 0, 0,
            FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, 0, 0, L"Segoe UI");
        EXPECT(font != nullptr);
        pane.apply_font(font);
        EXPECT(reinterpret_cast<HFONT>(SendMessageW(list, WM_GETFONT, 0, 0)) == font);
        check_identity();
        pane.apply_font(host.chrome_font()); DeleteObject(font);
    }
    // An unchanged pane rect still has a real DPI-only chrome/content update.
    const RECT fixed{20, 30, 700, 500};
    for (const UINT dpi : {144u, 192u}) {
        app_shell::WindowPositionBatch outer, children, content;
        const auto layout = pane.stage_layout(fixed, dpi, outer, children);
        EXPECT(layout.changed);
        pane.set_shell_rect(layout.shell_rect, &content);
        EXPECT(outer.commit()); EXPECT(children.commit()); EXPECT(content.commit());
        pane.finish_layout(layout, dpi); check_identity();
    }
    // Same-generation reorder invalidates the captured ordinal. finish_layout
    // must leave the newer viewport alone rather than scroll a different item.
    {
        app_shell::WindowPositionBatch outer, children, content;
        const auto layout = pane.stage_layout({20, 30, 720, 510}, 96, outer, children);
        pane.set_shell_rect(layout.shell_rect, &content);
        state.tabs[0].sort_column = "System.Size";
        state.tabs[0].sort_ascending = false;
        pane.apply_sort();
        EXPECT(pane.list_snapshot().rows.front().bytes == 299);
        EXPECT(outer.commit()); EXPECT(children.commit()); EXPECT(content.commit());
        ListView_EnsureVisible(list, 20, FALSE);
        const auto before_finish = pane.list_interaction_snapshot();
        EXPECT(before_finish.viewport_anchor.has_value());
        const std::wstring newer_anchor(before_finish.viewport_anchor->name);
        EXPECT(newer_anchor != anchor);
        pane.finish_layout(layout, 96);
        const auto after_finish = pane.list_interaction_snapshot();
        EXPECT(after_finish.viewport_anchor && after_finish.viewport_anchor->name == newer_anchor);
        EXPECT(after_finish.viewport_offset == before_finish.viewport_offset);
    }
    // A navigation between staging and finishing invalidates the saved anchor.
    {
        app_shell::WindowPositionBatch outer, children, content;
        const auto layout = pane.stage_layout({20, 30, 740, 520}, 96, outer, children);
        pane.set_shell_rect(layout.shell_rect, &content);
        EXPECT(SUCCEEDED(pane.navigate_to({L"C:\\B", {}, {}})));
        std::vector<managed_list::Row> next;
        for (int i = 0; i < 100; ++i)
            next.push_back({L"next-" + std::to_wstring(1000 + i), 1, false, 1});
        rows->batches.push_back({pane.list_snapshot().request, std::move(next), managed_list::Phase::complete, 0});
        pane.process_content_results();
        EXPECT(outer.commit()); EXPECT(children.commit()); EXPECT(content.commit());
        ListView_EnsureVisible(list, 40, FALSE);
        const auto before_finish = pane.list_interaction_snapshot();
        const std::wstring new_anchor(before_finish.viewport_anchor->name);
        pane.finish_layout(layout, 96);
        const auto after_finish = pane.list_interaction_snapshot();
        EXPECT(after_finish.viewport_anchor && after_finish.viewport_anchor->name == new_anchor);
        EXPECT(after_finish.viewport_offset == before_finish.viewport_offset);
    }
    pane.destroy(); DestroyWindow(parent);
}
}
int main() {
    test_parent_batches_dpi_font_and_anchor();
    return test::summary("managed_list_resize");
}
