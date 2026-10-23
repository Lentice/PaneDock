#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <windows.h>
#include <shobjidl.h>
#include <vector>

namespace panedock::explorer_host {

// Synchronous Shell integration seam. Targets are owned by the calling Pane;
// no row ordinal, menu COM object or verb escapes this boundary.
struct ContextMenuRequest final {
    std::wstring folder;
    // Inputs to the displayed folder's ParseDisplayName: local leaf keys or
    // namespace parsing identities, never display labels. Empty is background.
    std::vector<std::wstring> items;
    POINT screen{};
    bool align_above{};
    // Shift held when the menu was requested: Explorer's extended verbs.
    bool extended{};
    // Receives the chosen command's canonical verb before it is invoked;
    // returning true means the caller performs it after the menu closes
    // (e.g. "rename" starts PaneDock's own label edit). Called only while
    // the target is current; must not pump or keep the menu.
    std::function<bool(std::wstring_view verb)> intercept;
};
using MenuTargetCurrent = std::function<bool()>;
// Where opening one item should take the pane instead of starting a separate
// Explorer window: a shortcut's folder target, or an item that is itself a
// Shell folder although it is a file (a .zip). Owned values only, no PIDL.
struct FolderTarget final {
    std::wstring parsing_name;  // SIGDN_DESKTOPABSOLUTEPARSING
    // Enumerate through the Shell namespace (a .zip, a server, a virtual
    // folder) rather than the local filesystem reader.
    bool namespace_location{};
    bool operator==(const FolderTarget&) const = default;
};
class ContextMenuPresenter {
public:
    virtual ~ContextMenuPresenter() = default;
    virtual bool show(HWND owner, const ContextMenuRequest& request,
                      const MenuTargetCurrent& current) noexcept = 0;
    virtual bool invoke_default(HWND, const ContextMenuRequest&,
                                const MenuTargetCurrent&) noexcept {
        return false;
    }
    virtual std::optional<FolderTarget> resolve_folder_target(
        HWND, const ContextMenuRequest&, const MenuTargetCurrent&) noexcept {
        return std::nullopt;
    }
    // The pane's modal messages (MessageBoxW pumps); tests answer without UI.
    virtual int message_box(HWND owner, const wchar_t* text, UINT type) noexcept {
        return MessageBoxW(owner, text, L"PaneDock", type);
    }
};
bool show_native_context_menu(HWND owner, const ContextMenuRequest& request,
                              const MenuTargetCurrent& current) noexcept;
// The items' default verb, as Explorer's double-click/Enter. A type with no
// default verb (an unregistered extension) gets "Open with", as in Explorer.
bool invoke_default_shell_verb(HWND owner, const ContextMenuRequest& request,
                               const MenuTargetCurrent& current) noexcept;
// Exactly one item. Pumps (provider calls); `current` is re-checked after each.
std::optional<FolderTarget> resolve_folder_target(HWND owner,
    const ContextMenuRequest& request, const MenuTargetCurrent& current) noexcept;
// Native menu tracking with Shell extension message forwarding.
bool track_shell_context_menu(HWND owner, IContextMenu* menu,
    const ContextMenuRequest& request, const MenuTargetCurrent& current) noexcept;

} // namespace panedock::explorer_host
