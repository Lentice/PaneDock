#pragma once
#include "managed_list/list_worker.h"
#include <algorithm>
#include "shell_core/icon_image.h"

namespace panedock::managed_list {
// Separate from the four enumeration slots: at most eight list workers in
// total, including abandoned calls. Properties share these same slots/queues.
// No offscreen image prefetch; only full-sort demand requests offscreen keys. Viewports exceeding
// the interest cap retain placeholders for the excess until they scroll in.
inline constexpr std::size_t kIconWorkers = 4;
inline constexpr std::size_t kIconInterest = 128;
inline constexpr std::size_t kIconCache = 256;
inline constexpr std::size_t kIconResults = 2;
// 256 logical pixels at 200% DPI; larger raster requests are capped. Cache
// includes two placeholders, and replaces slots rather than growing on eviction.
// Four pane caches <=16 MiB payload; 4 workers x (2 queued + 1 building)
// results <=12.375 MiB at 512px, excluding provider/GDI/allocator overhead.
inline constexpr int kMaxImageSize = 512;
inline constexpr std::size_t kImageCacheBytes = 4 * 1024 * 1024;
// Each interest/request collection also has an owned-value byte ceiling.
// Long Shell identities reduce batch size, never increase worker/queue counts.
inline constexpr std::size_t kIconValueBytes = 1024 * 1024;
inline constexpr std::size_t image_bytes(int size) noexcept {
    return static_cast<std::size_t>(size) * (size * 4 + ((size + 15) / 16) * 2);
}
inline constexpr std::size_t image_capacity(int size) noexcept {
    const auto slots = kImageCacheBytes / image_bytes(size);
    return std::min(kIconCache, slots > 2 ? slots - 2 : 0);
}
struct IconKey final {
    core::ShellLocation location;
    ItemIdentity identity;
    std::uint64_t bytes{}, modified{};
    bool directory{};
    int size{16};
    UINT dpi{96};
    bool thumbnail{};
    std::optional<core::ShellLocation> child_location{};
    CloudAttributes cloud{};
    bool matches(const Row& row) const noexcept {
        return identity == row.identity && child_location == row.child_location &&
            bytes == row.bytes && modified == row.date_modified && directory == row.directory &&
            cloud == row.cloud;
    }
    // Value-only target selection. Only local leaf identities are joined.
    // All parsing/provider work stays on the existing worker STA.
    std::wstring parsing_name() const {
        if (child_location) return child_location->parsing_name;
        if (identity.shell_item) return identity.key;
        auto path = location.parsing_name;
        if (!path.empty() && path.back() != L'\\') path += L'\\';
        return path + identity.key;
    }
    bool operator==(const IconKey&) const = default;
};
inline std::size_t owned_location_bytes(const core::ShellLocation& location) noexcept {
    return owned_string_bytes(location.parsing_name) +
        owned_string_bytes(location.known_folder_identity) + owned_string_bytes(location.fallback_path);
}
inline std::size_t owned_icon_key_bytes(const IconKey& key) noexcept {
    return owned_location_bytes(key.location) + owned_string_bytes(key.identity.key) +
        (key.child_location ? owned_location_bytes(*key.child_location) : 0);
}
struct IconRequest final {
    Request request;
    std::uint64_t viewport{};
    IconKey key;
    std::string property{}; // empty = image; otherwise worker-owned property query
    int property_slot{};
    std::size_t row_index{};
    bool operator==(const IconRequest&) const = default;
};
inline std::size_t owned_icon_request_bytes(const IconRequest& request) noexcept {
    return sizeof(IconRequest) + owned_icon_key_bytes(request.key) +
        owned_location_bytes(request.request.location) + request.property.capacity() + 1 +
        request.request.binding.group_id.capacity() + 1 + request.request.binding.tab_id.capacity() + 1;
}
inline bool append_icon_request(std::vector<IconRequest>& requests, IconRequest request) {
    auto bytes = owned_icon_request_bytes(request);
    for (const auto& pending : requests) bytes += owned_icon_request_bytes(pending);
    if (requests.size() >= kIconInterest || bytes > kIconValueBytes) return false;
    requests.push_back(std::move(request));
    return true;
}
struct IconResult final {
    IconRequest request;
    shell_core::IconImage image; // empty on failure; no negative cache
    PropertyValue property{};
};
class IconSource {
  public:
    virtual ~IconSource() = default;
    // Replaces all queued interest. At most one obsolete provider call survives.
    virtual bool replace(std::vector<IconRequest> requests) noexcept = 0;
    virtual std::optional<IconResult> take() noexcept = 0;
    virtual HANDLE event() const noexcept = 0;
    virtual void cancel() noexcept = 0;
};
std::unique_ptr<IconSource> make_icon_source() noexcept;
} // namespace panedock::managed_list
