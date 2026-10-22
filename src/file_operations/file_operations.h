#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <oleidl.h>
#include <wrl/client.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <span>
#include <string>

namespace panedock::file_operations {

// Shell boundary injectable by the Pane harness; production uses the folder's
// own IDropTarget. No transfer or modifier policy is implemented here.
class DropTargetResolver {
public:
    virtual ~DropTargetResolver() = default;
    virtual HRESULT resolve(HWND owner, std::wstring_view folder,
        Microsoft::WRL::ComPtr<IDropTarget>& target) noexcept = 0;
};
struct Callbacks;
HRESULT folder_drop_target(HWND owner, std::wstring_view folder,
    const Callbacks& callbacks, Microsoft::WRL::ComPtr<IDropTarget>& target) noexcept;
HRESULT drag_continue(bool escape, DWORD keys, bool cancelled) noexcept;

enum class Command { copy, cut, paste, delete_items };

enum class OperationKind { copy, move, unsupported };

OperationKind select_operation(
    std::optional<std::uint32_t> preferred_effect) noexcept;

struct Callbacks final {
    void* context{};
    void (*started)(void* context) noexcept{};
    void (*finished)(void* context) noexcept{};
    bool (*cancel_requested)(void* context) noexcept{};
    bool (*abort_setup)(void* context) noexcept{};
};

HRESULT selection_data_object(std::span<const std::wstring> items,
    const Callbacks& callbacks, Microsoft::WRL::ComPtr<IDataObject>& data) noexcept;
HRESULT drag_selection(IDataObject* data, const Callbacks& callbacks,
                       DWORD& effect) noexcept;

struct PasteResult final {
    HRESULT result{E_UNEXPECTED};
    bool handled{};
    bool aborted{};
    OperationKind operation{OperationKind::unsupported};
};

std::uint32_t clipboard_effect(bool cut) noexcept;
std::optional<std::uint32_t> completed_effect(const PasteResult& result) noexcept;

struct RenameResult final {
    HRESULT result{E_UNEXPECTED};
    bool aborted{};
    std::wstring parsing_name{}, leaf_name{}, display_name{};
};
RenameResult rename_item(HWND owner, std::wstring_view item,
                         std::wstring_view name, const Callbacks& callbacks) noexcept;

// Absolute parsing identities may have different parents (e.g. search results).
HRESULT selection_to_clipboard(std::span<const std::wstring> items, bool cut,
                               const Callbacks& callbacks) noexcept;
PasteResult delete_selection(HWND owner, std::span<const std::wstring> items,
                             const Callbacks& callbacks) noexcept;

PasteResult paste_from_clipboard(HWND owner,
                                 std::wstring_view destination_parsing_name,
                                 const Callbacks& callbacks) noexcept;

}  // namespace panedock::file_operations
