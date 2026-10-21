#include "app_shell/pane.h"
#include "app_shell/pane_host.h"
#include "com_ref_counted.h"

#include <new>

namespace panedock::app_shell {

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
        if (busy_) { cancelled_ = true; return S_OK; }
        if (!pane_ && !drag_active_ && !target_ && !data_) return S_OK;
        Microsoft::WRL::ComPtr<PaneListDropTarget> keep(this);
        ShellCallScope call(*guard_);
        busy_ = true;
        finish();
        busy_ = false;
        return S_OK;
    }
    void detach() noexcept {
        pane_ = nullptr; // no future callback can touch the Pane
        if (busy_) { cancelled_ = true; return; }
        (void)DragLeave();
    }

private:
    enum class Action { enter, over, drop };
    bool current() const noexcept {
        return !cancelled_ && pane_ &&     pane_->list_window_ && pane_->list_.phase != managed_list::Phase::error &&
            pane_->file_command_is_current(snapshot_);
    }
    void leave_target() noexcept {
        auto target = std::move(target_);
        snapshot_ = {};
        if (target) (void)target->DragLeave();
    }
    void finish() noexcept {
        leave_target();
        data_.Reset();
        if (drag_active_) {
            drag_active_ = false;
            host_->file_drag_changed(false);
        }
    }
    HRESULT synchronize(DWORD keys, POINTL point, DWORD allowed, DWORD& effect) {
        if (target_ && current()) return S_OK;
        leave_target(); // may pump: capture the new binding only afterwards
        if (!pane_ || cancelled_ ||     pane_->list_.phase == managed_list::Phase::error) return E_ABORT;
        if (pane_->file_command_snapshot(file_operations::Command::paste, snapshot_) != S_OK)
            return E_PENDING;
        const auto folder = snapshot_.folder; // owned over provider calls
        const HWND owner = pane_->window();
        Microsoft::WRL::ComPtr<IDropTarget> target;
        file_operations::Callbacks callbacks{};
        callbacks.context = this;
        callbacks.abort_setup = [](void* raw) noexcept {
            return !static_cast<PaneListDropTarget*>(raw)->current();
        };
        const HRESULT hr = pane_->list_drop_resolver_
            ? pane_->list_drop_resolver_->resolve(owner, folder.parsing_name, target)
            : file_operations::folder_drop_target(owner, folder.parsing_name, callbacks, target);
        if (!current()) return E_ABORT;
        if (FAILED(hr) || !target) return FAILED(hr) ? hr : E_NOINTERFACE;
        target_ = target;
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
        if (busy_ || !pane_) return S_OK;
        const bool blocked = !pane_->active() || guard_->in_shell_call();
        Microsoft::WRL::ComPtr<PaneListDropTarget> keep(this);
        ShellCallScope call(*guard_);
        busy_ = true;
        try {
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
    bool busy_{}, cancelled_{}, drag_active_{};
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
    auto target = std::move(list_drop_target_);
    if (!target) return;
    ShellCallScope call(reentry_guard());
    const HWND window = list_window_;
    static_cast<PaneListDropTarget*>(target.Get())->detach();
    if (window) (void)RevokeDragDrop(window);
}

HRESULT Pane::begin_list_drag() noexcept try {
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
        }
        if (SUCCEEDED(hr) && file_command_is_current(context.snapshot)) {
            // Do not hold ShellCallScope over OLE's loop: existing hover messages
            // must be able to switch Group/tab. The drag gate protects teardown.
            callbacks.abort_setup = nullptr;
            DWORD effect{};
            hr = file_operations::drag_selection(data.Get(), callbacks, effect);
        } else if (SUCCEEDED(hr)) hr = E_ABORT;
    }
    return hr;
} catch (...) { return E_OUTOFMEMORY; }

} // namespace panedock::app_shell
