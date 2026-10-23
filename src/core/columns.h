#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace panedock::core {

// Details-view columns like Explorer's: the user picks which are shown, their order and
// width (PD-257). A tab stores them as "id:width,id:width" in display order; an empty
// string means the default set. Unknown ids survive a round trip so a newer build's
// columns are not dropped by an older one. Widths are 96-DPI pixels (DIPs); the list
// scales them to the pane's DPI when it applies them.
struct ColumnInfo final {
    std::string_view id;
    const wchar_t* label;
    int default_width;
    bool right_aligned;
};
// The index is the column's stable subitem: never reorder or remove entries, only append.
inline constexpr std::array<ColumnInfo, 6> kKnownColumns{{
    {"name", L"Name", 320, false},
    {"size", L"Size", 160, true},
    {"date", L"Date modified", 180, false},
    {"type", L"Type", 160, false},
    {"duration", L"Duration", 120, false},
    {"availability", L"Availability", 200, false},
}};
inline constexpr int kMinColumnWidth = 24;
inline constexpr int kMaxColumnWidth = 4000;

struct ColumnSpec final {
    std::string id;
    int width{};
    bool operator==(const ColumnSpec&) const = default;
};

// Explorer's Details defaults for a general folder, in its order. The other known
// columns are offered by the header menu.
inline constexpr std::array<std::string_view, 4> kDefaultColumnOrder{"name", "date", "type", "size"};

inline const ColumnInfo* find_column(std::string_view id) noexcept {
    const auto found = std::find_if(kKnownColumns.begin(), kKnownColumns.end(),
                                    [&](const auto& info) { return info.id == id; });
    return found == kKnownColumns.end() ? nullptr : &*found;
}

inline std::vector<ColumnSpec> default_columns() {
    std::vector<ColumnSpec> columns;
    for (const auto id : kDefaultColumnOrder) columns.push_back({std::string(id), find_column(id)->default_width});
    return columns;
}

// Name is always shown and always first, ids are unique, widths are clamped.
inline std::vector<ColumnSpec> parse_columns(std::string_view text) {
    if (text.empty()) return default_columns();
    std::vector<ColumnSpec> columns;
    while (!text.empty()) {
        const auto end = text.find(',');
        const auto token = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        const auto colon = token.find(':');
        const auto id = token.substr(0, colon);
        if (id.empty() || std::any_of(columns.begin(), columns.end(), [&](const auto& c) { return c.id == id; }))
            continue;
        const auto* info = find_column(id);
        int width = info ? info->default_width : 100;
        if (colon != std::string_view::npos) {
            int parsed = 0;
            const auto digits = token.substr(colon + 1);
            if (std::from_chars(digits.data(), digits.data() + digits.size(), parsed).ec == std::errc{})
                width = std::clamp(parsed, kMinColumnWidth, kMaxColumnWidth);
        }
        columns.push_back({std::string(id), width});
    }
    const auto name = std::find_if(columns.begin(), columns.end(), [](const auto& c) { return c.id == "name"; });
    if (name == columns.end()) columns.insert(columns.begin(), {"name", kKnownColumns[0].default_width});
    else std::rotate(columns.begin(), name, name + 1);
    return columns;
}

inline std::string format_columns(const std::vector<ColumnSpec>& columns) {
    std::string text;
    for (const auto& column : columns) {
        if (!text.empty()) text += ',';
        text += column.id + ':' + std::to_string(std::clamp(column.width, kMinColumnWidth, kMaxColumnWidth));
    }
    return text;
}

// Explorer's header menu order: the default columns first, then the rest.
inline std::vector<const ColumnInfo*> column_menu_order() {
    std::vector<const ColumnInfo*> order;
    for (const auto id : kDefaultColumnOrder) order.push_back(find_column(id));
    for (const auto& info : kKnownColumns)
        if (std::find(order.begin(), order.end(), &info) == order.end()) order.push_back(&info);
    return order;
}

// Stored widths are 96-DPI pixels; the header works in physical pixels.
inline int column_width_to_pixels(int stored, unsigned dpi) noexcept {
    return dpi == 0 ? stored : static_cast<int>((static_cast<long long>(stored) * dpi + 48) / 96);
}
inline int column_width_from_pixels(int pixels, unsigned dpi) noexcept {
    return dpi == 0 ? pixels : static_cast<int>((static_cast<long long>(pixels) * 96 + dpi / 2) / dpi);
}

inline bool column_visible(const std::vector<ColumnSpec>& columns, std::string_view id) noexcept {
    return std::any_of(columns.begin(), columns.end(), [&](const auto& c) { return c.id == id; });
}

// Shows or hides one column; Name cannot be hidden. A newly shown column goes last.
inline void toggle_column(std::vector<ColumnSpec>& columns, std::string_view id) {
    if (id == "name") return;
    const auto found = std::find_if(columns.begin(), columns.end(), [&](const auto& c) { return c.id == id; });
    if (found != columns.end()) { columns.erase(found); return; }
    const auto* info = find_column(id);
    if (info) columns.push_back({std::string(id), info->default_width});
}

// The setting for the columns the list shows now (`shown`, in display order).
// Columns this build does not know stay in it, after the shown ones, so an
// older build never drops a newer build's columns. nullopt when the setting
// would not change, including an empty (default) setting the shown set equals.
inline std::optional<std::string> captured_columns(std::string_view previous,
                                                   std::vector<ColumnSpec> shown) {
    for (const auto& column : parse_columns(previous))
        if (!column_visible(shown, column.id) && !find_column(column.id))
            shown.push_back(column);
    auto text = format_columns(shown);
    if (text == previous || (previous.empty() && text == format_columns(default_columns())))
        return std::nullopt;
    return text;
}

} // namespace panedock::core
