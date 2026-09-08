#pragma once

#include "core/model.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace panedock::core {

inline constexpr std::uint32_t kSessionSchemaVersion = 1;
inline constexpr std::string_view kSessionFileName = "session.json";
inline constexpr std::string_view kSessionBackupFileName = "session.json.bak";
inline constexpr std::string_view kSessionTemporaryFileName = "session.json.tmp";

struct SessionDocument final {
    ApplicationState application;
    std::string preserved_json;
    bool clean_shutdown{true};
};

// interrupted_write means the primary was gone but the temporary file left
// behind by a write that crashed between its two renames parsed cleanly. It
// is the newest complete document on disk, so nothing was lost and there is
// nothing to warn the user about.
enum class SessionSource {
    primary,
    interrupted_write,
    backup,
    default_state
};

struct SessionReadResult final {
    SessionDocument document;
    SessionSource source{SessionSource::default_state};
    bool recovered_from_corruption{};
};

std::string serialize_session(const SessionDocument& document);
std::optional<SessionDocument> deserialize_session(std::string_view json);

using SessionDurabilityHook =
    bool (*)(const std::filesystem::path& path);

bool write_session(const std::filesystem::path& directory,
                   const SessionDocument& document,
                   SessionDurabilityHook durability_hook = nullptr);
SessionReadResult read_session(const std::filesystem::path& directory,
                               ApplicationState default_state = {});

}  // namespace panedock::core
