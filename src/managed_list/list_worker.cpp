#include "managed_list/list_worker.h"
#include "managed_list/list_worker_test.h"
#include <array>
#include <algorithm>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include "shell_core/shell_core.h"

namespace panedock::managed_list {
namespace {
struct State final {
    std::mutex mutex;
    std::condition_variable changed;
    HANDLE ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HANDLE control{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool recovery{};
    bool recovering{};
    bool watch_open{};
    bool initial_complete{};
    DWORD injected_error{};
    bool stop{};
    bool running{};
    HANDLE thread{};
    std::size_t cancel_requests{}, cancel_accepted{};
    DWORD last_cancel_error{}, first_read_error{};
    bool first_read_returned{};
    std::optional<std::uint64_t> observed_serial;
    bool observed_read_returned{};
    DWORD observed_read_error{};
    std::shared_ptr<testing::ReadGate> gate;
    std::uint64_t serial{};
    std::uint64_t running_serial{};
    std::optional<Request> pending;
    std::deque<Batch> results;
    std::optional<Batch> emergency;
    ~State() { if (control) CloseHandle(control); if (thread) CloseHandle(thread); if (ready) CloseHandle(ready); }
};
// UI creates subscriptions on its STA. A blocked abandoned worker retains its
// slot, resources and event; reopening cannot create an unbounded thread leak.
std::array<std::weak_ptr<State>, kMaxSubscriptions> slots;

bool publish(const std::shared_ptr<State>& state, std::uint64_t serial,
             Batch batch) {
    if (batch.rows.size() > kBatchRows || owned_batch_bytes(batch) > kMaxBatchOwnedBytes) {
        std::vector<Row>().swap(batch.rows);
        std::vector<std::wstring>().swap(batch.removed);
        std::vector<std::pair<std::wstring, std::wstring>>().swap(batch.renamed);
        batch.phase = Phase::limited;
        batch.error = ERROR_BUFFER_OVERFLOW;
    }
    std::unique_lock lock(state->mutex);
    if (state->gate && state->results.size() == kQueuedBatches)
        SetEvent(state->gate->backpressure);
    state->changed.wait(lock, [&] {
        return state->stop || serial != state->serial ||
            state->results.size() < kQueuedBatches;
    });
    if (state->stop || serial != state->serial) return false;
    state->results.push_back(std::move(batch));
    SetEvent(state->ready);
    return true;
}
bool interested(const std::shared_ptr<State>& state, std::uint64_t serial) {
    std::lock_guard lock(state->mutex);
    return !state->stop && state->serial == serial;
}
struct FindHandle final {
    HANDLE value{INVALID_HANDLE_VALUE};
    ~FindHandle() { if (value != INVALID_HANDLE_VALUE) FindClose(value); }
};
struct Pidl final {
    PIDLIST_ABSOLUTE value{};
    ~Pidl() { if (value) CoTaskMemFree(value); }
};
struct RelativePidl final {
    PIDLIST_RELATIVE value{};
    ~RelativePidl() { if (value) CoTaskMemFree(value); }
};
struct Str final {
    PWSTR value{};
    ~Str() { if (value) CoTaskMemFree(value); }
};
// No provider call runs with State locked. Cancellation invalidates interest;
// it cannot interrupt an arbitrary provider (including COM Release on unwind).
template<class Call>
HRESULT provider_call(const std::shared_ptr<State>& state, std::uint64_t serial, Call call) {
    if (!interested(state, serial)) return E_ABORT;
    const HRESULT result = call();
    return interested(state, serial) ? result : E_ABORT;
}
DWORD provider_error(HRESULT result) {
    return HRESULT_FACILITY(result) == FACILITY_WIN32 ? HRESULT_CODE(result) : ERROR_INVALID_DATA;
}
bool bounded_location(const core::ShellLocation& value) {
    return valid_shell_location(value);
}
core::ShellLocationParent parent_identity(const core::ShellLocation& location) {
    return {location.parsing_name, location.known_folder_identity,
            location.fallback_path, location.namespace_location};
}
core::ShellLocation location_from_parent(const core::ShellLocationParent& parent) {
    core::ShellLocation location{
        parent.parsing_name, parent.known_folder_identity, parent.fallback_path};
    location.namespace_location = parent.namespace_location;
    return location;
}
bool copy_provider_text(PCWSTR text, std::size_t limit, std::wstring& output) {
    if (!text) return false;
    // Scan only the admitted prefix; never first copy an unbounded provider string.
    std::size_t size = 0;
    while (size <= limit && text[size]) ++size;
    if (size > limit) return false;
    output.assign(text, size);
    return true;
}
HRESULT shell_location(const std::shared_ptr<State>& state, std::uint64_t serial,
                       PIDLIST_ABSOLUTE pidl, core::ShellLocation& output) {
    Str parsing, path;
    auto result = provider_call(state, serial, [&] {
        return SHGetNameFromIDList(pidl, SIGDN_DESKTOPABSOLUTEPARSING, &parsing.value);
    });
    if (FAILED(result)) return result;
    if (!copy_provider_text(parsing.value, kMaxPath, output.parsing_name) || output.parsing_name.empty())
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    result = provider_call(state, serial, [&] {
        return SHGetNameFromIDList(pidl, SIGDN_FILESYSPATH, &path.value);
    });
    if (!interested(state, serial)) return E_ABORT;
    // Virtual folders normally have no filesystem path. Never navigate to a fallback.
    if (SUCCEEDED(result) && !copy_provider_text(path.value, kMaxPath, output.fallback_path))
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    Microsoft::WRL::ComPtr<IKnownFolderManager> manager;
    Microsoft::WRL::ComPtr<IKnownFolder> folder;
    result = provider_call(state, serial, [&] {
        return CoCreateInstance(CLSID_KnownFolderManager, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&manager));
    });
    if (SUCCEEDED(result)) result = provider_call(state, serial, [&] {
        return manager->FindFolderFromIDList(pidl, &folder);
    });
    if (SUCCEEDED(result) && folder) {
        KNOWNFOLDERID id{};
        result = provider_call(state, serial, [&] { return folder->GetId(&id); });
        wchar_t guid[39]{};
        if (SUCCEEDED(result) && StringFromGUID2(id, guid, ARRAYSIZE(guid)))
            output.known_folder_identity = guid;
    }
    return interested(state, serial) ? S_OK : E_ABORT;
}
void enumerate_shell_namespace(const std::shared_ptr<State>& state, std::uint64_t serial,
                               const Request& request) {
    Batch metadata{request, {}, Phase::loading, 0};
    metadata.notification = NotificationCapability::manual_refresh;
    const auto fail = [&](HRESULT result) {
        metadata.phase = Phase::error;
        metadata.error = provider_error(result);
        publish(state, serial, std::move(metadata));
    };
    // Shares production slots, interest checks, accounting and backpressure.
    // This substitutes owned values only, never Shell objects or COM behavior.
    if (state->gate && state->gate->namespace_batch) {
        SetEvent(state->gate->entered);
        WaitForSingleObject(state->gate->release, INFINITE);
        if (!interested(state, serial)) return;
        const auto& supplied = *state->gate->namespace_batch;
        if ((supplied.resolved_location && !bounded_location(*supplied.resolved_location)) ||
            (supplied.parent_location && !bounded_location(*supplied.parent_location))) {
            fail(HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)); return;
        }
        metadata.resolved_location = state->gate->namespace_batch->resolved_location;
        metadata.parent_location = state->gate->namespace_batch->parent_location;
        metadata.parent_outcome = state->gate->namespace_batch->parent_outcome;
        std::size_t retained = 0, count = 0;
        for (std::size_t n = 0; n < state->gate->batches; ++n) {
            if (!interested(state, serial)) return;
            auto batch = metadata;
            batch.rows = state->gate->namespace_batch->rows;
            bool valid = (!batch.resolved_location || bounded_location(*batch.resolved_location)) &&
                (!batch.parent_location || bounded_location(*batch.parent_location));
            for (auto& row : batch.rows) {
                valid = valid && prepare_row(row);
                retained += sizeof(Row) + owned_row_bytes(row);
            }
            count += batch.rows.size();
            if (!valid) { fail(HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW)); return; }
            if (retained > kMaxRowBytes || count > kMaxRows) {
                batch.rows = {}; batch.phase = Phase::limited;
                publish(state, serial, std::move(batch)); return;
            }
            const bool limited = batch.rows.size() > kBatchRows ||
                owned_batch_bytes(batch) > kMaxBatchOwnedBytes;
            if (!publish(state, serial, std::move(batch)) || limited) return;
        }
        metadata.phase = state->gate->namespace_batch->phase;
        metadata.error = state->gate->namespace_batch->error;
        publish(state, serial, std::move(metadata));
        return;
    }
    Pidl absolute;
    HRESULT result = E_INVALIDARG;
    // Known-folder identity takes precedence, like the legacy resolver, but a
    // failed resolution retries only the original parsing name, never fallback.
    if (!request.location.known_folder_identity.empty()) {
        GUID id{};
        if (SUCCEEDED(CLSIDFromString(request.location.known_folder_identity.c_str(), &id)))
            result = provider_call(state, serial, [&] {
                return SHGetKnownFolderIDList(id, KF_FLAG_DONT_VERIFY, nullptr, &absolute.value);
            });
    }
    if (!interested(state, serial)) return;
    if (!absolute.value) result = provider_call(state, serial, [&] {
        return SHParseDisplayName(request.location.parsing_name.c_str(), nullptr,
                                  &absolute.value, 0, nullptr);
    });
    if (FAILED(result) || !absolute.value) { fail(FAILED(result) ? result : E_FAIL); return; }
    core::ShellLocation resolved;
    result = shell_location(state, serial, absolute.value, resolved);
    if (FAILED(result)) { fail(result); return; }
    // A persisted parsing name can re-resolve to a different PIDL chain than
    // the item returned by its containing namespace folder. Keep the owned
    // parent value through navigation and restart; never persist the PIDL.
    resolved.navigation_parent = request.location.navigation_parent;
    resolved.namespace_location = request.location.namespace_location || ILIsEmpty(absolute.value);
    metadata.resolved_location = std::move(resolved);
    if (request.location.navigation_parent) {
        metadata.parent_location = location_from_parent(*request.location.navigation_parent);
        metadata.parent_outcome = ParentOutcome::value;
    } else {
        metadata.parent_outcome = ParentOutcome::root;
    }
    if (!request.location.navigation_parent && !ILIsEmpty(absolute.value)) {
        metadata.parent_outcome = ParentOutcome::failed;
        Pidl parent;
        parent.value = ILCloneFull(absolute.value);
        if (!parent.value) { fail(E_OUTOFMEMORY); return; }
        if (!ILRemoveLastID(parent.value)) { fail(E_FAIL); return; }
        core::ShellLocation parent_value;
        result = shell_location(state, serial, parent.value, parent_value);
        if (FAILED(result)) { fail(result); return; }
        metadata.parent_location = std::move(parent_value);
        metadata.parent_outcome = ParentOutcome::value;
    }
    Microsoft::WRL::ComPtr<IShellFolder> folder;
    result = provider_call(state, serial, [&] {
        // Desktop is the namespace root, not a child to BindToObject.
        return ILIsEmpty(absolute.value) ? SHGetDesktopFolder(&folder) :
            SHBindToObject(nullptr, absolute.value, nullptr, IID_PPV_ARGS(&folder));
    });
    if (FAILED(result) || !folder) { fail(FAILED(result) ? result : E_FAIL); return; }
    Microsoft::WRL::ComPtr<IEnumIDList> enumerator;
    result = provider_call(state, serial, [&] {
        return folder->EnumObjects(nullptr,
            SHCONTF_FOLDERS | SHCONTF_NONFOLDERS | SHCONTF_INCLUDEHIDDEN, &enumerator);
    });
    if (result == S_FALSE) {
        metadata.phase = Phase::empty; publish(state, serial, std::move(metadata)); return;
    }
    if (FAILED(result) || !enumerator) { fail(FAILED(result) ? result : E_FAIL); return; }
    auto batch = metadata;
    batch.rows.reserve(kBatchRows);
    std::size_t count = 0, retained = 0;
    const auto finish = [&](Phase phase, DWORD error = 0) {
        batch.phase = phase; batch.error = error;
        publish(state, serial, std::move(batch));
    };
    for (;;) {
        RelativePidl child;
        ULONG fetched{};
        result = provider_call(state, serial, [&] { return enumerator->Next(1, &child.value, &fetched); });
        if (FAILED(result)) { finish(Phase::error, provider_error(result)); return; }
        if (result == S_FALSE) { finish(count ? Phase::complete : Phase::empty); return; }
        if (fetched != 1 || !child.value) { finish(Phase::error, ERROR_INVALID_DATA); return; }
        if (count == kMaxRows) { finish(Phase::limited); return; }
        Pidl child_absolute;
        child_absolute.value = ILCombine(absolute.value, child.value);
        if (!child_absolute.value) { finish(Phase::error, ERROR_NOT_ENOUGH_MEMORY); return; }
        core::ShellLocation child_location;
        result = shell_location(state, serial, child_absolute.value, child_location);
        if (FAILED(result)) { finish(Phase::error, provider_error(result)); return; }
        Str name;
        result = provider_call(state, serial, [&] {
            return SHGetNameFromIDList(child_absolute.value, SIGDN_NORMALDISPLAY, &name.value);
        });
        if (FAILED(result)) { finish(Phase::error, provider_error(result)); return; }
        Row row;
        if (!copy_provider_text(name.value, kMaxName, row.name) || row.name.empty()) {
            finish(Phase::error, ERROR_BUFFER_OVERFLOW); return;
        }
        SFGAOF attributes = SFGAO_FOLDER;
        PCUITEMID_CHILD relative = child.value;
        result = provider_call(state, serial, [&] { return folder->GetAttributesOf(1, &relative, &attributes); });
        if (FAILED(result)) { finish(Phase::error, provider_error(result)); return; }
        row.directory = (attributes & SFGAO_FOLDER) != 0;
        row.identity = {child_location.parsing_name, true};
        child_location.navigation_parent = parent_identity(*metadata.resolved_location);
        row.child_location = std::move(child_location);
        // Optional properties belong to the existing visible-item enrichment
        // path; no size/date provider query delays the first operable batch.
        const auto bytes = owned_row_bytes(row);
        if (retained + sizeof(Row) + bytes > kMaxRowBytes) { finish(Phase::limited); return; }
        if (owned_batch_bytes(batch) + bytes > kMaxBatchOwnedBytes) {
            if (batch.rows.empty()) { finish(Phase::limited); return; }
            if (!publish(state, serial, std::move(batch))) return;
            batch = metadata;
            batch.rows.reserve(kBatchRows);
        }
        batch.rows.push_back(std::move(row));
        retained += sizeof(Row) + bytes;
        ++count;
        if (batch.rows.size() == kBatchRows) {
            if (!publish(state, serial, std::move(batch))) return;
            batch = metadata;
            batch.rows.reserve(kBatchRows);
        }
    }
}
void enumerate(const std::shared_ptr<State>& state, std::uint64_t serial,
               const Request& request, bool recovery = false) {
    Batch batch{request, {}, Phase::loading, 0};
    if (recovery) batch.change = Change::rescan;
    const auto& path = request.location.parsing_name;
    if (uses_shell_namespace(request.location)) {
        enumerate_shell_namespace(state, serial, request);
        return;
    }
    // All provider/drive calls stay here, including disconnected mappings.
    // Reject device namespaces; this backend accepts ordinary UNC share paths.
    if (!is_filesystem_location(path) || path.size() > kMaxPath ||
        path.starts_with(L"\\\\.\\") || path.find_first_of(L"*?") != std::wstring::npos ||
        (path.starts_with(L"\\\\") && path.find(L'\\', 2) == std::wstring::npos)) {
        batch.phase = Phase::error; batch.error = ERROR_NOT_SUPPORTED;
        publish(state, serial, std::move(batch)); return;
    }
    if (state->gate && path.starts_with(L"\\\\pd226-controlled\\share")) {
        // A controlled OS-read substitute, on the real bounded worker. The
        // gate owns kernel events; no callback can reach Pane or UI state.
        SetEvent(state->gate->entered);
        WaitForSingleObject(state->gate->release, INFINITE);
        for (std::size_t n = 0; n < state->gate->batches; ++n) {
            batch.rows.assign(kBatchRows, {L"controlled", 0, false});
            if (!publish(state, serial, std::move(batch))) return;
            batch = {request, {}, Phase::loading, 0};
            if (recovery) batch.change = Change::rescan;
        }
        batch.phase = Phase::error; batch.error = ERROR_BAD_NETPATH;
        publish(state, serial, std::move(batch)); return;
    }
    if (!interested(state, serial)) return;
    auto pattern = path;
    if (pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';
    WIN32_FIND_DATAW data{};
    FindHandle find{FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data,
                                    FindExSearchNameMatch, nullptr, 0)};
    const DWORD first_error = find.value == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    {
        std::lock_guard lock(state->mutex);
        state->first_read_returned = true;
        state->first_read_error = first_error;
        if (state->observed_serial == serial) {
            state->observed_read_returned = true;
            state->observed_read_error = first_error;
        }
    }
    if (find.value == INVALID_HANDLE_VALUE) {
        batch.error = first_error;
        if (batch.error == ERROR_FILE_NOT_FOUND) {
            const DWORD attr = GetFileAttributesW(path.c_str());
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                batch.phase = Phase::empty; batch.error = 0;
            } else batch.phase = Phase::error;
        } else batch.phase = Phase::error;
        publish(state, serial, std::move(batch)); return;
    }
    std::size_t count = 0;
    for (;;) {
        if (!interested(state, serial)) return;
        std::wstring name(data.cFileName);
        if (name != L"." && name != L"..") {
            if (count == kMaxRows) { batch.phase = Phase::limited; break; }
            if (name.size() > kMaxName) {
                batch.phase = Phase::error; batch.error = ERROR_BUFFER_OVERFLOW; break;
            }
            batch.rows.push_back(basic_row(data, std::move(name)));
            ++count;
            if (batch.rows.size() == kBatchRows) {
                if (!publish(state, serial, std::move(batch))) return;
                batch = {request, {}, Phase::loading, 0};
                if (recovery) batch.change = Change::rescan;
            }
        }
        if (!FindNextFileW(find.value, &data)) {
            batch.error = GetLastError();
            if (batch.error == ERROR_NO_MORE_FILES) {
                batch.error = 0; batch.phase = count ? Phase::complete : Phase::empty;
            } else batch.phase = Phase::error;
            break;
        }
    }
    publish(state, serial, std::move(batch));
}
// Directory I/O, its OVERLAPPED and buffers live ONLY on this bounded worker.
// UI cancellation never waits; even a broken driver's cancellation retains its
// existing slot until completion, rather than freeing storage still used by I/O.
struct DirectoryWatch final {
    HANDLE directory{INVALID_HANDLE_VALUE};
    OVERLAPPED io{};
    alignas(DWORD) std::array<std::byte, 64 * 1024> buffer{};
    bool pending{};
    DirectoryWatch() { io.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr); }
    ~DirectoryWatch() {
        if (pending) {
            CancelIoEx(directory, &io);
            DWORD bytes{};
            (void)GetOverlappedResult(directory, &io, &bytes, TRUE);
        }
        if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
        if (io.hEvent) CloseHandle(io.hEvent);
    }
    DWORD arm() {
        ResetEvent(io.hEvent);
        if (!ReadDirectoryChangesW(directory, buffer.data(), static_cast<DWORD>(buffer.size()),
            FALSE, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
            FILE_NOTIFY_CHANGE_ATTRIBUTES, nullptr, &io, nullptr)) return GetLastError();
        pending = true;
        return ERROR_SUCCESS;
    }
};
bool leaf_name(std::wstring_view name) {
    return !name.empty() && name.size() <= kMaxName && name != L"." && name != L".." &&
        name.find_first_of(L"\\/:*?\"") == std::wstring::npos;
}
// Notification detail has a fixed cap. Exceeding it discards detail and requests
// ONE recovery, just like ERROR_NOTIFY_ENUM_DIR / a zero-byte completion.
bool decode_changes(const std::shared_ptr<State>& state, std::uint64_t serial,
                    const DirectoryWatch& watch, DWORD bytes, Batch& batch,
                    std::wstring& old_name) {
    std::vector<std::wstring> names;
    std::size_t offset = 0;
    constexpr auto header = offsetof(FILE_NOTIFY_INFORMATION, FileName);
    while (offset < bytes) {
        if (bytes - offset < header) return false;
        const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(watch.buffer.data() + offset);
        if (info->FileNameLength % sizeof(wchar_t) || info->FileNameLength > bytes - offset - header)
            return false;
        std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
        if (!leaf_name(name)) return false;
        if (info->Action < FILE_ACTION_ADDED || info->Action > FILE_ACTION_RENAMED_NEW_NAME) return false;
        if (info->Action == FILE_ACTION_RENAMED_OLD_NAME) {
            // A split rename pair must not first delete the selected identity.
            // Kernel order pairs old/new; an unpaired old name needs recovery.
            if (!old_name.empty()) return false;
            old_name = name;
        }
        else if (info->Action == FILE_ACTION_RENAMED_NEW_NAME && !old_name.empty()) {
            batch.renamed.emplace_back(old_name, name);
            if (std::find(names.begin(), names.end(), old_name) == names.end()) names.push_back(old_name);
            old_name.clear();
            if (batch.renamed.size() > kBatchRows) return false;
        } else if (!old_name.empty()) return false;
        if (info->Action != FILE_ACTION_RENAMED_OLD_NAME &&
            std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
        if (names.size() > kBatchRows) return false;
        if (!info->NextEntryOffset) break;
        if (info->NextEntryOffset < header + info->FileNameLength ||
            info->NextEntryOffset % alignof(DWORD) || info->NextEntryOffset >= bytes - offset) return false;
        offset += info->NextEntryOffset;
    }
    for (auto& name : names) {
        if (!interested(state, serial)) return false;
        auto path = batch.request.location.parsing_name;
        if (path.back() != L'\\') path += L'\\';
        path += name;
        WIN32_FIND_DATAW data{};
        FindHandle find{FindFirstFileExW(path.c_str(), FindExInfoBasic, &data,
            FindExSearchNameMatch, nullptr, 0)};
        if (find.value != INVALID_HANDLE_VALUE) batch.rows.push_back(basic_row(data, std::move(name)));
        else {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return false;
            batch.removed.push_back(std::move(name));
        }
    }
    return true;
}
void enumerate_and_watch(const std::shared_ptr<State>& state, std::uint64_t serial,
                         const Request& request) {
    if (!is_filesystem_location(request.location.parsing_name)) {
        enumerate(state, serial, request);
        { std::lock_guard lock(state->mutex); state->initial_complete = true; }
        return; // Virtual locations explicitly require manual Refresh.
    }
    DirectoryWatch watch;
    const auto& path = request.location.parsing_name;
    DWORD error = ERROR_SUCCESS;
    // No UNC/mapped-network subscriptions. Existing enumeration routing and
    // location support remain unchanged. All drive probing stays off the STA.
    const bool drive_path = path.size() >= 3 && path[1] == L':' && path[2] == L'\\' &&
        path.find_first_of(L"*?") == std::wstring::npos;
    bool local = false;
    if (!interested(state, serial)) return;
    if (drive_path) {
        const auto root = path.substr(0, 3);
        const auto type = GetDriveTypeW(root.c_str());
        local = type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_RAMDISK || type == DRIVE_CDROM;
    }
    if (!interested(state, serial)) return;
    if (local) {
        if (!watch.io.hEvent) error = ERROR_NOT_ENOUGH_MEMORY;
        else {
            watch.directory = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
            if (watch.directory == INVALID_HANDLE_VALUE) error = GetLastError();
            else {
                // A local drive path can traverse a junction to a server.
                // Resolve only on this worker; never subscribe to its UNC target.
                std::wstring resolved(kMaxPath + 1, L'\0');
                const DWORD length = GetFinalPathNameByHandleW(watch.directory, resolved.data(),
                    static_cast<DWORD>(resolved.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
                if (!length || length >= resolved.size()) error = length ? ERROR_BUFFER_OVERFLOW : GetLastError();
                else if (resolved.starts_with(L"\\\\?\\UNC\\")) error = ERROR_NOT_SUPPORTED;
                else if (!interested(state, serial)) return;
                else error = watch.arm();
            }
        }
    }
    // Arm BEFORE the initial read to close the enumerate/subscribe loss window.
    enumerate(state, serial, request);
    { std::lock_guard lock(state->mutex); state->initial_complete = true; }
    if (!local || !interested(state, serial)) return;
    if (error) {
        publish(state, serial, {request, {}, Phase::complete, error, Change::watch_error}); return;
    }
    { std::lock_guard lock(state->mutex); state->watch_open = true; }
    if (!publish(state, serial, {request, {}, Phase::complete, 0, Change::watching})) return;
    std::wstring old_name;
    unsigned recovery_scans = 0;
    for (;;) {
        {
            std::lock_guard lock(state->mutex);
            state->running = false; // control event releases this idle wait
        }
        const HANDLE handles[]{state->control, watch.io.hEvent};
        const DWORD wake = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        bool recovery = false;
        {
            std::lock_guard lock(state->mutex);
            if (state->stop || state->serial != serial) return;
            state->running = true;
            recovery = state->recovery;
            error = state->injected_error;
            state->injected_error = 0;
            state->recovery = false;
            ResetEvent(state->control);
        }
        if (error) {
            publish(state, serial, {request, {}, Phase::complete, error, Change::watch_error}); return;
        }
        if (wake == WAIT_FAILED) {
            publish(state, serial, {request, {}, Phase::complete, GetLastError(), Change::watch_error}); return;
        }
        Batch batch{request, {}, Phase::complete, 0, Change::delta};
        if (WaitForSingleObject(watch.io.hEvent, 0) == WAIT_OBJECT_0) {
            DWORD bytes{};
            const BOOL success = GetOverlappedResult(watch.directory, &watch.io, &bytes, FALSE);
            error = success ? ERROR_SUCCESS : GetLastError();
            watch.pending = false;
            if (error && error != ERROR_NOTIFY_ENUM_DIR) {
                publish(state, serial, {request, {}, Phase::complete, error, Change::watch_error}); return;
            }
            recovery = recovery || error == ERROR_NOTIFY_ENUM_DIR || !bytes;
            if (!recovery) recovery = !decode_changes(state, serial, watch, bytes, batch, old_name);
            if (!interested(state, serial)) return;
            // Kernel retains changes between reads. Rearm before publishing or
            // scanning so UI backpressure cannot open a notification gap.
            error = watch.arm();
            if (error) {
                publish(state, serial, {request, {}, Phase::complete, error, Change::watch_error}); return;
            }
        }
        if (!interested(state, serial)) return;
        if (recovery) {
            // At most two consecutive loss recoveries without usable detail.
            // A failing/deleted directory must not cause an endless rescan loop.
            if (++recovery_scans > 2) {
                publish(state, serial, {request, {}, Phase::complete, ERROR_NOTIFY_ENUM_DIR, Change::watch_error}); return;
            }
            { std::lock_guard lock(state->mutex); state->recovering = true; }
            old_name.clear();
            if (!publish(state, serial, {request, {}, Phase::loading, 0, Change::rescan_begin})) return;
            enumerate(state, serial, request, true);
            { std::lock_guard lock(state->mutex); state->recovering = false; }
        } else if (!batch.rows.empty() || !batch.removed.empty()) {
            recovery_scans = 0;
            if (!publish(state, serial, std::move(batch))) return;
        }
    }
}
void run(std::shared_ptr<State> state) noexcept {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct Apartment final {
        HRESULT result;
        ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    } apartment_cleanup{apartment};
    for (;;) {
        std::optional<Request> request;
        std::uint64_t serial{};
        {
            std::unique_lock lock(state->mutex);
            state->changed.wait(lock, [&] { return state->stop || state->pending.has_value(); });
            if (state->stop) return;
            request = std::move(state->pending); state->pending.reset();
            serial = state->serial;
            ResetEvent(state->control);
            state->recovery = false;
            state->initial_complete = false;
            state->running = true;
            state->running_serial = serial;
            state->first_read_returned = false;
            state->first_read_error = 0;
        }
        try {
            if (!is_filesystem_location(request->location.parsing_name) && FAILED(apartment)) {
                Batch error{*request, {}, Phase::error, provider_error(apartment)};
                error.notification = NotificationCapability::manual_refresh;
                publish(state, serial, std::move(error));
            } else enumerate_and_watch(state, serial, *request);
        }
        catch (...) {
            bool complete = false;
            { std::lock_guard lock(state->mutex); complete = state->initial_complete; }
            try {
                Batch error{*request, {}, Phase::error, ERROR_NOT_ENOUGH_MEMORY,
                    complete ? Change::watch_error : Change::none};
                if (!is_filesystem_location(request->location.parsing_name))
                    error.notification = NotificationCapability::manual_refresh;
                publish(state, serial, std::move(error));
            }
            catch (...) { // No allocation is needed to expose a terminal failure.
                std::lock_guard lock(state->mutex);
                if (!state->stop && state->serial == serial) {
                    state->results.clear();
                    state->emergency.emplace(Batch{std::move(*request), {},
                        Phase::error, ERROR_NOT_ENOUGH_MEMORY,
                        complete ? Change::watch_error : Change::none});
                    if (!is_filesystem_location(state->emergency->request.location.parsing_name))
                        state->emergency->notification = NotificationCapability::manual_refresh;
                    SetEvent(state->ready);
                }
                // Wait for a replacement or shutdown; never repeatedly signal.
            }
        }
        {
            std::lock_guard lock(state->mutex);
            state->watch_open = false;
            state->running = false;
        }
    }
}
std::shared_ptr<State> acquire(std::shared_ptr<testing::ReadGate> gate = {}) {
    for (auto& slot : slots) {
        if (!slot.expired()) continue;
        auto state = std::make_shared<State>();
        if (!state->ready || !state->control) return {};
        state->gate = std::move(gate);
        slot = state;
        std::thread worker(run, state);
        if (!DuplicateHandle(GetCurrentProcess(), worker.native_handle(),
            GetCurrentProcess(), &state->thread, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            // Do not admit work without the owned cancellation handle. The
            // new thread has no request and can only be waiting for stop.
            {
                std::lock_guard lock(state->mutex);
                state->stop = true;
                state->changed.notify_all();
            }
            worker.detach();
            return {};
        }
        worker.detach();
        return state;
    }
    return {};
}
// Called under the worker lock. Best effort only: cancellation can race entry
// into a system call or be unsupported. The slot remains occupied until exit.
void invalidate(State& state, bool stop) noexcept {
    SetEvent(state.control);
    state.recovery = false;
    ++state.serial;
    state.stop = stop;
    state.pending.reset(); state.results.clear(); state.emergency.reset();
    ResetEvent(state.ready);
    if (state.running && state.thread) {
        ++state.cancel_requests;
        const BOOL accepted = CancelSynchronousIo(state.thread);
        state.last_cancel_error = accepted ? ERROR_SUCCESS : GetLastError();
        state.cancel_accepted += accepted != FALSE;
    }
    state.changed.notify_all();
}
class LocalSource final : public Source {
    std::shared_ptr<State> state_;
    std::shared_ptr<testing::ReadGate> gate_;
  public:
    explicit LocalSource(std::shared_ptr<State> state) : state_(std::move(state)), gate_(state_->gate) {}
    ~LocalSource() override {
        // Never join an OS call. Worker owns State, handles and its STA COM resources, and
        // exits after its current OS call returns. State is freed on last use.
        if (state_) { std::lock_guard lock(state_->mutex); invalidate(*state_, true); }
    }
    bool overflow(DWORD error = 0) noexcept {
        if (!state_) return false;
        std::lock_guard lock(state_->mutex);
        if (state_->stop) return false;
        if (state_->recovering) return true;
        state_->injected_error = error;
        state_->recovery = !error;
        SetEvent(state_->control);
        return true;
    }
    bool submit(Request request) noexcept override try {
        if (!valid_shell_location(request.location) ||
            request.binding.group_id.size() > 256 || request.binding.tab_id.size() > 256)
            return false;
        const auto request_bytes = owned_location_bytes(request.location) +
            request.binding.group_id.capacity() + request.binding.tab_id.capacity();
        if (request_bytes > kMaxBatchOwnedBytes / 4) return false;
        bool retire = false;
        if (state_) {
            std::lock_guard lock(state_->mutex);
            // Never queue a successor behind an obsolete OS call. Retiring
            // counts against the SAME four-slot cap, even after Pane dies.
            // Also retire an idle watch: its worker may be awaiting a broken
            // driver's asynchronous cancellation. Never queue navigation behind it.
            retire = state_->running || state_->watch_open;
            if (retire) invalidate(*state_, true);
        }
        if (retire) state_.reset();
        if (!state_) state_ = acquire(gate_);
        if (!state_) return false;
        std::lock_guard lock(state_->mutex);
        if (state_->stop) return false;
        SetEvent(state_->control);
        ++state_->serial; state_->results.clear(); state_->emergency.reset(); ResetEvent(state_->ready);
        state_->pending = std::move(request);
        state_->changed.notify_all(); return true;
    } catch (...) { return false; }
    std::optional<Batch> take() noexcept override {
        if (!state_) return {};
        std::lock_guard lock(state_->mutex);
        if (state_->emergency) {
            auto result = std::move(state_->emergency);
            state_->emergency.reset(); ResetEvent(state_->ready);
            return result;
        }
        if (state_->results.empty()) {
            ResetEvent(state_->ready);
            return {};
        }
        auto result = std::move(state_->results.front()); state_->results.pop_front();
        if (state_->results.empty()) ResetEvent(state_->ready);
        state_->changed.notify_all(); return result;
    }
    HANDLE event() const noexcept override { return state_ ? state_->ready : nullptr; }
    void cancel() noexcept override {
        if (state_) { std::lock_guard lock(state_->mutex); invalidate(*state_, false); }
    }
};
std::unique_ptr<Source> make_source(std::shared_ptr<testing::ReadGate> gate = {}) {
    auto state = acquire(std::move(gate));
    if (!state) return {};
    try { return std::make_unique<LocalSource>(state); }
    catch (...) {
        // Allocation failure after launching must not orphan an idle worker.
        std::lock_guard lock(state->mutex);
        invalidate(*state, true);
        throw;
    }
}
} // namespace
std::unique_ptr<Source> make_local_source() noexcept try {
    return make_source();
} catch (...) { return {}; }
namespace testing {
ReadGate::ReadGate(std::size_t result_batches, std::optional<Batch> result)
    : entered(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      release(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      backpressure(CreateEventW(nullptr, TRUE, FALSE, nullptr)), batches(result_batches), namespace_batch(std::move(result)) {}
ReadGate::~ReadGate() { CloseHandle(entered); CloseHandle(release); CloseHandle(backpressure); }
std::unique_ptr<Source> make_blocked_source(std::shared_ptr<ReadGate> gate) noexcept try {
    if (!gate || !gate->entered || !gate->release || !gate->backpressure) return {};
    return make_source(std::move(gate));
} catch (...) { return {}; }
bool inject_overflow(Source& source) noexcept {
    auto* local = dynamic_cast<LocalSource*>(&source);
    return local && local->overflow();
}
bool inject_watch_failure(Source& source, DWORD error) noexcept {
    auto* local = dynamic_cast<LocalSource*>(&source);
    return error && local && local->overflow(error);
}
Counts counts() noexcept {
    Counts result;
    for (auto& slot : slots) if (auto state = slot.lock()) {
        std::lock_guard lock(state->mutex);
        ++result.workers;
        result.running += state->running;
        result.pending += state->pending.has_value();
        result.batches += state->results.size() + state->emergency.has_value();
    }
    return result;
}
struct ReadObservation final {
    std::array<std::shared_ptr<State>, kMaxSubscriptions> states;
    std::array<std::size_t, kMaxSubscriptions> cancel_requests{}, cancel_accepted{};
    DWORD count{};
};
std::shared_ptr<ReadObservation> observe_running_reads() noexcept try {
    auto observation = std::make_shared<ReadObservation>();
    for (auto& slot : slots) if (auto state = slot.lock()) {
        std::lock_guard lock(state->mutex);
        if (!state->running) continue;
        state->observed_serial = state->running_serial;
        state->observed_read_returned = state->first_read_returned;
        state->observed_read_error = state->first_read_error;
        const auto n = observation->count++;
        observation->cancel_requests[n] = state->cancel_requests;
        observation->cancel_accepted[n] = state->cancel_accepted;
        observation->states[n] = std::move(state);
    }
    return observation;
} catch (...) { return {}; }
RetiredReadReport finish_read_observation(const std::shared_ptr<ReadObservation>& observation,
                                        DWORD timeout) noexcept {
    if (!observation) return {};
    std::array<HANDLE, kMaxSubscriptions> handles{};
    RetiredReadReport report;
    DWORD count = 0;
    for (DWORD n = 0; n < observation->count; ++n) {
        const auto& state = observation->states[n];
        std::lock_guard lock(state->mutex);
        // A read that returned before submit() can reuse the existing worker;
        // never wait for that current source's idle thread to exit.
        if (state->stop && state->thread) handles[count++] = state->thread;
    }
    report.observed = observation->count;
    if (count) (void)WaitForMultipleObjects(count, handles.data(), TRUE, timeout);
    for (DWORD n = 0; n < observation->count; ++n) {
        const auto& state = observation->states[n];
        report.exited += state->thread && WaitForSingleObject(state->thread, 0) == WAIT_OBJECT_0;
        std::lock_guard lock(state->mutex);
        report.cancel_requests += state->cancel_requests - observation->cancel_requests[n];
        report.cancel_accepted += state->cancel_accepted - observation->cancel_accepted[n];
        report.reads_returned += state->observed_read_returned;
        report.reads_aborted += state->observed_read_returned && state->observed_read_error == ERROR_OPERATION_ABORTED;
        report.last_cancel_error = state->last_cancel_error;
        report.last_read_error = state->observed_read_error;
    }
    return report;
}
bool wait_for_workers(DWORD timeout) noexcept {
    // Hold State until the thread signals: weak slots cannot expire before the
    // OS thread itself exits, and no test uses an arbitrary settling sleep.
    std::array<std::shared_ptr<State>, kMaxSubscriptions> states;
    std::array<HANDLE, kMaxSubscriptions> handles{};
    DWORD count = 0;
    for (auto& slot : slots) if (auto state = slot.lock()) {
        if (!state->thread) return false;
        handles[count] = state->thread;
        states[count++] = std::move(state);
    }
    return !count || WaitForMultipleObjects(count, handles.data(), TRUE, timeout) == WAIT_OBJECT_0;
}
} // namespace testing
} // namespace panedock::managed_list

// Icon/property enrichment has separate, fixed capacity so a provider cannot hold up
// enumeration/first usable rows. All synchronization stays in this file.
#include "managed_list/icon_source.h"
#include "managed_list/icon_source_test.h"
#include <objbase.h>
#include <shobjidl.h>
#include <propsys.h>
#include <propvarutil.h>
#include <cmath>
#include <wrl/client.h>
namespace panedock::managed_list {
namespace {
struct IconState final {
    std::mutex mutex;
    std::condition_variable changed;
    HANDLE ready{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool stop{};
    std::uint64_t serial{};
    HANDLE done{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    bool running{};
    std::shared_ptr<testing::IconGate> gate;
    std::deque<IconRequest> pending;
    std::deque<IconResult> results;
    ~IconState() { if (ready) CloseHandle(ready); if (done) CloseHandle(done); }
};
std::array<std::weak_ptr<IconState>, kIconWorkers> icon_slots;
PropertyValue read_property(const IconRequest& request) {
    PropertyValue result;
    result.state = PropertyState::failed;
    PROPERTYKEY key{};
    std::wstring property(request.property.begin(), request.property.end());
    if (FAILED(PSGetPropertyKeyFromName(property.c_str(), &key)) &&
        FAILED(PSPropertyKeyFromString(property.c_str(), &key))) return result;
    const auto path = request.key.parsing_name();
    Microsoft::WRL::ComPtr<IShellItem2> item;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item)))) return result;
    PROPVARIANT value{};
    struct Clear { PROPVARIANT& value; ~Clear() { PropVariantClear(&value); } } clear{value};
    if (FAILED(item->GetProperty(key, &value))) return result;
    if (value.vt == VT_EMPTY || value.vt == VT_NULL) {
        result.state = PropertyState::missing; return result;
    }
    if (value.vt == VT_UI8 || value.vt == VT_UI4 || value.vt == VT_UI2 || value.vt == VT_UI1) {
        ULONGLONG number{};
        if (FAILED(PropVariantToUInt64(value, &number))) return result;
        result.number = number; result.numeric = true;
    } else if (value.vt == VT_I8 || value.vt == VT_I4 || value.vt == VT_I2 || value.vt == VT_I1) {
        LONGLONG number{};
        if (FAILED(PropVariantToInt64(value, &number))) return result;
        result.number = number; result.numeric = true;
    } else if (value.vt == VT_R8 || value.vt == VT_R4) {
        double number{};
        if (FAILED(PropVariantToDouble(value, &number)) || !std::isfinite(number)) return result;
        result.number = number; result.numeric = true;
    } else if (value.vt == VT_FILETIME) {
        result.number = (static_cast<std::uint64_t>(value.filetime.dwHighDateTime) << 32) |
            value.filetime.dwLowDateTime;
        result.numeric = true;
    } else {
        // Bounded conversion: a provider cannot make a retained property string grow.
        wchar_t text[129]{};
        if (FAILED(PropVariantToString(value, text, ARRAYSIZE(text)))) return result;
        result.text = text;
    }
    result.state = PropertyState::value;
    return result;
}
void run_icons(std::shared_ptr<IconState> state) noexcept {
    struct Done { HANDLE event; ~Done() { SetEvent(event); } } done{state->done};
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    struct Apartment { HRESULT result; ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); } } cleanup{apartment};
    for (;;) {
        IconRequest request;
        std::uint64_t serial{};
        {
            std::unique_lock lock(state->mutex);
            state->changed.wait(lock, [&] { return state->stop || !state->pending.empty(); });
            if (state->stop) return;
            request = std::move(state->pending.front()); state->pending.pop_front();
            serial = state->serial;
            state->running = true;
        }
        IconResult result{std::move(request), {}, {}};
        result.property.state = PropertyState::failed;
        try {
            if (state->gate) {
                SetEvent(state->gate->entered);
                WaitForSingleObject(state->gate->release, INFINITE);
                const int size = result.request.key.size;
                result.image.pixels.assign(static_cast<std::size_t>(size * size), 0xff336699);
                result.image.mask.assign(static_cast<std::size_t>(((size + 15) / 16) * 2 * size), 0);
            } else if (result.request.key.cloud.metadata_only()) {
                // PD-235: no item-specific icon/overlay, thumbnail or property
                // handler has a documented non-hydrating contract here. Keep
                // local placeholders and unavailable optional properties.
                result.property.state = PropertyState::missing;
            } else if (SUCCEEDED(apartment) && !result.request.property.empty())
                result.property = read_property(result.request);
            else if (SUCCEEDED(apartment)) {
                const auto target = result.request.key.parsing_name();
                result.image = result.request.key.thumbnail ?
                    shell_core::load_item_thumbnail(target, result.request.key.size) :
                    shell_core::load_item_icon(target, result.request.key.size);
            }
        } catch (...) { /* Decorative failure keeps a usable placeholder. */ }
        std::unique_lock lock(state->mutex);
        state->running = false;
        if (state->gate && state->results.size() == kIconResults) SetEvent(state->gate->backpressure);
        state->changed.wait(lock, [&] { return state->stop || state->serial != serial ||
            state->results.size() < kIconResults; });
        if (state->stop) return;
        if (state->serial != serial) continue;
        try { state->results.push_back(std::move(result)); SetEvent(state->ready); }
        catch (...) { /* No repeated wakeups or retry loop on allocation failure. */ }
    }
}
class WorkerIcons final : public IconSource {
    std::shared_ptr<IconState> state_;
  public:
    explicit WorkerIcons(std::shared_ptr<IconState> state) : state_(std::move(state)) {}
    ~WorkerIcons() override {
        // No join: a stuck Shell call owns its apartment/state and bounded slot
        // until return. It never holds a Pane, HWND or UI-owned object.
        std::lock_guard lock(state_->mutex);
        state_->stop = true; ++state_->serial;
        state_->pending.clear(); state_->results.clear();
        ResetEvent(state_->ready); state_->changed.notify_all();
    }
    bool replace(std::vector<IconRequest> requests) noexcept override try {
        if (requests.size() > kIconInterest) return false;
        std::size_t bytes{};
        for (const auto& r : requests) {
            bytes += owned_icon_request_bytes(r);
            const auto& identity = r.key.identity;
            const auto& child = r.key.child_location;
            if (bytes > kIconValueBytes || !valid_shell_location(r.key.location) ||
                !valid_shell_location(r.request.location) || identity.key.empty() ||
                identity.key.size() > (identity.shell_item ? kMaxPath : kMaxName) ||
                (!identity.shell_item && (child || identity.key == L"." || identity.key == L".." ||
                    identity.key.find_first_of(L"\\/:*?\"") != std::wstring::npos)) ||
                (child && (child->parsing_name != identity.key ||
                    !valid_shell_location(*child))) ||
                r.request.binding.group_id.size() > 256 || r.request.binding.tab_id.size() > 256 ||
                r.property.size() > 128 || r.property_slot < 0 || r.property_slot > 2 ||
                r.key.size < 16 || r.key.size > kMaxImageSize) return false;
        }
        std::deque<IconRequest> pending;
        for (auto& r : requests) pending.push_back(std::move(r));
        std::lock_guard lock(state_->mutex);
        ++state_->serial;
        state_->pending.swap(pending); state_->results.clear(); ResetEvent(state_->ready);
        state_->changed.notify_all(); return true;
    } catch (...) { cancel(); return false; }
    std::optional<IconResult> take() noexcept override {
        std::lock_guard lock(state_->mutex);
        if (state_->results.empty()) { ResetEvent(state_->ready); return {}; }
        auto result = std::move(state_->results.front()); state_->results.pop_front();
        if (state_->results.empty()) ResetEvent(state_->ready);
        state_->changed.notify_all(); return result;
    }
    HANDLE event() const noexcept override { return state_->ready; }
    void cancel() noexcept override {
        std::lock_guard lock(state_->mutex);
        ++state_->serial; state_->pending.clear(); state_->results.clear();
        ResetEvent(state_->ready); state_->changed.notify_all();
    }
};
std::unique_ptr<IconSource> acquire_icons(std::shared_ptr<testing::IconGate> gate = {}) {
    for (auto& slot : icon_slots) {
        if (!slot.expired()) continue;
        auto state = std::make_shared<IconState>();
        if (!state->ready || !state->done) return {};
        state->gate = std::move(gate);
        auto source = std::make_unique<WorkerIcons>(state);
        slot = state;
        std::thread worker(run_icons, state);
        worker.detach();
        return source;
    }
    return {};
}
} // namespace
std::unique_ptr<IconSource> make_icon_source() noexcept try { return acquire_icons(); }
catch (...) { return {}; }
namespace testing {
IconGate::~IconGate() { CloseHandle(entered); CloseHandle(release); CloseHandle(backpressure); }
std::unique_ptr<IconSource> make_gated_icons(std::shared_ptr<IconGate> gate) noexcept try {
    if (!gate || !gate->entered || !gate->release || !gate->backpressure) return {};
    return acquire_icons(std::move(gate));
} catch (...) { return {}; }
IconCounts icon_counts() noexcept {
    IconCounts result;
    for (auto& slot : icon_slots) if (auto state = slot.lock()) {
        std::lock_guard lock(state->mutex);
        ++result.workers; result.running += state->running;
        result.pending += state->pending.size(); result.results += state->results.size();
    }
    return result;
}
bool wait_for_icon_workers(DWORD timeout) noexcept {
    std::array<std::shared_ptr<IconState>, kIconWorkers> states;
    std::array<HANDLE, kIconWorkers> handles{};
    DWORD count = 0;
    for (auto& slot : icon_slots) if (auto state = slot.lock()) {
        handles[count] = state->done; states[count++] = std::move(state);
    }
    return !count || WaitForMultipleObjects(count, handles.data(), TRUE, timeout) == WAIT_OBJECT_0;
}
} // namespace testing
} // namespace panedock::managed_list
