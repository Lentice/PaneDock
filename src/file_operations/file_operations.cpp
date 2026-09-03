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

#include <ole2.h>
#include <shlobj.h>
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
    explicit ProgressSink(Callbacks callbacks) noexcept
        : callbacks_(callbacks) {}

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
    PANEDOCK_POST_METHOD(PostRenameItem,
                        (DWORD, IShellItem*, LPCWSTR, HRESULT, IShellItem*))
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

}  // namespace

OperationKind select_operation(
    std::optional<std::uint32_t> preferred_effect) noexcept {
    if (!preferred_effect.has_value() ||
        *preferred_effect == DROPEFFECT_COPY)
        return OperationKind::copy;
    if (*preferred_effect == DROPEFFECT_MOVE) return OperationKind::move;
    return OperationKind::unsupported;
}

PasteResult paste_from_clipboard(
    HWND owner, std::wstring_view destination_parsing_name,
    const Callbacks& callbacks) noexcept {
    PasteResult result;
    Microsoft::WRL::ComPtr<IDataObject> data;
    result.result = OleGetClipboard(data.GetAddressOf());
    if (setup_aborted(callbacks)) {
        result.result = E_ABORT;
        return result;
    }
    if (FAILED(result.result) || data == nullptr) return result;

    bool malformed = false;
    const auto effect = preferred_effect(*data.Get(), malformed);
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
    if (destination_parsing_name.empty()) {
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
    result.result = SHCreateItemFromParsingName(
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

    result.result = result.operation == OperationKind::move
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

    if (SUCCEEDED(result.result) && !result.aborted) {
        const DWORD performed = result.operation == OperationKind::move
                                    ? DROPEFFECT_MOVE
                                    : DROPEFFECT_COPY;
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

}  // namespace panedock::file_operations
