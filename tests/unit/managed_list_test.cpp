#include "unit/test_pane_host.h"
#include "unit/test_util.h"
#include <deque>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <sddl.h>

using namespace panedock;
namespace {
class Controlled final : public managed_list::Source {
    HANDLE ready_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  public:
    bool accepts{true};
    std::deque<managed_list::Batch> batches;
    ~Controlled() override { CloseHandle(ready_); }
    bool submit(managed_list::Request) noexcept override { return accepts; }
    std::optional<managed_list::Batch> take() noexcept override {
        if (batches.empty()) { ResetEvent(ready_); return {}; }
        auto b = std::move(batches.front()); batches.pop_front();
        if (batches.empty()) ResetEvent(ready_);
        return b;
    }
    HANDLE event() const noexcept override { return ready_; }
    // Retain already-dispatched results deliberately: cancellation cannot be
    // assumed to recall a batch that was already delivered by a provider.
    void cancel() noexcept override {}
    void deliver(managed_list::Batch batch) {
        batches.push_back(std::move(batch)); SetEvent(ready_);
    }
};
struct Fixture final {
    test::TestPaneHost host;
    core::PaneState state{"pane", {{"tab", {L"C:\\A", {}, {}}, "details", "preserved-sort", true, {}, 0}}, "tab"};
    app_shell::Pane pane;
    HWND parent{};
    Controlled* source{};
    Fixture() {
        const auto instance = GetModuleHandleW(nullptr);
        EXPECT(app_shell::Pane::register_window_class(instance));
        parent = CreateWindowExW(0, L"STATIC", L"managed-list test", 0,
            0, 0, 800, 600, nullptr, nullptr, instance, nullptr);
        EXPECT(parent != nullptr);
        pane.set_host(&host); pane.set_reentry_guard(&host.guard);
        EXPECT(pane.create(parent, 0));
        pane.bind(&state);
        auto controlled = std::make_unique<Controlled>(); source = controlled.get();
        pane.use_managed_list(std::move(controlled));
    }
    ~Fixture() { pane.destroy(); if (parent) DestroyWindow(parent); }
    void navigate(const wchar_t* path) { EXPECT(SUCCEEDED(pane.navigate_to({path, {}, {}}))); }
    void deliver(managed_list::Phase phase, std::vector<managed_list::Row> rows = {}, DWORD error = 0) {
        source->deliver({pane.list_snapshot().request, std::move(rows), phase, error});
        pane.process_content_results();
    }
};
void progressive_and_stale() {
    Fixture f;
    f.navigate(L"C:\\A");
    auto a = f.pane.list_snapshot().request;
    EXPECT(f.pane.list_snapshot().phase == managed_list::Phase::loading);
    f.deliver(managed_list::Phase::loading, {{L"child", 0, true}, {L"file", 4, false}});
    EXPECT(f.pane.list_snapshot().rows.size() == 2);
    EXPECT(!f.pane.open_list_row(1));
    EXPECT(f.pane.open_list_row(0)); // usable while the source remains paused
    EXPECT(f.pane.list_snapshot().request.location.parsing_name == L"C:\\A\\child");
    f.deliver(managed_list::Phase::empty);
    EXPECT(f.pane.list_snapshot().phase == managed_list::Phase::empty);
    EXPECT(SUCCEEDED(f.pane.navigate_up_one_level()));
    f.deliver(managed_list::Phase::empty);
    EXPECT(f.pane.active_tab()->location.parsing_name == L"C:\\A");
    f.navigate(L"C:\\B");
    f.source->deliver({a, {{L"old", 0, true}}, managed_list::Phase::loading, 0});
    f.source->deliver({a, {}, managed_list::Phase::error, ERROR_ACCESS_DENIED});
    f.pane.process_content_results();
    EXPECT(f.pane.list_snapshot().rows.empty());
    EXPECT(f.pane.list_snapshot().phase == managed_list::Phase::loading);
    auto wrong_location = f.pane.list_snapshot().request;
    wrong_location.location.parsing_name = L"C:\\A";
    f.source->deliver({wrong_location, {}, managed_list::Phase::error, ERROR_ACCESS_DENIED});
    f.pane.process_content_results();
    EXPECT(f.pane.list_snapshot().phase == managed_list::Phase::loading);
    f.deliver(managed_list::Phase::complete, {{L"current", 10, false}});
    app_shell::Pane::SelectionSnapshot counts;
    EXPECT(SUCCEEDED(f.pane.selection_snapshot(counts)) && counts.total == 1);
    EXPECT(f.pane.list_snapshot().rows[0].name == L"current");
    EXPECT(f.pane.active_tab()->view_mode == "details");
    EXPECT(f.pane.active_tab()->sort_column == "preserved-sort");
    f.pane.navigate_history(true);
    f.deliver(managed_list::Phase::empty);
    EXPECT(f.pane.active_tab()->location.parsing_name == L"C:\\A");
}
void reentrant_acknowledgement() {
    struct RedirectHost final : test::TestPaneHost {
        app_shell::Pane* pane{};
        bool redirect{true};
        void schedule_session_save() noexcept override {
            if (!redirect) return;
            redirect = false;
            (void)pane->navigate_to({L"C:\\replacement", {}, {}});
        }
    } host;
    core::PaneState state{"pane", {{"tab", {}, {}, {}, true, {}, 0}}, "tab"};
    EXPECT(app_shell::Pane::register_window_class(GetModuleHandleW(nullptr)));
    const HWND parent = CreateWindowExW(0, L"STATIC", L"reentry test", 0,
        0, 0, 800, 600, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    app_shell::Pane pane; host.pane = &pane;
    pane.set_host(&host); pane.set_reentry_guard(&host.guard);
    EXPECT(pane.create(parent, 0)); pane.bind(&state);
    auto source = std::make_unique<Controlled>(); auto* delivery = source.get();
    pane.use_managed_list(std::move(source));
    EXPECT(SUCCEEDED(pane.navigate_to({L"C:\\original", {}, {}})));
    delivery->deliver({pane.list_snapshot().request, {{L"obsolete", 0, false}}, managed_list::Phase::complete, 0});
    pane.process_content_results();
    EXPECT(pane.list_snapshot().request.location.parsing_name == L"C:\\replacement");
    delivery->deliver({pane.list_snapshot().request, {{L"correct", 0, false}}, managed_list::Phase::complete, 0});
    pane.process_content_results();
    EXPECT(pane.list_snapshot().phase == managed_list::Phase::complete);
    EXPECT(pane.list_snapshot().rows.size() == 1);
    pane.destroy();
    if (parent) DestroyWindow(parent);
}
void binding_errors_and_cleanup() {
    Fixture f;
    f.navigate(L"C:\\A");
    auto old = f.pane.list_snapshot().request;
    f.host.group_id = "replacement-group";
    f.source->deliver({old, {{L"old", 0, false}}, managed_list::Phase::complete, 0});
    f.pane.process_content_results();
    EXPECT(f.pane.list_snapshot().rows.empty());
    f.navigate(L"C:\\B");
    f.deliver(managed_list::Phase::empty);
    EXPECT(f.pane.list_snapshot().phase == managed_list::Phase::empty);
    f.navigate(L"C:\\denied");
    f.deliver(managed_list::Phase::error, {}, ERROR_ACCESS_DENIED);
    EXPECT(f.pane.list_snapshot().phase == managed_list::Phase::error);
    EXPECT(f.pane.list_snapshot().error == ERROR_ACCESS_DENIED);
    f.source->accepts = false;
    f.pane.refresh_view();
    EXPECT(f.pane.list_snapshot().error == ERROR_NO_SYSTEM_RESOURCES);
    f.source->accepts = true;
    f.pane.refresh_view();
    EXPECT(f.pane.list_snapshot().phase == managed_list::Phase::loading);
    old = f.pane.list_snapshot().request;
    f.pane.active_tab()->id = "replacement-tab";
    f.state.active_tab_id = "replacement-tab";
    f.source->deliver({old, {{L"wrong-tab", 0, false}}, managed_list::Phase::error, ERROR_ACCESS_DENIED});
    f.pane.process_content_results();
    EXPECT(f.pane.list_snapshot().phase == managed_list::Phase::loading);
    EXPECT(f.pane.list_snapshot().rows.empty());
    f.pane.unbind();
    f.source->deliver({old, {{L"late", 0, false}}, managed_list::Phase::complete, 0});
    f.pane.process_content_results();
    EXPECT(f.pane.list_snapshot().rows.empty());
    f.pane.bind(&f.state); f.navigate(L"C:\\C");
    f.host.shutting_down = true;
    f.deliver(managed_list::Phase::complete, {{L"late", 0, false}});
    EXPECT(f.pane.list_snapshot().rows.empty());
    f.pane.destroy();
    EXPECT(f.pane.content_event() == nullptr);
    EXPECT(f.pane.list_snapshot().phase == managed_list::Phase::idle);
}
void bounded_rows() {
    Fixture f; f.navigate(L"C:\\large");
    for (std::size_t i = 0; i <= managed_list::kMaxRows / managed_list::kBatchRows; ++i) {
        std::vector<managed_list::Row> rows;
        for (std::size_t j = 0; j < managed_list::kBatchRows; ++j)
            rows.push_back({L"row"+std::to_wstring(i*managed_list::kBatchRows+j), 0, false});
        f.deliver(managed_list::Phase::loading, std::move(rows));
    }
    // PD-231's inline property slots also consume the original 24 MiB cap.
    // HEAD already reaches that cap before 100,000 rows on LLVM-MinGW.
    const auto& retained = f.pane.list_snapshot().rows;
    EXPECT(!retained.empty() && retained.size() <= managed_list::kMaxRows);
    auto bytes = retained.capacity() * sizeof(managed_list::Row);
    for (const auto& row : retained) bytes += (row.name.capacity() + 1) * sizeof(wchar_t);
    EXPECT(bytes <= managed_list::kMaxRowBytes);
    EXPECT(retained.size() == managed_list::kMaxRows ||
        bytes + managed_list::kBatchRows * sizeof(managed_list::Row) > managed_list::kMaxRowBytes);
    EXPECT(f.pane.list_snapshot().phase == managed_list::Phase::limited);
    f.navigate(L"C:\\long-names");
    for (int i = 0; i < 1000 && f.pane.list_snapshot().phase == managed_list::Phase::loading; ++i)
        f.deliver(managed_list::Phase::loading,
            std::vector<managed_list::Row>(managed_list::kBatchRows,
                {std::wstring(managed_list::kMaxName, L'x'), 0, false}));
    EXPECT(f.pane.list_snapshot().phase == managed_list::Phase::limited);
    EXPECT(f.pane.list_snapshot().rows.size() < managed_list::kMaxRows);
    f.navigate(L"C:\\bad");
    f.deliver(managed_list::Phase::loading,
        std::vector<managed_list::Row>(managed_list::kBatchRows+1, {L"row", 0, false}));
    EXPECT(f.pane.list_snapshot().phase == managed_list::Phase::error);
}
void wait_for_complete(app_shell::Pane& pane) {
    while (pane.list_snapshot().phase == managed_list::Phase::loading) {
        const DWORD result = WaitForSingleObject(pane.content_event(), 10000);
        EXPECT(result == WAIT_OBJECT_0);
        if (result != WAIT_OBJECT_0) return;
        pane.process_content_results();
    }
    // Notification readiness/idle is independently awaited by PD-232's
    // integration test; enumeration alone does not promise an empty event.
}
void real_local_directory() {
    // All four global slots remain owned during this integration run; a fifth
    // Pane must show a recoverable startup/capacity error, never spawn more.
    std::array<std::unique_ptr<managed_list::Source>, managed_list::kMaxSubscriptions> sources;
    for (auto& source : sources) { source = managed_list::make_local_source(); EXPECT(source != nullptr); }
    EXPECT(managed_list::make_local_source() == nullptr);
    {
        test::TestPaneHost unavailable_host;
        core::PaneState unavailable_state{"pane", {{"tab", {L"C:\\", {}, {}}, {}, {}, true, {}, 0}}, "tab"};
        app_shell::Pane unavailable;
        unavailable.set_host(&unavailable_host); unavailable.set_reentry_guard(&unavailable_host.guard);
        const auto instance = GetModuleHandleW(nullptr);
        EXPECT(app_shell::Pane::register_window_class(instance));
        const HWND unavailable_parent = CreateWindowExW(0, L"STATIC", L"capacity test", 0,
            0, 0, 800, 600, nullptr, nullptr, instance, nullptr);
        EXPECT(unavailable_parent != nullptr);
        EXPECT(unavailable.create(unavailable_parent, 0));
        unavailable.bind(&unavailable_state); unavailable.use_managed_list();
        EXPECT(SUCCEEDED(unavailable.navigate_to({L"C:\\", {}, {}})));
        EXPECT(unavailable.list_snapshot().phase == managed_list::Phase::error);
        EXPECT(unavailable.list_snapshot().error == ERROR_NO_SYSTEM_RESOURCES);
        unavailable.destroy();
        if (unavailable_parent) DestroyWindow(unavailable_parent);
    }
    const auto root = std::filesystem::current_path() /
        (L"pd224-test-" + std::to_wstring(GetCurrentProcessId()));
    EXPECT(!std::filesystem::exists(root));
    std::filesystem::create_directories(root / L"child");
    for (int i = 0; i < 400; ++i) std::ofstream(root / (std::to_wstring(i) + L".txt")) << "data";
    test::TestPaneHost host;
    core::PaneState state{"pane", {{"tab", {root.wstring(), {}, {}}, {}, {}, true, {}, 0}}, "tab"};
    app_shell::Pane pane; pane.set_host(&host); pane.set_reentry_guard(&host.guard); pane.bind(&state);
    // A real owner-data common control, hosted by Pane. These are hidden
    // control integration checks, not desktop focus/accessibility evidence.
    const auto instance = GetModuleHandleW(nullptr);
    EXPECT(app_shell::Pane::register_window_class(instance));
    const HWND parent = CreateWindowExW(0, L"STATIC", L"test", 0, 0, 0, 800, 600,
        nullptr, nullptr, instance, nullptr);
    EXPECT(parent != nullptr);
    EXPECT(pane.create(parent, 0));
    pane.bind(&state);
    pane.use_managed_list(std::move(sources[0]));
    EXPECT(SUCCEEDED(pane.realize({0, 0, 600, 400}, {root.wstring(), {}, {}})));
    const HWND list = FindWindowExW(pane.explorer_container(), nullptr, panedock::app_shell::kManagedListClassName, nullptr);
    EXPECT(list != nullptr);
    EXPECT((GetWindowLongPtrW(list, GWL_STYLE) & LVS_OWNERDATA) != 0);
    EXPECT(WaitForSingleObject(pane.content_event(), 10000) == WAIT_OBJECT_0);
    pane.process_content_results();
    EXPECT(!pane.list_snapshot().rows.empty());
    EXPECT(pane.list_snapshot().rows.size() <= managed_list::kBatchRows * managed_list::kQueuedBatches);
    EXPECT(pane.list_snapshot().phase == managed_list::Phase::loading);
    wait_for_complete(pane);
    EXPECT(pane.list_snapshot().phase == managed_list::Phase::complete);
    EXPECT(pane.list_snapshot().rows.size() == 401);
    EXPECT(ListView_GetItemCount(list) == 401);
    wchar_t name[256]{};
    LVITEMW text_item{}; text_item.pszText = name; text_item.cchTextMax = 256;
    SendMessageW(list, LVM_GETITEMTEXTW, 0, reinterpret_cast<LPARAM>(&text_item));
    if (!pane.list_snapshot().rows.empty()) EXPECT(pane.list_snapshot().rows[0].name == name);
    ListView_SetItemState(list, 0, LVIS_SELECTED, LVIS_SELECTED);
    app_shell::Pane::SelectionSnapshot selection;
    EXPECT(SUCCEEDED(pane.selection_snapshot(selection)) && selection.selected == 1);
    const auto& rows = pane.list_snapshot().rows;
    const auto child = std::find_if(rows.begin(), rows.end(), [](const auto& row) { return row.name == L"child"; });
    EXPECT(child != rows.end());
    if (child != rows.end()) {
        EXPECT(child->directory);
        EXPECT(pane.open_list_row(static_cast<std::size_t>(child-rows.begin())));
        wait_for_complete(pane);
        EXPECT(pane.list_snapshot().phase == managed_list::Phase::empty);
        EXPECT(SUCCEEDED(pane.navigate_up_one_level())); wait_for_complete(pane);
        EXPECT(pane.list_snapshot().rows.size() == 401);
    }
    EXPECT(SUCCEEDED(pane.navigate_to({(root / L"missing").wstring(), {}, {}})));
    wait_for_complete(pane);
    EXPECT(pane.list_snapshot().phase == managed_list::Phase::error);
    // Deny only directory listing on a disposable directory we own, then
    // restore its original DACL before cleanup (no admin privilege required).
    const auto denied = root / L"denied";
    std::filesystem::create_directory(denied);
    DWORD needed{};
    GetFileSecurityW(denied.c_str(), DACL_SECURITY_INFORMATION, nullptr, 0, &needed);
    std::vector<unsigned char> original(needed);
    EXPECT(GetFileSecurityW(denied.c_str(), DACL_SECURITY_INFORMATION,
        original.data(), needed, &needed));
    PSECURITY_DESCRIPTOR descriptor{};
    EXPECT(ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:(D;;0x1;;;WD)", SDDL_REVISION_1, &descriptor, nullptr));
    const BOOL changed = descriptor && SetFileSecurityW(denied.c_str(), DACL_SECURITY_INFORMATION, descriptor);
    const DWORD acl_error = changed ? ERROR_SUCCESS : GetLastError();
    // Some restricted test identities cannot change the DACL even on a
    // disposable directory they created. Keep the denied-provider seam tests
    // deterministic and report the real-filesystem branch as skipped.
    EXPECT(changed || acl_error == ERROR_ACCESS_DENIED);
    if (!changed && acl_error == ERROR_ACCESS_DENIED)
        std::cout << "SKIP real ACL denial branch: current identity cannot set DACL\n";
    if (descriptor) LocalFree(descriptor);
    // An elevated or backup-privileged identity can still enumerate the denied
    // directory; the real denial branch is then not observable.
    std::error_code probe_error;
    const bool still_listable = changed &&
        (std::filesystem::directory_iterator(denied, probe_error), !probe_error);
    if (still_listable)
        std::cout << "SKIP real ACL denial branch: current identity can still enumerate\n";
    if (changed && !still_listable) {
        EXPECT(SUCCEEDED(pane.navigate_to({denied.wstring(), {}, {}})));
        wait_for_complete(pane);
        EXPECT(pane.list_snapshot().phase == managed_list::Phase::error);
        EXPECT(pane.list_snapshot().error == ERROR_ACCESS_DENIED);
    }
    if (changed) EXPECT(SetFileSecurityW(denied.c_str(), DACL_SECURITY_INFORMATION, original.data()));
    // Replace an in-flight request, and destroy while its successor may still
    // be in FindFirst/FindNext or blocked by bounded queue backpressure.
    EXPECT(SUCCEEDED(pane.navigate_to({root.wstring(), {}, {}})));
    EXPECT(SUCCEEDED(pane.navigate_to({(root / L"child").wstring(), {}, {}})));
    wait_for_complete(pane);
    EXPECT(pane.list_snapshot().phase == managed_list::Phase::empty);
    EXPECT(SUCCEEDED(pane.navigate_to({root.wstring(), {}, {}})));
    const auto before = GetTickCount64(); pane.destroy();
    EXPECT(GetTickCount64()-before < 1000);
    EXPECT(!IsWindow(list));
    DestroyWindow(parent);
    // Windows read-only directory enumeration uses share-delete semantics.
    std::filesystem::remove_all(root);
}
} // namespace
int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    progressive_and_stale(); reentrant_acknowledgement(); binding_errors_and_cleanup(); bounded_rows(); real_local_directory();
    return test::summary("managed_list");
}
