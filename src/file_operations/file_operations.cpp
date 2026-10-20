#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define _WIN32_WINNT 0x0A00
#include <windows.h>

#include "file_operations/file_operations.h"

#include <atomic>
#include <cstring>
#include <cwchar>
#include <new>
#include <optional>
#include <string>
#include <vector>
#include <memory>

#include <ole2.h>
#include <shlobj.h>
#include <shellapi.h>
#include <wrl/client.h>

#include "com_ref_counted.h"

namespace panedock::file_operations {
namespace {

bool setup_aborted(const Callbacks& callbacks) noexcept {
    return callbacks.abort_setup != nullptr &&
           callbacks.abort_setup(callbacks.context);
}

void log_hresult(const wchar_t* operation, HRESULT result) noexcept {
    if (SUCCEEDED(result)) return;
    wchar_t message[160]{};
    std::swprintf(message, sizeof(message) / sizeof(message[0]),
                  L"PaneDock: %ls failed (0x%08lX)\n", operation,
                  static_cast<unsigned long>(result));
    OutputDebugStringW(message);
}

class ProgressSink final
    : public panedock::ComRefCounted<ProgressSink,
                                     IFileOperationProgressSink> {
public:
    explicit ProgressSink(Callbacks callbacks, RenameResult* rename = nullptr) noexcept
        : callbacks_(callbacks), rename_(rename) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid,
                                              void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (riid != IID_IUnknown && riid != IID_IFileOperationProgressSink)
            return E_NOINTERFACE;
        *object = static_cast<IFileOperationProgressSink*>(this);
        AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE StartOperations() override {
        if (callbacks_.started != nullptr)
            callbacks_.started(callbacks_.context);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override {
        if (callbacks_.finished != nullptr)
            callbacks_.finished(callbacks_.context);
        return S_OK;
    }

#define PANEDOCK_PRE_METHOD(name, signature) \
    HRESULT STDMETHODCALLTYPE name signature override { return cancel_result(); }
#define PANEDOCK_POST_METHOD(name, signature) \
    HRESULT STDMETHODCALLTYPE name signature override { return S_OK; }
    PANEDOCK_PRE_METHOD(PreRenameItem,
                       (DWORD, IShellItem*, LPCWSTR))
    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD, IShellItem*, LPCWSTR,
                                               HRESULT hr, IShellItem* item) override {
        if (!rename_) return S_OK;
        rename_->result = hr;
        if (FAILED(hr) || !item) return S_OK;
        try {
            const auto read = [&](SIGDN kind, std::wstring& target) {
                PWSTR text{};
                const HRESULT result = item->GetDisplayName(kind, &text);
                std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned(text, CoTaskMemFree);
                if (SUCCEEDED(result) && text) target = text;
                return result;
            };
            hr = read(SIGDN_DESKTOPABSOLUTEPARSING, rename_->parsing_name);
            if (SUCCEEDED(hr)) hr = read(SIGDN_PARENTRELATIVEPARSING, rename_->leaf_name);
            if (SUCCEEDED(hr)) hr = read(SIGDN_NORMALDISPLAY, rename_->display_name);
            if (FAILED(hr)) rename_->result = hr;
        } catch (...) { rename_->result = E_OUTOFMEMORY; }
        return S_OK;
    }
    PANEDOCK_PRE_METHOD(PreMoveItem,
                       (DWORD, IShellItem*, IShellItem*, LPCWSTR))
    PANEDOCK_POST_METHOD(PostMoveItem,
                        (DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT,
                         IShellItem*))
    PANEDOCK_PRE_METHOD(PreCopyItem,
                       (DWORD, IShellItem*, IShellItem*, LPCWSTR))
    PANEDOCK_POST_METHOD(PostCopyItem,
                        (DWORD, IShellItem*, IShellItem*, LPCWSTR, HRESULT,
                         IShellItem*))
    PANEDOCK_PRE_METHOD(PreDeleteItem, (DWORD, IShellItem*))
    PANEDOCK_POST_METHOD(PostDeleteItem,
                        (DWORD, IShellItem*, HRESULT, IShellItem*))
    PANEDOCK_PRE_METHOD(PreNewItem,
                       (DWORD, IShellItem*, LPCWSTR))
    PANEDOCK_POST_METHOD(PostNewItem,
                        (DWORD, IShellItem*, LPCWSTR, LPCWSTR, DWORD, HRESULT,
                         IShellItem*))
#undef PANEDOCK_PRE_METHOD
#undef PANEDOCK_POST_METHOD

    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT, UINT) override {
        return cancel_result();
    }
    HRESULT STDMETHODCALLTYPE ResetTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE PauseTimer() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE ResumeTimer() override { return S_OK; }

private:
    HRESULT cancel_result() const noexcept {
        return callbacks_.cancel_requested != nullptr &&
                       callbacks_.cancel_requested(callbacks_.context)
                   ? HRESULT_FROM_WIN32(ERROR_CANCELLED)
                   : S_OK;
    }

    Callbacks callbacks_;
    RenameResult* rename_{};
};

std::optional<std::uint32_t> preferred_effect(IDataObject& data,
                                               bool& malformed) noexcept {
    malformed = false;
    const CLIPFORMAT format = static_cast<CLIPFORMAT>(
        RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT));
    if (format == 0) {
        malformed = true;
        return std::nullopt;
    }
    FORMATETC requested{format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    const HRESULT hr = data.GetData(&requested, &medium);
    if (hr == DV_E_FORMATETC) return std::nullopt;
    if (FAILED(hr)) {
        malformed = true;
        return std::nullopt;
    }

    std::optional<std::uint32_t> effect;
    if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal != nullptr &&
        GlobalSize(medium.hGlobal) >= sizeof(DWORD)) {
        const void* bytes = GlobalLock(medium.hGlobal);
        if (bytes != nullptr) {
            DWORD value{};
            std::memcpy(&value, bytes, sizeof(value));
            GlobalUnlock(medium.hGlobal);
            effect = value;
        } else {
            malformed = true;
        }
    } else {
        malformed = true;
    }
    ReleaseStgMedium(&medium);
    return effect;
}

HRESULT report_effect(IDataObject& data, const wchar_t* format_name,
                      DWORD effect) noexcept {
    const CLIPFORMAT format =
        static_cast<CLIPFORMAT>(RegisterClipboardFormatW(format_name));
    if (format == 0) return HRESULT_FROM_WIN32(GetLastError());
    HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE, sizeof(effect));
    if (storage == nullptr) return E_OUTOFMEMORY;
    void* bytes = GlobalLock(storage);
    if (bytes == nullptr) {
        GlobalFree(storage);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    std::memcpy(bytes, &effect, sizeof(effect));
    GlobalUnlock(storage);
    FORMATETC target{format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = storage;
    const HRESULT hr = data.SetData(&target, &medium, TRUE);
    if (FAILED(hr)) GlobalFree(storage);
    return hr;
}

// Keep absolute PIDLs owned on this STA until the Shell array is built.
struct PidlDeleter { void operator()(ITEMIDLIST* value) const noexcept { CoTaskMemFree(value); } };
HRESULT selection_data(std::span<const std::wstring> items,
                       const Callbacks& callbacks,
                       Microsoft::WRL::ComPtr<IDataObject>& data) noexcept try {
    if (items.empty()) return E_INVALIDARG;
    std::vector<std::unique_ptr<ITEMIDLIST, PidlDeleter>> owned;
    std::vector<PCIDLIST_ABSOLUTE> ids;
    owned.reserve(items.size());
    ids.reserve(items.size());
    for (const auto& name : items) {
        if (setup_aborted(callbacks)) return E_ABORT;
        PIDLIST_ABSOLUTE raw{};
        const HRESULT hr = SHParseDisplayName(name.c_str(), nullptr, &raw, 0, nullptr);
        owned.emplace_back(raw);
        if (setup_aborted(callbacks)) return E_ABORT;
        if (FAILED(hr)) return hr; // never silently operate on a partial selection
        ids.push_back(raw);
    }
    Microsoft::WRL::ComPtr<IShellItemArray> array;
    HRESULT hr = SHCreateShellItemArrayFromIDLists(static_cast<UINT>(ids.size()),
                                                 ids.data(), &array);
    if (setup_aborted(callbacks)) return E_ABORT;
    if (FAILED(hr)) return hr;
    hr = array->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data));
    return setup_aborted(callbacks) ? E_ABORT : hr;
} catch (...) { return E_OUTOFMEMORY; }

}  // namespace

HRESULT selection_data_object(std::span<const std::wstring> items,
    const Callbacks& callbacks, Microsoft::WRL::ComPtr<IDataObject>& data) noexcept {
    data.Reset();
    return selection_data(items, callbacks, data);
}

HRESULT folder_drop_target(HWND owner, std::wstring_view folder,
    const Callbacks& callbacks, Microsoft::WRL::ComPtr<IDropTarget>& target) noexcept try {
    target.Reset();
    if (setup_aborted(callbacks)) return E_ABORT;
    const std::wstring name(folder);
    Microsoft::WRL::ComPtr<IShellItem> item;
    HRESULT hr = SHCreateItemFromParsingName(name.c_str(), nullptr, IID_PPV_ARGS(&item));
    if (setup_aborted(callbacks)) return E_ABORT;
    if (FAILED(hr)) return hr;
    Microsoft::WRL::ComPtr<IShellFolder> shell_folder;
    hr = item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&shell_folder));
    if (setup_aborted(callbacks)) return E_ABORT;
    if (FAILED(hr)) return hr;
    hr = shell_folder->CreateViewObject(owner, IID_PPV_ARGS(&target));
    if (setup_aborted(callbacks)) { target.Reset(); return E_ABORT; }
    return hr;
} catch (...) { return E_OUTOFMEMORY; }

HRESULT drag_continue(bool escape, DWORD keys, bool cancelled) noexcept {
    if (escape || cancelled) return DRAGDROP_S_CANCEL;
    return (keys & MK_LBUTTON) ? S_OK : DRAGDROP_S_DROP;
}

class SelectionDragSource final : public panedock::ComRefCounted<SelectionDragSource, IDropSource> {
public:
    explicit SelectionDragSource(Callbacks callbacks) noexcept : callbacks_(callbacks) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid != IID_IUnknown && iid != IID_IDropSource) return E_NOINTERFACE;
        *object = static_cast<IDropSource*>(this); AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        return drag_continue(escape != FALSE, keys, callbacks_.cancel_requested &&
            callbacks_.cancel_requested(callbacks_.context));
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
private:
    Callbacks callbacks_;
};

HRESULT drag_selection(IDataObject* data, const Callbacks& callbacks, DWORD& effect) noexcept {
    effect = DROPEFFECT_NONE;
    if (!data || setup_aborted(callbacks)) return E_ABORT;
    if (callbacks.cancel_requested && callbacks.cancel_requested(callbacks.context))
        return DRAGDROP_S_CANCEL;
    Microsoft::WRL::ComPtr<IDataObject> owned_data(data);
    Microsoft::WRL::ComPtr<SelectionDragSource> source;
    source.Attach(new (std::nothrow) SelectionDragSource(callbacks));
    if (!source) return E_OUTOFMEMORY;
    // OLE/Shell owns transfer and performed-effect reporting, including optimized
    // moves. Never delete source items based on the returned cursor effect.
    const HRESULT hr = DoDragDrop(owned_data.Get(), source.Get(),
        DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK, &effect);
    if (hr != DRAGDROP_S_DROP) effect = DROPEFFECT_NONE;
    return hr;
}

OperationKind select_operation(
    std::optional<std::uint32_t> preferred_effect) noexcept {
    if (!preferred_effect.has_value() ||
        *preferred_effect == DROPEFFECT_COPY)
        return OperationKind::copy;
    if (*preferred_effect == DROPEFFECT_MOVE) return OperationKind::move;
    return OperationKind::unsupported;
}

std::uint32_t clipboard_effect(bool cut) noexcept {
    return cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
}

std::optional<std::uint32_t> completed_effect(const PasteResult& result) noexcept {
    if (FAILED(result.result) || result.aborted || !result.handled ||
        result.operation == OperationKind::unsupported) return {};
    return result.operation == OperationKind::move ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
}

static PasteResult perform_operation(
    HWND owner, std::wstring_view destination_parsing_name,
    const Callbacks& callbacks, Microsoft::WRL::ComPtr<IDataObject> data,
    bool deleting) noexcept {
    PasteResult result;
    if (setup_aborted(callbacks)) { result.result = E_ABORT; return result; }
    bool malformed = false;
    const auto effect = deleting ? std::optional<std::uint32_t>{} :
        preferred_effect(*data.Get(), malformed);
    if (setup_aborted(callbacks)) {
        result.result = E_ABORT;
        return result;
    }
    result.operation = malformed ? OperationKind::unsupported
                                 : select_operation(effect);
    if (result.operation == OperationKind::unsupported) {
        result.handled = false;
        result.result = DV_E_FORMATETC;
        return result;
    }
    if (!deleting && destination_parsing_name.empty()) {
        result.result = E_INVALIDARG;
        return result;
    }

    std::wstring destination_text;
    try {
        destination_text.assign(destination_parsing_name);
    } catch (const std::bad_alloc&) {
        result.result = E_OUTOFMEMORY;
        return result;
    }
    Microsoft::WRL::ComPtr<IShellItem> destination;
    result.result = deleting ? S_OK : SHCreateItemFromParsingName(
        destination_text.c_str(), nullptr, IID_PPV_ARGS(&destination));
    if (setup_aborted(callbacks)) {
        result.result = E_ABORT;
        return result;
    }
    if (FAILED(result.result)) return result;

    Microsoft::WRL::ComPtr<IFileOperation> operation;
    result.result = CoCreateInstance(CLSID_FileOperation, nullptr,
                                     CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&operation));
    if (setup_aborted(callbacks)) {
        result.result = E_ABORT;
        return result;
    }
    if (FAILED(result.result)) return result;
    result.result = operation->SetOwnerWindow(owner);
    if (setup_aborted(callbacks)) {
        result.result = E_ABORT;
        return result;
    }
    if (FAILED(result.result)) return result;

    if (deleting) {
        result.result = operation->SetOperationFlags(FOF_ALLOWUNDO | FOFX_RECYCLEONDELETE);
        if (setup_aborted(callbacks)) { result.result = E_ABORT; return result; }
        if (FAILED(result.result)) return result;
    }

    Microsoft::WRL::ComPtr<ProgressSink> sink;
    sink.Attach(new (std::nothrow) ProgressSink(callbacks));
    if (sink == nullptr) {
        result.result = E_OUTOFMEMORY;
        return result;
    }
    DWORD cookie{};
    result.result = operation->Advise(sink.Get(), &cookie);
    if (setup_aborted(callbacks)) {
        if (SUCCEEDED(result.result)) (void)operation->Unadvise(cookie);
        result.result = E_ABORT;
        return result;
    }
    if (FAILED(result.result)) return result;

    result.result = deleting ? operation->DeleteItems(data.Get()) :
        result.operation == OperationKind::move
                        ? operation->MoveItems(data.Get(), destination.Get())
                        : operation->CopyItems(data.Get(), destination.Get());
    if (setup_aborted(callbacks)) {
        (void)operation->Unadvise(cookie);
        result.result = E_ABORT;
        return result;
    }
    if (FAILED(result.result)) {
        log_hresult(L"IFileOperation::CopyItems/MoveItems", result.result);
        (void)operation->Unadvise(cookie);
        return result;
    }
    result.handled = true;
    result.result = operation->PerformOperations();
    log_hresult(L"IFileOperation::PerformOperations", result.result);
    BOOL aborted = FALSE;
    const HRESULT aborted_result =
        operation->GetAnyOperationsAborted(&aborted);
    log_hresult(L"IFileOperation::GetAnyOperationsAborted", aborted_result);
    result.aborted = FAILED(aborted_result) || aborted != FALSE;
    const HRESULT unadvise_result = operation->Unadvise(cookie);
    log_hresult(L"IFileOperation::Unadvise", unadvise_result);

    if (const auto effect = completed_effect(result); !deleting && effect) {
        const DWORD performed = *effect;
        const HRESULT performed_result =
            report_effect(*data.Get(), CFSTR_PERFORMEDDROPEFFECT, performed);
        log_hresult(L"IDataObject::SetData(CFSTR_PERFORMEDDROPEFFECT)",
                    performed_result);
        if (result.operation == OperationKind::move) {
            const HRESULT paste_result =
                report_effect(*data.Get(), CFSTR_PASTESUCCEEDED, performed);
            log_hresult(L"IDataObject::SetData(CFSTR_PASTESUCCEEDED)",
                        paste_result);
        }
    }
    return result;
}

RenameResult rename_item(HWND owner, std::wstring_view target,
                         std::wstring_view name, const Callbacks& callbacks) noexcept try {
    RenameResult result;
    if (setup_aborted(callbacks)) { result.result = E_ABORT; return result; }
    if (target.empty() || name.empty()) { result.result = E_INVALIDARG; return result; }
    const std::wstring source(target), replacement(name);
    Microsoft::WRL::ComPtr<IShellItem> item;
    Microsoft::WRL::ComPtr<IFileOperation> operation;
    Microsoft::WRL::ComPtr<ProgressSink> sink;
    sink.Attach(new (std::nothrow) ProgressSink(callbacks, &result));
    if (!sink) { result.result = E_OUTOFMEMORY; return result; }
    HRESULT hr = SHCreateItemFromParsingName(source.c_str(), nullptr, IID_PPV_ARGS(&item));
    const auto stop = [&] {
        if (setup_aborted(callbacks)) hr = E_ABORT;
        if (FAILED(hr)) result.result = hr;
        return FAILED(hr);
    };
    if (stop()) return result;
    hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&operation));
    if (stop()) return result;
    hr = operation->SetOwnerWindow(owner);
    if (stop()) return result;
    hr = operation->SetOperationFlags(FOF_ALLOWUNDO);
    if (stop()) return result;
    hr = operation->RenameItem(item.Get(), replacement.c_str(), sink.Get());
    if (stop()) return result;
    hr = operation->PerformOperations();
    if (FAILED(hr)) result.result = hr;
    BOOL aborted{};
    hr = operation->GetAnyOperationsAborted(&aborted);
    result.aborted = FAILED(hr) || aborted != FALSE;
    return result;
} catch (...) { RenameResult result; result.result = E_OUTOFMEMORY; return result; }

HRESULT selection_to_clipboard(std::span<const std::wstring> items, bool cut,
                               const Callbacks& callbacks) noexcept {
    Microsoft::WRL::ComPtr<IDataObject> data;
    HRESULT hr = selection_data(items, callbacks, data);
    if (FAILED(hr)) return hr;
    hr = report_effect(*data.Get(), CFSTR_PREFERREDDROPEFFECT,
                       clipboard_effect(cut));
    if (setup_aborted(callbacks)) return E_ABORT;
    if (FAILED(hr)) return hr;
    hr = OleSetClipboard(data.Get());
    // Once published, materialize Shell formats before releasing our source.
    // Clipboard data then survives Group changes and application shutdown.
    if (SUCCEEDED(hr)) hr = OleFlushClipboard();
    return hr;
}

PasteResult delete_selection(HWND owner, std::span<const std::wstring> items,
                             const Callbacks& callbacks) noexcept {
    Microsoft::WRL::ComPtr<IDataObject> data;
    const HRESULT hr = selection_data(items, callbacks, data);
    if (FAILED(hr)) { PasteResult result; result.result = hr; return result; }
    return perform_operation(owner, {}, callbacks, std::move(data), true);
}

PasteResult paste_from_clipboard(HWND owner, std::wstring_view destination,
                                 const Callbacks& callbacks) noexcept {
    Microsoft::WRL::ComPtr<IDataObject> data;
    const HRESULT hr = OleGetClipboard(&data);
    if (setup_aborted(callbacks)) { PasteResult result; result.result = E_ABORT; return result; }
    if (FAILED(hr) || !data) { PasteResult result; result.result = hr; return result; }
    return perform_operation(owner, destination, callbacks, std::move(data), false);
}

}  // namespace panedock::file_operations
