#include "app_shell/session_writer.h"

#include <stdexcept>

namespace panedock::app_shell {
namespace {

// core::write_session does the atomic replace; this is the durability half.
// Opening the already-replaced file and flushing it is what makes the write
// survive a power loss rather than merely a process crash.
bool flush_session_file(const std::filesystem::path& path) noexcept {
    const HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const BOOL flushed = FlushFileBuffers(file);
    const BOOL closed = CloseHandle(file);
    return flushed != FALSE && closed != FALSE;
}

}  // namespace

bool SessionWriter::write(const core::ApplicationState& application,
                          bool clean_shutdown, HWND timer_owner) noexcept {
    dirty_ = true;
    try {
        document_.application = application;
        document_.clean_shutdown = clean_shutdown;
        if (!core::write_session(directory_, document_, flush_session_file))
            throw std::runtime_error("write_session failed");
    } catch (...) {
        // noexcept: a failed session write must not terminate the process.
        OutputDebugStringW(L"PaneDock: session persistence failed\n");
        // The WM_TIMER handler killed the timer before calling in, so
        // without this the dirty document waits for the next model
        // change or for shutdown. Re-arming makes the retry real.
        (void)arm_timer(timer_owner);
        return false;
    }
    dirty_ = false;
    cancel_timer(timer_owner);
    return true;
}

bool SessionWriter::write_clean_marker(
    const core::ApplicationState& application) noexcept {
    try {
        document_.application = application;
        document_.clean_shutdown = true;
        if (!core::write_session(directory_, document_, flush_session_file))
            throw std::runtime_error("write_session failed");
    } catch (...) {
        OutputDebugStringW(
            L"PaneDock: final clean-shutdown marker write failed\n");
        return false;
    }
    return true;
}

}  // namespace panedock::app_shell
