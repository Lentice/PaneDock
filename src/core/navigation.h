#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace panedock::core {

struct NavigationRequest final {
    std::uint64_t generation{};
    std::string group_id;
    std::string tab_id;
};

inline bool navigation_request_matches(
    const NavigationRequest& request, std::uint64_t generation,
    std::string_view group_id, std::string_view tab_id) noexcept {
    return request.generation == generation && request.group_id == group_id &&
           request.tab_id == tab_id;
}

}  // namespace panedock::core
