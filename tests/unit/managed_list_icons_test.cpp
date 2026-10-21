#include "unit/test_pane_host.h"
#include "unit/test_util.h"
#include "app_shell/window_helpers.h"
#include "managed_list/icon_source_test.h"
#include <algorithm>
#include <array>
#include <deque>
using namespace panedock;
namespace ml = managed_list;
namespace {
class Rows final : public ml::Source {
  public:
    std::deque<ml::Batch> batches;
    bool submit(ml::Request) noexcept override { return true; }
    std::optional<ml::Batch> take() noexcept override {
        if (batches.empty()) return {};
        auto b = std::move(batches.front()); batches.pop_front(); return b;
    }
    HANDLE event() const noexcept override { return nullptr; }
    void cancel() noexcept override {}
};
class Icons final : public ml::IconSource {
    HANDLE ready_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  public:
    std::vector<ml::IconRequest> interest;
    std::deque<ml::IconResult> results;
    std::size_t replacements{};
    ~Icons() override { CloseHandle(ready_); }
    bool replace(std::vector<ml::IconRequest> r) noexcept override {
        interest = std::move(r); ++replacements; return true;
    }
    std::optional<ml::IconResult> take() noexcept override {
        if (results.empty()) { ResetEvent(ready_); return {}; }
        auto r = std::move(results.front()); results.pop_front();
        if (results.empty()) ResetEvent(ready_);
        return r;
    }
    HANDLE event() const noexcept override { return ready_; }
    void cancel() noexcept override { interest.clear(); }
    void deliver(ml::IconRequest request, bool success = true) {
        shell_core::IconImage image;
        if (success) {
            const int size = request.key.size;
            image.pixels.assign(static_cast<std::size_t>(size * size), 0xff1267ab);
            image.mask.assign(static_cast<std::size_t>(((size + 15) / 16) * 2 * size), 0);
        }
        results.push_back({std::move(request), std::move(image)}); SetEvent(ready_);
    }
};
struct Fixture {
    test::TestPaneHost host;
    core::PaneState state{"pane", {{"tab", {L"C:\\A", {}, {}}, {}, {}, true, {}, 0}}, "tab"};
    app_shell::Pane pane;
    Rows* rows{};
    Icons* icons{};
    HWND parent{};
    Fixture(bool controls = true) {
        pane.set_host(&host); pane.set_reentry_guard(&host.guard); pane.bind(&state);
        if (controls) {
            EXPECT(app_shell::Pane::register_window_class(GetModuleHandleW(nullptr)));
            parent = CreateWindowExW(0, L"STATIC", L"image mode test", 0,
                0, 0, 800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
            EXPECT(parent != nullptr);
            EXPECT(pane.create(parent, 0));
            pane.bind(&state);
        }
        auto r = std::make_unique<Rows>(); rows = r.get(); pane.use_managed_list(std::move(r));
        auto i = std::make_unique<Icons>(); icons = i.get(); pane.use_list_icons(std::move(i));
        if (controls) EXPECT(SUCCEEDED(pane.realize({0, 0, 640, 420}, {L"C:\\A", {}, {}})));
        else navigate();
    }
    ~Fixture() { pane.destroy(); if (parent) DestroyWindow(parent); }
    void navigate(const wchar_t* path = L"C:\\A") { EXPECT(SUCCEEDED(pane.navigate_to({path, {}, {}}))); }
    void deliver(std::vector<ml::Row> items, ml::Phase phase = ml::Phase::complete) {
        rows->batches.push_back({pane.list_snapshot().request, std::move(items), phase, 0});
        pane.process_content_results();
    }
    std::size_t row(std::wstring_view name) {
        const auto& items = pane.list_snapshot().rows;
        return static_cast<std::size_t>(std::find_if(items.begin(), items.end(),
            [&](const auto& r) { return r.name == name; }) - items.begin());
    }
    void viewport(std::size_t first, std::size_t count) { pane.set_list_icon_viewport(first, count); }
    void finish() {
        auto requests = icons->interest;
        for (auto& r : requests) icons->deliver(r);
        while (!icons->results.empty()) pane.process_icon_results();
    }
};
ml::Row shell_row(std::wstring key) {
    ml::Row row{L"Same display", 1, false, 1};
    row.identity = {key, true};
    row.child_location = core::ShellLocation{std::move(key), L"KnownFolder", L"C:\\fallback"};
    return row;
}
void absolute_identity_interest_completion_and_cache() {
    Fixture f;
    auto first = shell_row(L"::{provider}\\one");
    auto second = shell_row(L"::{provider}\\two");
    f.deliver({first, second}); f.viewport(0, 2);
    const auto requests = f.icons->interest;
    EXPECT(requests.size() == 2);
    EXPECT(requests[0].key.identity != requests[1].key.identity);
    for (const auto& request : requests) {
        const auto& row = f.pane.list_snapshot().rows[request.key.identity == first.identity ? 0 : 1];
        EXPECT(request.key.child_location == row.child_location);
        EXPECT(request.key.parsing_name() == request.key.identity.key);
        EXPECT(request.key.parsing_name() != row.child_location->fallback_path);
    }
    // Matching display/version/ordinal is insufficient. Every identity field,
    // request generation and viewport must still match outstanding interest.
    for (int field = 0; field < 7; ++field) {
        auto wrong = requests.front();
        if (field == 0) wrong.key.identity = second.identity;
        if (field == 1) wrong.key.child_location->known_folder_identity = L"Other";
        if (field == 2) wrong.key.child_location->fallback_path = L"C:\\other";
        if (field == 3) ++wrong.request.binding.generation;
        if (field == 4) ++wrong.viewport;
        if (field == 5) wrong.request.binding.group_id = "other-group";
        if (field == 6) ++wrong.row_index;
        f.icons->deliver(wrong); f.pane.process_icon_results();
        EXPECT(f.pane.list_item_image(0) == 0 && f.pane.list_item_image(1) == 0);
    }
    f.icons->deliver(requests[1]); f.pane.process_icon_results();
    EXPECT(f.pane.list_item_image(0) == 0 && f.pane.list_item_image(1) >= 2);
    f.icons->deliver(requests[0]); f.pane.process_icon_results();
    EXPECT(f.pane.list_item_image(0) >= 2);
    EXPECT(f.pane.list_item_image(0) != f.pane.list_item_image(1));
    // Display text is not part of the icon identity. Renewed viewport interest
    // with unchanged metadata reuses the image in this active list.
    first.name = L"Renamed display";
    EXPECT(requests.front().key.matches(first));
    const auto replacements = f.icons->replacements;
    f.viewport(0, 1); EXPECT(f.icons->replacements == replacements);
    EXPECT(f.pane.list_item_image(0) >= 2);
    f.navigate(); first.child_location->fallback_path = L"C:\\new-fallback";
    f.deliver({first}); f.viewport(0, 1);
    EXPECT(f.pane.list_item_image(0) == 0);
    const auto old = f.icons->interest.front();
    f.pane.add_tab({L"C:\\A", {}, {}}); f.navigate(); f.deliver({first}); f.viewport(0, 1);
    f.icons->deliver(old); f.pane.process_icon_results();
    EXPECT(f.pane.list_item_image(0) == 0);
    f.finish(); EXPECT(f.pane.list_item_image(0) >= 2);
    // A Shell key without an optional child remains absolute. A local key
    // remains parent+leaf even when its display text is different.
    f.navigate(); second.child_location.reset();
    ml::Row local{L"Display only", 1, false}; local.identity = {L"real.txt", false};
    f.deliver({second, local}); f.viewport(0, 2);
    for (const auto& request : f.icons->interest)
        EXPECT(request.key.parsing_name() == (request.key.identity.shell_item ?
            second.identity.key : L"C:\\A\\real.txt"));
    f.finish();
    EXPECT(f.pane.list_item_image(0) >= 2 && f.pane.list_item_image(1) >= 2);
}
void long_identity_interest_and_cache_are_byte_bounded() {
    Fixture f;
    std::vector<ml::Row> rows;
    for (int n = 0; n < 40; ++n) {
        auto row = shell_row(L"::{provider}\\" + std::to_wstring(n) + std::wstring(16000, L'x'));
        row.child_location->known_folder_identity.assign(16000, L'k');
        row.child_location->fallback_path.assign(16000, L'f');
        rows.push_back(std::move(row));
    }
    f.deliver(std::move(rows)); f.viewport(0, 40);
    EXPECT(!f.icons->interest.empty() && f.icons->interest.size() < 40);
    std::size_t bytes{};
    for (const auto& request : f.icons->interest) bytes += ml::owned_icon_request_bytes(request);
    EXPECT(bytes <= ml::kIconValueBytes);
    for (std::size_t row = 0; row < 40; ++row) {
        f.viewport(row, 1); f.finish();
        EXPECT(f.pane.list_item_image(row) >= 2);
        EXPECT(f.pane.list_image_bytes() <= ml::kImageCacheBytes);
    }
    EXPECT(f.pane.list_item_image(0) == 0); // key bytes force eviction before 256 rasters
    const auto last_slot = f.pane.list_item_image(39);
    EXPECT(last_slot >= 2);
    f.viewport(0, 1); f.finish();
    EXPECT(f.pane.list_item_image(0) >= 2 && f.pane.list_item_image(39) >= 2);
    EXPECT(f.pane.list_item_image(0) != f.pane.list_item_image(39));
}
void stale_identity_failure_and_reuse() {
    Fixture f;
    f.deliver({{L"z-file", 1, false}, {L"folder", 0, true}, {L"a-file", 2, false}}, ml::Phase::loading);
    EXPECT(f.pane.list_snapshot().phase == ml::Phase::loading);
    EXPECT(f.pane.list_item_image(f.row(L"folder")) == 1);
    f.viewport(f.row(L"z-file"), 1);
    auto old = f.icons->interest.at(0);
    // A paused icon does not stop progressive rows or their current sort order.
    f.deliver({{L"b-file", 3, false}});
    f.viewport(f.row(L"a-file"), 1);
    auto current = f.icons->interest.at(0);
    f.icons->deliver(old); f.pane.process_icon_results();
    EXPECT(f.pane.list_item_image(f.row(L"z-file")) == 0);
    EXPECT(f.pane.list_item_image(f.row(L"a-file")) == 0);
    f.icons->deliver(current, false); f.pane.process_icon_results();
    EXPECT(f.pane.list_item_image(f.row(L"a-file")) == 0);
    const auto attempts = f.icons->replacements;
    f.viewport(f.row(L"a-file"), 1);
    EXPECT(f.icons->replacements == attempts); // no failed-icon busy retry
    EXPECT(WaitForSingleObject(f.pane.icon_event(), 0) == WAIT_TIMEOUT);
    f.pane.set_shell_visible(false); EXPECT(f.icons->interest.empty());
    f.viewport(f.row(L"a-file"), 1); f.finish();
    EXPECT(f.pane.list_item_image(f.row(L"a-file")) >= 2);
    f.viewport(0, 0);
    const auto cached = f.icons->replacements;
    f.viewport(f.row(L"a-file"), 1);
    EXPECT(f.icons->replacements == cached);
    // Tab generation and location identity both guard completions, even when
    // a new row has the same name and ordinal as the old one.
    f.viewport(f.row(L"z-file"), 1); old = f.icons->interest.at(0);
    f.pane.add_tab({L"C:\\B", {}, {}});
    f.navigate(L"C:\\B");
    f.deliver({{L"z-file", 1, false}});
    f.viewport(0, 1);
    f.icons->deliver(old); f.pane.process_icon_results();
    EXPECT(f.pane.list_item_image(0) == 0);
    f.finish(); EXPECT(f.pane.list_item_image(0) >= 2);
    // A DPI/resize viewport drops old size results and rebuilds bounded images.
    app_shell::WindowPositionBatch outer, children;
    (void)f.pane.stage_layout({0, 0, 640, 480}, 192, outer, children);
    f.viewport(0, 1);
    EXPECT(f.icons->interest.size() == 1 && f.icons->interest[0].key.size == 32);
    f.finish(); EXPECT(f.pane.list_item_image(0) >= 2);
    // Binding change invalidates decoration without touching another Group.
    f.pane.unbind();
    EXPECT(f.icons->interest.empty());
}
void bounded_cache_eviction_and_resource_cleanup() {
    const DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    for (int repeat = 0; repeat < 4; ++repeat) {
        Fixture f;
        std::vector<ml::Row> rows;
        for (std::size_t n = 0; n < 400; ++n) rows.push_back({L"item-" + std::to_wstring(1000+n), n, false});
        // Enumeration batches obey the existing limit.
        for (std::size_t first = 0; first < rows.size(); first += ml::kBatchRows) {
            const auto last = std::min(rows.size(), first + ml::kBatchRows);
            f.deliver({rows.begin()+first, rows.begin()+last},
                last == rows.size() ? ml::Phase::complete : ml::Phase::loading);
        }
        f.viewport(0, 400);
        EXPECT(f.icons->interest.size() == ml::kIconInterest); // no offscreen prefetch
        for (std::size_t first = 0; first < 400; first += 32) {
            f.viewport(first, 32); f.finish();
        }
        EXPECT(f.pane.list_item_image(0) == 0); // LRU image evicted, no stale slot mapping
        f.viewport(0, 1); EXPECT(f.icons->interest.size() == 1); f.finish();
        EXPECT(f.pane.list_item_image(0) >= 2);
        // Renewed interest in unchanged metadata reuses cached decoration.
        // Explicit navigation/Refresh has its own PD-238 invalidation contract.
        const auto requests = f.icons->replacements;
        f.viewport(0, 1); EXPECT(f.icons->replacements == requests);
        EXPECT(f.pane.list_item_image(0) >= 2);
        f.navigate();
        auto changed = rows.front(); ++changed.bytes;
        f.deliver({changed}); f.viewport(0, 1);
        EXPECT(f.icons->interest.size() == 1); // same name, changed identity version
        EXPECT(f.pane.list_item_image(0) == 0);
        f.finish(); EXPECT(f.pane.list_item_image(0) >= 2);
    }
    EXPECT(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= before + 2);
}
void thumbnails_modes_stale_failure_and_budget() {
    const DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    {
        Fixture f;
        std::vector<ml::Row> rows;
        for (int n = 0; n < 100; ++n) rows.push_back({L"image-" + std::to_wstring(n), 1, false});
        f.deliver(std::move(rows));
        const std::array sizes{256, 96, 48, 16, 16, 16, 48, 64};
        for (int repeat = 0; repeat < 20; ++repeat) {
            for (std::size_t mode = 0; mode < shell_core::kViewModeOptions.size(); ++mode) {
                f.viewport(0, 1);
                auto old = f.icons->interest.empty() ? ml::IconRequest{} : f.icons->interest.front();
                const auto option = shell_core::kViewModeOptions[mode];
                f.pane.set_view_mode(option);
                EXPECT(f.state.tabs[0].view_mode == shell_core::view_mode_name(
                    option.selection.mode, option.selection.image_size));
                f.viewport(0, 100);
                EXPECT(!f.icons->interest.empty());
                const auto current = f.icons->interest.front();
                EXPECT(current.key.size == sizes[mode]);
                EXPECT(current.key.dpi == 96);
                EXPECT(current.key.thumbnail == (sizes[mode] > 16));
                f.icons->deliver(old); f.pane.process_icon_results();
                EXPECT(f.pane.list_item_image(0) == 0);
                f.icons->deliver(current, false); f.pane.process_icon_results();
                EXPECT(f.pane.list_item_image(0) == 0);
                const auto attempts = f.icons->replacements;
                f.viewport(0, 100); EXPECT(f.icons->replacements == attempts);
                // Complete every visible image; size-dependent eviction must
                // bound large rasters as well as the small-icon cache count.
                f.finish();
                EXPECT(f.pane.list_image_bytes() <= ml::kImageCacheBytes);
                EXPECT(WaitForSingleObject(f.pane.icon_event(), 0) == WAIT_TIMEOUT);
                f.pane.set_shell_visible(false); // renewed interest for next mode
            }
        }
        f.pane.set_view_mode(shell_core::kViewModeOptions[0]);
        app_shell::WindowPositionBatch outer, children;
        (void)f.pane.stage_layout({0, 0, 640, 480}, 192, outer, children);
        f.viewport(0, 100);
        EXPECT(f.icons->interest.front().key.size == 512);
        f.finish();
        EXPECT(f.pane.list_image_bytes() <= ml::kImageCacheBytes);
        EXPECT(f.pane.list_item_image(0) == 0); // evicted instead of wrong reused slot
    }
    EXPECT(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= before + 2);
}
void hidden_control_mode_layout_and_restore() {
    Fixture f(true);
    f.deliver({{L"picture.png", 10, false}, {L"folder", 0, true}});
    const HWND list = FindWindowExW(f.pane.explorer_container(), nullptr, WC_LISTVIEWW, nullptr);
    EXPECT(list != nullptr);
    const std::array<DWORD, 8> views{LV_VIEW_ICON, LV_VIEW_ICON, LV_VIEW_ICON, LV_VIEW_SMALLICON,
        LV_VIEW_LIST, LV_VIEW_DETAILS, LV_VIEW_ICON, LV_VIEW_DETAILS};
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    for (std::size_t n = 0; n < views.size(); ++n) {
        f.pane.set_view_mode(shell_core::kViewModeOptions[n]);
        EXPECT((GetWindowLongPtrW(list, GWL_STYLE) & LVS_TYPEMASK) == views[n]);
        EXPECT((GetWindowLongPtrW(list, GWL_STYLE) & LVS_OWNERDATA) != 0);
        EXPECT(((GetWindowLongPtrW(list, GWL_STYLE) & LVS_NOCOLUMNHEADER) != 0) == (n == 7));
        EXPECT(f.pane.list_interaction_snapshot().selected.size() == 1);
        f.viewport(0, 2);
        const auto images = ListView_GetImageList(list, n < 3 || n == 6 ? LVSIL_NORMAL : LVSIL_SMALL);
        int width{}, height{};
        EXPECT(ImageList_GetIconSize(images, &width, &height));
        EXPECT(width == (n == 0 ? 256 : n == 1 ? 96 : n == 2 || n == 6 ? 48 : n == 7 ? 64 : 16));
        EXPECT(width == height);
        // Hidden-control paint exercises the card fallback without Shell or a
        // desktop window. Native focus/accessibility remains a desktop check.
        const HDC dc = CreateCompatibleDC(nullptr);
        const HBITMAP bitmap = CreateBitmap(640, 420, 1, 32, nullptr);
        const auto previous = SelectObject(dc, bitmap);
        NMLVCUSTOMDRAW draw{};
        draw.nmcd.hdr = {list, 0, NM_CUSTOMDRAW};
        draw.nmcd.hdc = dc; draw.nmcd.dwDrawStage = CDDS_ITEMPREPAINT;
        draw.nmcd.dwItemSpec = 0;
        const auto result = f.pane.list_notification(&draw.nmcd.hdr);
        if (n >= 6) EXPECT(result && *result == CDRF_SKIPDEFAULT);
        SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc);
        const auto saved = f.state.tabs[0].view_mode;
        f.pane.add_tab({L"C:\\B", {}, {}});
        f.pane.activate_tab_at(0);
        EXPECT(f.state.tabs[0].view_mode == saved);
        EXPECT((GetWindowLongPtrW(list, GWL_STYLE) & LVS_TYPEMASK) == views[n]);
        f.deliver({{L"picture.png", 10, false}, {L"folder", 0, true}});
        ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
}
void visible_idle_viewport_stops_after_one_refresh() {
    Fixture f(true);
    f.deliver({{L"file.txt", 10, false}, {L"folder", 0, true}});
    const HWND list = FindWindowExW(f.pane.explorer_container(), nullptr, WC_LISTVIEWW, nullptr);
    EXPECT(list != nullptr);
    ShowWindow(f.parent, SW_SHOWNOACTIVATE);
    UpdateWindow(f.parent);
    f.viewport(0, 2);
    f.finish();

    constexpr UINT viewport_message = WM_APP + 229;
    MSG message{};
    unsigned dispatched = 0;
    while (dispatched < 16 && PeekMessageW(&message, list, viewport_message,
                                            viewport_message, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
        ++dispatched;
    }
    EXPECT(dispatched <= 2);
    EXPECT(!PeekMessageW(&message, list, viewport_message, viewport_message, PM_NOREMOVE));
}
void production_worker_capacity_cancel_and_backpressure() {
    auto gate = std::make_shared<ml::testing::IconGate>();
    std::array<std::unique_ptr<ml::IconSource>, ml::kIconWorkers> sources;
    ml::IconRequest request{{{1, "group", "tab"}, {L"C:\\A", {}, {}}}, 1,
        {{L"C:\\A", {}, {}}, {L"file", false}, 1, 1, false, 16}};
    for (auto& source : sources) {
        ResetEvent(gate->entered);
        source = ml::testing::make_gated_icons(gate); EXPECT(source != nullptr);
        EXPECT(source->replace({request}));
        EXPECT(WaitForSingleObject(gate->entered, 10000) == WAIT_OBJECT_0);
    }
    EXPECT(ml::testing::icon_counts().running == ml::kIconWorkers);
    EXPECT(!ml::testing::make_gated_icons(gate));
    for (std::size_t n = 0; n < 1000; ++n) {
        request.viewport = n + 2;
        EXPECT(sources[0]->replace({request}));
    }
    auto counts = ml::testing::icon_counts();
    EXPECT(counts.pending == 1 && counts.results == 0);
    sources[0]->cancel(); EXPECT(ml::testing::icon_counts().pending == 0);
    const auto start = GetTickCount64();
    for (auto& source : sources) source.reset();
    EXPECT(GetTickCount64() - start < 1000);
    EXPECT(!ml::testing::make_gated_icons(gate)); // abandoned calls retain all four slots
    SetEvent(gate->release);
    EXPECT(ml::testing::wait_for_icon_workers(10000));
    EXPECT(ml::testing::icon_counts().workers == 0);
    gate = std::make_shared<ml::testing::IconGate>();
    auto source = ml::testing::make_gated_icons(gate);
    std::vector<ml::IconRequest> requests(ml::kIconInterest, request);
    EXPECT(source->replace(requests));
    EXPECT(WaitForSingleObject(gate->entered, 10000) == WAIT_OBJECT_0);
    requests.push_back(request); EXPECT(!source->replace(requests));
    SetEvent(gate->release);
    EXPECT(WaitForSingleObject(gate->backpressure, 10000) == WAIT_OBJECT_0);
    counts = ml::testing::icon_counts();
    EXPECT(counts.results == ml::kIconResults);
    EXPECT(counts.pending <= ml::kIconInterest);
    source->cancel();
    EXPECT(WaitForSingleObject(source->event(), 0) == WAIT_TIMEOUT);
    EXPECT(!source->take());
    request.viewport = 9999;
    request.key.size = ml::kMaxImageSize;
    request.key.thumbnail = true;
    request.key.dpi = 192;
    EXPECT(source->replace({request}));
    EXPECT(WaitForSingleObject(source->event(), 10000) == WAIT_OBJECT_0);
    auto result = source->take();
    EXPECT(result && result->request.viewport == 9999 &&
        result->image.pixels.size() == static_cast<std::size_t>(512 * 512));
    EXPECT(WaitForSingleObject(source->event(), 0) == WAIT_TIMEOUT);
    source.reset(); EXPECT(ml::testing::wait_for_icon_workers(10000));
}
void worker_identity_length_capacity_and_target_contract() {
    auto gate = std::make_shared<ml::testing::IconGate>();
    auto source = ml::testing::make_gated_icons(gate); EXPECT(source != nullptr);
    ml::IconRequest request{{{1, "group", "tab"}, {L"C:\\A", {}, {}}}, 1,
        {{L"C:\\A", {}, {}}, {L"::{item}", true}, 1, 1, false, 16, 96, false,
            core::ShellLocation{L"::{item}", L"KnownFolder", L"C:\\fallback"}}};
    // Reject excessive length and capacity independently for every new string.
    for (int field = 0; field < 4; ++field) {
        for (bool capacity : {false, true}) {
            auto wrong = request;
            auto& key = wrong.key;
            auto& text = field == 0 ? key.identity.key : field == 1 ? key.child_location->parsing_name :
                field == 2 ? key.child_location->known_folder_identity : key.child_location->fallback_path;
            if (capacity) text.reserve(ml::kIconValueBytes / sizeof(wchar_t));
            else text.assign(ml::kMaxPath + 1, L'x');
            std::vector<ml::IconRequest> input; input.push_back(std::move(wrong));
            EXPECT(!source->replace(std::move(input)));
            EXPECT(ml::testing::icon_counts().pending == 0);
        }
    }
    auto wrong = request; wrong.key.child_location->parsing_name = L"::{other}";
    EXPECT(!source->replace({wrong}));
    // The maximum legal Shell key is not restricted to a 255-character leaf.
    request.key.identity.key.assign(ml::kMaxPath, L'x');
    request.key.child_location->parsing_name = request.key.identity.key;
    EXPECT(source->replace({request}));
    EXPECT(WaitForSingleObject(gate->entered, 10000) == WAIT_OBJECT_0);
    std::vector<ml::IconRequest> too_many_bytes(16, request);
    EXPECT(!source->replace(std::move(too_many_bytes)));
    SetEvent(gate->release);
    EXPECT(WaitForSingleObject(source->event(), 10000) == WAIT_OBJECT_0);
    const auto result = source->take();
    EXPECT(result && result->request.key == request.key);
    source.reset(); EXPECT(ml::testing::wait_for_icon_workers(10000));
}
}
int main() {
    absolute_identity_interest_completion_and_cache();
    long_identity_interest_and_cache_are_byte_bounded();
    stale_identity_failure_and_reuse();
    bounded_cache_eviction_and_resource_cleanup();
    thumbnails_modes_stale_failure_and_budget();
    hidden_control_mode_layout_and_restore();
    visible_idle_viewport_stops_after_one_refresh();
    production_worker_capacity_cancel_and_backpressure();
    worker_identity_length_capacity_and_target_contract();
    return test::summary("managed_list_icons");
}
