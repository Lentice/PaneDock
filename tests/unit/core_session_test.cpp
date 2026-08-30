#include "core/session.h"
#include "unit/test_util.h"

#include <array>
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
            "FVM_ICON:48", "System.ItemNameDisplay", false, {}, 0};
}

ApplicationState sample() {
    PaneState first{"pane-1", {tab("tab-1"), tab("tab-2")}, "tab-2"};
    PaneState second{"pane-2", {tab("tab-3")}, "tab-3"};
    GroupState group{"group-1", L"工作", LayoutTemplate::left_right, {0.375},
                     {std::move(first), std::move(second)}, "pane-2"};
    return {kSessionSchemaVersion, {std::move(group)}, "group-1",
            {10, -20, 1280, 720, true},
            kDefaultSidebarWidth,
            {{L"C:\\Users", L"", L"C:\\Users"},
             {L"\\\\server\\share", L"", L""}}};
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << text;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}

enum class DurabilityFailure { none, temporary, backup };

struct DurabilityProbe final {
    std::filesystem::path primary;
    std::array<std::filesystem::path, 2> calls{};
    std::array<std::string, 2> primary_snapshots{};
    std::size_t call_count{};
    DurabilityFailure failure{DurabilityFailure::none};
};

DurabilityProbe* active_probe = nullptr;

bool fake_durability_hook(const std::filesystem::path& path) {
    if (active_probe == nullptr) return false;
    if (active_probe->call_count < active_probe->calls.size()) {
        const std::size_t index = active_probe->call_count++;
        active_probe->calls[index] = path.filename();
        active_probe->primary_snapshots[index] =
            read_text(active_probe->primary);
    }
    const auto name = path.filename();
    if (active_probe->failure == DurabilityFailure::temporary &&
        name == std::filesystem::path(kSessionTemporaryFileName)) return false;
    return active_probe->failure != DurabilityFailure::backup ||
           name != std::filesystem::path(kSessionBackupTemporaryFileName);
}

void test_round_trip_and_plain_json() {
    const SessionDocument original{sample(), {}, false};
    const std::string json = serialize_session(original);
    const auto restored = deserialize_session(json);
    EXPECT(restored.has_value());
    EXPECT(restored->application == original.application);
    EXPECT(!restored->clean_shutdown);
    EXPECT(json.find("\"clean_shutdown\":false") != std::string::npos);
    EXPECT(json.front() == '{');
    EXPECT(json.find("PIDL") == std::string::npos);
    EXPECT(json.find("blob") == std::string::npos);
    EXPECT(json.find("工作") != std::string::npos);
    EXPECT(json.find("\"view_mode\":\"FVM_ICON:48\"") != std::string::npos);
    EXPECT(json.find("\"pinned_locations\"") != std::string::npos);

    std::string legacy = json;
    const std::string current_view_mode = "\"view_mode\":\"FVM_ICON:48\"";
    const std::size_t view_mode_position = legacy.find(current_view_mode);
    EXPECT(view_mode_position != std::string::npos);
    if (view_mode_position != std::string::npos) {
        legacy.replace(view_mode_position, current_view_mode.size(),
                       "\"view_mode\":\"FVM_SMALLICON\"");
        const auto legacy_document = deserialize_session(legacy);
        EXPECT(legacy_document.has_value());
        if (legacy_document.has_value())
            EXPECT(legacy_document->application.groups.front().panes.front()
                       .tabs.front().view_mode == "FVM_SMALLICON");
    }

    const auto clean = deserialize_session(serialize_session(
        SessionDocument{sample(), {}, true}));
    EXPECT(clean.has_value());
    EXPECT(clean->clean_shutdown);

    const auto empty = deserialize_session(serialize_session({ApplicationState{}, {}}));
    EXPECT(empty.has_value());
    EXPECT(empty->application == ApplicationState{});
    EXPECT(empty->clean_shutdown);
}

void test_optional_sidebar_width() {
    ApplicationState original = sample();
    original.sidebar_width = 320;
    const std::string json = serialize_session({original, {}});
    const auto restored = deserialize_session(json);
    EXPECT(restored.has_value());
    if (restored.has_value())
        EXPECT(restored->application.sidebar_width == 320);

    std::string legacy = json;
    const auto start = legacy.find("\"sidebar_width\":320");
    const auto end = start == std::string::npos ? std::string::npos
                                                : legacy.find(',', start);
    EXPECT(start != std::string::npos);
    EXPECT(end != std::string::npos);
    if (start != std::string::npos && end != std::string::npos) {
        legacy.erase(start, end - start + 1);
        const auto legacy_document = deserialize_session(legacy);
        EXPECT(legacy_document.has_value());
        if (legacy_document.has_value())
            EXPECT(legacy_document->application.sidebar_width ==
                   kDefaultSidebarWidth);
    }
}

void test_optional_pinned_locations() {
    std::string json = serialize_session({sample(), {}});
    const auto start = json.find(",\"pinned_locations\":[");
    const auto end = json.find("],\"schema_version\"", start);
    EXPECT(start != std::string::npos);
    EXPECT(end != std::string::npos);
    if (start == std::string::npos || end == std::string::npos) return;
    json.erase(start, end - start + 1);
    const auto restored = deserialize_session(json);
    EXPECT(restored.has_value());
    if (restored.has_value())
        EXPECT(restored->application.pinned_locations.empty());
}

void test_two_over_one_round_trip() {
    ApplicationState original = sample();
    auto& group = original.groups.front();
    group.layout_template = LayoutTemplate::two_over_one;
    group.divider_ratios = {0.25, 0.75};
    group.panes.push_back({"pane-3", {tab("tab-4")}, "tab-4"});
    EXPECT(is_valid(original));

    const std::string json = serialize_session({original, {}, false});
    EXPECT(json.find("\"layout_template\":\"two_over_one\"") !=
           std::string::npos);
    const auto restored = deserialize_session(json);
    EXPECT(restored.has_value());
    if (restored.has_value()) EXPECT(restored->application == original);
}

void test_new_three_pane_layout_round_trips() {
    struct Case final {
        LayoutTemplate layout;
        std::string_view identity;
    };
    constexpr Case cases[]{{LayoutTemplate::one_over_two, "one_over_two"},
                           {LayoutTemplate::two_beside_one,
                            "two_beside_one"}};
    for (const auto& item : cases) {
        ApplicationState original = sample();
        auto& group = original.groups.front();
        group.layout_template = item.layout;
        group.divider_ratios = {0.25, 0.75};
        group.panes.push_back({"pane-3", {tab("tab-4")}, "tab-4"});
        EXPECT(is_valid(original));

        const std::string json = serialize_session({original, {}, false});
        EXPECT(json.find("\"layout_template\":\"" +
                         std::string(item.identity) + "\"") !=
               std::string::npos);
        const auto restored = deserialize_session(json);
        EXPECT(restored.has_value());
        if (restored.has_value()) EXPECT(restored->application == original);
    }
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
    std::string json = serialize_session({sample(), {}, true});
    const auto clean_field = json.find("\"clean_shutdown\":true,");
    EXPECT(clean_field != std::string::npos);
    if (clean_field == std::string::npos) return;
    json.erase(clean_field, std::string("\"clean_shutdown\":true,").size());
    json.insert(json.find('{') + 1, "\"future_root\":{\"enabled\":true},");
    const auto location = json.find("\"shell_location\":{");
    json.insert(location + std::string("\"shell_location\":{").size(),
                "\"future_location\":[1,2,3],");
    const auto document = deserialize_session(json);
    EXPECT(document.has_value());
    EXPECT(document->clean_shutdown);
    const std::string rewritten = serialize_session(*document);
    EXPECT(rewritten.find("\"future_root\":{\"enabled\":true}") != std::string::npos);
    EXPECT(rewritten.find("\"future_location\":[1,2,3]") != std::string::npos);

    const auto pinned = json.find("\"pinned_locations\":[");
    EXPECT(pinned != std::string::npos);
    if (pinned != std::string::npos) {
        const auto object_start = json.find('{', pinned);
        json.insert(object_start + 1, "\"future_pinned\":true,");
        const auto pinned_document = deserialize_session(json);
        EXPECT(pinned_document.has_value());
        if (pinned_document.has_value()) {
            const std::string pinned_rewritten =
                serialize_session(*pinned_document);
            EXPECT(pinned_rewritten.find("\"future_pinned\":true") !=
                   std::string::npos);
        }
    }
}

void test_clean_shutdown_type_mismatch_defaults_true() {
    std::string json = serialize_session({sample(), {}, false});
    const auto clean_field = json.find("\"clean_shutdown\":false");
    EXPECT(clean_field != std::string::npos);
    if (clean_field == std::string::npos) return;
    json.replace(clean_field, std::string("\"clean_shutdown\":false").size(),
                 "\"clean_shutdown\":\"unexpected\"");
    const auto document = deserialize_session(json);
    EXPECT(document.has_value());
    EXPECT(document->clean_shutdown);
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

void test_corrupt_primary_does_not_replace_good_backup() {
    TemporaryDirectory directory;
    SessionDocument document{sample(), {}, true};
    EXPECT(write_session(directory.path, document));
    document.application.window_placement.width = 1440;
    EXPECT(write_session(directory.path, document));
    const std::string good_backup =
        read_text(directory.path / kSessionBackupFileName);
    EXPECT(deserialize_session(good_backup).has_value());

    write_text(directory.path / kSessionFileName, "not json at all");
    document.application.window_placement.width = 1600;
    EXPECT(write_session(directory.path, document));
    EXPECT(read_text(directory.path / kSessionBackupFileName) == good_backup);
    EXPECT(deserialize_session(
               read_text(directory.path / kSessionBackupFileName))
               .has_value());
}

void test_durability_hook_order_and_failure() {
    TemporaryDirectory directory;
    SessionDocument document{sample(), {}};
    EXPECT(write_session(directory.path, document));
    const auto primary = directory.path / kSessionFileName;
    const std::string old_primary = read_text(primary);

    document.application.window_placement.width = 1440;
    DurabilityProbe probe{primary};
    active_probe = &probe;
    EXPECT(write_session(directory.path, document, fake_durability_hook));
    active_probe = nullptr;
    EXPECT(probe.call_count == 2);
    EXPECT(probe.calls[0] == std::filesystem::path(kSessionTemporaryFileName));
    EXPECT(probe.calls[1] ==
           std::filesystem::path(kSessionBackupTemporaryFileName));
    EXPECT(probe.primary_snapshots[0] == old_primary);
    EXPECT(probe.primary_snapshots[1] == old_primary);
    const std::string new_primary = read_text(primary);
    EXPECT(new_primary != old_primary);

    DurabilityProbe temporary_failure{primary};
    temporary_failure.failure = DurabilityFailure::temporary;
    active_probe = &temporary_failure;
    EXPECT(!write_session(directory.path, document, fake_durability_hook));
    active_probe = nullptr;
    EXPECT(temporary_failure.call_count == 1);
    EXPECT(read_text(primary) == new_primary);
    EXPECT(!std::filesystem::exists(
        directory.path / kSessionTemporaryFileName));

    DurabilityProbe backup_failure{primary};
    backup_failure.failure = DurabilityFailure::backup;
    document.application.window_placement.width = 1600;
    active_probe = &backup_failure;
    EXPECT(!write_session(directory.path, document, fake_durability_hook));
    active_probe = nullptr;
    EXPECT(backup_failure.call_count == 2);
    EXPECT(read_text(primary) == new_primary);
    EXPECT(!std::filesystem::exists(
        directory.path / kSessionTemporaryFileName));
    EXPECT(!std::filesystem::exists(
        directory.path / kSessionBackupTemporaryFileName));
}

void test_backup_replace_failure_preserves_primary() {
    TemporaryDirectory directory;
    SessionDocument document{sample(), {}};
    EXPECT(write_session(directory.path, document));
    document.application.window_placement.width = 1440;
    EXPECT(write_session(directory.path, document));
    const auto primary = directory.path / kSessionFileName;
    const std::string unchanged_primary = read_text(primary);

    const auto backup = directory.path / kSessionBackupFileName;
    std::filesystem::remove(backup);
    std::filesystem::create_directory(backup);
    write_text(backup / "keep", "do not replace");

    document.application.window_placement.width = 1600;
    EXPECT(!write_session(directory.path, document));
    EXPECT(read_text(primary) == unchanged_primary);
    EXPECT(std::filesystem::is_directory(backup));
    EXPECT(read_text(backup / "keep") == "do not replace");
    EXPECT(!std::filesystem::exists(
        directory.path / kSessionBackupTemporaryFileName));
}

}  // namespace

int main() {
    test_round_trip_and_plain_json();
    test_optional_sidebar_width();
    test_optional_pinned_locations();
    test_two_over_one_round_trip();
    test_new_three_pane_layout_round_trips();
    test_corrupt_and_invalid_documents();
    test_unknown_fields_survive_write_back();
    test_clean_shutdown_type_mismatch_defaults_true();
    test_read_fallbacks();
    test_atomic_write_and_backup();
    test_backup_replace_failure_preserves_primary();
    test_corrupt_primary_does_not_replace_good_backup();
    test_durability_hook_order_and_failure();
    return panedock::test::summary("core_session");
}
