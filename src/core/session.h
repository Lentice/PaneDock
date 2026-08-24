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
};

enum class SessionSource { primary, backup, default_state };

struct SessionReadResult final {
    SessionDocument document;
    SessionSource source{SessionSource::default_state};
    bool recovered_from_corruption{};
};

std::string serialize_session(const SessionDocument& document);
std::optional<SessionDocument> deserialize_session(std::string_view json);

bool write_session(const std::filesystem::path& directory,
                   const SessionDocument& document);
SessionReadResult read_session(const std::filesystem::path& directory,
                               ApplicationState default_state = {});

}  // namespace panedock::core
