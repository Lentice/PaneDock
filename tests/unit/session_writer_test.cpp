#include "app_shell/session_writer.h"
#include "unit/test_util.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

// SessionWriter owns *when and whether* the session file is written. Its one
// load-bearing rule is that a failed write leaves the dirty flag set, so the
// next attempt retries instead of silently losing the user's arrangement.
//
// That rule used to be checked by grepping session_writer.cpp for the order of
// two assignments -- a check that never executes the code and that any
// rewording defeats. This drives the real object against a real directory
// through its public interface instead.

namespace {
using panedock::app_shell::SessionWriter;
using namespace panedock::core;

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        path = std::filesystem::temp_directory_path() /
               ("panedock-session-writer-" +
                std::to_string(std::chrono::steady_clock::now()
                                   .time_since_epoch()
                                   .count()));
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};

// write_session stages through session.json.tmp. A non-empty directory in that
// name cannot be removed, so the write fails before touching the real file --
// a deterministic failure that needs no permission manipulation.
void block_writes(const std::filesystem::path& directory) {
    const auto blocker = directory / kSessionTemporaryFileName;
    std::filesystem::create_directories(blocker / "occupied");
}

void unblock_writes(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::remove_all(directory / kSessionTemporaryFileName, error);
}

ApplicationState sample(std::string group_id) {
    PaneState pane{"pane-1",
                   {TabState{"tab-1",
                             {L"C:\\fallback", L"", L"C:\\fallback"},
                             "FVM_ICON:48",
                             "System.ItemNameDisplay",
                             false,
                             {},
                             0}},
                   "tab-1"};
    GroupState group{group_id,
                     L"Work",
                     LayoutTemplate::single,
                     {},
                     {std::move(pane)},
                     "pane-1"};
    return {kSessionSchemaVersion, {std::move(group)}, std::move(group_id),
            {0, 0, 1280, 720, false}, kDefaultSidebarWidth, {}};
}

void failed_write_stays_dirty_and_a_later_write_clears_it() {
    TemporaryDirectory temporary;
    SessionWriter writer;
    writer.set_directory(temporary.path);
    EXPECT(!writer.dirty());

    block_writes(temporary.path);
    EXPECT(!writer.write(sample("group-lost"), false, nullptr));
    // The write is still owed: nothing may clear the flag but a success.
    EXPECT(writer.dirty());
    EXPECT(!std::filesystem::exists(temporary.path / kSessionFileName));

    unblock_writes(temporary.path);
    EXPECT(writer.write(sample("group-kept"), false, nullptr));
    EXPECT(!writer.dirty());

    const auto result = read_session(temporary.path);
    EXPECT(result.source == SessionSource::primary);
    EXPECT(result.document.application.active_group_id == "group-kept");
    EXPECT(result.document.clean_shutdown == false);
}

void mark_dirty_survives_until_a_write_succeeds() {
    TemporaryDirectory temporary;
    SessionWriter writer;
    writer.set_directory(temporary.path);

    writer.mark_dirty();
    EXPECT(writer.dirty());
    block_writes(temporary.path);
    EXPECT(!writer.write(sample("group-1"), false, nullptr));
    EXPECT(writer.dirty());
    unblock_writes(temporary.path);
    EXPECT(writer.write(sample("group-1"), false, nullptr));
    EXPECT(!writer.dirty());
}

// The clean marker is written after Shell, the parent HWND and COM are gone.
// It is the flag the next startup reads to decide whether the last shutdown
// completed, so it must be true on disk and must not resurrect the dirty flag.
void clean_marker_records_a_completed_shutdown() {
    TemporaryDirectory temporary;
    SessionWriter writer;
    writer.set_directory(temporary.path);

    EXPECT(writer.write(sample("group-1"), false, nullptr));
    EXPECT(read_session(temporary.path).document.clean_shutdown == false);

    EXPECT(writer.write_clean_marker(sample("group-1")));
    EXPECT(read_session(temporary.path).document.clean_shutdown == true);
    EXPECT(!writer.dirty());
}

// read_session preserves fields a future schema adds; adopt() is what carries
// them into the writer so the next write does not drop them.
void adopted_document_keeps_unrecognized_fields() {
    TemporaryDirectory temporary;
    {
        // A file written by a future version: same schema plus one field this
        // build knows nothing about.
        std::string json = serialize_session({sample("group-1"), "", true});
        json.insert(json.find('{') + 1, "\"future_field\":42,");
        std::ofstream stream(temporary.path / kSessionFileName,
                             std::ios::binary | std::ios::trunc);
        stream << json;
    }
    auto read_back = read_session(temporary.path);
    EXPECT(read_back.source == SessionSource::primary);

    SessionWriter writer;
    writer.set_directory(temporary.path);
    writer.adopt(std::move(read_back.document));
    EXPECT(writer.write(sample("group-1"), false, nullptr));

    std::ifstream stream(temporary.path / kSessionFileName, std::ios::binary);
    const std::string json{std::istreambuf_iterator<char>(stream), {}};
    EXPECT(json.find("future_field") != std::string::npos);
}

}  // namespace

int main() {
    failed_write_stays_dirty_and_a_later_write_clears_it();
    mark_dirty_survives_until_a_write_succeeds();
    clean_marker_records_a_completed_shutdown();
    adopted_document_keeps_unrecognized_fields();
    return panedock::test::summary("session_writer_test");
}
