#include "explorer_host/explorer_host.h"

#include <atomic>
#include <new>
#include <string>

#include <shlwapi.h>

namespace panedock::explorer_host {
namespace {

void log_message(const wchar_t* message) noexcept {
    OutputDebugStringW(message);
    OutputDebugStringW(L"\n");
}

void log_hresult(const wchar_t* operation, HRESULT result) noexcept {
    if (FAILED(result)) {
        OutputDebugStringW(operation);
        OutputDebugStringW(L" failed\n");
    }
}

void log_service_query(REFGUID service_id) noexcept {
    wchar_t guid_text[64]{};
    OutputDebugStringW(L"ExplorerHost::QueryService service=");
    if (StringFromGUID2(service_id, guid_text, ARRAYSIZE(guid_text)) != 0) {
        OutputDebugStringW(guid_text);
    } else {
        OutputDebugStringW(L"<unformatted>");
    }
    OutputDebugStringW(L"\n");
}

class Site final : public IServiceProvider, public IExplorerBrowserEvents {
public:
    Site() noexcept = default;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                              void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;

        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid, IID_IServiceProvider)) {
            *object = static_cast<IServiceProvider*>(this);
        } else if (IsEqualIID(iid, IID_IExplorerBrowserEvents)) {
            *object = static_cast<IExplorerBrowserEvents*>(this);
        } else {
            return E_NOINTERFACE;
        }

        references_.fetch_add(1, std::memory_order_relaxed);
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining =
            references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) {
            delete this;
        }
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE QueryService(REFGUID service_id, REFIID iid,
                                            void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        log_service_query(service_id);
        (void)iid;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnNavigationPending(
        PCIDLIST_ABSOLUTE) override {
        log_message(L"ExplorerHost: navigation pending");
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnViewCreated(IShellView*) override {
        log_message(L"ExplorerHost: view created");
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnNavigationComplete(
        PCIDLIST_ABSOLUTE) override {
        log_message(L"ExplorerHost: navigation complete");
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnNavigationFailed(PCIDLIST_ABSOLUTE) override {
        log_message(L"ExplorerHost: navigation failed");
        return S_OK;
    }

private:
    std::atomic<ULONG> references_{1};
};

HRESULT reset_uninitialized_browser(
    Microsoft::WRL::ComPtr<IExplorerBrowser>& browser,
    Microsoft::WRL::ComPtr<IServiceProvider>& site,
    Microsoft::WRL::ComPtr<IExplorerBrowserEvents>& events,
    HRESULT result) noexcept {
    if (browser != nullptr) {
        (void)IUnknown_SetSite(browser.Get(), nullptr);
    }
    events.Reset();
    site.Reset();
    browser.Reset();
    return result;
}

}  // namespace

ExplorerHost::~ExplorerHost() {
    destroy();
}

HRESULT ExplorerHost::initialize(HWND parent, const RECT& rect,
                                 std::wstring_view location) {
    if (parent == nullptr || initialized_ || browser_ != nullptr) {
        return E_INVALIDARG;
    }

    HRESULT hr = CoCreateInstance(CLSID_ExplorerBrowser, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&browser_));
    if (FAILED(hr)) {
        log_hresult(L"CoCreateInstance(CLSID_ExplorerBrowser)", hr);
        return hr;
    }

    Site* site = new (std::nothrow) Site();
    if (site == nullptr) {
        browser_.Reset();
        return E_OUTOFMEMORY;
    }
    site_.Attach(static_cast<IServiceProvider*>(site));

    hr = site_.As(&events_);
    if (FAILED(hr)) {
        return reset_uninitialized_browser(browser_, site_, events_, hr);
    }

    hr = IUnknown_SetSite(browser_.Get(), site_.Get());
    if (FAILED(hr)) {
        log_hresult(L"IUnknown_SetSite", hr);
        return reset_uninitialized_browser(browser_, site_, events_, hr);
    }

    FOLDERSETTINGS settings{};
    settings.ViewMode = FVM_DETAILS;
    settings.fFlags = FWF_AUTOARRANGE | FWF_NOWEBVIEW;
    hr = browser_->Initialize(parent, &rect, &settings);
    if (FAILED(hr)) {
        log_hresult(L"IExplorerBrowser::Initialize", hr);
        return reset_uninitialized_browser(browser_, site_, events_, hr);
    }
    initialized_ = true;
    live_view_.mark_initialized();

    hr = browser_->SetOptions(EBO_NOBORDER | EBO_NOTRAVELLOG);
    if (FAILED(hr)) {
        log_hresult(L"IExplorerBrowser::SetOptions", hr);
        destroy();
        return hr;
    }

    hr = browser_->Advise(events_.Get(), &advise_cookie_);
    if (FAILED(hr)) {
        log_hresult(L"IExplorerBrowser::Advise", hr);
        destroy();
        return hr;
    }
    advised_ = true;

    std::wstring location_text(location);
    Microsoft::WRL::ComPtr<IShellItem> item;
    hr = SHCreateItemFromParsingName(location_text.c_str(), nullptr,
                                     IID_PPV_ARGS(&item));
    if (FAILED(hr)) {
        log_hresult(L"SHCreateItemFromParsingName", hr);
        destroy();
        return hr;
    }

    hr = browser_->BrowseToObject(item.Get(), SBSP_ABSOLUTE);
    if (FAILED(hr)) {
        log_hresult(L"IExplorerBrowser::BrowseToObject", hr);
        destroy();
    }
    return hr;
}

void ExplorerHost::set_rect(const RECT& rect) noexcept {
    if (initialized_ && browser_ != nullptr) {
        log_hresult(L"IExplorerBrowser::SetRect",
                    browser_->SetRect(nullptr, rect));
    }
}

void ExplorerHost::destroy() noexcept {
    if (destroying_ || !initialized_) {
        return;
    }

    destroying_ = true;
    initialized_ = false;

    if (advised_) {
        log_hresult(L"IExplorerBrowser::Unadvise",
                    browser_->Unadvise(advise_cookie_));
        advised_ = false;
        advise_cookie_ = 0;
    }

    // Detach the site while the browser is still initialized. Keeping the
    // site attached until after Destroy can leave Shell teardown re-entering
    // the host contract.
    (void)IUnknown_SetSite(browser_.Get(), nullptr);
    log_hresult(L"IExplorerBrowser::Destroy", browser_->Destroy());
    live_view_.reset();

    events_.Reset();
    site_.Reset();
    browser_.Reset();
    destroying_ = false;
}

}  // namespace panedock::explorer_host
