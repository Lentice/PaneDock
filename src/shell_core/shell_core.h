#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "core/model.h"

namespace panedock::shell_core {

core::ShellLocation location(std::wstring parsing_name);

std::wstring display_text_for_parsing_name(std::wstring_view parsing_name);

std::optional<std::filesystem::path> session_directory() noexcept;

}  // namespace panedock::shell_core
