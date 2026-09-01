#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shobjidl.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "core/model.h"

namespace panedock::shell_core {

struct ViewModeSelection final {
    FOLDERVIEWMODE mode;
    int image_size;

    bool operator==(const ViewModeSelection&) const = default;
};

inline constexpr int kExtraLargeIconSize = 256;
inline constexpr int kLargeIconSize = 96;
inline constexpr int kMediumIconSize = 48;
inline constexpr int kSmallIconSize = 16;

std::string view_mode_name(FOLDERVIEWMODE mode, int image_size = -1);
std::optional<ViewModeSelection> parse_view_mode(std::string_view name);

core::ShellLocation capture_location(std::wstring parsing_name);

std::wstring resolve_location(const core::ShellLocation& location);

std::wstring display_text_for_parsing_name(std::wstring_view parsing_name);

std::optional<std::filesystem::path> session_directory() noexcept;

}  // namespace panedock::shell_core
