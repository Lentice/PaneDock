#include "explorer_host/explorer_host.h"

#include <algorithm>
#include <atomic>
#include <new>
#include <string>
#include <utility>

#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <windowsx.h>
#include <propkey.h>
#include <propsys.h>
#include <commctrl.h>

#include "shell_core/shell_core.h"
#include "com_ref_counted.h"

namespace panedock::explorer_host {
namespace {

constexpr UINT_PTR kContextMenuSubclassId = 0x5044;
constexpr DWORD kNavigationResolutionTimeoutMs = 1000;
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

Microsoft::WRL::ComPtr<IBindCtx> navigation_bind_context() noexcept {
    Microsoft::WRL::ComPtr<IBindCtx> result;
    if (FAILED(CreateBindCtx(0, &result))) return {};

    BIND_OPTS options{};
    options.cbStruct = sizeof(options);
    options.dwTickCountDeadline =
        GetTickCount() + kNavigationResolutionTimeoutMs;
    if (FAILED(result->SetBindOptions(&options))) return {};
    return result;
}

class ViewCallback final
    : public panedock::ComRefCounted<ViewCallback, IShellFolderViewCB> {
public:
    explicit ViewCallback(ExplorerHost* host) noexcept : host_(host) {}

    void detach() noexcept { host_ = nullptr; }

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

    void set_previous(IShellFolderViewCB* previous) noexcept {
        previous_ = previous;
    }

    HRESULT STDMETHODCALLTYPE MessageSFVCB(UINT message, WPARAM wparam,
                                            LPARAM lparam) noexcept override {
        if (host_ == nullptr) return E_NOTIMPL;
        ExplorerHost::ShellCallScope shell_call(*host_);
        if (previous_ != nullptr) {
            (void)previous_->MessageSFVCB(message, wparam, lparam);
        }
        if (message == kSfvmSelectionChanged && host_ != nullptr) {
            host_->selection_changed();
        }
        return E_NOTIMPL;
    }

private:
    ExplorerHost* host_{};
    Microsoft::WRL::ComPtr<IShellFolderViewCB> previous_;
};

class Site final
    : public panedock::ComRefCounted<Site, IServiceProvider, ICommDlgBrowser,
                                     IExplorerBrowserEvents> {
public:
    explicit Site(ExplorerHost* host) noexcept : host_(host) {}

    void detach() noexcept { host_ = nullptr; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                              void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;

        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid, IID_IServiceProvider)) {
            *object = static_cast<IServiceProvider*>(this);
        } else if (IsEqualIID(iid, IID_ICommDlgBrowser)) {
            *object = static_cast<ICommDlgBrowser*>(this);
        } else if (IsEqualIID(iid, IID_IExplorerBrowserEvents)) {
            *object = static_cast<IExplorerBrowserEvents*>(this);
        } else {
            return E_NOINTERFACE;
        }

        AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE QueryService(REFGUID service_id, REFIID iid,
                                            void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        if (host_ == nullptr) {
            return E_NOINTERFACE;
        }
        if (!IsEqualGUID(service_id, SID_SExplorerBrowserFrame)) {
            return E_NOINTERFACE;
        }
        return QueryInterface(iid, object);
    }

    HRESULT STDMETHODCALLTYPE OnDefaultCommand(IShellView*) override {
        // Let the default Shell view handle its command. The host only
        // supplies the documented frame service; it does not own navigation.
        return S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE OnStateChange(IShellView*, ULONG) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE IncludeObject(IShellView*,
                                             PCUITEMID_CHILD) override {
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnNavigationPending(
        PCIDLIST_ABSOLUTE) override {
        if (host_ == nullptr) return S_OK;
        log_message(L"ExplorerHost: navigation pending");
        host_->navigation_pending();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnViewCreated(IShellView*) override {
        if (host_ == nullptr) return S_OK;
        log_message(L"ExplorerHost: view created");
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnNavigationComplete(
        PCIDLIST_ABSOLUTE pidl) override {
        if (host_ == nullptr) return S_OK;
        log_message(L"ExplorerHost: navigation complete");
        host_->navigation_complete(pidl);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnNavigationFailed(PCIDLIST_ABSOLUTE) override {
        if (host_ == nullptr) return S_OK;
        log_message(L"ExplorerHost: navigation failed");
        host_->navigation_failed();
        return S_OK;
    }

private:
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

ExplorerHost::ShellCallScope::ShellCallScope(ExplorerHost& host) noexcept
    : host_(host) {
    host_.enter_shell_call();
}

ExplorerHost::ShellCallScope::~ShellCallScope() noexcept {
    host_.leave_shell_call();
}

void ExplorerHost::set_shell_call_callback(
    void* context, ShellCallCallback callback) noexcept {
    shell_call_context_ = context;
    shell_call_callback_ = callback;
}

ExplorerHost::NavigationGeneration ExplorerHost::begin_navigation() noexcept {
    return ++next_navigation_generation_;
}

ExplorerHost::NavigationGeneration ExplorerHost::enqueue_navigation(
    NavigationGeneration generation) noexcept {
    if (generation == 0) generation = begin_navigation();
    if (generation > next_navigation_generation_)
        next_navigation_generation_ = generation;
    try {
        navigation_requests_.push_back({generation, false});
    } catch (...) {
        log_message(L"ExplorerHost: navigation request allocation failed");
        return 0;
    }
    latest_navigation_generation_ =
        std::max(latest_navigation_generation_, generation);
    return generation;
}

void ExplorerHost::fail_enqueued_navigation(
    NavigationGeneration generation) noexcept {
    // Withdraw this request's own record. take_navigation_generation() pops
    // the front, which belongs to whichever navigation is still in flight;
    // consuming it here would make that navigation's later completion answer
    // to *this* generation -- the failure would be dropped as stale and the
    // old folder recorded as this request's destination.
    for (auto request = navigation_requests_.rbegin();
         request != navigation_requests_.rend(); ++request) {
        if (request->generation != generation) continue;
        navigation_requests_.erase(std::next(request).base());
        break;
    }
    report_navigation_failed(generation);
}

ExplorerHost::NavigationGeneration
ExplorerHost::take_navigation_generation() noexcept {
    if (navigation_requests_.empty()) {
        const auto generation = begin_navigation();
        latest_navigation_generation_ =
            std::max(latest_navigation_generation_, generation);
        return generation;
    }
    const auto generation = navigation_requests_.front().generation;
    navigation_requests_.pop_front();
    return generation;
}

void ExplorerHost::navigation_pending() noexcept {
    for (auto& request : navigation_requests_) {
        if (!request.pending_notified) {
            request.pending_notified = true;
            return;
        }
    }

    const auto generation = begin_navigation();
    try {
        navigation_requests_.push_back({generation, true});
        latest_navigation_generation_ = generation;
    } catch (...) {
        log_message(L"ExplorerHost: navigation request allocation failed");
    }
}

void ExplorerHost::enter_shell_call() noexcept {
    if (shell_call_callback_ != nullptr) {
        shell_call_callback_(shell_call_context_, true);
    }
}

void ExplorerHost::leave_shell_call() noexcept {
    if (shell_call_callback_ != nullptr) {
        shell_call_callback_(shell_call_context_, false);
    }
}

void ExplorerHost::install_context_menu_subclass() noexcept {
    remove_context_menu_subclass();
    if (current_view_ == nullptr) return;

    HWND window = nullptr;
    if (FAILED(current_view_->GetWindow(&window)) || window == nullptr) {
        return;
    }
    if (!SetWindowSubclass(window, context_menu_subclass_proc,
                           kContextMenuSubclassId,
                           reinterpret_cast<DWORD_PTR>(this))) {
        log_message(L"ExplorerHost: SetWindowSubclass failed");
        return;
    }
    context_menu_view_window_ = window;
}

void ExplorerHost::remove_context_menu_subclass() noexcept {
    if (context_menu_view_window_ != nullptr) {
        (void)RemoveWindowSubclass(context_menu_view_window_,
                                   context_menu_subclass_proc,
                                   kContextMenuSubclassId);
        context_menu_view_window_ = nullptr;
    }
}

bool ExplorerHost::show_background_context_menu(HWND owner,
                                                 LPARAM lparam) noexcept {
    POINT point{};
    if (lparam == -1) {
        if (!GetCursorPos(&point)) return false;
    } else {
        point.x = GET_X_LPARAM(lparam);
        point.y = GET_Y_LPARAM(lparam);
    }
    return show_folder_context_menu_at(owner, point, false, false);
}

bool ExplorerHost::show_folder_context_menu(HWND owner,
                                             POINT screen_point) noexcept {
    return show_folder_context_menu_at(owner, screen_point, true, true);
}

bool ExplorerHost::show_folder_context_menu_at(HWND owner, POINT screen_point,
                                                bool clear_selection,
                                                bool align_above) noexcept {
    if (owner == nullptr || current_view_ == nullptr ||
        context_menu_active_ || destroying_) {
        return false;
    }

    ShellCallScope shell_call(*this);
    Microsoft::WRL::ComPtr<IFolderView2> folder_view;
    if (FAILED(current_view_->QueryInterface(IID_PPV_ARGS(&folder_view)))) {
        return false;
    }
    if (clear_selection) {
        const HRESULT selection_result =
            current_view_->SelectItem(nullptr, SVSI_DESELECTOTHERS);
        if (FAILED(selection_result)) {
            log_hresult(L"IShellView::SelectItem(SVSI_DESELECTOTHERS)",
                        selection_result);
            return false;
        }
    } else {
        int selected = 0;
        if (FAILED(folder_view->ItemCount(SVGIO_SELECTION, &selected)) ||
            selected != 0) {
            return false;
        }
    }

    Microsoft::WRL::ComPtr<IContextMenu> menu;
    HRESULT hr = current_view_->GetItemObject(
        SVGIO_BACKGROUND, IID_PPV_ARGS(&menu));
    if (FAILED(hr)) {
        log_hresult(L"IShellView::GetItemObject(SVGIO_BACKGROUND)", hr);
        return false;
    }
    // Background verbs such as Directory\Background\shell commands need
    // the hosting view as their site; it also keeps extension callbacks on
    // the same Shell view used to create the menu.
    (void)IUnknown_SetSite(menu.Get(), current_view_.Get());

    HMENU popup = CreatePopupMenu();
    if (popup == nullptr) return false;
    hr = menu->QueryContextMenu(popup, 0, 1, 0x7fff, CMF_NORMAL);
    if (FAILED(hr)) {
        log_hresult(L"IContextMenu::QueryContextMenu", hr);
        DestroyMenu(popup);
        return false;
    }

    context_menu_ = std::move(menu);
    (void)context_menu_.As(&context_menu2_);
    (void)context_menu_.As(&context_menu3_);
    context_menu_active_ = true;
    const HWND menu_owner = context_menu_view_window_ != nullptr
                                ? context_menu_view_window_
                                : owner;
    SetForegroundWindow(owner);
    const UINT menu_flags = TPM_RETURNCMD | TPM_RIGHTBUTTON |
                            (align_above ? TPM_BOTTOMALIGN : 0);
    const int command = TrackPopupMenuEx(
        popup, menu_flags, screen_point.x, screen_point.y, menu_owner,
        nullptr);
    context_menu_active_ = false;

    if (command != 0) {
        std::string ansi_directory;
        if (!location_.parsing_name.empty()) {
            const int length = WideCharToMultiByte(
                CP_ACP, WC_NO_BEST_FIT_CHARS,
                location_.parsing_name.c_str(), -1, nullptr, 0, nullptr,
                nullptr);
            if (length > 0) {
                try {
                    ansi_directory.resize(static_cast<std::size_t>(length));
                    if (WideCharToMultiByte(
                            CP_ACP, WC_NO_BEST_FIT_CHARS,
                            location_.parsing_name.c_str(),
                            -1, ansi_directory.data(), length, nullptr,
                            nullptr) == 0) {
                        ansi_directory.clear();
                    }
                } catch (const std::bad_alloc&) {
                    ansi_directory.clear();
                }
            }
        }
        CMINVOKECOMMANDINFOEX invoke{};
        invoke.cbSize = sizeof(invoke);
        invoke.fMask = CMIC_MASK_UNICODE;
        invoke.hwnd = owner;
        invoke.lpVerb = MAKEINTRESOURCEA(command - 1);
        invoke.lpVerbW = MAKEINTRESOURCEW(command - 1);
        invoke.lpDirectory = ansi_directory.empty() ? nullptr
                                                     : ansi_directory.c_str();
        invoke.lpDirectoryW = location_.parsing_name.empty()
                                  ? nullptr
                                  : location_.parsing_name.c_str();
        invoke.nShow = SW_SHOWNORMAL;
        hr = context_menu_->InvokeCommand(
            reinterpret_cast<LPCMINVOKECOMMANDINFO>(&invoke));
        log_hresult(L"IContextMenu::InvokeCommand", hr);
    }
    DestroyMenu(popup);
    PostMessageW(menu_owner, WM_NULL, 0, 0);
    context_menu3_.Reset();
    context_menu2_.Reset();
    context_menu_.Reset();
    return true;
}

LRESULT ExplorerHost::handle_context_menu_message(HWND window,
                                                   UINT message,
                                                   WPARAM wparam,
                                                   LPARAM lparam) noexcept {
    if (context_menu_active_ && context_menu3_ != nullptr &&
        (message == WM_INITMENUPOPUP || message == WM_DRAWITEM ||
         message == WM_MEASUREITEM || message == WM_MENUCHAR)) {
        LRESULT result = 0;
        if (SUCCEEDED(context_menu3_->HandleMenuMsg2(
                message, wparam, lparam, &result))) {
            return result;
        }
    }
    if (context_menu_active_ && context_menu2_ != nullptr &&
        (message == WM_INITMENUPOPUP || message == WM_DRAWITEM ||
         message == WM_MEASUREITEM)) {
        if (SUCCEEDED(context_menu2_->HandleMenuMsg(message, wparam,
                                                    lparam))) {
            return 0;
        }
    }
    if (message == WM_CONTEXTMENU &&
        show_background_context_menu(window, lparam)) {
        return 0;
    }
    return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT CALLBACK ExplorerHost::context_menu_subclass_proc(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    UINT_PTR subclass_id, DWORD_PTR ref_data) noexcept {
    auto* host = reinterpret_cast<ExplorerHost*>(ref_data);
    if (host == nullptr || subclass_id != kContextMenuSubclassId) {
        return DefSubclassProc(window, message, wparam, lparam);
    }
    return host->handle_context_menu_message(window, message, wparam, lparam);
}

HRESULT ExplorerHost::initialize(HWND parent, const RECT& rect,
                                 const core::ShellLocation& location) {
    if (parent == nullptr || initialized_ || browser_ != nullptr) {
        return E_INVALIDARG;
    }

    parent_ = parent;
    rect_ = rect;
    location_ = location;
    item_counts_cache_.reset();

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

    // navigate() now reports its own failures through its HRESULT, but the
    // browser is live and Destroy'able either way: a folder that will not
    // resolve is an error-overlay case, not a failed realization. Returning
    // it here would leave Pane::realized() false on an initialized browser,
    // and the next apply_layout would try to initialize it a second time.
    log_hresult(L"ExplorerHost: initial navigation", navigate(location));
    return S_OK;
}

HRESULT ExplorerHost::navigate(const core::ShellLocation& location) {
    const auto generation = prepared_navigation_generation_;
    prepared_navigation_generation_ = 0;
    if (!initialized_ || browser_ == nullptr) return E_UNEXPECTED;
    const auto enqueued = enqueue_navigation(generation);
    if (enqueued == 0) return E_OUTOFMEMORY;
    ShellCallScope shell_call(*this);

    // Preserve the requested parsing name while navigation is pending or if
    // the Shell cannot currently resolve it. Session capture must not replace
    // a newly selected Group's destination with the previous Group's folder.
    location_ = location;
    const std::wstring location_text = shell_core::resolve_location(location_);
    const auto bind_context = navigation_bind_context();
    if (bind_context == nullptr) {
        log_message(L"ExplorerHost: navigation bind context unavailable");
        fail_enqueued_navigation(enqueued);
        return E_FAIL;
    }
    Microsoft::WRL::ComPtr<IShellItem> item;
    HRESULT hr = SHCreateItemFromParsingName(location_text.c_str(),
                                              bind_context.Get(),
                                              IID_PPV_ARGS(&item));
    if (FAILED(hr) && location_.fallback_path != location_text) {
        item.Reset();
        hr = SHCreateItemFromParsingName(location_.fallback_path.c_str(),
                                          bind_context.Get(),
                                          IID_PPV_ARGS(&item));
    }
    if (FAILED(hr)) {
        log_hresult(L"SHCreateItemFromParsingName", hr);
        fail_enqueued_navigation(enqueued);
        return hr;
    }

    hr = browser_->BrowseToObject(item.Get(), SBSP_ABSOLUTE);
    if (FAILED(hr)) {
        log_hresult(L"IExplorerBrowser::BrowseToObject", hr);
        fail_enqueued_navigation(enqueued);
    }
    return hr;
}

HRESULT ExplorerHost::navigate(const core::ShellLocation& location,
                               NavigationGeneration generation) {
    prepared_navigation_generation_ = generation;
    return navigate(location);
}

HRESULT ExplorerHost::refresh() {
    return navigate(location_);
}

HRESULT ExplorerHost::refresh(NavigationGeneration generation) {
    return navigate(location_, generation);
}

HRESULT ExplorerHost::set_view_mode(FOLDERVIEWMODE mode,
                                     int image_size) noexcept {
    if (browser_ == nullptr) return E_UNEXPECTED;
    Microsoft::WRL::ComPtr<IFolderView2> folder_view;
    const HRESULT hr = browser_->GetCurrentView(IID_PPV_ARGS(&folder_view));
    if (FAILED(hr)) return hr;
    const HRESULT view_mode_hr =
        folder_view->SetViewModeAndIconSize(mode, image_size);
    if (FAILED(view_mode_hr)) return view_mode_hr;
    const DWORD flags = mode == FVM_DETAILS ? 0 : FWF_NOCOLUMNHEADER;
    return folder_view->SetCurrentFolderFlags(FWF_NOCOLUMNHEADER, flags);
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

HRESULT ExplorerHost::set_sort(std::string_view column,
                               bool ascending) noexcept {
    if (browser_ == nullptr) return E_UNEXPECTED;
    if (column.empty() || column.size() >= PKEYSTR_MAX) return E_INVALIDARG;
    wchar_t column_text[PKEYSTR_MAX]{};
    for (std::size_t index = 0; index < column.size(); ++index) {
        const auto value = static_cast<unsigned char>(column[index]);
        if (value == 0 || value > 0x7f) return E_INVALIDARG;
        column_text[index] = static_cast<wchar_t>(value);
    }
    SORTCOLUMN sort{};
    HRESULT hr = PSPropertyKeyFromString(column_text, &sort.propkey);
    if (FAILED(hr)) {
        log_hresult(L"PSPropertyKeyFromString", hr);
        return hr;
    }
    sort.direction = ascending ? SORT_ASCENDING : SORT_DESCENDING;
    Microsoft::WRL::ComPtr<IFolderView2> folder_view;
    hr = browser_->GetCurrentView(IID_PPV_ARGS(&folder_view));
    if (FAILED(hr)) {
        log_hresult(L"IExplorerBrowser::GetCurrentView", hr);
        return hr;
    }
    hr = folder_view->SetSortColumns(&sort, 1);
    log_hresult(L"IFolderView2::SetSortColumns", hr);
    return hr;
}

HRESULT ExplorerHost::get_sort(std::string& column,
                               bool& ascending) const noexcept {
    if (browser_ == nullptr) return E_UNEXPECTED;
    Microsoft::WRL::ComPtr<IFolderView2> folder_view;
    HRESULT hr = browser_->GetCurrentView(IID_PPV_ARGS(&folder_view));
    if (FAILED(hr)) {
        log_hresult(L"IExplorerBrowser::GetCurrentView", hr);
        return hr;
    }
    SORTCOLUMN sort{};
    hr = folder_view->GetSortColumns(&sort, 1);
    if (FAILED(hr)) {
        log_hresult(L"IFolderView2::GetSortColumns", hr);
        return hr;
    }
    if (sort.direction != SORT_ASCENDING &&
        sort.direction != SORT_DESCENDING) {
        hr = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        log_hresult(L"IFolderView2::GetSortColumns(direction)", hr);
        return hr;
    }
    wchar_t text[PKEYSTR_MAX]{};
    hr = PSStringFromPropertyKey(sort.propkey, text, ARRAYSIZE(text));
    if (FAILED(hr)) {
        log_hresult(L"PSStringFromPropertyKey", hr);
        return hr;
    }
    char result[PKEYSTR_MAX]{};
    std::size_t length = 0;
    for (const wchar_t value : std::wstring_view(text)) {
        if (value > 0x7f) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        result[length++] = static_cast<char>(value);
    }
    try {
        column.assign(result, length);
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    ascending = sort.direction == SORT_ASCENDING;
    return S_OK;
}

HRESULT ExplorerHost::navigate_up() noexcept {
    const auto generation = prepared_navigation_generation_;
    prepared_navigation_generation_ = 0;
    if (!initialized_ || browser_ == nullptr) return E_UNEXPECTED;
    const auto enqueued = enqueue_navigation(generation);
    if (enqueued == 0) return E_OUTOFMEMORY;
    // BrowseToObject requires a non-null punk; BrowseToIDList is the
    // documented way to pass a null pidl with SBSP_PARENT.
    const HRESULT hr = browser_->BrowseToIDList(nullptr, SBSP_PARENT);
    if (FAILED(hr)) {
        log_hresult(L"IExplorerBrowser::BrowseToIDList(SBSP_PARENT)", hr);
        fail_enqueued_navigation(enqueued);
    }
    return hr;
}

HRESULT ExplorerHost::navigate_up(NavigationGeneration generation) noexcept {
    prepared_navigation_generation_ = generation;
    return navigate_up();
}

void ExplorerHost::set_navigation_callback(
    std::function<void(NavigationGeneration,
                       const core::ShellLocation&)> callback) {
    navigation_callback_ = std::move(callback);
}

void ExplorerHost::set_navigation_failed_callback(
    std::function<void(NavigationGeneration)> callback) {
    navigation_failed_callback_ = std::move(callback);
}

void ExplorerHost::set_selection_changed_callback(
    std::function<void()> callback) {
    selection_changed_callback_ = std::move(callback);
}

HRESULT ExplorerHost::item_counts(ItemCounts& counts) const noexcept {
    counts = {};
    if (current_view_ == nullptr) return E_UNEXPECTED;
    if (item_counts_cache_.has_value()) {
        counts = *item_counts_cache_;
        return S_OK;
    }

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
        item_counts_cache_ = counts;
        return S_OK;
    }
    if (counts.selected > kSelectionSizeItemLimit) {
        item_counts_cache_ = counts;
        return S_OK;
    }

    Microsoft::WRL::ComPtr<IShellItemArray> items;
    hr = folder_view->Items(SVGIO_SELECTION, IID_PPV_ARGS(&items));
    if (FAILED(hr)) return hr;
    DWORD item_count = 0;
    hr = items->GetCount(&item_count);
    if (FAILED(hr)) return hr;
    if (item_count > static_cast<UINT>(kSelectionSizeItemLimit)) {
        item_counts_cache_ = counts;
        return S_OK;
    }

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
    item_counts_cache_ = counts;
    return S_OK;
}

void ExplorerHost::selection_changed() noexcept {
    item_counts_cache_.reset();
    if (selection_changed_callback_) {
        try {
            selection_changed_callback_();
        } catch (...) {
            log_message(L"ExplorerHost: selection changed callback failed");
        }
    }
}

void ExplorerHost::set_rect(const RECT& rect, HDWP* deferred) noexcept {
    rect_ = rect;
    if (initialized_ && browser_ != nullptr) {
        // The supplied batch belongs to parent_, which is the Explorer
        // container. It must not be the app's main-window batch: Win32
        // requires every DeferWindowPos entry in one batch to share a parent.
        log_hresult(L"IExplorerBrowser::SetRect",
                    browser_->SetRect(deferred, rect));
    }
    error_overlay_.set_rect(rect);
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
    error_overlay_.set_visible(visible);
}

void ExplorerHost::focus() noexcept {
    if (error_overlay_.focus()) return;
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

void ExplorerHost::process_retry_request() noexcept {
    if (!error_overlay_.retry_requested()) return;
    error_overlay_.clear_retry_request();
    try {
        const core::ShellLocation location = location_;
        (void)navigate(location);
    } catch (...) {
        log_message(L"ExplorerHost: retry location copy failed");
    }
}

void ExplorerHost::navigation_complete(PCIDLIST_ABSOLUTE pidl) noexcept {
    const auto generation = take_navigation_generation();
    ShellCallScope shell_call(*this);
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

    remove_context_menu_subclass();
    item_counts_cache_.reset();
    current_view_.Reset();
    previous_view_callback_.Reset();
    view_callback_.Reset();
    if (browser_ != nullptr &&
        SUCCEEDED(browser_->GetCurrentView(IID_PPV_ARGS(&current_view_)))) {
        install_context_menu_subclass();
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
    if (FAILED(SHCreateItemFromIDList(pidl, IID_PPV_ARGS(&item)))) return;
    PWSTR parsing_name = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING,
                                    &parsing_name)) ||
        parsing_name == nullptr) return;
    core::ShellLocation completed_location;
    try {
        const std::wstring completed(parsing_name);
        CoTaskMemFree(parsing_name);
        parsing_name = nullptr;
        completed_location = shell_core::capture_location(completed);
    } catch (...) {
        CoTaskMemFree(parsing_name);
        return;
    }

    // The Shell event has no request identity of its own. Do not let a
    // completion from an older queued request replace the latest location or
    // reach the app model.
    if (generation != latest_navigation_generation_) return;
    location_ = completed_location;

    error_overlay_.hide();
    if (navigation_callback_) {
        try {
            navigation_callback_(generation, completed_location);
        } catch (...) {
            log_message(L"ExplorerHost: navigation callback failed");
        }
    }
    selection_changed();
}

void ExplorerHost::navigation_failed() noexcept {
    report_navigation_failed(take_navigation_generation());
}

void ExplorerHost::report_navigation_failed(
    NavigationGeneration generation) noexcept {
    ShellCallScope shell_call(*this);
    if (parent_ == nullptr || generation != latest_navigation_generation_) {
        return;
    }

    (void)error_overlay_.show(parent_, rect_, location_.parsing_name);
    if (navigation_failed_callback_) {
        try {
            navigation_failed_callback_(generation);
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

    remove_context_menu_subclass();
    context_menu_active_ = false;
    context_menu3_.Reset();
    context_menu2_.Reset();
    context_menu_.Reset();

    if (site_ != nullptr) {
        static_cast<Site*>(site_.Get())->detach();
    }
    if (view_callback_ != nullptr) {
        static_cast<ViewCallback*>(view_callback_.Get())->detach();
    }

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
    item_counts_cache_.reset();
    log_hresult(L"IExplorerBrowser::Destroy", browser_->Destroy());
    live_view_.reset();
    error_overlay_.destroy();

    events_.Reset();
    site_.Reset();
    browser_.Reset();
    parent_ = nullptr;
    location_ = {};
    navigation_requests_.clear();
    navigation_callback_ = {};
    navigation_failed_callback_ = {};
    selection_changed_callback_ = {};
    destroying_ = false;
}

}  // namespace panedock::explorer_host
