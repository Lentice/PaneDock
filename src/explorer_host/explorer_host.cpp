#include "explorer_host/explorer_host.h"

#include <algorithm>
#include <atomic>
#include <new>
#include <string>
#include <utility>

#include <shlwapi.h>
#include <shlobj.h>
#include <propkey.h>

namespace panedock::explorer_host {
namespace {

constexpr wchar_t kErrorWindowClassName[] = L"PaneDock.ErrorPanel";
constexpr int kRetryButtonId = 1;
// ponytail: fixed 1000-item UI ceiling; raise only with measured
// non-blocking Shell enumeration.
constexpr int kSelectionSizeItemLimit = 1000;
constexpr GUID kIidShellFolderView{
    0x37a378c0, 0xf82d, 0x11ce,
    {0xae, 0x65, 0x08, 0x00, 0x2b, 0x2e, 0x12, 0x62}};
#ifndef SFVM_SELECTIONCHANGED
constexpr UINT kSfvmSelectionChanged = 8;
#else
constexpr UINT kSfvmSelectionChanged = SFVM_SELECTIONCHANGED;
#endif

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

class ViewCallback final : public IShellFolderViewCB {
public:
    explicit ViewCallback(ExplorerHost* host) noexcept : host_(host) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                              void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid, IID_IShellFolderViewCB)) {
            *object = static_cast<IShellFolderViewCB*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining =
            references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0) delete this;
        return remaining;
    }

    void set_previous(IShellFolderViewCB* previous) noexcept {
        previous_ = previous;
    }

    HRESULT STDMETHODCALLTYPE MessageSFVCB(UINT message, WPARAM wparam,
                                            LPARAM lparam) noexcept override {
        if (previous_ != nullptr) {
            (void)previous_->MessageSFVCB(message, wparam, lparam);
        }
        if (message == kSfvmSelectionChanged && host_ != nullptr) {
            host_->selection_changed();
        }
        return E_NOTIMPL;
    }

private:
    std::atomic<ULONG> references_{1};
    ExplorerHost* host_{};
    Microsoft::WRL::ComPtr<IShellFolderViewCB> previous_;
};

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

bool ExplorerHost::register_error_window_class() noexcept {
    WNDCLASSW window_class{};
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpfnWndProc = error_window_proc;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground =
        reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kErrorWindowClassName;
    return RegisterClassW(&window_class) != 0 ||
           GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

LRESULT CALLBACK ExplorerHost::error_window_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    } else if (message == WM_COMMAND && LOWORD(wparam) == kRetryButtonId &&
               HIWORD(wparam) == BN_CLICKED) {
        auto* host = reinterpret_cast<ExplorerHost*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (host != nullptr) host->retry_navigation();
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void ExplorerHost::layout_error_controls() noexcept {
    if (error_window_ == nullptr) return;
    RECT client{};
    GetClientRect(error_window_, &client);
    const int dpi = static_cast<int>(GetDpiForWindow(error_window_));
    const int padding = MulDiv(16, dpi, 96);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    const int button_width =
        std::min(MulDiv(80, dpi, 96), std::max(0, width - padding * 2));
    const int button_height =
        std::min(MulDiv(28, dpi, 96), std::max(0, height - padding * 2));
    const int button_x = std::max(0, (width - button_width) / 2);
    const int button_y = std::max(0, height - padding - button_height);
    if (error_message_ != nullptr) {
        SetWindowPos(error_message_, nullptr, padding, padding,
                     std::max(0, width - padding * 2),
                     std::max(0, button_y - padding * 2),
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (retry_button_ != nullptr) {
        SetWindowPos(retry_button_, nullptr, button_x, button_y,
                     button_width, button_height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void ExplorerHost::retry_navigation() noexcept {
    try {
        const std::wstring location = location_;
        (void)navigate(location);
    } catch (...) {
        log_message(L"ExplorerHost: retry location copy failed");
    }
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

    // PD-065: EBO_NOBORDER selects the window styles the browser gives its
    // internal host windows, and Initialize is what creates them, so setting
    // it afterwards never had any effect -- the dark 1px line along the top
    // and left of the file list was that border. SetOptions has to come
    // first. The failure path here cannot call destroy(): nothing has been
    // Initialize'd yet, so it uses the same reset_uninitialized_browser as
    // the other pre-Initialize steps.
    hr = browser_->SetOptions(EBO_NOBORDER | EBO_NOTRAVELLOG);
    if (FAILED(hr)) {
        log_hresult(L"IExplorerBrowser::SetOptions", hr);
        return reset_uninitialized_browser(browser_, site_, events_, hr);
    }

    FOLDERSETTINGS settings{};
    settings.ViewMode = FVM_DETAILS;
    // PD-065: FWF_NOCLIENTEDGE asks the view itself not to draw a client
    // edge, which is the other half of the same border.
    settings.fFlags = FWF_AUTOARRANGE | FWF_NOWEBVIEW | FWF_NOCLIENTEDGE;
    hr = browser_->Initialize(parent, &rect, &settings);
    if (FAILED(hr)) {
        log_hresult(L"IExplorerBrowser::Initialize", hr);
        return reset_uninitialized_browser(browser_, site_, events_, hr);
    }
    initialized_ = true;
    live_view_.mark_initialized();

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

HRESULT ExplorerHost::refresh() {
    return navigate(location_);
}

HRESULT ExplorerHost::set_view_mode(FOLDERVIEWMODE mode,
                                     int image_size) noexcept {
    if (browser_ == nullptr) return E_UNEXPECTED;
    Microsoft::WRL::ComPtr<IFolderView2> folder_view;
    const HRESULT hr = browser_->GetCurrentView(IID_PPV_ARGS(&folder_view));
    if (FAILED(hr)) return hr;
    return folder_view->SetViewModeAndIconSize(mode, image_size);
}

HRESULT ExplorerHost::get_view_mode(FOLDERVIEWMODE& mode,
                                    int* image_size) const noexcept {
    mode = FVM_AUTO;
    if (image_size != nullptr) *image_size = -1;
    if (browser_ == nullptr) return E_UNEXPECTED;
    Microsoft::WRL::ComPtr<IFolderView2> folder_view;
    HRESULT hr = browser_->GetCurrentView(IID_PPV_ARGS(&folder_view));
    if (FAILED(hr)) return hr;
    int value{};
    hr = folder_view->GetViewModeAndIconSize(&mode, &value);
    if (SUCCEEDED(hr) && image_size != nullptr) *image_size = value;
    return hr;
}

HRESULT ExplorerHost::navigate_up() noexcept {
    if (!initialized_ || browser_ == nullptr) return E_UNEXPECTED;
    // BrowseToObject requires a non-null punk; BrowseToIDList is the
    // documented way to pass a null pidl with SBSP_PARENT.
    const HRESULT hr = browser_->BrowseToIDList(nullptr, SBSP_PARENT);
    if (FAILED(hr)) {
        log_hresult(L"IExplorerBrowser::BrowseToIDList(SBSP_PARENT)", hr);
        navigation_failed();
    }
    return hr;
}

void ExplorerHost::set_navigation_callback(
    std::function<void(std::wstring_view)> callback) {
    navigation_callback_ = std::move(callback);
}

void ExplorerHost::set_navigation_failed_callback(
    std::function<void()> callback) {
    navigation_failed_callback_ = std::move(callback);
}

void ExplorerHost::set_selection_changed_callback(
    std::function<void()> callback) {
    selection_changed_callback_ = std::move(callback);
}

HRESULT ExplorerHost::item_counts(ItemCounts& counts) const noexcept {
    counts = {};
    if (current_view_ == nullptr) return E_UNEXPECTED;

    Microsoft::WRL::ComPtr<IFolderView2> folder_view;
    HRESULT hr = current_view_->QueryInterface(
        IID_IFolderView2, reinterpret_cast<void**>(folder_view.GetAddressOf()));
    if (FAILED(hr)) return hr;
    hr = folder_view->ItemCount(SVGIO_ALLVIEW, &counts.total);
    if (FAILED(hr)) return hr;
    hr = folder_view->ItemCount(SVGIO_SELECTION, &counts.selected);
    if (FAILED(hr)) return hr;
    if (counts.selected < 0) return E_UNEXPECTED;
    if (counts.selected == 0) {
        counts.selected_bytes_valid = true;
        return S_OK;
    }
    if (counts.selected > kSelectionSizeItemLimit) return S_OK;

    Microsoft::WRL::ComPtr<IShellItemArray> items;
    hr = folder_view->Items(SVGIO_SELECTION, IID_PPV_ARGS(&items));
    if (FAILED(hr)) return hr;
    DWORD item_count = 0;
    hr = items->GetCount(&item_count);
    if (FAILED(hr)) return hr;
    if (item_count > static_cast<UINT>(kSelectionSizeItemLimit)) return S_OK;

    counts.selected_bytes_valid = true;
    for (UINT index = 0; index < item_count; ++index) {
        Microsoft::WRL::ComPtr<IShellItem> item;
        if (FAILED(items->GetItemAt(index, &item))) continue;
        Microsoft::WRL::ComPtr<IShellItem2> item2;
        if (FAILED(item.As(&item2))) continue;
        ULONGLONG size = 0;
        if (SUCCEEDED(item2->GetUInt64(PKEY_Size, &size)))
            counts.selected_bytes +=
                static_cast<unsigned long long>(size);
    }
    return S_OK;
}

void ExplorerHost::selection_changed() noexcept {
    if (selection_changed_callback_) {
        try {
            selection_changed_callback_();
        } catch (...) {
            log_message(L"ExplorerHost: selection changed callback failed");
        }
    }
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
        layout_error_controls();
    }
}

void ExplorerHost::set_visible(bool visible) noexcept {
    if (!initialized_ || browser_ == nullptr) {
        return;
    }

    const HWND window = view_window(browser_.Get());
    if (window != nullptr) {
        // PD-038: IExplorerBrowser can finish BrowseToObject (and populate
        // the Shell view) synchronously while this view HWND is still
        // hidden -- e.g. the very first navigate() call happens inside
        // initialize(), which runs before apply_layout() ever calls
        // set_visible(true). ShowWindow(SW_SHOW) alone does not reliably
        // repaint content that was built while hidden, so force one
        // explicit redraw on the hidden -> visible transition.
        const bool was_visible = IsWindowVisible(window) != FALSE;
        ShowWindow(window, visible ? SW_SHOW : SW_HIDE);
        if (visible && !was_visible) {
            RedrawWindow(window, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        }
    }
    if (error_window_ != nullptr) {
        ShowWindow(error_window_, visible && error_visible_ ? SW_SHOW : SW_HIDE);
    }
}

void ExplorerHost::focus() noexcept {
    if (error_window_ != nullptr && error_visible_) {
        SetFocus(retry_button_ != nullptr ? retry_button_ : error_window_);
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
    // PD-038: OnNavigationComplete can fire synchronously inside
    // BrowseToObject -- for the very first navigate() call (from
    // initialize()) that happens before the view HWND is ever shown, and
    // for a re-navigate on an already-visible pane (Group/tab switch) where
    // the newly populated content otherwise sits behind a stale/empty
    // update region. Force a redraw here so the completion event itself
    // (not a later mouse move) is what makes the content appear.
    if (const HWND window = view_window(browser_.Get()); window != nullptr) {
        RedrawWindow(window, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    }

    current_view_.Reset();
    previous_view_callback_.Reset();
    view_callback_.Reset();
    if (browser_ != nullptr &&
        SUCCEEDED(browser_->GetCurrentView(IID_PPV_ARGS(&current_view_)))) {
        Microsoft::WRL::ComPtr<IShellFolderView> folder_view;
        if (SUCCEEDED(current_view_->QueryInterface(
                kIidShellFolderView,
                reinterpret_cast<void**>(folder_view.GetAddressOf())))) {
            auto* callback = new (std::nothrow) ViewCallback(this);
            if (callback != nullptr) {
                view_callback_.Attach(callback);
                (void)folder_view->SetCallback(view_callback_.Get(),
                                               &previous_view_callback_);
                callback->set_previous(previous_view_callback_.Get());
            }
        }
    }
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
    if (navigation_callback_) {
        try {
            navigation_callback_(location_);
        } catch (...) {
            log_message(L"ExplorerHost: navigation callback failed");
        }
    }
    selection_changed();
}

void ExplorerHost::navigation_failed() noexcept {
    if (parent_ == nullptr) {
        return;
    }

    error_visible_ = true;
    if (error_window_ == nullptr && register_error_window_class()) {
        error_window_ = CreateWindowExW(
            WS_EX_CLIENTEDGE, kErrorWindowClassName, L"",
            WS_CHILD | WS_CLIPCHILDREN, rect_.left,
            rect_.top, rect_.right - rect_.left, rect_.bottom - rect_.top,
            parent_, nullptr, GetModuleHandleW(nullptr), this);
    }
    if (error_window_ != nullptr) {
        if (error_message_ == nullptr) {
            error_message_ = CreateWindowExW(
                0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER,
                0, 0, 0, 0, error_window_, nullptr, GetModuleHandleW(nullptr),
                nullptr);
        }
        if (retry_button_ == nullptr) {
            retry_button_ = CreateWindowExW(
                0, L"BUTTON", L"Retry",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 0, 0, error_window_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRetryButtonId)),
                GetModuleHandleW(nullptr), nullptr);
        }
        try {
            const std::wstring message =
                L"This location is not available:\n" + location_ +
                L"\n\nReconnect the drive or check the path, then retry.";
            SetWindowTextW(error_message_, message.c_str());
        } catch (...) {
            SetWindowTextW(error_message_, L"This location is not available.");
        }
        ShowWindow(error_window_, SW_SHOW);
        SetWindowPos(error_window_, HWND_TOP, rect_.left, rect_.top,
                     rect_.right - rect_.left, rect_.bottom - rect_.top,
                     SWP_NOACTIVATE);
        layout_error_controls();
    }
    if (navigation_failed_callback_) {
        try {
            navigation_failed_callback_();
        } catch (...) {
            log_message(L"ExplorerHost: navigation failed callback failed");
        }
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
    if (current_view_ != nullptr && view_callback_ != nullptr) {
        Microsoft::WRL::ComPtr<IShellFolderView> folder_view;
        if (SUCCEEDED(current_view_->QueryInterface(
                kIidShellFolderView,
                reinterpret_cast<void**>(folder_view.GetAddressOf())))) {
            // CDefView::SetCallback stores the outgoing callback through the
            // second parameter without checking it for null, so passing
            // nullptr faults inside shell32 during teardown. Hand it a real
            // slot and drop the value instead.
            Microsoft::WRL::ComPtr<IShellFolderViewCB> replaced;
            (void)folder_view->SetCallback(previous_view_callback_.Get(),
                                           replaced.GetAddressOf());
        }
    }
    view_callback_.Reset();
    previous_view_callback_.Reset();
    current_view_.Reset();
    if (error_window_ != nullptr) {
        DestroyWindow(error_window_);
        error_window_ = nullptr;
        error_message_ = nullptr;
        retry_button_ = nullptr;
    }
    error_visible_ = false;
    log_hresult(L"IExplorerBrowser::Destroy", browser_->Destroy());
    live_view_.reset();

    events_.Reset();
    site_.Reset();
    browser_.Reset();
    parent_ = nullptr;
    location_.clear();
    navigation_callback_ = {};
    navigation_failed_callback_ = {};
    selection_changed_callback_ = {};
    destroying_ = false;
}

}  // namespace panedock::explorer_host
