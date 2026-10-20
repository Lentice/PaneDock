#include "explorer_host/context_menu.h"
#include <commctrl.h>
#include <shellapi.h>
#include <memory>

namespace panedock::explorer_host {
namespace {
struct MenuSession final {
    Microsoft::WRL::ComPtr<IContextMenu> menu;
    Microsoft::WRL::ComPtr<IContextMenu2> menu2;
    Microsoft::WRL::ComPtr<IContextMenu3> menu3;
    HWND owner{};
    HMENU popup{CreatePopupMenu()};
    bool subclassed{};
    ~MenuSession() {
        if (subclassed) RemoveWindowSubclass(owner, forward, reinterpret_cast<UINT_PTR>(this));
        if (popup) DestroyMenu(popup);
    }
    static LRESULT CALLBACK forward(HWND window, UINT message, WPARAM wp, LPARAM lp,
                                     UINT_PTR, DWORD_PTR data) noexcept {
        auto* session = reinterpret_cast<MenuSession*>(data);
        if (message == WM_INITMENUPOPUP || message == WM_DRAWITEM ||
            message == WM_MEASUREITEM || message == WM_MENUCHAR) {
            // COM callbacks may pump; local references survive nested cleanup.
            auto menu3 = session->menu3;
            auto menu2 = session->menu2;
            LRESULT result{};
            if (menu3 && SUCCEEDED(menu3->HandleMenuMsg2(message, wp, lp, &result))) return result;
            if (message != WM_MENUCHAR && menu2 &&
                SUCCEEDED(menu2->HandleMenuMsg(message, wp, lp))) return 0;
        }
        if (message == WM_NCDESTROY) {
            session->subclassed = false;
            RemoveWindowSubclass(window, forward, reinterpret_cast<UINT_PTR>(session));
        }
        return DefSubclassProc(window, message, wp, lp);
    }
};
struct PidlDeleter { void operator()(ITEMIDLIST* value) const noexcept { CoTaskMemFree(value); } };
} // namespace

bool track_shell_context_menu(HWND owner, IContextMenu* menu,
    const ContextMenuRequest& request, const MenuTargetCurrent& current) noexcept try {
    if (!owner || !menu || !current()) return false;
    MenuSession session;
    session.owner = owner;
    session.menu = menu;
    if (!session.popup) return false;
    (void)session.menu.As(&session.menu2);
    if (!current()) return false;
    (void)session.menu.As(&session.menu3);
    if (!current()) return false;
    session.subclassed = SetWindowSubclass(owner, MenuSession::forward,
        reinterpret_cast<UINT_PTR>(&session), reinterpret_cast<DWORD_PTR>(&session)) != FALSE;
    if (!session.subclassed) return false; // never display an unforwarded extension menu
    HRESULT hr = menu->QueryContextMenu(session.popup, 0, 1, 0x7fff, CMF_NORMAL);
    if (FAILED(hr) || !current()) return false;
    SetForegroundWindow(owner);
    if (!current()) return false;
    const int command = TrackPopupMenuEx(session.popup,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | (request.align_above ? TPM_BOTTOMALIGN : 0),
        request.screen.x, request.screen.y, owner, nullptr);
    // A close, tab or Group change during any modal pump invalidates invocation.
    if (command && current() && IsWindow(owner)) {
        std::string ansi_directory;
        const int length = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
            request.folder.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (length > 0) {
            ansi_directory.resize(static_cast<std::size_t>(length));
            if (!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, request.folder.c_str(),
                -1, ansi_directory.data(), length, nullptr, nullptr)) ansi_directory.clear();
        }
        CMINVOKECOMMANDINFOEX invoke{};
        invoke.cbSize = sizeof(invoke);
        invoke.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE;
        invoke.hwnd = owner;
        invoke.lpVerb = MAKEINTRESOURCEA(command - 1);
        invoke.lpVerbW = MAKEINTRESOURCEW(command - 1);
        invoke.lpDirectory = ansi_directory.empty() ? nullptr : ansi_directory.c_str();
        invoke.lpDirectoryW = request.folder.empty() ? nullptr : request.folder.c_str();
        invoke.nShow = SW_SHOWNORMAL;
        invoke.ptInvoke = request.screen;
        hr = menu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&invoke));
        if (FAILED(hr)) OutputDebugStringW(L"PaneDock: native context menu verb failed\n");
    }
    if (IsWindow(owner)) PostMessageW(owner, WM_NULL, 0, 0);
    return true;
} catch (...) { return false; }

bool show_native_context_menu(HWND owner, const ContextMenuRequest& request,
                              const MenuTargetCurrent& current) noexcept try {
    if (!owner || !current()) return false;
    Microsoft::WRL::ComPtr<IShellItem> item;
    HRESULT hr = SHCreateItemFromParsingName(request.folder.c_str(), nullptr, IID_PPV_ARGS(&item));
    if (!current() || FAILED(hr)) return false;
    Microsoft::WRL::ComPtr<IShellFolder> folder;
    hr = item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&folder));
    if (!current() || FAILED(hr)) return false;
    Microsoft::WRL::ComPtr<IContextMenu> menu;
    if (request.items.empty()) {
        hr = folder->CreateViewObject(owner, IID_PPV_ARGS(&menu));
    } else {
        std::vector<std::unique_ptr<ITEMIDLIST, PidlDeleter>> owned;
        std::vector<PCUITEMID_CHILD> children;
        for (auto name : request.items) {
            PIDLIST_RELATIVE raw{};
            // Parse in the displayed namespace, including virtual folders;
            // never silently substitute a filesystem parent or partial selection.
            hr = folder->ParseDisplayName(owner, nullptr, name.data(), nullptr, &raw, nullptr);
            owned.emplace_back(raw);
            if (!current() || FAILED(hr)) return false;
            // GetUIObjectOf accepts exactly one SHITEMID per child. A parser
            // may accept an absolute name but return a deeper relative chain;
            // passing that chain (or its last component) would mistarget verbs.
            if (!raw || ILIsEmpty(raw) || !ILIsEmpty(ILNext(raw))) return false;
            children.push_back(raw);
        }
        hr = folder->GetUIObjectOf(owner, static_cast<UINT>(children.size()), children.data(),
                                  IID_IContextMenu, nullptr, &menu);
    }
    if (!current() || FAILED(hr)) return false;
    return track_shell_context_menu(owner, menu.Get(), request, current);
} catch (...) { return false; }
} // namespace panedock::explorer_host
