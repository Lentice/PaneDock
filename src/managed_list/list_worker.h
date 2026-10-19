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
#include <vector>
#include "core/model.h"
#include "core/navigation.h"

namespace panedock::managed_list {
// Four worker slots process-wide, INCLUDING abandoned reads. Each has one
// replaceable pending request and two queued batches. A successor may acquire
// a free slot; exhaustion fails immediately until an explicit retry succeeds.
// Pane separately caps retained rows at 100,000 / 24 MiB.
inline constexpr std::size_t kMaxSubscriptions = 4;
inline constexpr std::size_t kBatchRows = 128;
inline constexpr std::size_t kQueuedBatches = 2;
inline constexpr std::size_t kMaxRows = 100000;
// Separate per-visible-Pane recovery/delta index ceiling, in addition to the
// 24 MiB row budget. Counts allocator-requested node bytes, like row capacity;
// implementation-specific malloc bookkeeping is not retained row/index data.
inline constexpr std::size_t kMaxIndexBytes = 4 * 1024 * 1024;
inline constexpr std::size_t kMaxRowBytes = 24 * 1024 * 1024;
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
};
inline std::size_t owned_row_bytes(const Row& row) noexcept {
    auto bytes = owned_string_bytes(row.name) + owned_string_bytes(row.identity.key);
    if (row.child_location) {
        bytes += owned_string_bytes(row.child_location->parsing_name) +
            owned_string_bytes(row.child_location->known_folder_identity) +
            owned_string_bytes(row.child_location->fallback_path);
    }
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
        child.known_folder_identity.size() <= kMaxPath && child.fallback_path.size() <= kMaxPath;
}
enum class Change { none, delta, rescan_begin, rescan, watching, watch_error };
enum class WatchState { inactive, watching, recovering, failed };
enum class Phase { idle, loading, complete, empty, error, limited };
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
};
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
} // namespace panedock::managed_list
