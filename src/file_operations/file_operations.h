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

namespace panedock::file_operations {

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

PasteResult paste_from_clipboard(HWND owner,
                                 std::wstring_view destination_parsing_name,
                                 const Callbacks& callbacks) noexcept;

}  // namespace panedock::file_operations
