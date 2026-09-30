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

class CountingCapture final : public panedock::app_shell::LiveLocationCapture {
public:
    int captures{};
    void capture_live_locations() noexcept override { ++captures; }
};

using CaptureSuppression = SessionWriter::CaptureSuppression;

void save_now_captures_then_writes_the_bound_model() {
    TemporaryDirectory temporary;
    SessionWriter writer;
    writer.set_directory(temporary.path);
    const ApplicationState application = sample("group-bound");
    CountingCapture capture;
    writer.bind(application, capture);

    EXPECT(writer.save_now());
    EXPECT(capture.captures == 1);
    EXPECT(!writer.dirty());
    EXPECT(read_session(temporary.path).document.application.active_group_id ==
           "group-bound");
}

void suppressed_save_is_refused_and_stays_owed() {
    TemporaryDirectory temporary;
    SessionWriter writer;
    writer.set_directory(temporary.path);
    const ApplicationState application = sample("group-a");
    CountingCapture capture;
    writer.bind(application, capture);
    {
        CaptureSuppression suppression(writer);
        EXPECT(writer.capture_gate().suppressed());
        writer.capture_live_locations();
        EXPECT(!writer.save_now());
        EXPECT(capture.captures == 0);
        EXPECT(writer.dirty());
        EXPECT(!std::filesystem::exists(temporary.path / kSessionFileName));
    }
    EXPECT(!writer.capture_gate().suppressed());
    // No timer owner: the release reschedules, which can only mark it owed.
    EXPECT(writer.dirty());
    EXPECT(writer.save_now());
    EXPECT(capture.captures == 1);
}

void forced_save_writes_without_reading_a_live_view() {
    TemporaryDirectory temporary;
    SessionWriter writer;
    writer.set_directory(temporary.path);
    const ApplicationState application = sample("group-forced");
    CountingCapture capture;
    writer.bind(application, capture);
    CaptureSuppression suppression(writer);
    EXPECT(writer.save_now(false, true));
    EXPECT(capture.captures == 0);
    EXPECT(!writer.dirty());
}

void nested_suppression_lifts_only_at_the_outermost_release() {
    SessionWriter writer;
    {
        CaptureSuppression outer(writer);
        {
            CaptureSuppression inner(writer);
        }
        EXPECT(writer.capture_gate().suppressed());
    }
    EXPECT(!writer.capture_gate().suppressed());
}

// The save a Group transition refuses must not wait for the next model
// change: releasing the suppression arms the debounce timer again.
void a_refused_save_is_rescheduled_when_suppression_ends() {
    TemporaryDirectory temporary;
    const HWND owner = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                       HWND_MESSAGE, nullptr, nullptr, nullptr);
    EXPECT(owner != nullptr);
    SessionWriter writer;
    writer.set_directory(temporary.path);
    const ApplicationState application = sample("group-a");
    CountingCapture capture;
    writer.bind(application, capture);
    writer.set_timer_owner(owner);
    {
        CaptureSuppression suppression(writer);
        EXPECT(!writer.save_now());
        EXPECT(!writer.timer_armed());
    }
    EXPECT(writer.timer_armed());
    EXPECT(writer.dirty());
    writer.cancel_timer(owner);
    DestroyWindow(owner);
}

void a_save_that_was_not_refused_does_not_reschedule() {
    const HWND owner = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0,
                                       HWND_MESSAGE, nullptr, nullptr, nullptr);
    EXPECT(owner != nullptr);
    SessionWriter writer;
    writer.set_timer_owner(owner);
    {
        CaptureSuppression suppression(writer);
    }
    EXPECT(!writer.timer_armed());
    DestroyWindow(owner);
}

}  // namespace

int main() {
    save_now_captures_then_writes_the_bound_model();
    suppressed_save_is_refused_and_stays_owed();
    forced_save_writes_without_reading_a_live_view();
    nested_suppression_lifts_only_at_the_outermost_release();
    a_refused_save_is_rescheduled_when_suppression_ends();
    a_save_that_was_not_refused_does_not_reschedule();
    failed_write_stays_dirty_and_a_later_write_clears_it();
    mark_dirty_survives_until_a_write_succeeds();
    clean_marker_records_a_completed_shutdown();
    adopted_document_keeps_unrecognized_fields();
    return panedock::test::summary("session_writer_test");
}
