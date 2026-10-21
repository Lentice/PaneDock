#include "unit/test_pane_host.h"
#include "unit/test_util.h"
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
        auto value = std::move(batches.front()); batches.pop_front(); return value;
    }
    HANDLE event() const noexcept override { return nullptr; }
    void cancel() noexcept override {} // intentionally retain stale deliveries
};
ml::Row row(DWORD flags, DWORD tag = IO_REPARSE_TAG_CLOUD) {
    WIN32_FIND_DATAW data{};
    data.dwFileAttributes = flags;
    data.dwReserved0 = tag;
    data.nFileSizeLow = 42;
    data.ftLastWriteTime.dwLowDateTime = 123;
    return ml::basic_row(data, L"same.txt");
}
void controlled_attributes() {
    constexpr DWORD cloud = FILE_ATTRIBUTE_REPARSE_POINT;
    for (const DWORD tag : {DWORD(IO_REPARSE_TAG_CLOUD), DWORD(IO_REPARSE_TAG_CLOUD_1),
                            DWORD(IO_REPARSE_TAG_CLOUD_F)}) {
        const auto online = row(cloud | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS, tag);
        EXPECT(online.cloud.cloud_placeholder());
        EXPECT(online.cloud.metadata_only());
        EXPECT(std::wstring_view(online.cloud.text()) == L"Not fully local");
    }
    EXPECT(std::wstring_view(row(cloud).cloud.text()) == L"Locally available");
    EXPECT(std::wstring_view(row(cloud | FILE_ATTRIBUTE_PINNED).cloud.text()) == L"Always available (pinned)");
    EXPECT(std::wstring_view(row(cloud | FILE_ATTRIBUTE_PINNED | FILE_ATTRIBUTE_RECALL_ON_OPEN).cloud.text()) ==
        L"Pinned - not fully local");
    EXPECT(std::wstring_view(row(cloud | FILE_ATTRIBUTE_PINNED | FILE_ATTRIBUTE_UNPINNED).cloud.text()) == L"Unknown");
    EXPECT(!row(cloud, IO_REPARSE_TAG_SYMLINK).cloud.cloud_placeholder());
    EXPECT(!row(cloud, IO_REPARSE_TAG_SYMLINK).cloud.metadata_only());
    EXPECT(!row(FILE_ATTRIBUTE_NORMAL).cloud.metadata_only());
    EXPECT(row(FILE_ATTRIBUTE_NORMAL).cloud.reparse_tag == 0);
    EXPECT(!ml::CloudAttributes{}.metadata_only());
    EXPECT(std::wstring_view(ml::CloudAttributes{}.text()) == L"Unknown");
}
void pane_refresh_and_identity() {
    test::TestPaneHost host;
    core::PaneState state{"pane", {{"tab", {L"C:\\controlled-cloud", {}, {}},
        "FVM_DETAILS", "System.ItemNameDisplay", true, {}, 0}}, "tab"};
    app_shell::Pane pane;
    pane.set_host(&host); pane.set_reentry_guard(&host.guard); pane.bind(&state);
    EXPECT(app_shell::Pane::register_window_class(GetModuleHandleW(nullptr)));
    const HWND parent = CreateWindowExW(0, L"STATIC", L"cloud test", 0,
        0, 0, 800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    EXPECT(parent != nullptr);
    EXPECT(pane.create(parent, 0)); pane.bind(&state);
    auto source = std::make_unique<Rows>(); auto* input = source.get();
    pane.use_managed_list(std::move(source)); pane.use_list_icons(nullptr);
    EXPECT(SUCCEEDED(pane.realize({0, 0, 640, 420}, state.tabs[0].location)));
    if (pane.list_snapshot().phase != ml::Phase::loading) {
        pane.destroy(); DestroyWindow(parent); return;
    }
    const HWND list = FindWindowExW(pane.explorer_container(), nullptr, WC_LISTVIEWW, nullptr);
    EXPECT(list != nullptr);
    auto availability = [&] {
        wchar_t text[80]{};
        NMLVDISPINFOW info{};
        info.hdr = {list, 0, LVN_GETDISPINFOW};
        info.item.mask = LVIF_TEXT; info.item.iSubItem = 5;
        info.item.pszText = text; info.item.cchTextMax = 80;
        EXPECT(pane.list_notification(&info.hdr).has_value());
        return std::wstring(text);
    };
    auto deliver = [&](ml::Row value, ml::Change change = ml::Change::none) {
        input->batches.push_back({pane.list_snapshot().request, {std::move(value)}, ml::Phase::complete, 0, change});
        pane.process_content_results();
    };
    constexpr DWORD cloud = FILE_ATTRIBUTE_REPARSE_POINT;
    deliver(row(cloud | FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS));
    const auto identity = pane.list_snapshot().rows.at(0).identity;
    ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetSelectionMark(list, 0);
    EXPECT(availability() == L"Not fully local");
    const auto request = pane.list_snapshot().request;
    ml::IconKey old_key;
    old_key.identity = identity; old_key.bytes = 42; old_key.modified = 123;
    old_key.cloud = pane.list_snapshot().rows.at(0).cloud;
    EXPECT(old_key.matches(pane.list_snapshot().rows.at(0)));
    deliver(row(cloud), ml::Change::delta);
    EXPECT(pane.list_snapshot().rows.at(0).identity == identity);
    EXPECT(std::wstring_view(pane.list_snapshot().rows.at(0).cloud.text()) == L"Locally available");
    EXPECT(!old_key.matches(pane.list_snapshot().rows.at(0)));
    EXPECT(availability() == L"Locally available");
    EXPECT(pane.list_interaction_snapshot().selected.at(0).name == identity.key);
    input->batches.push_back({request, {}, ml::Phase::loading, 0, ml::Change::rescan_begin});
    pane.process_content_results();
    deliver(row(cloud | FILE_ATTRIBUTE_PINNED), ml::Change::rescan);
    EXPECT(pane.list_snapshot().rows.at(0).identity == identity);
    EXPECT(std::wstring_view(pane.list_snapshot().rows.at(0).cloud.text()) == L"Always available (pinned)");
    EXPECT(availability() == L"Always available (pinned)");
    EXPECT(pane.list_interaction_snapshot().focused->name == identity.key);
    pane.refresh_view();
    EXPECT(!(pane.list_snapshot().request == request));
    deliver(row(cloud | FILE_ATTRIBUTE_OFFLINE));
    input->batches.push_back({request, {row(cloud)}, ml::Phase::complete});
    pane.process_content_results();
    EXPECT(pane.list_snapshot().rows.at(0).identity == identity);
    EXPECT(std::wstring_view(pane.list_snapshot().rows.at(0).cloud.text()) == L"Not fully local");
    pane.destroy();
    DestroyWindow(parent);
}
void protected_enrichment() {
    auto source = ml::make_icon_source();
    EXPECT(source != nullptr);
    if (!source) return;
    for (const auto cloud : {row(FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_PINNED).cloud,
                            row(FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS).cloud,
                            row(FILE_ATTRIBUTE_OFFLINE).cloud}) {
        ml::IconRequest request;
        request.key.cloud = cloud;
        request.key.location = request.request.location = {L"C:\\nonexistent-pd235", {}, {}};
        request.key.identity = {L"C:\\nonexistent-pd235\\same.txt", true};
        request.key.thumbnail = true;
        for (const auto property : {"", "System.Media.Duration"}) {
            request.property = property;
            EXPECT(source->replace({request}));
            EXPECT(WaitForSingleObject(source->event(), 5000) == WAIT_OBJECT_0);
            const auto result = source->take();
            EXPECT(result.has_value());
            if (result) {
                EXPECT(result->image.pixels.empty());
                EXPECT(result->property.state == ml::PropertyState::missing);
                EXPECT(result->request.key.identity == request.key.identity);
            }
        }
    }
    source->cancel();
}
} // namespace
int main() {
    // Controlled values and hidden Win32 controls; no OneDrive or GUI automation.
    controlled_attributes(); pane_refresh_and_identity(); protected_enrichment();
    return test::summary("managed_list_cloud");
}
