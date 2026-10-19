#pragma once
#include "managed_list/list_worker.h"
#include <algorithm>
#include "shell_core/icon_image.h"

namespace panedock::managed_list {
// Separate from the four enumeration slots: at most eight list workers in
// total, including abandoned calls. No offscreen prefetch. Viewports exceeding
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
inline constexpr std::size_t image_bytes(int size) noexcept {
    return static_cast<std::size_t>(size) * (size * 4 + ((size + 15) / 16) * 2);
}
inline constexpr std::size_t image_capacity(int size) noexcept {
    const auto slots = kImageCacheBytes / image_bytes(size);
    return std::min(kIconCache, slots > 2 ? slots - 2 : 0);
}
struct IconKey final {
    core::ShellLocation location;
    std::wstring name;
    std::uint64_t bytes{}, modified{};
    bool directory{};
    int size{16};
    UINT dpi{96};
    bool thumbnail{};
    bool operator==(const IconKey&) const = default;
};
struct IconRequest final {
    Request request;
    std::uint64_t viewport{};
    IconKey key;
};
struct IconResult final {
    IconRequest request;
    shell_core::IconImage image; // empty on failure; no negative cache
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
