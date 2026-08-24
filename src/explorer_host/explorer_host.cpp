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
    explicit Site(ExplorerHost* host) noexcept : host_(host) {}

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
        PCIDLIST_ABSOLUTE pidl) override {
        log_message(L"ExplorerHost: navigation complete");
        if (host_ != nullptr) {
            host_->navigation_complete(pidl);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnNavigationFailed(PCIDLIST_ABSOLUTE) override {
        log_message(L"ExplorerHost: navigation failed");
        if (host_ != nullptr) {
            host_->navigation_failed();
        }
        return S_OK;
    }

private:
    std::atomic<ULONG> references_{1};
    ExplorerHost* host_{nullptr};
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

HWND view_window(IExplorerBrowser* browser) noexcept {
    if (browser == nullptr) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IShellView> view;
    const HRESULT view_result =
        browser->GetCurrentView(IID_PPV_ARGS(&view));
    if (FAILED(view_result)) {
        log_hresult(L"IExplorerBrowser::GetCurrentView", view_result);
        return nullptr;
    }

    HWND window = nullptr;
    const HRESULT window_result = view->GetWindow(&window);
    if (FAILED(window_result)) {
        log_hresult(L"IShellView::GetWindow", window_result);
        return nullptr;
    }
    return window;
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

    parent_ = parent;
    rect_ = rect;
    location_ = location;

    HRESULT hr = CoCreateInstance(CLSID_ExplorerBrowser, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&browser_));
    if (FAILED(hr)) {
        log_hresult(L"CoCreateInstance(CLSID_ExplorerBrowser)", hr);
        return hr;
    }

    Site* site = new (std::nothrow) Site(this);
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

    return navigate(location);
}

HRESULT ExplorerHost::navigate(std::wstring_view location) {
    if (!initialized_ || browser_ == nullptr) return E_UNEXPECTED;

    // Preserve the requested parsing name while navigation is pending or if
    // the Shell cannot currently resolve it. Session capture must not replace
    // a newly selected Group's destination with the previous Group's folder.
    location_ = location;
    std::wstring location_text(location);
    Microsoft::WRL::ComPtr<IShellItem> item;
    HRESULT hr = SHCreateItemFromParsingName(location_text.c_str(), nullptr,
                                              IID_PPV_ARGS(&item));
    if (FAILED(hr)) {
        log_hresult(L"SHCreateItemFromParsingName", hr);
        navigation_failed();
        return S_OK;
    }

    hr = browser_->BrowseToObject(item.Get(), SBSP_ABSOLUTE);
    if (FAILED(hr)) {
        log_hresult(L"IExplorerBrowser::BrowseToObject", hr);
        navigation_failed();
    }
    return S_OK;
}

void ExplorerHost::set_rect(const RECT& rect) noexcept {
    rect_ = rect;
    if (initialized_ && browser_ != nullptr) {
        log_hresult(L"IExplorerBrowser::SetRect",
                    browser_->SetRect(nullptr, rect));
    }
    if (error_window_ != nullptr) {
        SetWindowPos(error_window_, HWND_TOP, rect.left, rect.top,
                     rect.right - rect.left, rect.bottom - rect.top,
                     SWP_NOACTIVATE);
    }
}

void ExplorerHost::set_visible(bool visible) noexcept {
    if (!initialized_ || browser_ == nullptr) {
        return;
    }

    const HWND window = view_window(browser_.Get());
    if (window != nullptr) {
        ShowWindow(window, visible ? SW_SHOW : SW_HIDE);
    }
    if (error_window_ != nullptr) {
        ShowWindow(error_window_, visible && error_visible_ ? SW_SHOW : SW_HIDE);
    }
}

void ExplorerHost::set_active(bool active) noexcept {
    if (!initialized_ || browser_ == nullptr) {
        return;
    }

    const HWND window = view_window(browser_.Get());
    if (window == nullptr) {
        return;
    }

    const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const LONG_PTR desired =
        active ? style | WS_EX_CLIENTEDGE : style & ~WS_EX_CLIENTEDGE;
    if (desired == style) {
        return;
    }

    SetWindowLongPtrW(window, GWL_EXSTYLE, desired);
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_FRAMECHANGED);
    RedrawWindow(window, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
}

void ExplorerHost::focus() noexcept {
    if (error_window_ != nullptr && error_visible_) {
        SetFocus(error_window_);
        return;
    }
    if (const HWND window = view_window(browser_.Get()); window != nullptr) {
        SetFocus(window);
    }
}

HRESULT ExplorerHost::translate_accelerator(MSG* message) noexcept {
    if (message == nullptr ||
        (message->message != WM_KEYDOWN &&
         message->message != WM_SYSKEYDOWN)) {
        return S_FALSE;
    }
    if (browser_ == nullptr) {
        return S_FALSE;
    }

    Microsoft::WRL::ComPtr<IShellView> view;
    const HRESULT view_result =
        browser_->GetCurrentView(IID_PPV_ARGS(&view));
    if (FAILED(view_result)) {
        log_hresult(L"IExplorerBrowser::GetCurrentView", view_result);
        return S_FALSE;
    }

    return view->TranslateAcceleratorW(message);
}

void ExplorerHost::navigation_complete(PCIDLIST_ABSOLUTE pidl) noexcept {
    if (pidl == nullptr) {
        return;
    }

    Microsoft::WRL::ComPtr<IShellItem> item;
    if (FAILED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&item)))) {
        return;
    }

    PWSTR parsing_name = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,
                                    &parsing_name))) {
        return;
    }
    try {
        location_.assign(parsing_name);
    } catch (...) {
        CoTaskMemFree(parsing_name);
        return;
    }
    CoTaskMemFree(parsing_name);

    error_visible_ = false;
    if (error_window_ != nullptr) {
        ShowWindow(error_window_, SW_HIDE);
    }
}

void ExplorerHost::navigation_failed() noexcept {
    if (parent_ == nullptr) {
        return;
    }

    error_visible_ = true;
    if (error_window_ == nullptr) {
        error_window_ = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"STATIC",
            L"This location is not available. Reconnect the drive and retry.",
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, rect_.left,
            rect_.top, rect_.right - rect_.left, rect_.bottom - rect_.top,
            parent_, nullptr, GetModuleHandleW(nullptr), nullptr);
    }
    if (error_window_ != nullptr) {
        ShowWindow(error_window_, SW_SHOW);
        SetWindowPos(error_window_, HWND_TOP, rect_.left, rect_.top,
                     rect_.right - rect_.left, rect_.bottom - rect_.top,
                     SWP_NOACTIVATE);
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
    if (error_window_ != nullptr) {
        DestroyWindow(error_window_);
        error_window_ = nullptr;
    }
    error_visible_ = false;
    log_hresult(L"IExplorerBrowser::Destroy", browser_->Destroy());
    live_view_.reset();

    events_.Reset();
    site_.Reset();
    browser_.Reset();
    parent_ = nullptr;
    location_.clear();
    destroying_ = false;
}

}  // namespace panedock::explorer_host
