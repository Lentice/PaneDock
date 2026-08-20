#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace panedock::core {

enum class PrototypeLayout { two_pane, four_pane };

struct PrototypeLocationState final {
    static constexpr std::size_t kPaneCount = 4;

    std::array<std::wstring, kPaneCount> locations;
    PrototypeLayout layout{PrototypeLayout::four_pane};
    std::size_t active_pane{0};
};

inline bool is_digit(wchar_t character) noexcept {
    return character >= L'0' && character <= L'9';
}

inline std::size_t parse_active_pane(std::wstring_view line) noexcept {
    if (line.empty()) {
        return 0;
    }

    std::size_t value = 0;
    for (const wchar_t character : line) {
        if (!is_digit(character)) {
            return 0;
        }
        value = value * 10 + static_cast<std::size_t>(character - L'0');
        if (value >= PrototypeLocationState::kPaneCount) {
            return 0;
        }
    }
    return value;
}

inline PrototypeLocationState parse_prototype_location_state(
    std::wstring_view contents,
    const std::array<std::wstring, PrototypeLocationState::kPaneCount>&
        defaults) {
    PrototypeLocationState state;
    state.locations = defaults;

    std::array<std::wstring_view, 6> lines{};
    std::size_t line_count = 0;
    std::size_t line_start = 0;
    while (line_start <= contents.size() && line_count < lines.size()) {
        const std::size_t line_end = contents.find(L'\n', line_start);
        const std::size_t end = line_end == std::wstring_view::npos
                                    ? contents.size()
                                    : line_end;
        std::wstring_view line = contents.substr(line_start, end - line_start);
        if (!line.empty() && line.back() == L'\r') {
            line.remove_suffix(1);
        }
        lines[line_count++] = line;
        if (line_end == std::wstring_view::npos) {
            break;
        }
        line_start = line_end + 1;
    }

    if (line_count > 0 && lines[0] == L"two") {
        state.layout = PrototypeLayout::two_pane;
    } else if (line_count > 0 && lines[0] == L"four") {
        state.layout = PrototypeLayout::four_pane;
    }

    if (line_count > 1) {
        state.active_pane = parse_active_pane(lines[1]);
    }
    if (state.layout == PrototypeLayout::two_pane && state.active_pane >= 2) {
        state.active_pane = 0;
    }

    for (std::size_t pane = 0; pane < state.locations.size(); ++pane) {
        const std::size_t line = pane + 2;
        if (line < line_count && !lines[line].empty()) {
            state.locations[pane] = std::wstring(lines[line]);
        }
    }
    return state;
}

inline std::wstring serialize_prototype_location_state(
    const PrototypeLocationState& state) {
    std::wstring contents = state.layout == PrototypeLayout::two_pane
                                ? L"two\n"
                                : L"four\n";
    contents += std::to_wstring(state.active_pane);
    contents += L'\n';
    for (const std::wstring& location : state.locations) {
        contents += location;
        contents += L'\n';
    }
    return contents;
}

}  // namespace panedock::core
