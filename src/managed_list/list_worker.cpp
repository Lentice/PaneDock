#include "managed_list/list_worker.h"
#include "managed_list/list_worker_test.h"
#include <array>
#include <algorithm>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <objbase.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include "shell_core/shell_core.h"

namespace panedock::managed_list {
HiddenFilePolicy read_hidden_file_policy() noexcept {
    SHELLSTATEW state{};
    SHGetSetSettings(reinterpret_cast<LPSHELLSTATE>(&state), SSF_SHOWALLOBJECTS | SSF_SHOWSUPERHIDDEN, FALSE);
    return {state.fShowAllObjects != 0, state.fShowSuperHidden != 0};
}
namespace {
// The Type and Duration cells of a local file follow from its extension alone, so they
// are filled while the folder is enumerated instead of through one worker request per
// visible row. Scrolling then never shows Loading... for them (PD-256). One lookup per
// distinct extension; about 0.06 ms each.
class TypeCells final {
    struct Cell { std::wstring type; bool no_duration{}; bool hide_extension{}; };
    std::unordered_map<std::wstring, Cell> files_;
    std::optional<Cell> folder_;
    static Cell lookup(const std::wstring& extension, bool directory) {
        Cell cell;
        SHFILEINFOW info{};
        const std::wstring name = L"file" + extension;
        // The same attribute-only call yields the Shell's display name for a probe
        // leaf, so "file" back means the extension is hidden for this type: Explorer's
        // "File name extensions" option and per-type NeverShowExt/AlwaysShowExt in
        // one per-extension lookup, with no file system access.
        if (SHGetFileInfoW(name.c_str(), directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
                           &info, sizeof(info),
                           SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES | (directory ? 0 : SHGFI_DISPLAYNAME))) {
            cell.type = info.szTypeName;
            cell.hide_extension = !directory && !extension.empty() &&
                std::wstring_view(info.szDisplayName) == L"file";
        }
        PERCEIVED perceived{};
        PERCEIVEDFLAG flags{};
        // Only types known not to be audio/video are ruled out; unknown ones keep Loading.
        cell.no_duration = directory || (!extension.empty() &&
            SUCCEEDED(AssocGetPerceivedType(extension.c_str(), &perceived, &flags, nullptr)) &&
            perceived != PERCEIVED_TYPE_AUDIO && perceived != PERCEIVED_TYPE_VIDEO &&
            perceived != PERCEIVED_TYPE_UNSPECIFIED);
        return cell;
    }
  public:
    // Fills a local row whose name is still the real leaf: moves the leaf into
    // identity.key and sets the Shell display text as name.
    void fill(Row& row) {
        if (row.identity.key.empty()) row.identity = {row.name, false};
        const auto& leaf = row.identity.key;
        const Cell* cell = nullptr;
        if (row.directory) {
            if (!folder_) folder_ = lookup({}, true);
            cell = &*folder_;
        } else {
            std::wstring extension;
            const auto dot = leaf.rfind(L'.');
            if (dot != std::wstring::npos) {
                extension.assign(leaf, dot, std::wstring::npos);
                CharLowerBuffW(extension.data(), static_cast<DWORD>(extension.size()));
            }
            auto found = files_.find(extension);
            if (found == files_.end()) found = files_.emplace(extension, lookup(extension, false)).first;
            cell = &found->second;
        }
        if (!cell->type.empty() && cell->type.size() <= 128) {
            row.properties[0].state = PropertyState::value;
            row.properties[0].text = cell->type;
        }
        if (cell->no_duration) row.properties[1].state = PropertyState::missing;
        if (cell->hide_extension) row.name = display_leaf(leaf, row.directory, true);
    }
};
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
    // Phase of the last batch accepted into results; lets a recovery rescan
    // tell whether it completed without re-deriving that from each return.
    Phase published_phase{Phase::loading};
    ~State() { if (control) CloseHandle(control); if (thread) CloseHandle(thread); if (ready) CloseHandle(ready); }
};
// UI creates subscriptions on its STA. The first kMaxSubscriptions entries are
// live slots; the tail is the retired reserve. A blocked abandoned worker keeps
// its reserve (or, when the reserve is full, live) entry, resources and event
// until it exits; reopening cannot create an unbounded thread leak.
constexpr std::size_t kMaxWorkers = kMaxSubscriptions + kMaxRetiredWorkers;
std::array<std::weak_ptr<State>, kMaxWorkers> slots;

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
    const auto phase = batch.phase;
    state->results.push_back(std::move(batch));
    state->published_phase = phase;
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
    // 64 KiB is also the largest buffer ReadDirectoryChangesW accepts for a
    // directory on a network share; do not grow it.
    DWORD arm(DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE |
            FILE_NOTIFY_CHANGE_ATTRIBUTES) {
        ResetEvent(io.hEvent);
        if (!ReadDirectoryChangesW(directory, buffer.data(), static_cast<DWORD>(buffer.size()),
            FALSE, filter, nullptr, &io, nullptr)) return GetLastError();
        pending = true;
        return ERROR_SUCCESS;
    }
};
// Watches for a namespace location that is backed by local folders. An error
// is reported after the initial read, like the filesystem watch.
struct NamespaceWatches final {
    std::vector<std::unique_ptr<DirectoryWatch>> watches;
    DWORD error{};
};
// Every ordinary filesystem path is watched, local or network (UNC shares and
// mapped drives, like Explorer). A directory that refuses a watch reports
// watch_error, which the status bar shows as "Automatic refresh unavailable".
bool watchable_path(const std::wstring& path) {
    return is_filesystem_location(path) && path.size() <= kMaxPath &&
        !path.starts_with(L"\\\\.\\") && path.find_first_of(L"*?") == std::wstring::npos &&
        (!path.starts_with(L"\\\\") || path.find(L'\\', 2) != std::wstring::npos);
}
// A burst of changes (extracting or copying thousands of files) overflows the
// notification detail. Coalesce it into one recovery rescan once the folder
// has been quiet for kRescanQuietMs; continuous churn still rescans every
// kRescanMaxDelayMs so a long copy keeps updating. Armed only by a change
// event; an idle folder waits INFINITE (0% CPU, no I/O). Do not replace this
// with a retry limit: churn is not failure, and a strike-out left the list
// frozen mid-copy. A rescan that fails ends the watch instead.
constexpr DWORD kRescanQuietMs = 250;
constexpr ULONGLONG kRescanMaxDelayMs = 2000;
struct RescanSettle final {
    bool pending{};
    ULONGLONG since{};
    void note() noexcept { if (!pending) { pending = true; since = GetTickCount64(); } }
    DWORD timeout() const noexcept {
        if (!pending) return INFINITE;
        const auto elapsed = GetTickCount64() - since;
        return elapsed >= kRescanMaxDelayMs ? 0 :
            static_cast<DWORD>(std::min<ULONGLONG>(kRescanQuietMs, kRescanMaxDelayMs - elapsed));
    }
    bool due(bool timed_out) const noexcept {
        return pending && (timed_out || GetTickCount64() - since >= kRescanMaxDelayMs);
    }
};
// The Desktop is the merged user + public folders; any other namespace folder
// that has a filesystem path is watched through that path. Virtual locations
// (This PC, Recycle Bin) have none and stay manual-refresh.
std::vector<std::wstring> namespace_watch_directories(const core::ShellLocation& resolved,
                                                      bool namespace_root) {
    std::vector<std::wstring> directories;
    const auto add_known = [&](const KNOWNFOLDERID& id) {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DONT_VERIFY, nullptr, &path)) && path)
            directories.emplace_back(path);
        CoTaskMemFree(path);
    };
    wchar_t desktop_id[39]{};
    StringFromGUID2(FOLDERID_Desktop, desktop_id, ARRAYSIZE(desktop_id));
    if (namespace_root || _wcsicmp(resolved.known_folder_identity.c_str(), desktop_id) == 0) {
        add_known(FOLDERID_Desktop);
        add_known(FOLDERID_PublicDesktop);
    } else if (!resolved.fallback_path.empty()) {
        directories.push_back(resolved.fallback_path);
    }
    std::vector<std::wstring> watched;
    for (auto& path : directories)
        if (watchable_path(path) && std::find(watched.begin(), watched.end(), path) == watched.end())
            watched.push_back(std::move(path));
    return watched;
}
void arm_namespace_watches(const std::shared_ptr<State>& state, std::uint64_t serial,
                           const std::vector<std::wstring>& directories, NamespaceWatches& out) {
    for (const auto& path : directories) {
        if (!interested(state, serial)) return;
        auto watch = std::make_unique<DirectoryWatch>();
        if (!watch->io.hEvent) { out.error = ERROR_NOT_ENOUGH_MEMORY; return; }
        watch->directory = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (watch->directory == INVALID_HANDLE_VALUE) { out.error = GetLastError(); return; }
        // Network targets (redirected folders, junctions to a share) are
        // watched too, like Explorer; a refusing server reports watch_error.
        if (const DWORD armed = watch->arm()) { out.error = armed; return; }
        out.watches.push_back(std::move(watch));
    }
}
// Used and total size of a local fixed or removable drive root such as "C:\".
// Network and optical drives are skipped: their calls can block on a provider,
// and the cards simply show no bar for them. Failure (no media) leaves it unknown.
void fill_drive_space(Row& row, const std::wstring& root) {
    if (root.size() != 3 || root[1] != L':' || root[2] != L'\\') return;
    const UINT type = GetDriveTypeW(root.c_str());
    if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) return;
    DWORD previous{};
    SetThreadErrorMode(SEM_FAILCRITICALERRORS, &previous); // never raise a "no disk" dialog
    ULARGE_INTEGER total{}, free{};
    if (GetDiskFreeSpaceExW(root.c_str(), nullptr, &total, &free) && total.QuadPart != 0 &&
        free.QuadPart <= total.QuadPart) {
        row.capacity_bytes = total.QuadPart;
        row.free_bytes = free.QuadPart;
    }
    SetThreadErrorMode(previous, nullptr);
}
void enumerate_shell_namespace(const std::shared_ptr<State>& state, std::uint64_t serial,
                               const Request& request, bool recovery = false,
                               NamespaceWatches* watch_out = nullptr) {
    Batch metadata{request, {}, Phase::loading, 0};
    metadata.notification = NotificationCapability::manual_refresh;
    if (recovery) metadata.change = Change::rescan;
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
    if (watch_out) {
        // Arm BEFORE the read so a change during enumeration is not lost.
        const auto directories = namespace_watch_directories(resolved, ILIsEmpty(absolute.value));
        if (!directories.empty()) {
            metadata.notification = NotificationCapability::filesystem_watch;
            arm_namespace_watches(state, serial, directories, *watch_out);
            if (!interested(state, serial)) return;
        }
    }
    const bool this_pc = is_this_pc_location(resolved) || is_this_pc_location(request.location);
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
    if (recovery) {
        // A rescan only reconciles rows; the location metadata was already accepted.
        metadata.resolved_location.reset();
        metadata.parent_location.reset();
        metadata.parent_outcome = ParentOutcome::unresolved;
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
        const auto policy = read_hidden_file_policy();
        constexpr SHCONTF kIncludeSuperHidden = static_cast<SHCONTF>(0x10000);
        return folder->EnumObjects(nullptr,
            SHCONTF_FOLDERS | SHCONTF_NONFOLDERS |
                (policy.show_hidden ? SHCONTF_INCLUDEHIDDEN : 0) |
                (policy.show_hidden && policy.show_protected ? kIncludeSuperHidden : 0),
            &enumerator);
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
        if (this_pc) fill_drive_space(row, child_location.fallback_path);
        // A filesystem child's PIDL already carries its size and write time, so
        // this reads no provider; virtual items without find data stay at 0.
        WIN32_FIND_DATAW find{};
        if (SUCCEEDED(provider_call(state, serial, [&] {
                return SHGetDataFromIDListW(folder.Get(), relative, SHGDFIL_FINDDATA,
                                            &find, sizeof(find));
            }))) {
            const auto found = basic_row(find, {});
            row.bytes = found.bytes;
            row.date_modified = found.date_modified;
            row.cloud = found.cloud;
        }
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
        enumerate_shell_namespace(state, serial, request, recovery);
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
    const auto hidden_policy = read_hidden_file_policy();
    TypeCells type_cells;
    for (;;) {
        if (!interested(state, serial)) return;
        std::wstring name(data.cFileName);
        if (name != L"." && name != L".." && !hidden_policy.hides(data.dwFileAttributes)) {
            if (count == kMaxRows) { batch.phase = Phase::limited; break; }
            if (name.size() > kMaxName) {
                batch.phase = Phase::error; batch.error = ERROR_BUFFER_OVERFLOW; break;
            }
            batch.rows.push_back(basic_row(data, std::move(name)));
            type_cells.fill(batch.rows.back());
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
bool leaf_name(std::wstring_view name) {
    return !name.empty() && name.size() <= kMaxName && name != L"." && name != L".." &&
        name.find_first_of(L"\\/:*?\"") == std::wstring::npos;
}
// An 8.3 alias such as "REPORT~1.TXT". The kernel can report one; a deleted
// item's long name cannot be recovered from it, so its removal needs a rescan.
bool possible_short_name(std::wstring_view name) {
    if (name.size() > 12 || name.find(L'~') == std::wstring_view::npos) return false;
    return std::none_of(name.begin(), name.end(), [](wchar_t c) { return IsCharLowerW(c); });
}
enum class Decoded { delta, rescan, lost };
// Notification detail has a fixed cap. Exceeding it discards detail and requests
// ONE recovery, just like ERROR_NOTIFY_ENUM_DIR / a zero-byte completion.
Decoded decode_changes(const std::shared_ptr<State>& state, std::uint64_t serial,
                       const DirectoryWatch& watch, DWORD bytes, Batch& batch,
                       std::wstring& old_name) {
    std::vector<std::wstring> names;
    std::size_t offset = 0;
    constexpr auto header = offsetof(FILE_NOTIFY_INFORMATION, FileName);
    while (offset < bytes) {
        if (bytes - offset < header) return Decoded::rescan;
        const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(watch.buffer.data() + offset);
        if (info->FileNameLength % sizeof(wchar_t) || info->FileNameLength > bytes - offset - header)
            return Decoded::rescan;
        std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
        if (!leaf_name(name)) return Decoded::rescan;
        if (info->Action < FILE_ACTION_ADDED || info->Action > FILE_ACTION_RENAMED_NEW_NAME) return Decoded::rescan;
        if (info->Action == FILE_ACTION_RENAMED_OLD_NAME) {
            // A split rename pair must not first delete the selected identity.
            // Kernel order pairs old/new; an unpaired old name needs recovery.
            if (!old_name.empty()) return Decoded::rescan;
            old_name = name;
        }
        else if (info->Action == FILE_ACTION_RENAMED_NEW_NAME && !old_name.empty()) {
            batch.renamed.emplace_back(old_name, name);
            if (std::find(names.begin(), names.end(), old_name) == names.end()) names.push_back(old_name);
            old_name.clear();
            if (batch.renamed.size() > kBatchRows) return Decoded::rescan;
        } else if (!old_name.empty()) return Decoded::rescan;
        if (info->Action != FILE_ACTION_RENAMED_OLD_NAME &&
            std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
        if (names.size() > kBatchRows) return Decoded::rescan;
        if (!info->NextEntryOffset) break;
        if (info->NextEntryOffset < header + info->FileNameLength ||
            info->NextEntryOffset % alignof(DWORD) || info->NextEntryOffset >= bytes - offset) return Decoded::rescan;
        offset += info->NextEntryOffset;
    }
    const auto hidden_policy = read_hidden_file_policy();
    TypeCells type_cells;
    const auto remove = [&](const std::wstring& leaf) {
        if (std::find(batch.removed.begin(), batch.removed.end(), leaf) == batch.removed.end())
            batch.removed.push_back(leaf);
    };
    for (const auto& name : names) {
        if (!interested(state, serial)) return Decoded::rescan;
        auto path = batch.request.location.parsing_name;
        if (path.back() != L'\\') path += L'\\';
        path += name;
        WIN32_FIND_DATAW data{};
        FindHandle find{FindFirstFileExW(path.c_str(), FindExInfoBasic, &data,
            FindExSearchNameMatch, nullptr, 0)};
        if (find.value == INVALID_HANDLE_VALUE) {
            const DWORD error = GetLastError();
            // The leaf is gone (FILE_NOT_FOUND). PATH_NOT_FOUND means the
            // displayed folder's own path no longer resolves: it, or a parent,
            // was renamed or deleted while this handle kept watching it.
            if (error == ERROR_PATH_NOT_FOUND) return Decoded::lost;
            if (error != ERROR_FILE_NOT_FOUND || possible_short_name(name)) return Decoded::rescan;
            remove(name);
            continue;
        }
        // The lookup is case-insensitive and resolves 8.3 aliases, but a local
        // identity compares exactly. Rows always carry the real long name; a
        // notified spelling that differs (case-only rename, short name) is
        // reported removed so no ghost row keeps the old spelling.
        std::wstring real(data.cFileName);
        if (!leaf_name(real)) return Decoded::rescan;
        if (real != name) remove(name);
        if (hidden_policy.hides(data.dwFileAttributes)) remove(real); // became hidden: leaves the list
        else if (std::none_of(batch.rows.begin(), batch.rows.end(),
                     [&](const Row& row) { return row.identity.key == real; })) {
            batch.rows.push_back(basic_row(data, std::move(real)));
            type_cells.fill(batch.rows.back());
        }
        if (batch.removed.size() > kBatchRows) return Decoded::rescan;
    }
    return Decoded::delta;
}
// The displayed folder's existence, probed only after a change event says it
// may be gone. Our own watch handle must already be closed: while it is open,
// a deleted folder stays delete-pending and reads as access denied.
bool directory_exists(const std::wstring& path, DWORD& error) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) { error = GetLastError(); return false; }
    error = (attributes & FILE_ATTRIBUTE_DIRECTORY) ? ERROR_SUCCESS : ERROR_DIRECTORY;
    return error == ERROR_SUCCESS;
}
// Only these mean "no longer there". Network errors (server or share
// unreachable) keep the location and report watch_error instead.
bool missing_error(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
        error == ERROR_INVALID_DRIVE || error == ERROR_NOT_READY || error == ERROR_DIRECTORY;
}
// "C:\a\b" -> "C:\a" -> "C:\" -> ""; "\\srv\share\a" -> "\\srv\share" -> "".
std::wstring parent_directory(std::wstring path) {
    while (path.size() > 3 && path.back() == L'\\') path.pop_back();
    if (path.size() <= 3 && path.size() >= 2 && path[1] == L':') return {};
    const auto slash = path.rfind(L'\\');
    if (slash == std::wstring::npos) return {};
    if (path.starts_with(L"\\\\")) {
        const auto server = path.find(L'\\', 2);
        if (server == std::wstring::npos || path.find(L'\\', server + 1) == std::wstring::npos) return {};
        return path.substr(0, slash);
    }
    return slash == 2 && path[1] == L':' ? path.substr(0, 3) : path.substr(0, slash);
}
// Where the still-open handle now points ("\\?\C:\x" -> "C:\x").
std::wstring final_directory_path(HANDLE directory) {
    std::wstring value(kMaxPath + 1, L'\0');
    const DWORD length = GetFinalPathNameByHandleW(directory, value.data(),
        static_cast<DWORD>(value.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!length || length >= value.size()) return {};
    value.resize(length);
    if (value.starts_with(L"\\\\?\\UNC\\")) return L"\\\\" + value.substr(8);
    if (value.starts_with(L"\\\\?\\")) value.erase(0, 4);
    return value;
}
std::vector<std::wstring_view> path_components(std::wstring_view path) {
    std::vector<std::wstring_view> parts;
    for (std::size_t start = 0; start < path.size();) {
        auto end = path.find(L'\\', start);
        if (end == std::wstring_view::npos) end = path.size();
        if (end > start) parts.push_back(path.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}
// Follow a rename only when exactly one component of the handle's normalized
// path changed between opening the watch and losing the folder (the folder or
// one parent renamed in place). A move, a Recycle Bin delete or a removed
// volume never qualifies: those go to the nearest existing parent. Both sides
// come from GetFinalPathNameByHandleW, so 8.3 and mapped-drive spellings
// cannot fake a difference. Returns the new path in the caller's spelling.
std::wstring renamed_path(const std::wstring& path, const std::wstring& opened, const std::wstring& now) {
    const auto before = path_components(opened), after = path_components(now);
    if (before.size() != after.size() || before.size() < 2) return {};
    std::size_t changed = before.size();
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (CompareStringOrdinal(before[i].data(), static_cast<int>(before[i].size()),
                after[i].data(), static_cast<int>(after[i].size()), TRUE) == CSTR_EQUAL) continue;
        if (changed != before.size()) return {};
        changed = i;
    }
    if (changed == before.size()) return {};
    const auto from_end = before.size() - 1 - changed;
    const auto own = path_components(path);
    const std::size_t root = path.starts_with(L"\\\\") ? 2 : 1; // never rewrite C: or \\server\share
    if (from_end + root >= own.size()) return {};
    const auto& part = own[own.size() - 1 - from_end];
    const auto start = static_cast<std::size_t>(part.data() - path.data());
    std::wstring result = path.substr(0, start);
    result.append(after[changed]);
    result.append(path, start + part.size(), std::wstring::npos);
    return result.size() <= kMaxPath ? result : std::wstring{};
}
// The Explorer-like destination for a displayed folder that no longer exists,
// or nothing when it still exists or only a network path became unreachable.
std::optional<core::ShellLocation> lost_location_target(const std::wstring& path,
                                                        const std::wstring& opened_final,
                                                        const std::wstring& final_path, bool network) {
    DWORD error{};
    if (directory_exists(path, error) || !missing_error(error)) return {};
    if (!opened_final.empty() && !final_path.empty())
        if (auto renamed = renamed_path(path, opened_final, final_path);
            !renamed.empty() && directory_exists(renamed, error))
            return core::ShellLocation{std::move(renamed), {}, {}};
    for (auto parent = parent_directory(path); !parent.empty(); parent = parent_directory(parent))
        if (directory_exists(parent, error)) return core::ShellLocation{parent, {}, {}};
    if (network) return {};
    return core::ShellLocation{L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}", {}, {}}; // This PC
}
// A namespace folder backed by local directories (the Desktop) is watched, but
// its changes are not decoded: any event coalesces into one full rescan.
void watch_shell_namespace(const std::shared_ptr<State>& state, std::uint64_t serial,
                           const Request& request) {
    NamespaceWatches set;
    enumerate_shell_namespace(state, serial, request, false, &set);
    { std::lock_guard lock(state->mutex); state->initial_complete = true; }
    // Virtual locations explicitly require manual Refresh.
    if (set.watches.empty() && !set.error) return;
    if (!interested(state, serial)) return;
    if (set.error) {
        publish(state, serial, {request, {}, Phase::complete, set.error, Change::watch_error}); return;
    }
    { std::lock_guard lock(state->mutex); state->watch_open = true; }
    if (!publish(state, serial, {request, {}, Phase::complete, 0, Change::watching})) return;
    std::vector<HANDLE> handles{state->control};
    for (const auto& watch : set.watches) handles.push_back(watch->io.hEvent);
    RescanSettle settle; // every event coalesces into one rescan after a quiet period
    for (;;) {
        {
            std::lock_guard lock(state->mutex);
            state->running = false; // control event releases this idle wait
        }
        const DWORD wake = WaitForMultipleObjects(static_cast<DWORD>(handles.size()),
                                                  handles.data(), FALSE, settle.timeout());
        DWORD error = 0;
        {
            std::lock_guard lock(state->mutex);
            if (state->stop || state->serial != serial) return;
            state->running = true;
            if (state->recovery) settle.note();
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
        for (auto& watch : set.watches) {
            if (WaitForSingleObject(watch->io.hEvent, 0) != WAIT_OBJECT_0) continue;
            DWORD bytes{};
            const BOOL success = GetOverlappedResult(watch->directory, &watch->io, &bytes, FALSE);
            error = success ? ERROR_SUCCESS : GetLastError();
            watch->pending = false;
            if (error && error != ERROR_NOTIFY_ENUM_DIR) {
                publish(state, serial, {request, {}, Phase::complete, error, Change::watch_error}); return;
            }
            settle.note();
            if (!interested(state, serial)) return;
            // Kernel retains changes between reads; rearm before scanning.
            error = watch->arm();
            if (error) {
                publish(state, serial, {request, {}, Phase::complete, error, Change::watch_error}); return;
            }
        }
        if (!settle.due(wake == WAIT_TIMEOUT) || !interested(state, serial)) continue;
        settle = {};
        { std::lock_guard lock(state->mutex); state->recovering = true; }
        if (!publish(state, serial, {request, {}, Phase::loading, 0, Change::rescan_begin})) return;
        enumerate_shell_namespace(state, serial, request, true);
        { std::lock_guard lock(state->mutex); state->recovering = false; }
    }
}
// The watch of the displayed folder's parent, for the one event the folder's
// own handle never reports: the folder itself deleted to the Recycle Bin,
// moved or renamed (the handle follows the object). Directory names only.
constexpr DWORD kParentWatchFilter = FILE_NOTIFY_CHANGE_DIR_NAME;
bool parent_event_names(const DirectoryWatch& watch, DWORD bytes, std::wstring_view leaf) {
    if (!bytes) return true; // overflow: probe
    constexpr auto header = offsetof(FILE_NOTIFY_INFORMATION, FileName);
    for (std::size_t offset = 0; offset < bytes;) {
        if (bytes - offset < header) return true;
        const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(watch.buffer.data() + offset);
        if (info->FileNameLength % sizeof(wchar_t) || info->FileNameLength > bytes - offset - header)
            return true;
        if ((info->Action == FILE_ACTION_REMOVED || info->Action == FILE_ACTION_RENAMED_OLD_NAME) &&
            CompareStringOrdinal(info->FileName, static_cast<int>(info->FileNameLength / sizeof(wchar_t)),
                leaf.data(), static_cast<int>(leaf.size()), TRUE) == CSTR_EQUAL) return true;
        if (!info->NextEntryOffset) return false;
        if (info->NextEntryOffset % alignof(DWORD) || info->NextEntryOffset >= bytes - offset) return true;
        offset += info->NextEntryOffset;
    }
    return false;
}
std::unique_ptr<DirectoryWatch> open_directory_watch(const std::wstring& path, DWORD filter, DWORD& error) {
    auto watch = std::make_unique<DirectoryWatch>();
    if (!watch->io.hEvent) { error = ERROR_NOT_ENOUGH_MEMORY; return {}; }
    watch->directory = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (watch->directory == INVALID_HANDLE_VALUE) { error = GetLastError(); return {}; }
    if ((error = watch->arm(filter))) return {};
    return watch;
}
void enumerate_and_watch(const std::shared_ptr<State>& state, std::uint64_t serial,
                         const Request& request) {
    if (uses_shell_namespace(request.location)) {
        watch_shell_namespace(state, serial, request);
        return;
    }
    const auto& path = request.location.parsing_name;
    // The controlled harness share is never opened: it names no real server.
    const bool watched = watchable_path(path) &&
        !(state->gate && path.starts_with(L"\\\\pd226-controlled\\share"));
    // All drive probing stays on this worker, never the STA.
    bool network = path.starts_with(L"\\\\") &&
        (!path.starts_with(L"\\\\?\\") || path.starts_with(L"\\\\?\\UNC\\"));
    UINT drive_type = DRIVE_UNKNOWN;
    if (!interested(state, serial)) return;
    if (watched && path.size() >= 3 && path[1] == L':' && path[2] == L'\\') {
        drive_type = GetDriveTypeW(path.substr(0, 3).c_str());
        network = drive_type == DRIVE_REMOTE;
    }
    DWORD error = ERROR_SUCCESS;
    std::unique_ptr<DirectoryWatch> watch;
    // Arm BEFORE the initial read to close the enumerate/subscribe loss window.
    if (watched) watch = open_directory_watch(path, FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES, error);
    if (!interested(state, serial)) return;
    enumerate(state, serial, request);
    Phase listed;
    {
        std::lock_guard lock(state->mutex);
        state->initial_complete = true;
        listed = state->published_phase;
    }
    // An unreadable location shows its read error only; Refresh retries both.
    if (!watched || listed == Phase::error || !interested(state, serial)) return;
    if (!watch) {
        publish(state, serial, {request, {}, Phase::complete, error, Change::watch_error}); return;
    }
    { std::lock_guard lock(state->mutex); state->watch_open = true; }
    Batch watching{request, {}, Phase::complete, 0, Change::watching};
    if (drive_type == DRIVE_REMOVABLE || drive_type == DRIVE_FIXED || drive_type == DRIVE_CDROM) {
        // Zero access: no media read, no lock. Only for the UI's eject registration.
        const wchar_t volume[]{L'\\', L'\\', L'.', L'\\', path[0], L':', L'\0'};
        const HANDLE device = CreateFileW(volume, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (device != INVALID_HANDLE_VALUE)
            watching.device_handle = std::shared_ptr<void>(device, [](void* value) { CloseHandle(value); });
    }
    if (!publish(state, serial, std::move(watching))) return;
    // Normalized once, to tell an in-place rename from a move when the folder is lost.
    const auto opened_final = final_directory_path(watch->directory);
    // Optional: a parent that cannot be watched (no parent, no access) only
    // loses early detection of a Recycle Bin delete of the folder itself.
    const auto parent_path = parent_directory(path);
    DWORD parent_error{};
    auto parent = parent_path.empty() ? nullptr :
        open_directory_watch(parent_path, kParentWatchFilter, parent_error);
    std::wstring leaf = path;
    while (leaf.size() > 3 && leaf.back() == L'\\') leaf.pop_back();
    leaf.erase(0, leaf.rfind(L'\\') + 1);
    // A folder that is gone sends the Pane to its replacement; any other
    // failure keeps the rows and reports that automatic refresh stopped.
    const auto lose = [&](DWORD reason) {
        const auto final_path = final_directory_path(watch->directory);
        watch.reset(); parent.reset(); // close first: a deleted folder is delete-pending until then
        if (auto target = lost_location_target(path, opened_final, final_path, network)) {
            Batch lost{request, {}, Phase::complete, 0, Change::location_lost};
            lost.parent_location = std::move(*target);
            lost.parent_outcome = ParentOutcome::value;
            publish(state, serial, std::move(lost));
        } else {
            publish(state, serial, {request, {}, Phase::complete, reason, Change::watch_error});
        }
    };
    std::wstring old_name;
    RescanSettle settle;
    for (;;) {
        {
            std::lock_guard lock(state->mutex);
            state->running = false; // control event releases this idle wait
        }
        const HANDLE handles[]{state->control, watch->io.hEvent, parent ? parent->io.hEvent : nullptr};
        const DWORD wake = WaitForMultipleObjects(parent ? 3 : 2, handles, FALSE, settle.timeout());
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
        bool probe = false;
        Batch batch{request, {}, Phase::complete, 0, Change::delta};
        if (WaitForSingleObject(watch->io.hEvent, 0) == WAIT_OBJECT_0) {
            DWORD bytes{};
            const BOOL success = GetOverlappedResult(watch->directory, &watch->io, &bytes, FALSE);
            error = success ? ERROR_SUCCESS : GetLastError();
            watch->pending = false;
            // A deleted folder fails its read (delete pending); a removed
            // volume or dropped share fails it too. lose() tells them apart.
            if (error && error != ERROR_NOTIFY_ENUM_DIR) { lose(error); return; }
            // While a rescan is pending its detail is moot: drain and re-arm.
            recovery = recovery || settle.pending || error == ERROR_NOTIFY_ENUM_DIR || !bytes;
            if (!recovery) switch (decode_changes(state, serial, *watch, bytes, batch, old_name)) {
                case Decoded::delta: break;
                case Decoded::rescan: recovery = true; break;
                case Decoded::lost: probe = true; break;
            }
            if (!interested(state, serial)) return;
            // Kernel retains changes between reads. Rearm before publishing or
            // scanning so UI backpressure cannot open a notification gap.
            if ((error = watch->arm())) { lose(error); return; }
        }
        if (parent && WaitForSingleObject(parent->io.hEvent, 0) == WAIT_OBJECT_0) {
            DWORD bytes{};
            const BOOL success = GetOverlappedResult(parent->directory, &parent->io, &bytes, FALSE);
            parent->pending = false;
            probe = probe || !success || parent_event_names(*parent, bytes, leaf);
            if (!success || parent->arm(kParentWatchFilter)) parent.reset();
        }
        if (!interested(state, serial)) return;
        if (probe) {
            DWORD missing{};
            if (!directory_exists(path, missing)) { lose(missing); return; }
            if (!batch.rows.empty() || !batch.removed.empty() || !batch.renamed.empty())
                recovery = true; // a lookup raced the probe: reconcile instead
        }
        if (recovery) {
            old_name.clear();
            settle.note();
        }
        if (settle.due(wake == WAIT_TIMEOUT)) {
            // Before rescanning, make sure the folder itself is still there.
            DWORD missing{};
            if (!directory_exists(path, missing)) { lose(missing); return; }
            settle = {};
            { std::lock_guard lock(state->mutex); state->recovering = true; }
            if (!publish(state, serial, {request, {}, Phase::loading, 0, Change::rescan_begin})) return;
            enumerate(state, serial, request, true);
            Phase scanned;
            {
                std::lock_guard lock(state->mutex);
                state->recovering = false;
                scanned = state->published_phase;
            }
            // The only bound: a rescan that cannot complete ends the watch.
            // The UI keeps its rows and offers Refresh; no retry loop here.
            if (scanned != Phase::complete && scanned != Phase::empty) return;
        } else if (!settle.pending && (!batch.rows.empty() || !batch.removed.empty())) {
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
    for (std::size_t n = 0; n < kMaxSubscriptions; ++n) {
        auto& slot = slots[n];
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
// UI STA only. Moves a stopped State out of its live slot into a free reserve
// entry so a successor can acquire immediately. A full reserve leaves it in its
// live slot (the pre-split behavior); it still exits after its OS call returns.
void retire(const std::shared_ptr<State>& state) noexcept {
    for (std::size_t live = 0; live < kMaxSubscriptions; ++live) {
        if (slots[live].lock() != state) continue;
        for (std::size_t n = kMaxSubscriptions; n < kMaxWorkers; ++n) {
            if (!slots[n].expired()) continue;
            slots[n] = std::move(slots[live]);
            slots[live].reset();
            return;
        }
        return;
    }
}
class LocalSource final : public Source {
    std::shared_ptr<State> state_;
    std::shared_ptr<testing::ReadGate> gate_;
  public:
    explicit LocalSource(std::shared_ptr<State> state) : state_(std::move(state)), gate_(state_->gate) {}
    ~LocalSource() override {
        // Never join an OS call. Worker owns State, handles and its STA COM resources, and
        // exits after its current OS call returns. State is freed on last use.
        if (!state_) return;
        bool busy;
        { std::lock_guard lock(state_->mutex); busy = state_->running || state_->watch_open; invalidate(*state_, true); }
        if (busy) retire(state_);
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
        bool retiring = false;
        if (state_) {
            std::lock_guard lock(state_->mutex);
            // Never queue a successor behind an obsolete OS call. Retiring
            // moves to the separate retired reserve, freeing the live slot.
            // Also retire an idle watch: its worker may be awaiting a broken
            // driver's asynchronous cancellation. Never queue navigation behind it.
            retiring = state_->running || state_->watch_open;
            if (retiring) invalidate(*state_, true);
        }
        if (retiring) { retire(state_); state_.reset(); }
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
    std::array<std::shared_ptr<State>, kMaxWorkers> states;
    std::array<std::size_t, kMaxWorkers> cancel_requests{}, cancel_accepted{};
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
    std::array<HANDLE, kMaxWorkers> handles{};
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
    std::array<std::shared_ptr<State>, kMaxWorkers> states;
    std::array<HANDLE, kMaxWorkers> handles{};
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
#include <shellapi.h>
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
    // The type of a local file is a registry lookup by extension. Opening a property
    // store per file cost about 5 ms (7 s for 1400 files) against 0.06 ms here, and a
    // scroll or sort used to cancel the slow requests before they finished (PD-254).
    if (request.property == "System.ItemTypeText" && !request.key.child_location &&
        !request.key.identity.shell_item) {
        SHFILEINFOW info{};
        const auto path = request.key.parsing_name();
        if (SHGetFileInfoW(path.c_str(),
                request.key.directory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL,
                &info, sizeof(info), SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES) && info.szTypeName[0]) {
            result.text = info.szTypeName;
            result.state = PropertyState::value;
            return result;
        }
    }
    // Duration only exists for audio and video. Ruling every other type out by its
    // registered perceived type avoids opening each file just to learn it has none.
    if (request.property == "System.Media.Duration" && !request.key.directory &&
        !request.key.child_location && !request.key.identity.shell_item) {
        const auto& name = request.key.identity.key;
        const auto dot = name.rfind(L'.');
        PERCEIVED type{};
        PERCEIVEDFLAG flags{};
        if (dot != std::wstring::npos &&
            SUCCEEDED(AssocGetPerceivedType(name.c_str() + dot, &type, &flags, nullptr)) &&
            type != PERCEIVED_TYPE_AUDIO && type != PERCEIVED_TYPE_VIDEO &&
            type != PERCEIVED_TYPE_UNSPECIFIED) {
            result.state = PropertyState::missing;
            return result;
        }
    }
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
            } else if (SUCCEEDED(apartment) && result.request.by_type) {
                result.image = shell_core::load_extension_icon(
                    std::wstring_view(result.request.key.identity.key).substr(1),
                    result.request.key.directory, result.request.key.size);
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
