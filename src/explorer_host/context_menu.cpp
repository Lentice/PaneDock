#include "explorer_host/context_menu.h"
#include <commctrl.h>
#include <shellapi.h>
#include <iterator>
#include <memory>
#include <wrl/client.h>
#include <shlobj.h>
#include <shlwapi.h>

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

bool create_shell_context_menu(HWND owner, const ContextMenuRequest& request,
                               const MenuTargetCurrent& current,
                               Microsoft::WRL::ComPtr<IContextMenu>& menu,
                               std::wstring* first_path = nullptr) noexcept try {
    if (!owner || !current()) return false;
    Microsoft::WRL::ComPtr<IShellItem> item;
    HRESULT hr = SHCreateItemFromParsingName(request.folder.c_str(), nullptr, IID_PPV_ARGS(&item));
    if (!current() || FAILED(hr)) return false;
    Microsoft::WRL::ComPtr<IShellFolder> folder;
    hr = item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&folder));
    if (!current() || FAILED(hr)) return false;
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
        if (first_path && children.size() == 1) {
            STRRET name{};
            PWSTR text{};
            if (SUCCEEDED(folder->GetDisplayNameOf(children.front(), SHGDN_FORPARSING, &name)) &&
                SUCCEEDED(StrRetToStrW(&name, children.front(), &text)) && text) {
                *first_path = text;
                CoTaskMemFree(text);
            }
            if (!current()) return false;
        }
        hr = folder->GetUIObjectOf(owner, static_cast<UINT>(children.size()), children.data(),
                                   IID_IContextMenu, nullptr, &menu);
    }
    return current() && SUCCEEDED(hr) && menu != nullptr;
} catch (...) { menu.Reset(); return false; }

// Invokes a QueryContextMenu command offset exactly as the native menu does.
HRESULT invoke_menu_verb(HWND owner, IContextMenu* menu, LPCSTR verb, LPCWSTR verb_w,
                         const ContextMenuRequest& request) {
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
    invoke.lpVerb = verb;
    invoke.lpVerbW = verb_w;
    invoke.lpDirectory = ansi_directory.empty() ? nullptr : ansi_directory.c_str();
    invoke.lpDirectoryW = request.folder.empty() ? nullptr : request.folder.c_str();
    invoke.nShow = SW_SHOWNORMAL;
    invoke.ptInvoke = request.screen;
    return menu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&invoke));
}
HRESULT invoke_menu_command(HWND owner, IContextMenu* menu, UINT offset,
                            const ContextMenuRequest& request) {
    return invoke_menu_verb(owner, menu, MAKEINTRESOURCEA(offset),
                            MAKEINTRESOURCEW(offset), request);
}
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
    UINT flags = CMF_NORMAL;
    if (!request.items.empty()) flags |= CMF_CANRENAME; // the host can edit labels
    if (request.extended) flags |= CMF_EXTENDEDVERBS;
    HRESULT hr = menu->QueryContextMenu(session.popup, 0, 1, 0x7fff, flags);
    if (FAILED(hr) || !current()) return false;
    SetForegroundWindow(owner);
    if (!current()) return false;
    const int command = TrackPopupMenuEx(session.popup,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | (request.align_above ? TPM_BOTTOMALIGN : 0),
        request.screen.x, request.screen.y, owner, nullptr);
    // A close, tab or Group change during any modal pump invalidates invocation.
    if (command && current() && IsWindow(owner)) {
        wchar_t verb[64]{};
        const bool named = request.intercept && SUCCEEDED(menu->GetCommandString(
            static_cast<UINT_PTR>(command - 1), GCS_VERBW, nullptr,
            reinterpret_cast<char*>(verb), static_cast<UINT>(std::size(verb) - 1)));
        verb[std::size(verb) - 1] = L'\0';
        if (!current() || !IsWindow(owner)) {
            // The provider pumped and the target changed: invoke nothing.
        } else if (!named || !request.intercept(verb)) {
            hr = invoke_menu_command(owner, menu, static_cast<UINT>(command - 1), request);
            if (FAILED(hr)) OutputDebugStringW(L"PaneDock: native context menu verb failed\n");
        }
    }
    if (IsWindow(owner)) PostMessageW(owner, WM_NULL, 0, 0);
    return true;
} catch (...) { return false; }

bool show_native_context_menu(HWND owner, const ContextMenuRequest& request,
                              const MenuTargetCurrent& current) noexcept try {
    Microsoft::WRL::ComPtr<IContextMenu> menu;
    if (!create_shell_context_menu(owner, request, current, menu)) return false;
    return track_shell_context_menu(owner, menu.Get(), request, current);
} catch (...) { return false; }

bool invoke_default_shell_verb(HWND owner, const ContextMenuRequest& request,
                               const MenuTargetCurrent& current) noexcept try {
    if (request.items.empty()) return false;
    Microsoft::WRL::ComPtr<IContextMenu> menu;
    std::wstring first_path;
    if (!create_shell_context_menu(owner, request, current, menu, &first_path) ||
        !current()) return false;
    // Like Explorer's double-click: the item's registered default verb (Open,
    // Edit, Play, ...), invoked by command offset after QueryContextMenu. A bare
    // "open" string skips the handler setup some providers (Office) require.
    struct Popup final {
        HMENU value{CreatePopupMenu()};
        ~Popup() { if (value) DestroyMenu(value); }
    } popup;
    if (!popup.value) return false;
    HRESULT hr = menu->QueryContextMenu(popup.value, 0, 1, 0x7fff, CMF_DEFAULTONLY);
    if (FAILED(hr) || !current()) return false;
    const UINT command = GetMenuDefaultItem(popup.value, FALSE, 0);
    if (!current()) return false;
    if (command == static_cast<UINT>(-1) || command < 1) {
        // No registered default (an unknown extension): Explorer asks how to
        // open it. The static "openas" verb first; a provider that refuses a
        // verb it did not add gets the system Open With dialog for its path.
        hr = invoke_menu_verb(owner, menu.Get(), "openas", L"openas", request);
        if (SUCCEEDED(hr) || !current()) return SUCCEEDED(hr) && current();
        if (first_path.empty() || request.items.size() != 1) return false;
        OPENASINFO info{first_path.c_str(), nullptr,
                        OAIF_ALLOW_REGISTRATION | OAIF_REGISTER_EXT | OAIF_EXEC};
        hr = SHOpenWithDialog(owner, &info);
        return SUCCEEDED(hr) && current();
    }
    hr = invoke_menu_command(owner, menu.Get(), command - 1, request);
    return SUCCEEDED(hr) && current();
} catch (...) { return false; }

std::optional<FolderTarget> resolve_folder_target(HWND owner,
    const ContextMenuRequest& request, const MenuTargetCurrent& current) noexcept try {
    if (request.items.size() != 1 || !current()) return std::nullopt;
    Microsoft::WRL::ComPtr<IShellItem> parent;
    HRESULT hr = SHCreateItemFromParsingName(request.folder.c_str(), nullptr, IID_PPV_ARGS(&parent));
    if (!current() || FAILED(hr)) return std::nullopt;
    Microsoft::WRL::ComPtr<IShellFolder> folder;
    hr = parent->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&folder));
    if (!current() || FAILED(hr)) return std::nullopt;
    // The same parse as the item's verbs: in the displayed namespace, one level.
    std::wstring name = request.items.front();
    PIDLIST_RELATIVE raw{};
    hr = folder->ParseDisplayName(owner, nullptr, name.data(), nullptr, &raw, nullptr);
    std::unique_ptr<ITEMIDLIST, PidlDeleter> child(raw);
    if (!current() || FAILED(hr) || !raw || ILIsEmpty(raw) || !ILIsEmpty(ILNext(raw)))
        return std::nullopt;
    Microsoft::WRL::ComPtr<IShellItem> target;
    hr = SHCreateItemWithParent(nullptr, folder.Get(), reinterpret_cast<PCUITEMID_CHILD>(child.get()),
                                IID_PPV_ARGS(&target));
    if (!current() || FAILED(hr)) return std::nullopt;
    constexpr SFGAOF kQuery = SFGAO_LINK | SFGAO_FOLDER | SFGAO_STREAM | SFGAO_FILESYSTEM;
    SFGAOF attributes{};
    hr = target->GetAttributes(kQuery, &attributes);
    if (!current() || FAILED(hr)) return std::nullopt;
    if (attributes & SFGAO_LINK) {
        // A shortcut opens its target; only a folder target stays in the pane.
        // The stored ID list is read without IShellLink::Resolve, which can
        // search the disk or show UI; a broken link falls back to its verb.
        Microsoft::WRL::ComPtr<IShellLinkW> link;
        hr = target->BindToHandler(nullptr, BHID_SFUIObject, IID_PPV_ARGS(&link));
        if (!current() || FAILED(hr)) return std::nullopt;
        PIDLIST_ABSOLUTE linked{};
        hr = link->GetIDList(&linked);
        std::unique_ptr<ITEMIDLIST, PidlDeleter> owned_link(linked);
        if (!current() || hr != S_OK || !linked) return std::nullopt;
        target.Reset();
        hr = SHCreateItemFromIDList(linked, IID_PPV_ARGS(&target));
        if (!current() || FAILED(hr)) return std::nullopt;
        attributes = 0;
        hr = target->GetAttributes(kQuery, &attributes);
        if (!current() || FAILED(hr)) return std::nullopt;
    }
    if (!(attributes & SFGAO_FOLDER)) return std::nullopt;
    PWSTR parsing{};
    hr = target->GetDisplayName(SIGDN_DESKTOPABSOLUTEPARSING, &parsing);
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned_name(parsing, &CoTaskMemFree);
    if (!current() || FAILED(hr) || !parsing || !*parsing) return std::nullopt;
    return FolderTarget{parsing, (attributes & SFGAO_STREAM) != 0 ||
                                     (attributes & SFGAO_FILESYSTEM) == 0};
} catch (...) { return std::nullopt; }

ContextMenuPresenter& native_context_menu_presenter() noexcept {
    class Native final : public ContextMenuPresenter {
    public:
        bool show(HWND owner, const ContextMenuRequest& request,
                  const MenuTargetCurrent& current) noexcept override {
            return show_native_context_menu(owner, request, current);
        }
        bool invoke_default(HWND owner, const ContextMenuRequest& request,
                            const MenuTargetCurrent& current) noexcept override {
            return invoke_default_shell_verb(owner, request, current);
        }
        std::optional<FolderTarget> resolve_folder_target(HWND owner,
            const ContextMenuRequest& request,
            const MenuTargetCurrent& current) noexcept override {
            return explorer_host::resolve_folder_target(owner, request, current);
        }
    };
    static Native native;
    return native;
}

} // namespace panedock::explorer_host
