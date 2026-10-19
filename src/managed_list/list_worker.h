#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
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
struct Row final {
    std::wstring name;
    std::uint64_t bytes{};
    bool directory{};
    // UTC FILETIME ticks copied directly from WIN32_FIND_DATA on the worker.
    // Zero means the basic timestamp was unavailable.
    std::uint64_t date_modified{};
    // Type, Duration, and the current persisted custom sort key. <=3 values per
    // retained row (<=100,000); structural and owned text bytes share the SAME
    // 24 MiB row cap. No inactive-tab cache. Released with rows.
    std::array<PropertyValue, 3> properties{};
};
enum class Phase { idle, loading, complete, empty, error, limited };
struct Batch final {
    Request request;
    std::vector<Row> rows;
    Phase phase{Phase::loading};
    DWORD error{};
};
// Owned values only. event() is a waitable kernel event, never a window.
// take() drains at most one bounded batch. cancel() drops interest immediately.
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
