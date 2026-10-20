#pragma once
#include <windows.h>
#include <optional>

namespace panedock::managed_list {
// Copied directory-entry metadata only. Unknown is not a local/online claim.
struct CloudAttributes final {
    std::optional<DWORD> attributes;
    DWORD reparse_tag{};
    bool cloud_placeholder() const noexcept {
        return attributes && (*attributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
            ((reparse_tag & ~IO_REPARSE_TAG_CLOUD_MASK) == IO_REPARSE_TAG_CLOUD ||
             reparse_tag == IO_REPARSE_TAG_ONEDRIVE ||
             reparse_tag == IO_REPARSE_TAG_FILE_PLACEHOLDER);
    }
    bool metadata_only() const noexcept {
        // Unknown metadata alone is not evidence of a cloud item. Preserve
        // ordinary/virtual enrichment; recognized cloud items stay protected
        // even when a pinned placeholder currently appears local.
        return cloud_placeholder() || (attributes && (*attributes & (
            FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_OPEN |
            FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS | FILE_ATTRIBUTE_PINNED |
            FILE_ATTRIBUTE_UNPINNED)));
    }
    const wchar_t* text() const noexcept {
        if (!attributes) return L"Unknown";
        const auto flags = *attributes;
        const bool remote = flags & (FILE_ATTRIBUTE_OFFLINE | FILE_ATTRIBUTE_RECALL_ON_OPEN |
                                     FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS);
        const bool pinned = flags & FILE_ATTRIBUTE_PINNED;
        const bool unpinned = flags & FILE_ATTRIBUTE_UNPINNED;
        if (pinned && unpinned) return L"Unknown";
        // PINNED is intent, not proof hydration has finished. Recall bits do
        // not distinguish a completely online-only file from partial data.
        if (remote) return pinned ? L"Pinned - not fully local" : L"Not fully local";
        if (cloud_placeholder()) return pinned ? L"Always available (pinned)" : L"Locally available";
        if (pinned || unpinned) return L"Unknown";
        return L"";
    }
    bool operator==(const CloudAttributes&) const = default;
};
} // namespace panedock::managed_list
