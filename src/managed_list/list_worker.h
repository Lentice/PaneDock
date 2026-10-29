#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <compare>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "core/model.h"
#include "core/navigation.h"
#include "managed_list/cloud_attributes.h"

namespace panedock::managed_list {
// Four live worker slots process-wide (one per Pane in practice). Each has one
// replaceable pending request and two queued batches. Pane separately caps
// retained rows at 100,000 / 24 MiB.
inline constexpr std::size_t kMaxSubscriptions = 4;
// Separate reserve for retired/abandoned workers still exiting or blocked in an
// OS call, so a successor can take the freed live slot at once. Retired workers
// have no pending request or queued batch. A full reserve leaves the retiree in
// its live slot; with both full, a successor fails immediately (no wait/timer)
// until an explicit retry succeeds. At most 8 enumeration threads in total.
inline constexpr std::size_t kMaxRetiredWorkers = 4;
inline constexpr std::size_t kBatchRows = 128;
inline constexpr std::size_t kQueuedBatches = 2;
inline constexpr std::size_t kMaxRows = 100000;
// Separate per-visible-Pane recovery/delta index ceiling, in addition to the
// 24 MiB row budget. Counts allocator-requested node bytes, like row capacity;
// implementation-specific malloc bookkeeping is not retained row/index data.
inline constexpr std::size_t kMaxIndexBytes = 4 * 1024 * 1024;
inline constexpr std::size_t kMaxRowBytes = 24 * 1024 * 1024;
// Eight queued batches across four live slots fit the 16 MiB result budget.
// A worker (live or retired) additionally owns one bounded batch under construction.
inline constexpr std::size_t kMaxBatchOwnedBytes = 2 * 1024 * 1024;
inline constexpr std::size_t kMaxName = 255;
inline constexpr std::size_t kMaxPath = 32760;
struct Request final {
    core::NavigationRequest binding;
    core::ShellLocation location;
    bool operator==(const Request& other) const noexcept {
        return binding.generation == other.binding.generation &&
            binding.group_id == other.binding.group_id &&
            binding.tab_id == other.binding.tab_id && location == other.location;
    }
};
enum class PropertyState { unresolved, value, missing, failed };
struct PropertyValue final {
    PropertyState state{PropertyState::unresolved};
    std::wstring text; // bounded to 128 characters; no provider-owned data
    long double number{};
    bool numeric{};
};
struct ItemIdentity final {
    std::wstring key;
    // Local keys are leaf names scoped by Request::location. Shell keys are
    // provider parsing identities, never display text or synthesized paths.
    bool shell_item{};
    auto operator<=>(const ItemIdentity&) const = default;
};
inline std::size_t owned_string_bytes(const std::wstring& value) noexcept {
    return (value.capacity() + 1) * sizeof(wchar_t);
}
inline std::size_t owned_location_bytes(const core::ShellLocation& location) noexcept {
    auto bytes = owned_string_bytes(location.parsing_name) +
        owned_string_bytes(location.known_folder_identity) +
        owned_string_bytes(location.fallback_path);
    if (location.navigation_parent) bytes +=
        owned_string_bytes(location.navigation_parent->parsing_name) +
        owned_string_bytes(location.navigation_parent->known_folder_identity) +
        owned_string_bytes(location.navigation_parent->fallback_path);
    return bytes;
}
inline bool valid_shell_location(const core::ShellLocation& location) noexcept {
    return !location.parsing_name.empty() && location.parsing_name.size() <= kMaxPath &&
        location.known_folder_identity.size() <= kMaxPath &&
        location.fallback_path.size() <= kMaxPath &&
        (!location.navigation_parent ||
            (!location.navigation_parent->parsing_name.empty() &&
             location.navigation_parent->parsing_name.size() <= kMaxPath &&
             location.navigation_parent->known_folder_identity.size() <= kMaxPath &&
             location.navigation_parent->fallback_path.size() <= kMaxPath));
}
struct Row final {
    std::wstring name; // display text; retained for existing Source aggregates
    std::uint64_t bytes{};
    bool directory{};
    // UTC FILETIME ticks copied directly from WIN32_FIND_DATA on the worker.
    // Zero means the basic timestamp was unavailable.
    std::uint64_t date_modified{};
    // Type, Duration, and the current persisted custom sort key. <=3 values per
    // retained row (<=100,000); structural and owned text bytes share the SAME
    // 24 MiB active row cap. Inactive snapshots use a separate global budget.
    std::array<PropertyValue, 3> properties{};
    ItemIdentity identity{};
    std::optional<core::ShellLocation> child_location{};
    CloudAttributes cloud{};
    // Local drive size for the This PC cards. capacity_bytes == 0 means unknown.
    std::uint64_t capacity_bytes{};
    std::uint64_t free_bytes{};
    // UI-only transient order for folders created here: nonzero rows stay at
    // the end until explicit sort/Refresh/navigation. Included in row budgets;
    // workers leave it zero and provider updates must preserve the UI value.
    std::uint64_t new_folder_order{};
};
// Shared by initial enumeration, notification deltas and recovery rescans.
inline Row basic_row(const WIN32_FIND_DATAW& data, std::wstring name) {
    Row row{std::move(name),
        (static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow,
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
        (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
            data.ftLastWriteTime.dwLowDateTime};
    row.cloud = {data.dwFileAttributes,
        (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ? data.dwReserved0 : 0};
    return row;
}
// Explorer's "Show hidden files" and "Hide protected operating system files"
// options (PD-253). Protected files are hidden files that also carry the system bit.
struct HiddenFilePolicy {
    bool show_hidden{true};
    bool show_protected{true};
    bool hides(DWORD attributes) const noexcept {
        if (!(attributes & FILE_ATTRIBUTE_HIDDEN)) return false;
        if ((attributes & FILE_ATTRIBUTE_SYSTEM) && !show_protected) return true;
        return !show_hidden;
    }
};
HiddenFilePolicy read_hidden_file_policy() noexcept;
// Display text for a local filesystem leaf when the Shell hides its extension
// (Explorer's "File name extensions" option plus per-type NeverShowExt /
// AlwaysShowExt, decided by the worker per extension). Folders and names that
// would become empty (".gitignore") keep the full leaf. Display only: the
// row's identity.key stays the real leaf for rename, file operations, deltas.
inline std::wstring display_leaf(std::wstring_view leaf, bool directory, bool hide_extension) {
    if (directory || !hide_extension) return std::wstring(leaf);
    const auto dot = leaf.rfind(L'.');
    if (dot == std::wstring_view::npos || dot == 0) return std::wstring(leaf);
    return std::wstring(leaf.substr(0, dot));
}
inline std::size_t owned_row_bytes(const Row& row) noexcept {
    auto bytes = owned_string_bytes(row.name) + owned_string_bytes(row.identity.key);
    if (row.child_location) bytes += owned_location_bytes(*row.child_location);
    for (const auto& property : row.properties)
        bytes += owned_string_bytes(property.text);
    return bytes;
}
// Compatibility for the injected local Source seam and the local enumerator.
// Normalize once at admission; every retained row then has an explicit key.
inline bool prepare_row(Row& row) {
    const auto leaf = [](std::wstring_view value) {
        return !value.empty() && value.size() <= kMaxName && value != L"." && value != L".." &&
            value.find_first_of(L"\\/:*?\"") == std::wstring_view::npos;
    };
    if (row.name.empty() || row.name.size() > kMaxName) return false;
    // Only the legacy local Source contract equates its name with a leaf.
    // A Shell row must always supply its key explicitly, even with a child.
    if (row.identity.key.empty() && !row.identity.shell_item && !row.child_location)
        row.identity.key = row.name;
    if (!row.identity.shell_item)
        return leaf(row.identity.key) && !row.child_location;
    if (row.identity.key.empty() || row.identity.key.size() > kMaxPath) return false;
    if (!row.child_location) return true;
    const auto& child = *row.child_location;
    return child.parsing_name == row.identity.key && child.parsing_name.size() <= kMaxPath &&
        child.known_folder_identity.size() <= kMaxPath && child.fallback_path.size() <= kMaxPath &&
        (!child.navigation_parent || valid_shell_location({
            child.navigation_parent->parsing_name,
            child.navigation_parent->known_folder_identity,
            child.navigation_parent->fallback_path}));
}
// location_lost: the displayed filesystem folder no longer exists (deleted,
// moved, renamed, or its volume removed). parent_location then carries where
// the Pane goes instead: the renamed folder when only one path component
// changed, else the nearest existing ancestor, else This PC.
enum class Change { none, delta, rescan_begin, rescan, watching, watch_error, location_lost };
enum class WatchState { inactive, watching, recovering, failed };
enum class Phase { idle, loading, complete, empty, error, limited };
enum class ParentOutcome { unresolved, root, value, failed };
enum class NotificationCapability { filesystem_watch, manual_refresh };
struct Batch final {
    Request request;
    std::vector<Row> rows;
    Phase phase{Phase::loading};
    DWORD error{};
    Change change{Change::none};
    // Local notification contract only: converted to non-Shell ItemIdentity
    // at the Pane boundary. These are never display-name or virtual deltas.
    std::vector<std::wstring> removed{}; // <=128 leaf identities
    std::vector<std::pair<std::wstring, std::wstring>> renamed{}; // <=128 pairs
    // Namespace metadata is repeated on every batch so consumers never need
    // to combine a child batch with a separately timed resolution result.
    std::optional<core::ShellLocation> resolved_location{};
    std::optional<core::ShellLocation> parent_location{};
    ParentOutcome parent_outcome{ParentOutcome::unresolved};
    NotificationCapability notification{NotificationCapability::filesystem_watch};
    // Change::watching on a local volume only: a zero-access handle to that
    // volume ("\.\E:"), opened on the worker and owned by this value. The UI
    // registers it for DBT_DEVTYP_HANDLE notifications so the volume can be
    // ejected; it closes on release or when the batch is dropped. Never a
    // directory handle (that would keep a deleted folder delete-pending).
    std::shared_ptr<void> device_handle{};
};
inline std::size_t owned_batch_bytes(const Batch& batch) noexcept {
    std::size_t bytes = sizeof(Batch) + batch.rows.capacity() * sizeof(Row) +
        batch.removed.capacity() * sizeof(std::wstring) +
        batch.renamed.capacity() * sizeof(std::pair<std::wstring, std::wstring>) +
        owned_location_bytes(batch.request.location) +
        batch.request.binding.group_id.capacity() + 1 + batch.request.binding.tab_id.capacity() + 1;
    if (batch.resolved_location) bytes += owned_location_bytes(*batch.resolved_location);
    if (batch.parent_location) bytes += owned_location_bytes(*batch.parent_location);
    for (const auto& row : batch.rows) bytes += owned_row_bytes(row);
    for (const auto& name : batch.removed) bytes += owned_string_bytes(name);
    for (const auto& [old_name, new_name] : batch.renamed)
        bytes += owned_string_bytes(old_name) + owned_string_bytes(new_name);
    return bytes;
}
// Owned values only. event() is a waitable kernel event, never a window.
// take() drains at most one bounded batch. Successful enumeration keeps a
// local change subscription alive; cancel() drops all interest immediately.
class Source {
  public:
    virtual ~Source() = default;
    virtual bool submit(Request request) noexcept = 0;
    virtual std::optional<Batch> take() noexcept = 0;
    virtual HANDLE event() const noexcept = 0;
    virtual void cancel() noexcept = 0;
};
// Subscription creation is UI-STA-only, like Pane ownership.
std::unique_ptr<Source> make_local_source() noexcept;
// Pure syntax only; never resolves a drive, server or Shell item on the STA.
inline bool is_filesystem_location(std::wstring_view path) noexcept {
    return (path.size() >= 3 && path[1] == L':' && path[2] == L'\\') ||
        (path.size() > 2 && path[0] == L'\\' && path[1] == L'\\');
}
// The Shell's "This PC" folder, by parsing name or by known-folder identity.
inline bool is_this_pc_location(const core::ShellLocation& location) noexcept {
    return _wcsicmp(location.parsing_name.c_str(),
                    L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}") == 0 ||
        _wcsicmp(location.known_folder_identity.c_str(),
                 L"{0AC0837C-BBF8-452A-850D-79D08E667CA7}") == 0;
}
inline bool uses_shell_namespace(const core::ShellLocation& location) noexcept {
    return location.namespace_location || !is_filesystem_location(location.parsing_name);
}
} // namespace panedock::managed_list
