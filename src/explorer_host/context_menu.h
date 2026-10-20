#pragma once

#include "explorer_host/explorer_host.h"
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
};
using MenuTargetCurrent = std::function<bool()>;
class ContextMenuPresenter {
public:
    virtual ~ContextMenuPresenter() = default;
    virtual bool show(HWND owner, const ContextMenuRequest& request,
                      const MenuTargetCurrent& current) noexcept = 0;
};
bool show_native_context_menu(HWND owner, const ContextMenuRequest& request,
                              const MenuTargetCurrent& current) noexcept;
// Shared by the legacy view's background entry and the managed-list bridge.
bool track_shell_context_menu(HWND owner, IContextMenu* menu,
    const ContextMenuRequest& request, const MenuTargetCurrent& current) noexcept;

} // namespace panedock::explorer_host
