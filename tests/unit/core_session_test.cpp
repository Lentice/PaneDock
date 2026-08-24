#include "core/session.h"
#include "unit/test_util.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error "windows.h reached the core test seam"
#endif

namespace {
using namespace panedock::core;

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        path = std::filesystem::temp_directory_path() /
               ("panedock-session-" + std::to_string(
                   std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
    std::filesystem::path path;
};

TabState tab(std::string id) {
    return {std::move(id), {L"::{測試}", L"known", L"C:\\fallback"},
            "details", "System.ItemNameDisplay", false};
}

ApplicationState sample() {
    PaneState first{"pane-1", {tab("tab-1"), tab("tab-2")}, "tab-2"};
    PaneState second{"pane-2", {tab("tab-3")}, "tab-3"};
    GroupState group{"group-1", L"工作", LayoutTemplate::left_right, {0.375},
                     {std::move(first), std::move(second)}, "pane-2"};
    return {kSessionSchemaVersion, {std::move(group)}, "group-1",
            {10, -20, 1280, 720, true}};
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}

void test_round_trip_and_plain_json() {
    const SessionDocument original{sample(), {}};
    const std::string json = serialize_session(original);
    const auto restored = deserialize_session(json);
    EXPECT(restored.has_value());
    EXPECT(restored->application == original.application);
    EXPECT(json.front() == '{');
    EXPECT(json.find("PIDL") == std::string::npos);
    EXPECT(json.find("blob") == std::string::npos);
    EXPECT(json.find("工作") != std::string::npos);

    const auto empty = deserialize_session(serialize_session({ApplicationState{}, {}}));
    EXPECT(empty.has_value());
    EXPECT(empty->application == ApplicationState{});
}

void test_corrupt_and_invalid_documents() {
    std::string json = serialize_session({sample(), {}});
    EXPECT(!deserialize_session(json.substr(0, json.size() / 2)));

    const auto pane = json.find("\"panes\":[");
    const auto second = json.find("{\"active_tab_id\"", pane + 10);
    const auto end = json.find("}],\"active_pane_id\"", second);
    std::string invalid = json;
    invalid.erase(second - 1, end - (second - 1));
    EXPECT(!deserialize_session(invalid));

    const auto version = json.find("\"schema_version\":1");
    json.replace(version, std::string("\"schema_version\":1").size(),
                 "\"schema_version\":2");
    EXPECT(!deserialize_session(json));
}

void test_unknown_fields_survive_write_back() {
    std::string json = serialize_session({sample(), {}});
    json.insert(json.find('{') + 1, "\"future_root\":{\"enabled\":true},");
    const auto location = json.find("\"shell_location\":{");
    json.insert(location + std::string("\"shell_location\":{").size(),
                "\"future_location\":[1,2,3],");
    const auto document = deserialize_session(json);
    EXPECT(document.has_value());
    const std::string rewritten = serialize_session(*document);
    EXPECT(rewritten.find("\"future_root\":{\"enabled\":true}") != std::string::npos);
    EXPECT(rewritten.find("\"future_location\":[1,2,3]") != std::string::npos);
}

void test_read_fallbacks() {
    TemporaryDirectory directory;
    const SessionDocument good{sample(), {}};
    write_text(directory.path / kSessionFileName, "{truncated");
    write_text(directory.path / kSessionBackupFileName, serialize_session(good));
    auto result = read_session(directory.path);
    EXPECT(result.source == SessionSource::backup);
    EXPECT(result.recovered_from_corruption);
    EXPECT(result.document.application == good.application);

    write_text(directory.path / kSessionBackupFileName, "[]");
    const ApplicationState default_state{};
    result = read_session(directory.path, default_state);
    EXPECT(result.source == SessionSource::default_state);
    EXPECT(result.recovered_from_corruption);
    EXPECT(result.document.application == default_state);

    std::filesystem::remove(directory.path / kSessionFileName);
    std::filesystem::remove(directory.path / kSessionBackupFileName);
    result = read_session(directory.path, default_state);
    EXPECT(result.source == SessionSource::default_state);
    EXPECT(!result.recovered_from_corruption);
}

void test_atomic_write_and_backup() {
    TemporaryDirectory directory;
    SessionDocument first{sample(), {}};
    EXPECT(write_session(directory.path, first));
    const std::string old_primary = read_text(directory.path / kSessionFileName);
    first.application.window_placement.width = 1440;
    EXPECT(write_session(directory.path, first));
    EXPECT(read_text(directory.path / kSessionBackupFileName) == old_primary);
    const auto primary = read_session(directory.path);
    EXPECT(primary.source == SessionSource::primary);
    EXPECT(primary.document.application == first.application);

    const std::string unchanged = read_text(directory.path / kSessionFileName);
    const auto temporary = directory.path / kSessionTemporaryFileName;
    std::filesystem::create_directory(temporary);
    write_text(temporary / "keep", "occupied");
    first.application.window_placement.width = 1600;
    EXPECT(!write_session(directory.path, first));
    EXPECT(read_text(directory.path / kSessionFileName) == unchanged);
}

}  // namespace

int main() {
    test_round_trip_and_plain_json();
    test_corrupt_and_invalid_documents();
    test_unknown_fields_survive_write_back();
    test_read_fallbacks();
    test_atomic_write_and_backup();
    return panedock::test::summary("core_session");
}
