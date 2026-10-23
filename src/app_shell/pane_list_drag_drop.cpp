#include "app_shell/pane.h"
#include "app_shell/pane_host.h"
#include "com_ref_counted.h"

#include <shlobj.h>
#include <shobjidl.h>

#include <algorithm>
#include <memory>
#include <new>

namespace panedock::app_shell {
namespace {

struct PidlDeleter { void operator()(ITEMIDLIST* value) const noexcept { CoTaskMemFree(value); } };

// Explorer drops onto an item only when its folder reports SFGAO_DROPTARGET
// (folders, archives, programs, shortcuts); any other row means the folder
// background. Local rows are leaf identities parsed by the displayed folder
// itself; Shell rows carry their own absolute parsing identity. S_FALSE: the
// row is not a drop target.
HRESULT item_drop_target(HWND owner, const core::ShellLocation& folder,
    const managed_list::ItemIdentity& item, const file_operations::Callbacks& callbacks,
    Microsoft::WRL::ComPtr<IDropTarget>& target) noexcept try {
    target.Reset();
    const auto aborted = [&] { return callbacks.abort_setup && callbacks.abort_setup(callbacks.context); };
    std::unique_ptr<ITEMIDLIST, PidlDeleter> owned;
    Microsoft::WRL::ComPtr<IShellFolder> parent;
    PCUITEMID_CHILD child{};
    HRESULT hr{};
    if (item.shell_item) {
        PIDLIST_ABSOLUTE raw{};
        hr = SHParseDisplayName(item.key.c_str(), nullptr, &raw, 0, nullptr);
        owned.reset(raw);
        if (aborted()) return E_ABORT;
        if (FAILED(hr)) return hr;
        hr = SHBindToParent(raw, IID_PPV_ARGS(&parent), &child);
    } else {
        Microsoft::WRL::ComPtr<IShellItem> folder_item;
        hr = SHCreateItemFromParsingName(folder.parsing_name.c_str(), nullptr, IID_PPV_ARGS(&folder_item));
        if (aborted()) return E_ABORT;
        if (FAILED(hr)) return hr;
        hr = folder_item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&parent));
        if (aborted()) return E_ABORT;
        if (FAILED(hr)) return hr;
        std::wstring name = item.key; // ParseDisplayName takes a mutable buffer
        PIDLIST_RELATIVE relative{};
        hr = parent->ParseDisplayName(owner, nullptr, name.data(), nullptr, &relative, nullptr);
        owned.reset(relative);
        if (SUCCEEDED(hr) && ILFindLastID(relative) != relative) hr = E_INVALIDARG; // one level only
        child = reinterpret_cast<PCUITEMID_CHILD>(relative);
    }
    if (aborted()) return E_ABORT;
    if (FAILED(hr)) return hr;
    SFGAOF attributes = SFGAO_DROPTARGET;
    hr = parent->GetAttributesOf(1, &child, &attributes);
    if (aborted()) return E_ABORT;
    if (FAILED(hr) || !(attributes & SFGAO_DROPTARGET)) return S_FALSE;
    hr = parent->GetUIObjectOf(owner, 1, &child, IID_IDropTarget, nullptr,
                               reinterpret_cast<void**>(target.GetAddressOf()));
    if (aborted()) { target.Reset(); return E_ABORT; }
    return hr;
} catch (...) { return E_OUTOFMEMORY; }

// A right-button drag mirrors the left one; pressing the other button cancels.
class ListDragSource final : public ComRefCounted<ListDragSource, IDropSource> {
public:
    ListDragSource(file_operations::Callbacks callbacks, bool right) noexcept
        : callbacks_(callbacks), right_(right) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDropSource) return E_NOINTERFACE;
        *object = static_cast<IDropSource*>(this); AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        return Pane::list_drag_continue(right_, escape != FALSE, keys,
            callbacks_.cancel_requested && callbacks_.cancel_requested(callbacks_.context));
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
private:
    file_operations::Callbacks callbacks_;
    bool right_{};
};

} // namespace

// One OLE session per list HWND. OLE may retain this object after revocation;
// detach removes its Pane access before releasing the registered reference.
class PaneListDropTarget final : public ComRefCounted<PaneListDropTarget, IDropTarget> {
public:
    explicit PaneListDropTarget(Pane& pane) noexcept
        : pane_(&pane), host_(pane.pane_host()), guard_(&pane.reentry_guard()) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDropTarget) return E_NOINTERFACE;
        *object = static_cast<IDropTarget*>(this); AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL point,
                                         DWORD* effect) override {
        return dispatch(data, keys, point, effect, Action::enter);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL point, DWORD* effect) override {
        return dispatch(nullptr, keys, point, effect, Action::over);
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL point,
                                    DWORD* effect) override {
        return dispatch(data, keys, point, effect, Action::drop);
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        image_leave();
        if (busy_) { cancelled_ = true; return S_OK; }
        if (!pane_ && !drag_active_ && !target_ && !data_ && !engaged_) return S_OK;
        Microsoft::WRL::ComPtr<PaneListDropTarget> keep(this);
        ShellCallScope call(*guard_);
        busy_ = true;
        finish();
        busy_ = false;
        return S_OK;
    }
    void detach() noexcept {
        pane_ = nullptr; // no future callback can touch the Pane
        if (busy_) { cancelled_ = true; image_leave(); return; }
        (void)DragLeave();
    }

private:
    enum class Action { enter, over, drop };
    struct Hover final {
        int index{-1};
        std::optional<managed_list::ItemIdentity> item; // nullopt: folder background
        bool self{}; // one of the items this list is dragging
    };
    bool current() const noexcept {
        return !cancelled_ && pane_ && pane_->list_window_ &&
            pane_->list_.phase != managed_list::Phase::error &&
            pane_->file_command_is_current(snapshot_);
    }
    // Owned identity of the row under the cursor; never kept across a pump.
    Hover hovered(POINTL point) const {
        Hover hover;
        if (!pane_ || !pane_->list_window_) return hover;
        LVHITTESTINFO hit{};
        hit.pt = {point.x, point.y};
        if (!ScreenToClient(pane_->list_window_, &hit.pt)) return hover;
        const int index = ListView_HitTest(pane_->list_window_, &hit);
        if (index < 0 || !(hit.flags & LVHT_ONITEM) ||
            static_cast<std::size_t>(index) >= pane_->list_.rows.size()) return hover;
        hover.index = index;
        hover.item = pane_->list_.rows[static_cast<std::size_t>(index)].identity;
        hover.self = pane_->list_source_drag_active_ &&
            (ListView_GetItemState(pane_->list_window_, index, LVIS_SELECTED) & LVIS_SELECTED);
        return hover;
    }
    void release_provider() noexcept { // may pump
        auto target = std::move(target_);
        row_target_ = false;
        if (pane_) pane_->set_list_drop_highlight({}, -1);
        if (target) (void)target->DragLeave();
    }
    void leave_target() noexcept {
        engaged_ = false;
        self_ = false;
        hover_.reset();
        snapshot_ = {};
        release_provider();
    }
    void finish() noexcept {
        leave_target();
        data_.Reset();
        if (drag_active_) {
            drag_active_ = false;
            host_->file_drag_changed(false);
        }
    }
    // Explorer's drag image follows the cursor over every pane.
    void image_enter(IDataObject* data, POINTL point, DWORD effect) noexcept {
        image_leave();
        // Nothing to show over a hidden list. The shared image manager keeps the
        // last data object until its next session (bounded to one, as in Explorer).
        if (!pane_ || !pane_->list_window_ || !data || !IsWindowVisible(pane_->list_window_)) return;
        if (!image_ && FAILED(CoCreateInstance(CLSID_DragDropHelper, nullptr,
                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&image_)))) return;
        POINT at{point.x, point.y};
        image_entered_ = SUCCEEDED(image_->DragEnter(pane_->list_window_, data, &at, effect));
    }
    void image_over(POINTL point, DWORD effect) noexcept {
        POINT at{point.x, point.y};
        if (image_entered_) (void)image_->DragOver(&at, effect);
    }
    void image_drop(IDataObject* data, POINTL point, DWORD effect) noexcept {
        POINT at{point.x, point.y};
        if (std::exchange(image_entered_, false)) (void)image_->Drop(data, &at, effect);
        image_.Reset();
    }
    void image_leave() noexcept {
        if (std::exchange(image_entered_, false)) (void)image_->DragLeave();
        image_.Reset();
    }
    HRESULT synchronize(DWORD keys, POINTL point, DWORD allowed, DWORD& effect) {
        if (engaged_ && !current()) leave_target(); // may pump
        if (!pane_ || cancelled_ || pane_->list_.phase == managed_list::Phase::error) return E_ABORT;
        if (!engaged_ &&
            pane_->file_command_snapshot(file_operations::Command::paste, snapshot_) != S_OK) {
            snapshot_ = {};
            return E_PENDING;
        }
        const auto hover = hovered(point);
        if (engaged_ && hover.item == hover_ && hover.self == self_) return S_OK;
        const auto folder = snapshot_.folder; // owned over provider calls
        const HWND owner = pane_->window();
        file_operations::Callbacks callbacks{};
        callbacks.context = this;
        callbacks.abort_setup = [](void* raw) noexcept {
            return !static_cast<PaneListDropTarget*>(raw)->current();
        };
        Microsoft::WRL::ComPtr<IDropTarget> target;
        if (hover.item && !hover.self) {
            // The harness resolver receives the row identity key for row targets.
            (void)(pane_->list_drop_resolver_
                ? pane_->list_drop_resolver_->resolve(owner, hover.item->key, target)
                : item_drop_target(owner, folder, *hover.item, callbacks, target));
            if (!current()) return E_ABORT;
        }
        const bool row = target != nullptr;
        if (!row && !hover.self && engaged_ && target_ && !row_target_) {
            hover_ = hover.item; // still the folder background: no provider churn
            return S_OK;
        }
        if (engaged_) {
            engaged_ = false;
            release_provider(); // may pump
            if (!current()) { leave_target(); return E_ABORT; }
        }
        self_ = false;
        hover_.reset();
        if (hover.self) { // Explorer refuses a drop onto the dragged items themselves
            engaged_ = self_ = true;
            hover_ = hover.item;
            effect = DROPEFFECT_NONE;
            return S_OK;
        }
        if (!row) {
            const HRESULT hr = pane_->list_drop_resolver_
                ? pane_->list_drop_resolver_->resolve(owner, folder.parsing_name, target)
                : file_operations::folder_drop_target(owner, folder.parsing_name, callbacks, target);
            if (!current()) return E_ABORT;
            if (FAILED(hr) || !target) return FAILED(hr) ? hr : E_NOINTERFACE;
        }
        target_ = target;
        row_target_ = row;
        hover_ = hover.item;
        engaged_ = true;
        if (row) pane_->set_list_drop_highlight(hover.item, hover.index);
        effect = allowed;
        const HRESULT entered = target->DragEnter(data_.Get(), keys, point, &effect);
        if (FAILED(entered) || !current()) {
            leave_target();
            effect = DROPEFFECT_NONE;
            return FAILED(entered) ? entered : E_ABORT;
        }
        return S_OK;
    }
    HRESULT dispatch(IDataObject* data, DWORD keys, POINTL point, DWORD* effect,
                     Action action) noexcept {
        if (!effect) return E_INVALIDARG;
        const DWORD allowed = *effect;
        *effect = DROPEFFECT_NONE;
        // The image manager drops before a provider can show a menu or dialog.
        if (action == Action::drop) image_drop(data, point, last_effect_);
        if (busy_ || !pane_) return S_OK;
        const bool blocked = !pane_->active() || guard_->in_shell_call();
        Microsoft::WRL::ComPtr<PaneListDropTarget> keep(this);
        ShellCallScope call(*guard_);
        busy_ = true;
        try {
            if (action == Action::enter) image_enter(data, point, DROPEFFECT_NONE);
            if (blocked) { finish(); busy_ = false; return S_OK; }
            if (action == Action::enter) {
                finish();
                cancelled_ = false;
                data_ = data;
                if (data_ && pane_) {
                    drag_active_ = true;
                    host_->file_drag_changed(true);
                }
            }
            DWORD result = DROPEFFECT_NONE;
            HRESULT hr = E_ABORT;
            if (data_ && !cancelled_) {
                hr = synchronize(keys, point, allowed, result);
                if (SUCCEEDED(hr) && current() && target_) {
                    auto target = target_; // survives nested detach/revoke
                    if (action != Action::enter) result = allowed;
                    if (action == Action::drop) {
                        // Drop terminates the Shell target session; no DragLeave after it.
                        target_.Reset();
                        Microsoft::WRL::ComPtr<IDataObject> dropped(data);
                        hr = dropped ? target->Drop(dropped.Get(), keys, point, &result) : E_INVALIDARG;
                    } else if (action == Action::over) {
                        hr = target->DragOver(keys, point, &result);
                    } // enter already negotiated the effect in synchronize
                    if (SUCCEEDED(hr) && current()) *effect = result & allowed;
                }
            }
            if (FAILED(hr) && hr != E_ABORT && hr != E_PENDING)
                OutputDebugStringW(L"PaneDock: managed-list Shell drop target refused the request\n");
            if (target_ && !current()) leave_target();
            if (action == Action::drop || cancelled_ || !pane_ || !pane_->active()) finish();
            last_effect_ = *effect;
            if (action != Action::drop) image_over(point, *effect);
            busy_ = false;
            return S_OK; // refusal is expressed by DROPEFFECT_NONE
        } catch (...) {
            *effect = DROPEFFECT_NONE;
            cancelled_ = true;
            finish();
            busy_ = false;
            return E_OUTOFMEMORY;
        }
    }

    Pane* pane_{};
    PaneHost* host_{};
    ShellReentryGuard* guard_{};
    Pane::FileCommandSnapshot snapshot_;
    Microsoft::WRL::ComPtr<IDataObject> data_;
    Microsoft::WRL::ComPtr<IDropTarget> target_;
    Microsoft::WRL::ComPtr<IDropTargetHelper> image_;
    std::optional<managed_list::ItemIdentity> hover_;
    DWORD last_effect_{};
    bool busy_{}, cancelled_{}, drag_active_{};
    bool engaged_{}, self_{}, row_target_{}, image_entered_{};
};

HRESULT Pane::register_list_drop_target() noexcept {
    Microsoft::WRL::ComPtr<PaneListDropTarget> target;
    target.Attach(new (std::nothrow) PaneListDropTarget(*this));
    if (!target) return E_OUTOFMEMORY;
    ShellCallScope call(reentry_guard());
    const HWND window = list_window_;
    const HRESULT hr = RegisterDragDrop(window, target.Get());
    if (FAILED(hr)) return hr;
    if (!active() || list_window_ != window) {
        target->detach();
        (void)RevokeDragDrop(window);
        return E_ABORT;
    }
    list_drop_target_ = target;
    return S_OK;
}

void Pane::revoke_list_drop_target() noexcept {
    clear_list_cut(); // the list HWND goes with this target
    list_drop_highlight_.reset();
    list_drop_highlight_index_ = -1;
    auto target = std::move(list_drop_target_);
    if (!target) return;
    ShellCallScope call(reentry_guard());
    const HWND window = list_window_;
    static_cast<PaneListDropTarget*>(target.Get())->detach();
    if (window) (void)RevokeDragDrop(window);
}

void Pane::set_list_drop_highlight(std::optional<managed_list::ItemIdentity> item,
                                   int index_hint) noexcept {
    if (list_drop_highlight_ == item) return;
    const int previous = list_drop_highlight_index_;
    list_drop_highlight_ = std::move(item);
    list_drop_highlight_index_ = list_drop_highlight_ ? index_hint : -1;
    if (!list_window_) return;
    const int count = ListView_GetItemCount(list_window_);
    for (const int index : {previous, list_drop_highlight_index_})
        if (index >= 0 && index < count) ListView_RedrawItems(list_window_, index, index);
}

UINT Pane::list_transfer_state(const managed_list::Row& row) const noexcept {
    UINT state = 0;
    if (list_drop_highlight_ && *list_drop_highlight_ == row.identity) state |= LVIS_DROPHILITED;
    if (!list_cut_.items.empty() &&
        list_cut_.folder == (list_.resolved_location ? *list_.resolved_location : list_.request.location) &&
        std::binary_search(list_cut_.items.begin(), list_cut_.items.end(), row.identity))
        state |= LVIS_CUT;
    return state;
}

void Pane::note_list_cut(const FileCommandSnapshot& snapshot) noexcept try {
    clear_list_cut();
    if (!list_window_ || snapshot.items.empty() || !file_command_is_current(snapshot)) return;
    // Items are absolute parsing names (file_command_snapshot): a Shell row's key
    // is the name itself; a local row's key is the leaf under the displayed folder.
    auto prefix = snapshot.folder.parsing_name;
    if (!prefix.empty() && prefix.back() != L'\\') prefix += L'\\';
    std::vector<managed_list::ItemIdentity> items;
    items.reserve(snapshot.items.size() * 2);
    for (const auto& name : snapshot.items) {
        items.push_back({name, true});
        if (name.size() > prefix.size() && name.starts_with(prefix) &&
            name.find(L'\\', prefix.size()) == std::wstring::npos)
            items.push_back({name.substr(prefix.size()), false});
    }
    std::sort(items.begin(), items.end());
    list_cut_ = {snapshot.folder, std::move(items), GetClipboardSequenceNumber()};
    (void)AddClipboardFormatListener(list_window_); // WM_CLIPBOARDUPDATE, not polling
    InvalidateRect(list_window_, nullptr, FALSE);
} catch (...) { clear_list_cut(); }

void Pane::clear_list_cut() noexcept {
    if (list_cut_.items.empty()) return;
    list_cut_ = {};
    if (!list_window_) return;
    (void)RemoveClipboardFormatListener(list_window_);
    InvalidateRect(list_window_, nullptr, FALSE);
}

void Pane::list_clipboard_changed() noexcept {
    if (!list_cut_.items.empty() && GetClipboardSequenceNumber() != list_cut_.sequence)
        clear_list_cut();
}

HRESULT Pane::list_drag_continue(bool right_button, bool escape, DWORD keys,
                                 bool cancelled) noexcept {
    if (!right_button) return file_operations::drag_continue(escape, keys, cancelled);
    if (keys & MK_LBUTTON) return DRAGDROP_S_CANCEL;
    return file_operations::drag_continue(escape, (keys & MK_RBUTTON) ? MK_LBUTTON : 0, cancelled);
}

HRESULT Pane::begin_list_drag(POINT client_point, bool right_button) noexcept try {
    if (list_source_drag_active_ || !active() || reentry_guard().in_shell_call()) return E_ABORT;
    FileCommandSnapshot snapshot;
    const HRESULT captured = file_command_snapshot(file_operations::Command::copy, snapshot);
    if (captured != S_OK) return captured;
    struct Context {
        Pane& pane;
        FileCommandSnapshot snapshot;
    } context{*this, std::move(snapshot)};
    file_operations::Callbacks callbacks{};
    callbacks.context = &context;
    callbacks.abort_setup = [](void* raw) noexcept {
        const auto& c = *static_cast<Context*>(raw);
        return !c.pane.file_command_is_current(c.snapshot);
    };
    // Hover may switch the source tab/Group after the owned data is built.
    // Only shutdown or removal of the source HWND cancels an active OLE drag.
    callbacks.cancel_requested = [](void* raw) noexcept {
        const auto& c = *static_cast<Context*>(raw);
        return !c.pane.active() || !c.pane.list_window_;
    };
    struct DragScope final {
        bool& active;
        PaneHost& host;
        DragScope(bool& flag, PaneHost& owner) noexcept : active(flag), host(owner) {
            active = true;
            host.file_drag_changed(true);
        }
        ~DragScope() { active = false; host.file_drag_changed(false); }
    } drag(list_source_drag_active_, *pane_host());
    HRESULT hr;
    {
        Microsoft::WRL::ComPtr<IDataObject> data;
        {
            ShellCallScope call(reentry_guard());
            hr = file_operations::selection_data_object(context.snapshot.items, callbacks, data);
            // The list renders its selected rows as Explorer's drag image.
            Microsoft::WRL::ComPtr<IDragSourceHelper> image;
            if (SUCCEEDED(hr) && list_window_ && file_command_is_current(context.snapshot) &&
                SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&image))))
                (void)image->InitializeFromWindow(list_window_, &client_point, data.Get());
        }
        if (SUCCEEDED(hr) && file_command_is_current(context.snapshot)) {
            // Do not hold ShellCallScope over OLE's loop: existing hover messages
            // must be able to switch Group/tab. The drag gate protects teardown.
            callbacks.abort_setup = nullptr;
            DWORD effect{};
            if (!right_button) {
                hr = file_operations::drag_selection(data.Get(), callbacks, effect);
            } else if (callbacks.cancel_requested(callbacks.context)) {
                hr = DRAGDROP_S_CANCEL;
            } else {
                Microsoft::WRL::ComPtr<ListDragSource> source;
                source.Attach(new (std::nothrow) ListDragSource(callbacks, true));
                // OLE/Shell owns the transfer and its menu; never act on the effect.
                hr = source ? DoDragDrop(data.Get(), source.Get(),
                    DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect) : E_OUTOFMEMORY;
            }
        } else if (SUCCEEDED(hr)) hr = E_ABORT;
    }
    return hr;
} catch (...) { return E_OUTOFMEMORY; }

} // namespace panedock::app_shell
