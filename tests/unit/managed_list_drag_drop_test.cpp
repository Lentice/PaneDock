#include "unit/test_pane_host.h"
#include "unit/test_util.h"
#include "com_ref_counted.h"
#include <functional>

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

struct Fixture final {
    struct Host : test::TestPaneHost {
        int drags{};
        void file_drag_changed(bool entering) noexcept override {
            drags += entering ? 1 : -1;
            EXPECT(drags >= 0);
        }
    } host;
    core::PaneState state{"pane", {
        {"tab-a", {L"C:\\A", {}, {}}, "details", {}, true, {}, 0},
        {"tab-b", {L"C:\\B", {}, {}}, "details", {}, true, {}, 0}}, "tab-a"};
    app_shell::Pane pane;
    Controlled* source{};
    HWND parent{};
    HWND list{};

    explicit Fixture(int index = 0,
        std::unique_ptr<file_operations::DropTargetResolver> resolver = {}) {
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
        pane.use_list_drop_resolver(std::move(resolver));
        EXPECT(SUCCEEDED(pane.realize({0, 0, 640, 420}, {L"C:\\A", {}, {}})));
        list = FindWindowExW(pane.explorer_container(), nullptr, panedock::app_shell::kManagedListClassName, nullptr);
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
    int row(std::wstring_view name) const {
        const auto& rows = pane.list_snapshot().rows;
        const auto found = std::find_if(rows.begin(), rows.end(),
            [&](const auto& item) { return item.name == name; });
        return found == rows.end() ? -1 : static_cast<int>(found - rows.begin());
    }
    void select(int index) {
        ListView_SetItemState(list, index, LVIS_SELECTED, LVIS_SELECTED);
    }
};

// A transport boundary double: these callbacks never read or mutate files.
class Data final : public ComRefCounted<Data, IDataObject> {
public:
    explicit Data(int& alive) : alive_(alive) { ++alive_; }
    ~Data() { --alive_; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDataObject) return E_NOINTERFACE;
        *out = static_cast<IDataObject*>(this); AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return E_NOTIMPL; }
private:
    int& alive_;
};
class Target final : public ComRefCounted<Target, IDropTarget> {
public:
    int enters{}, overs{}, leaves{}, drops{};
    DWORD allowed{}, keys{}, result{DROPEFFECT_LINK};
    POINTL point{};
    HRESULT status{S_OK};
    std::function<void()> reenter;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDropTarget) return E_NOINTERFACE;
        *out = static_cast<IDropTarget*>(this); AddRef(); return S_OK;
    }
    HRESULT negotiate(DWORD k, POINTL p, DWORD* effect) {
        allowed = *effect; keys = k; point = p;
        if (auto callback = std::exchange(reenter, {})) callback();
        *effect = result;
        return status;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject*, DWORD k, POINTL p, DWORD* e) override {
        ++enters; return negotiate(k, p, e);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD k, POINTL p, DWORD* e) override {
        ++overs; return negotiate(k, p, e);
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject*, DWORD k, POINTL p, DWORD* e) override {
        ++drops; return negotiate(k, p, e);
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override { ++leaves; return S_OK; }
};
class Resolver final : public file_operations::DropTargetResolver {
public:
    Microsoft::WRL::ComPtr<Target> target;
    std::wstring folder;
    std::function<void()> reenter;
    Resolver() { target.Attach(new Target); }
    HRESULT resolve(HWND, std::wstring_view value,
                    Microsoft::WRL::ComPtr<IDropTarget>& out) noexcept override {
        folder = value;
        out = target;
        if (auto callback = std::exchange(reenter, {})) callback();
        return S_OK;
    }
};
constexpr DWORD effects = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
struct Setup {
    int alive{};
    Microsoft::WRL::ComPtr<Data> data;
    Resolver* resolver{};
    std::unique_ptr<Resolver> make_resolver() {
        auto value = std::make_unique<Resolver>(); resolver = value.get(); return value;
    }
    Setup() { data.Attach(new Data(alive)); }
};
void effect_and_current_folder() {
    Setup s;
    Fixture f(0, s.make_resolver());
    f.pane.use_list_icons(nullptr);
    auto ole = f.pane.list_drop_target();
    EXPECT(ole != nullptr);
    DWORD effect = effects;
    EXPECT(ole->DragEnter(s.data.Get(), MK_CONTROL, {10,20}, &effect) == S_OK);
    EXPECT(effect == DROPEFFECT_NONE && f.host.drags == 1);
    f.deliver(LR"(C:\A)", managed_list::Phase::complete);
    effect = effects;
    ole->DragOver(MK_SHIFT, {30,40}, &effect);
    EXPECT(effect == DROPEFFECT_LINK); // Shell decision, never guessed from modifier/path
    EXPECT(s.resolver->folder == LR"(C:\A)");
    EXPECT(s.resolver->target->allowed == effects && s.resolver->target->keys == MK_SHIFT);
    EXPECT(s.resolver->target->point.x == 30 && s.resolver->target->point.y == 40);
    s.resolver->target->result = DROPEFFECT_MOVE;
    effect = DROPEFFECT_COPY;
    ole->DragOver(MK_CONTROL, {}, &effect);
    EXPECT(effect == DROPEFFECT_NONE); // disallowed provider effect is masked
    s.resolver->target->result = DROPEFFECT_COPY;
    effect = effects;
    ole->Drop(s.data.Get(), MK_CONTROL, {}, &effect);
    EXPECT(effect == DROPEFFECT_COPY && s.resolver->target->drops == 1);
    EXPECT(f.host.drags == 0);
    EXPECT(s.resolver->target->leaves == 0); // Drop terminates the provider session
}
void pending_switch_and_drop_revalidation() {
    Setup s;
    Fixture f(0, s.make_resolver());
    f.pane.use_list_icons(nullptr);
    f.deliver(LR"(C:\A)", managed_list::Phase::complete);
    auto ole = f.pane.list_drop_target();
    DWORD effect = effects;
    ole->DragEnter(s.data.Get(), 0, {}, &effect);
    EXPECT(effect == DROPEFFECT_LINK);
    f.pane.switch_active_tab("tab-b"); // same public operation as coordinator hover
    effect = effects;
    ole->Drop(s.data.Get(), 0, {}, &effect);
    EXPECT(effect == DROPEFFECT_NONE && s.resolver->target->drops == 0);
    EXPECT(s.resolver->target->leaves == 1 && f.host.drags == 0);
    f.deliver(LR"(C:\B)", managed_list::Phase::complete);
    effect = effects; ole->DragEnter(s.data.Get(), 0, {}, &effect);
    EXPECT(s.resolver->folder == LR"(C:\B)" && effect == DROPEFFECT_LINK);
    f.pane.switch_active_tab("tab-a");
    f.deliver(LR"(C:\A)", managed_list::Phase::complete);
    effect = effects; ole->Drop(s.data.Get(), MK_SHIFT, {}, &effect);
    EXPECT(effect == DROPEFFECT_LINK && s.resolver->folder == LR"(C:\A)");
    EXPECT(s.resolver->target->drops == 1 && f.host.drags == 0);
}
void stale_reentry_and_detached_lifetime() {
    for (int phase = 0; phase != 4; ++phase) {
        Setup s;
        Fixture f(0, s.make_resolver());
        f.pane.use_list_icons(nullptr);
        f.deliver(LR"(C:\A)", managed_list::Phase::complete);
        auto ole = f.pane.list_drop_target();
        auto target = s.resolver->target;
        if (phase == 0) s.resolver->reenter = [&] { f.pane.unbind(); };
        if (phase == 1) target->reenter = [&] {
            DWORD nested = effects;
            ole->Drop(s.data.Get(), 0, {}, &nested);
            EXPECT(nested == DROPEFFECT_NONE);
            f.host.group_id = "replacement";
        };
        DWORD effect = effects;
        ole->DragEnter(s.data.Get(), 0, {}, &effect);
        if (phase < 2) {
            EXPECT(effect == DROPEFFECT_NONE && target->drops == 0);
        } else {
            EXPECT(effect == DROPEFFECT_LINK);
            target->reenter = [&] {
                EXPECT(f.host.guard.in_shell_call());
                if (phase == 2) f.host.shutting_down = true;
                else f.pane.destroy(); // stronger than normal deferred teardown
            };
            effect = effects;
            ole->DragOver(0, {}, &effect);
            EXPECT(effect == DROPEFFECT_NONE && target->drops == 0);
        }
        ole->DragLeave();
        EXPECT(f.host.drags == 0);
        f.pane.destroy();
        effect = effects;
        ole->Drop(s.data.Get(), 0, {}, &effect);
        EXPECT(effect == DROPEFFECT_NONE);
        s.data.Reset();
        EXPECT(s.alive == 0); // OLE-held target outlives Pane content without data leaks
    }
}
void leave_reentry_and_failure() {
    Setup s;
    Fixture f(0, s.make_resolver());
    f.pane.use_list_icons(nullptr);
    f.deliver(LR"(C:\A)", managed_list::Phase::complete);
    auto ole = f.pane.list_drop_target();
    auto target = s.resolver->target;
    target->reenter = [&] { ole->DragLeave(); };
    DWORD effect = effects;
    ole->DragEnter(s.data.Get(), 0, {}, &effect);
    EXPECT(effect == DROPEFFECT_NONE && f.host.drags == 0);
    target->status = E_FAIL;
    effect = effects;
    ole->DragEnter(s.data.Get(), 0, {}, &effect);
    EXPECT(effect == DROPEFFECT_NONE);
    ole->DragLeave();
    EXPECT(f.host.drags == 0);
    EXPECT(ole->DragOver(0, {}, nullptr) == E_INVALIDARG);
}
void outer_reentry_and_shutdown_drop_finish_session() {
    for (bool shutdown : {false, true}) {
        Setup s;
        Fixture f(0, s.make_resolver());
        f.pane.use_list_icons(nullptr);
        f.deliver(LR"(C:\A)", managed_list::Phase::complete);
        auto ole = f.pane.list_drop_target();
        DWORD effect = effects;
        ole->DragEnter(s.data.Get(), 0, {}, &effect);
        if (shutdown) f.host.shutting_down = true;
        {
            app_shell::ShellCallScope outer(f.host.guard);
            effect = effects;
            ole->Drop(s.data.Get(), 0, {}, &effect);
        }
        EXPECT(effect == DROPEFFECT_NONE && f.host.drags == 0);
        EXPECT(s.resolver->target->drops == 0 && s.resolver->target->leaves == 1);
    }
    Setup s;
    Fixture f(0, s.make_resolver());
    f.pane.use_list_icons(nullptr);
    f.deliver(LR"(C:\A)", managed_list::Phase::error);
    auto ole = f.pane.list_drop_target();
    DWORD effect = effects;
    ole->DragEnter(s.data.Get(), 0, {}, &effect);
    EXPECT(effect == DROPEFFECT_NONE && s.resolver->target->enters == 0);
    ole->DragLeave();
}
void callbacks_after_host_lifetime() {
    Microsoft::WRL::ComPtr<IDropTarget> detached;
    int alive{};
    Microsoft::WRL::ComPtr<Data> data;
    data.Attach(new Data(alive));
    {
        Fixture f;
        detached = f.pane.list_drop_target();
        DWORD effect = effects;
        detached->DragEnter(data.Get(), 0, {}, &effect);
        EXPECT(f.host.drags == 1);
    }
    data.Reset();
    EXPECT(alive == 0);
    EXPECT(detached->DragLeave() == S_OK); // no PaneHost/guard remains
    DWORD effect = effects;
    detached->DragOver(0, {}, &effect);
    EXPECT(effect == DROPEFFECT_NONE);
}
void source_cancel_and_aborted_setup() {
    EXPECT(file_operations::drag_continue(false, MK_LBUTTON, false) == S_OK);
    EXPECT(file_operations::drag_continue(false, MK_CONTROL, false) == DRAGDROP_S_DROP);
    EXPECT(file_operations::drag_continue(true, MK_LBUTTON, false) == DRAGDROP_S_CANCEL);
    EXPECT(file_operations::drag_continue(false, 0, true) == DRAGDROP_S_CANCEL);
    file_operations::Callbacks callbacks{};
    callbacks.abort_setup = [](void*) noexcept { return true; };
    Microsoft::WRL::ComPtr<IDataObject> data;
    const std::vector<std::wstring> names{LR"(C:\never-resolved)"};
    EXPECT(file_operations::selection_data_object(names, callbacks, data) == E_ABORT);
    EXPECT(!data);
    DWORD effect = effects;
    EXPECT(file_operations::drag_selection(nullptr, callbacks, effect) == E_ABORT);
    EXPECT(effect == DROPEFFECT_NONE);
    int alive{};
    Microsoft::WRL::ComPtr<Data> owned;
    owned.Attach(new Data(alive));
    callbacks.abort_setup = nullptr;
    callbacks.cancel_requested = [](void*) noexcept { return true; };
    effect = effects;
    // Cancellation before OLE entry: never starts a drag loop or a transfer.
    EXPECT(file_operations::drag_selection(owned.Get(), callbacks, effect) == DRAGDROP_S_CANCEL);
    EXPECT(effect == DROPEFFECT_NONE);
}
// Rows and their transfer state, as the list asks for them through the callback mask.
UINT transfer_state(Fixture& f, int index) {
    NMLVDISPINFOW info{};
    info.hdr.hwndFrom = f.list; info.hdr.code = LVN_GETDISPINFOW;
    info.item.mask = LVIF_STATE; info.item.iItem = index;
    info.item.stateMask = LVIS_CUT | LVIS_DROPHILITED;
    EXPECT(f.pane.list_notification(&info.hdr).has_value());
    return info.item.state & info.item.stateMask;
}
POINTL row_point(Fixture& f, int index) {
    RECT rect{};
    EXPECT(index >= 0 && ListView_GetItemRect(f.list, index, &rect, LVIR_LABEL));
    POINT point{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
    ClientToScreen(f.list, &point);
    return {point.x, point.y};
}
std::vector<managed_list::Row> sample_rows() {
    managed_list::Row folder{L"Folder", 0, true}; folder.identity = {L"Folder", false};
    managed_list::Row doc{L"Doc.txt", 1, false}; doc.identity = {L"Doc.txt", false};
    managed_list::Row note{L"Note.txt", 1, false}; note.identity = {L"Note.txt", false};
    return {folder, doc, note};
}
// Row targets come from the Shell by row identity; other rows mean the background.
class RowResolver final : public file_operations::DropTargetResolver {
public:
    Microsoft::WRL::ComPtr<Target> background, folder;
    std::vector<std::wstring> asked;
    RowResolver() { background.Attach(new Target); folder.Attach(new Target); }
    HRESULT resolve(HWND, std::wstring_view value,
                    Microsoft::WRL::ComPtr<IDropTarget>& out) noexcept override {
        asked.emplace_back(value);
        if (value == LR"(C:\A)") out = background;
        else if (value == L"Folder") out = folder;
        else return S_FALSE; // not a drop target
        return S_OK;
    }
};
void drop_onto_folder_row_targets_that_folder() {
    auto owned = std::make_unique<RowResolver>();
    auto* resolver = owned.get();
    Setup s;
    Fixture f(0, std::move(owned));
    f.pane.use_list_icons(nullptr);
    f.deliver(LR"(C:\A)", managed_list::Phase::complete, sample_rows());
    const int folder = f.row(L"Folder"), doc = f.row(L"Doc.txt"), note = f.row(L"Note.txt");
    auto ole = f.pane.list_drop_target();
    DWORD effect = effects;
    ole->DragEnter(s.data.Get(), 0, row_point(f, folder), &effect);
    EXPECT(effect == DROPEFFECT_LINK && resolver->folder->enters == 1);
    EXPECT(resolver->background->enters == 0);
    EXPECT(transfer_state(f, folder) == LVIS_DROPHILITED && transfer_state(f, doc) == 0);
    effect = effects;
    ole->DragOver(0, row_point(f, folder), &effect);
    EXPECT(resolver->folder->overs == 1 && resolver->folder->enters == 1);
    // A plain file is not a target: the folder background takes it, unhighlighted.
    effect = effects;
    ole->DragOver(0, row_point(f, doc), &effect);
    EXPECT(resolver->folder->leaves == 1 && resolver->background->enters == 1);
    EXPECT(effect == DROPEFFECT_LINK && transfer_state(f, folder) == 0);
    // Across further plain files the background session continues without churn.
    effect = effects;
    ole->DragOver(0, row_point(f, note), &effect);
    EXPECT(resolver->background->enters == 1 && resolver->background->leaves == 0);
    EXPECT(resolver->background->overs == 2);
    resolver->folder->result = DROPEFFECT_COPY;
    effect = effects;
    ole->Drop(s.data.Get(), 0, row_point(f, folder), &effect);
    EXPECT(effect == DROPEFFECT_COPY && resolver->folder->drops == 1);
    EXPECT(resolver->background->drops == 0 && resolver->background->leaves == 1);
    EXPECT(transfer_state(f, folder) == 0 && f.host.drags == 0);
    // Leaving clears the highlight and ends the row session.
    effect = effects;
    ole->DragEnter(s.data.Get(), 0, row_point(f, folder), &effect);
    EXPECT(transfer_state(f, folder) == LVIS_DROPHILITED);
    ole->DragLeave();
    EXPECT(transfer_state(f, folder) == 0 && resolver->folder->leaves == 2);
    EXPECT(std::find(resolver->asked.begin(), resolver->asked.end(), L"Doc.txt") !=
           resolver->asked.end()); // identity key, never display text or a path
}
void right_drag_continues_with_right_button() {
    using app_shell::Pane;
    EXPECT(Pane::list_drag_continue(true, false, MK_RBUTTON, false) == S_OK);
    EXPECT(Pane::list_drag_continue(true, false, 0, false) == DRAGDROP_S_DROP);
    EXPECT(Pane::list_drag_continue(true, false, MK_RBUTTON | MK_LBUTTON, false) == DRAGDROP_S_CANCEL);
    EXPECT(Pane::list_drag_continue(true, true, MK_RBUTTON, false) == DRAGDROP_S_CANCEL);
    EXPECT(Pane::list_drag_continue(true, false, MK_RBUTTON, true) == DRAGDROP_S_CANCEL);
    EXPECT(Pane::list_drag_continue(false, false, MK_LBUTTON, false) == S_OK);
    EXPECT(Pane::list_drag_continue(false, false, MK_RBUTTON, false) == DRAGDROP_S_DROP);
}
void cut_rows_are_dimmed_until_the_clipboard_changes() {
    Fixture f;
    f.pane.use_list_icons(nullptr);
    f.deliver(LR"(C:\A)", managed_list::Phase::complete, sample_rows());
    const int folder = f.row(L"Folder"), doc = f.row(L"Doc.txt");
    f.select(doc);
    app_shell::Pane::FileCommandSnapshot snapshot;
    EXPECT(f.pane.file_command_snapshot(file_operations::Command::cut, snapshot) == S_OK);
    f.pane.note_list_cut(snapshot);
    EXPECT(transfer_state(f, doc) == LVIS_CUT && transfer_state(f, folder) == 0);
    SendMessageW(f.list, WM_CLIPBOARDUPDATE, 0, 0); // same sequence: still cut
    EXPECT(transfer_state(f, doc) == LVIS_CUT);
    f.pane.switch_active_tab("tab-b"); // another folder never shows those marks
    f.deliver(LR"(C:\B)", managed_list::Phase::complete, sample_rows());
    EXPECT(transfer_state(f, f.row(L"Doc.txt")) == 0);
    f.pane.switch_active_tab("tab-a");
    f.deliver(LR"(C:\A)", managed_list::Phase::complete, sample_rows());
    EXPECT(transfer_state(f, f.row(L"Doc.txt")) == LVIS_CUT);
    snapshot.binding = {}; // a stale snapshot is refused
    f.pane.note_list_cut(snapshot);
    EXPECT(transfer_state(f, f.row(L"Doc.txt")) == 0);
}
} // namespace
int main() {
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES};
    EXPECT(InitCommonControlsEx(&controls));
    effect_and_current_folder();
    pending_switch_and_drop_revalidation();
    stale_reentry_and_detached_lifetime();
    leave_reentry_and_failure();
    outer_reentry_and_shutdown_drop_finish_session();
    callbacks_after_host_lifetime();
    source_cancel_and_aborted_setup();
    drop_onto_folder_row_targets_that_folder();
    right_drag_continues_with_right_button();
    cut_rows_are_dimmed_until_the_clipboard_changes();
    return test::summary("managed_list_drag_drop");
}
