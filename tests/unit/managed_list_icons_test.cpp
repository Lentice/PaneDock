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
    Fixture() {
        pane.set_host(&host); pane.set_reentry_guard(&host.guard); pane.bind(&state);
        auto r = std::make_unique<Rows>(); rows = r.get(); pane.use_managed_list(std::move(r));
        auto i = std::make_unique<Icons>(); icons = i.get(); pane.use_list_icons(std::move(i));
        navigate();
    }
    ~Fixture() { pane.destroy(); }
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
        // Identical identities on a new navigation reuse cached decoration.
        f.navigate(); f.deliver({rows.front()});
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
void production_worker_capacity_cancel_and_backpressure() {
    auto gate = std::make_shared<ml::testing::IconGate>();
    std::array<std::unique_ptr<ml::IconSource>, ml::kIconWorkers> sources;
    ml::IconRequest request{{{1, "group", "tab"}, {L"C:\\A", {}, {}}}, 1,
        {{L"C:\\A", {}, {}}, L"file", 1, 1, false, 16}};
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
    EXPECT(source->replace({request}));
    EXPECT(WaitForSingleObject(source->event(), 10000) == WAIT_OBJECT_0);
    auto result = source->take();
    EXPECT(result && result->request.viewport == 9999 && !result->image.pixels.empty());
    EXPECT(WaitForSingleObject(source->event(), 0) == WAIT_TIMEOUT);
    source.reset(); EXPECT(ml::testing::wait_for_icon_workers(10000));
}
}
int main() {
    stale_identity_failure_and_reuse();
    bounded_cache_eviction_and_resource_cleanup();
    production_worker_capacity_cancel_and_backpressure();
    return test::summary("managed_list_icons");
}
