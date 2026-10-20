#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <span>
#include <string>

namespace panedock::file_operations {

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

struct PasteResult final {
    HRESULT result{E_UNEXPECTED};
    bool handled{};
    bool aborted{};
    OperationKind operation{OperationKind::unsupported};
};

std::uint32_t clipboard_effect(bool cut) noexcept;
std::optional<std::uint32_t> completed_effect(const PasteResult& result) noexcept;

// Absolute parsing identities may have different parents (e.g. search results).
HRESULT selection_to_clipboard(std::span<const std::wstring> items, bool cut,
                               const Callbacks& callbacks) noexcept;
PasteResult delete_selection(HWND owner, std::span<const std::wstring> items,
                             const Callbacks& callbacks) noexcept;

PasteResult paste_from_clipboard(HWND owner,
                                 std::wstring_view destination_parsing_name,
                                 const Callbacks& callbacks) noexcept;

}  // namespace panedock::file_operations
