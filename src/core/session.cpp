#include "core/session.h"

#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <variant>

namespace panedock::core {
namespace {

struct Json final {
    using Object = std::map<std::string, Json>;
    using Array = std::vector<Json>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value;
};

class Parser final {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    std::optional<Json> parse() {
        auto result = value();
        whitespace();
        return result && position_ == text_.size() ? result : std::nullopt;
    }

private:
    void whitespace() {
        while (position_ < text_.size() &&
               (text_[position_] == ' ' || text_[position_] == '\n' ||
                text_[position_] == '\r' || text_[position_] == '\t')) ++position_;
    }

    bool take(char expected) {
        whitespace();
        if (position_ == text_.size() || text_[position_] != expected) return false;
        ++position_;
        return true;
    }

    std::optional<Json> value() {
        whitespace();
        if (position_ == text_.size()) return std::nullopt;
        if (text_[position_] == '"') {
            auto result = string();
            if (result) return Json{*result};
            return std::nullopt;
        }
        if (text_[position_] == '{') return object();
        if (text_[position_] == '[') return array();
        if (text_.substr(position_, 4) == "true") { position_ += 4; return Json{true}; }
        if (text_.substr(position_, 5) == "false") { position_ += 5; return Json{false}; }
        if (text_.substr(position_, 4) == "null") { position_ += 4; return Json{nullptr}; }
        return number();
    }

    std::optional<std::string> string() {
        if (!take('"')) return std::nullopt;
        std::string result;
        while (position_ < text_.size()) {
            const unsigned char ch = static_cast<unsigned char>(text_[position_++]);
            if (ch == '"') return result;
            if (ch < 0x20) return std::nullopt;
            if (ch != '\\') { result += static_cast<char>(ch); continue; }
            if (position_ == text_.size()) return std::nullopt;
            const char escaped = text_[position_++];
            switch (escaped) {
                case '"': case '\\': case '/': result += escaped; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    if (position_ + 4 > text_.size()) return std::nullopt;
                    unsigned code{};
                    const auto* first = text_.data() + position_;
                    const auto parsed = std::from_chars(first, first + 4, code, 16);
                    if (parsed.ec != std::errc{} || parsed.ptr != first + 4) return std::nullopt;
                    position_ += 4;
                    if (code >= 0xd800 && code <= 0xdbff) {
                        if (position_ + 6 > text_.size() || text_[position_] != '\\' ||
                            text_[position_ + 1] != 'u') return std::nullopt;
                        unsigned low{};
                        const auto* low_first = text_.data() + position_ + 2;
                        const auto low_parsed = std::from_chars(low_first, low_first + 4, low, 16);
                        if (low_parsed.ec != std::errc{} || low_parsed.ptr != low_first + 4 ||
                            low < 0xdc00 || low > 0xdfff) return std::nullopt;
                        position_ += 6;
                        code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                    } else if (code >= 0xdc00 && code <= 0xdfff) return std::nullopt;
                    if (code <= 0x7f) result += static_cast<char>(code);
                    else if (code <= 0x7ff) {
                        result += static_cast<char>(0xc0 | (code >> 6));
                        result += static_cast<char>(0x80 | (code & 0x3f));
                    } else if (code <= 0xffff) {
                        result += static_cast<char>(0xe0 | (code >> 12));
                        result += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
                        result += static_cast<char>(0x80 | (code & 0x3f));
                    } else {
                        result += static_cast<char>(0xf0 | (code >> 18));
                        result += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
                        result += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
                        result += static_cast<char>(0x80 | (code & 0x3f));
                    }
                    break;
                }
                default: return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<Json> number() {
        whitespace();
        const std::size_t start = position_;
        if (position_ < text_.size() && text_[position_] == '-') ++position_;
        if (position_ == text_.size()) return std::nullopt;
        if (text_[position_] == '0') ++position_;
        else {
            if (text_[position_] < '1' || text_[position_] > '9') return std::nullopt;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        }
        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            const std::size_t digits = position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
            if (digits == position_) return std::nullopt;
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_;
            const std::size_t digits = position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
            if (digits == position_) return std::nullopt;
        }
        double result{};
        const auto parsed = std::from_chars(text_.data() + start, text_.data() + position_, result);
        if (parsed.ec != std::errc{} || !std::isfinite(result)) return std::nullopt;
        return Json{result};
    }

    std::optional<Json> array() {
        if (!take('[')) return std::nullopt;
        Json::Array result;
        whitespace();
        if (take(']')) return Json{std::move(result)};
        do {
            auto item = value();
            if (!item) return std::nullopt;
            result.push_back(std::move(*item));
            whitespace();
            if (take(']')) return Json{std::move(result)};
        } while (take(','));
        return std::nullopt;
    }

    std::optional<Json> object() {
        if (!take('{')) return std::nullopt;
        Json::Object result;
        whitespace();
        if (take('}')) return Json{std::move(result)};
        do {
            auto key = string();
            if (!key || !take(':')) return std::nullopt;
            auto item = value();
            if (!item || !result.emplace(std::move(*key), std::move(*item)).second) return std::nullopt;
            whitespace();
            if (take('}')) return Json{std::move(result)};
        } while (take(','));
        return std::nullopt;
    }

    std::string_view text_;
    std::size_t position_{};
};

std::string escape(std::string_view text) {
    std::string result{"\""};
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : text) {
        switch (ch) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (ch < 0x20) {
                    result += "\\u00";
                    result += hex[ch >> 4];
                    result += hex[ch & 15];
                } else result += static_cast<char>(ch);
        }
    }
    return result + '"';
}

std::string dump(const Json& json) {
    if (std::holds_alternative<std::nullptr_t>(json.value)) return "null";
    if (const auto* value = std::get_if<bool>(&json.value)) return *value ? "true" : "false";
    if (const auto* value = std::get_if<double>(&json.value)) {
        std::ostringstream stream;
        stream.precision(17);
        stream << *value;
        return stream.str();
    }
    if (const auto* value = std::get_if<std::string>(&json.value)) return escape(*value);
    if (const auto* value = std::get_if<Json::Array>(&json.value)) {
        std::string result{"["};
        for (std::size_t i = 0; i < value->size(); ++i) {
            if (i) result += ',';
            result += dump((*value)[i]);
        }
        return result + ']';
    }
    std::string result{"{"};
    std::size_t index{};
    for (const auto& [key, value] : std::get<Json::Object>(json.value)) {
        if (index++) result += ',';
        result += escape(key) + ':' + dump(value);
    }
    return result + '}';
}

std::string utf8(std::wstring_view input) {
    std::string result;
    for (std::size_t i = 0; i < input.size(); ++i) {
        std::uint32_t code = static_cast<std::uint16_t>(input[i]);
        if (code >= 0xd800 && code <= 0xdbff && i + 1 < input.size()) {
            const std::uint32_t low = static_cast<std::uint16_t>(input[i + 1]);
            if (low >= 0xdc00 && low <= 0xdfff) {
                code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                ++i;
            }
        }
        if (code <= 0x7f) result += static_cast<char>(code);
        else if (code <= 0x7ff) {
            result += static_cast<char>(0xc0 | code >> 6);
            result += static_cast<char>(0x80 | (code & 0x3f));
        } else if (code <= 0xffff) {
            result += static_cast<char>(0xe0 | code >> 12);
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            result += static_cast<char>(0xf0 | code >> 18);
            result += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
            result += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            result += static_cast<char>(0x80 | (code & 0x3f));
        }
    }
    return result;
}

std::optional<std::wstring> wide(std::string_view input) {
    std::wstring result;
    for (std::size_t i = 0; i < input.size();) {
        const unsigned char first = static_cast<unsigned char>(input[i++]);
        std::uint32_t code{};
        int remaining{};
        if (first <= 0x7f) code = first;
        else if ((first & 0xe0) == 0xc0) { code = first & 0x1f; remaining = 1; }
        else if ((first & 0xf0) == 0xe0) { code = first & 0x0f; remaining = 2; }
        else if ((first & 0xf8) == 0xf0) { code = first & 0x07; remaining = 3; }
        else return std::nullopt;
        if (i + remaining > input.size()) return std::nullopt;
        for (int n = 0; n < remaining; ++n) {
            const unsigned char next = static_cast<unsigned char>(input[i++]);
            if ((next & 0xc0) != 0x80) return std::nullopt;
            code = (code << 6) | (next & 0x3f);
        }
        if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return std::nullopt;
        if (code <= 0xffff) result += static_cast<wchar_t>(code);
        else {
            code -= 0x10000;
            result += static_cast<wchar_t>(0xd800 + (code >> 10));
            result += static_cast<wchar_t>(0xdc00 + (code & 0x3ff));
        }
    }
    return result;
}

Json::Object* object(Json& value) { return std::get_if<Json::Object>(&value.value); }
const Json::Object* object(const Json& value) { return std::get_if<Json::Object>(&value.value); }
const Json::Array* array(const Json& value) { return std::get_if<Json::Array>(&value.value); }

const Json* field(const Json::Object& value, std::string_view name) {
    const auto found = value.find(std::string(name));
    return found == value.end() ? nullptr : &found->second;
}

template <typename T>
const T* as(const Json::Object& value, std::string_view name) {
    const Json* item = field(value, name);
    return item ? std::get_if<T>(&item->value) : nullptr;
}

Json::Object preserved_object(const Json::Array* values, std::string_view id) {
    if (!values) return {};
    for (const auto& value : *values) {
        const auto* candidate = object(value);
        const auto* candidate_id = candidate ? as<std::string>(*candidate, "id") : nullptr;
        if (candidate_id && *candidate_id == id) return *candidate;
    }
    return {};
}

Json::Object preserved_location_object(const Json::Array* values,
                                       std::string_view parsing_name) {
    if (!values) return {};
    for (const auto& value : *values) {
        const auto* candidate = object(value);
        const auto* candidate_name =
            candidate ? as<std::string>(*candidate, "parsing_name") : nullptr;
        if (candidate_name && *candidate_name == parsing_name) return *candidate;
    }
    return {};
}

Json::Object preserved_tab_object(const Json::Array* panes, std::string_view id) {
    if (!panes) return {};
    for (const auto& pane : *panes) {
        const auto* value = object(pane);
        const Json* tabs = value ? field(*value, "tabs") : nullptr;
        auto preserved = preserved_object(tabs ? array(*tabs) : nullptr, id);
        if (!preserved.empty()) return preserved;
    }
    return {};
}

std::optional<ShellLocation> decode_shell_location(const Json::Object& value) {
    const auto* parsing = as<std::string>(value, "parsing_name");
    const auto* known = as<std::string>(value, "known_folder_identity");
    const auto* fallback = as<std::string>(value, "fallback_path");
    const auto parsing_wide = parsing ? wide(*parsing) : std::nullopt;
    const auto known_wide = known ? wide(*known) : std::nullopt;
    const auto fallback_wide = fallback ? wide(*fallback) : std::nullopt;
    if (!parsing_wide || !known_wide || !fallback_wide) return std::nullopt;
    return ShellLocation{*parsing_wide, *known_wide, *fallback_wide};
}

std::optional<int> integer(const Json::Object& value, std::string_view name) {
    const double* number = as<double>(value, name);
    if (!number || std::trunc(*number) != *number ||
        *number < std::numeric_limits<int>::min() || *number > std::numeric_limits<int>::max()) return std::nullopt;
    return static_cast<int>(*number);
}

std::optional<LayoutTemplate> layout(std::string_view value) {
    if (value == "single") return LayoutTemplate::single;
    if (value == "left_right") return LayoutTemplate::left_right;
    if (value == "top_bottom") return LayoutTemplate::top_bottom;
    if (value == "three_pane") return LayoutTemplate::three_pane;
    if (value == "two_over_one") return LayoutTemplate::two_over_one;
    if (value == "one_over_two") return LayoutTemplate::one_over_two;
    if (value == "two_beside_one") return LayoutTemplate::two_beside_one;
    if (value == "four_pane_grid") return LayoutTemplate::four_pane_grid;
    return std::nullopt;
}

std::string layout(LayoutTemplate value) {
    switch (value) {
        case LayoutTemplate::single: return "single";
        case LayoutTemplate::left_right: return "left_right";
        case LayoutTemplate::top_bottom: return "top_bottom";
        case LayoutTemplate::three_pane: return "three_pane";
        case LayoutTemplate::two_over_one: return "two_over_one";
        case LayoutTemplate::one_over_two: return "one_over_two";
        case LayoutTemplate::two_beside_one: return "two_beside_one";
        case LayoutTemplate::four_pane_grid: return "four_pane_grid";
    }
    return {};
}

bool migrate(Json&, int version) {
    switch (version) {
        case 1: return true;
        default: return false;
    }
}

Json encode(const ApplicationState& application, Json root,
            bool clean_shutdown) {
    Json::Object result = object(root) ? std::move(*object(root)) : Json::Object{};
    result["schema_version"] = Json{static_cast<double>(kSessionSchemaVersion)};
    result["active_group_id"] = Json{application.active_group_id};
    Json::Array groups;
    const Json::Array* old_groups = nullptr;
    if (const Json* old = field(result, "groups")) old_groups = array(*old);
    for (std::size_t gi = 0; gi < application.groups.size(); ++gi) {
        const auto& group = application.groups[gi];
        Json::Object encoded = preserved_object(old_groups, group.id);
        encoded["id"] = Json{group.id};
        encoded["name"] = Json{utf8(group.name)};
        encoded["layout_template"] = Json{layout(group.layout_template)};
        Json::Array ratios;
        for (double ratio : group.divider_ratios) ratios.push_back(Json{ratio});
        encoded["divider_ratios"] = Json{std::move(ratios)};
        Json::Array panes;
        const Json::Array* old_panes = nullptr;
        if (const Json* old = field(encoded, "panes")) old_panes = array(*old);
        for (std::size_t pi = 0; pi < group.panes.size(); ++pi) {
            const auto& pane = group.panes[pi];
            Json::Object encoded_pane = preserved_object(old_panes, pane.id);
            encoded_pane["id"] = Json{pane.id};
            encoded_pane["active_tab_id"] = Json{pane.active_tab_id};
            Json::Array tabs;
            const Json::Array* old_tabs = nullptr;
            if (const Json* old = field(encoded_pane, "tabs")) old_tabs = array(*old);
            for (std::size_t ti = 0; ti < pane.tabs.size(); ++ti) {
                const auto& tab = pane.tabs[ti];
                Json::Object encoded_tab = preserved_object(old_tabs, tab.id);
                // A tab keeps its identity when dragged to another pane.
                if (encoded_tab.empty())
                    encoded_tab = preserved_tab_object(old_panes, tab.id);
                encoded_tab["id"] = Json{tab.id};
                Json::Object location;
                if (const Json* old = field(encoded_tab, "shell_location"); old && object(*old)) location = *object(*old);
                location["parsing_name"] = Json{utf8(tab.location.parsing_name)};
                location["known_folder_identity"] = Json{utf8(tab.location.known_folder_identity)};
                location["fallback_path"] = Json{utf8(tab.location.fallback_path)};
                encoded_tab["shell_location"] = Json{std::move(location)};
                encoded_tab["view_mode"] = Json{tab.view_mode};
                encoded_tab["sort_column"] = Json{tab.sort_column};
                encoded_tab["sort_ascending"] = Json{tab.sort_ascending};
                tabs.push_back(Json{std::move(encoded_tab)});
            }
            encoded_pane["tabs"] = Json{std::move(tabs)};
            panes.push_back(Json{std::move(encoded_pane)});
        }
        encoded["panes"] = Json{std::move(panes)};
        encoded["active_pane_id"] = Json{group.active_pane_id};
        groups.push_back(Json{std::move(encoded)});
    }
    result["groups"] = Json{std::move(groups)};
    Json::Array pinned_locations;
    const Json::Array* old_pinned_locations = nullptr;
    if (const Json* old = field(result, "pinned_locations"))
        old_pinned_locations = array(*old);
    for (const auto& pinned : application.pinned_locations) {
        const std::string parsing_name = utf8(pinned.parsing_name);
        Json::Object encoded = preserved_location_object(
            old_pinned_locations, parsing_name);
        encoded["parsing_name"] = Json{parsing_name};
        encoded["known_folder_identity"] =
            Json{utf8(pinned.known_folder_identity)};
        encoded["fallback_path"] = Json{utf8(pinned.fallback_path)};
        pinned_locations.push_back(Json{std::move(encoded)});
    }
    result["pinned_locations"] = Json{std::move(pinned_locations)};
    result["clean_shutdown"] = Json{clean_shutdown};
    Json::Object placement;
    if (const Json* old = field(result, "window_placement"); old && object(*old)) placement = *object(*old);
    placement["x"] = Json{static_cast<double>(application.window_placement.x)};
    placement["y"] = Json{static_cast<double>(application.window_placement.y)};
    placement["width"] = Json{static_cast<double>(application.window_placement.width)};
    placement["height"] = Json{static_cast<double>(application.window_placement.height)};
    placement["maximized"] = Json{application.window_placement.maximized};
    result["window_placement"] = Json{std::move(placement)};
    result["sidebar_width"] = Json{static_cast<double>(application.sidebar_width)};
    return Json{std::move(result)};
}

std::optional<ApplicationState> decode(const Json& root) {
    const auto* value = object(root);
    if (!value) return std::nullopt;
    const auto version = integer(*value, "schema_version");
    Json migrated = root;
    if (!version || !migrate(migrated, *version)) return std::nullopt;
    value = object(migrated);
    const auto* active_group = as<std::string>(*value, "active_group_id");
    const Json* groups_json = field(*value, "groups");
    const auto* groups = groups_json ? array(*groups_json) : nullptr;
    const Json* placement_json = field(*value, "window_placement");
    const auto* placement = placement_json ? object(*placement_json) : nullptr;
    const Json* sidebar_width_json = field(*value, "sidebar_width");
    const auto sidebar_width =
        sidebar_width_json ? integer(*value, "sidebar_width")
                           : std::optional<int>{kDefaultSidebarWidth};
    const Json* pinned_locations_json = field(*value, "pinned_locations");
    const auto* pinned_locations =
        pinned_locations_json ? array(*pinned_locations_json) : nullptr;
    if (!active_group || !groups || !placement || !sidebar_width ||
        (pinned_locations_json != nullptr && pinned_locations == nullptr))
        return std::nullopt;
    ApplicationState application;
    application.schema_version = kSessionSchemaVersion;
    application.active_group_id = *active_group;
    application.sidebar_width = *sidebar_width;
    if (pinned_locations != nullptr) {
        for (const auto& pinned_json : *pinned_locations) {
            const auto* pinned_object = object(pinned_json);
            if (!pinned_object) return std::nullopt;
            const auto pinned = decode_shell_location(*pinned_object);
            if (!pinned || !add_pinned_location(application, *pinned))
                return std::nullopt;
        }
    }
    const auto x = integer(*placement, "x"), y = integer(*placement, "y"),
               width = integer(*placement, "width"), height = integer(*placement, "height");
    const auto* maximized = as<bool>(*placement, "maximized");
    if (!x || !y || !width || !height || !maximized) return std::nullopt;
    application.window_placement = {*x, *y, *width, *height, *maximized};
    for (const auto& group_json : *groups) {
        const auto* group_object = object(group_json);
        if (!group_object) return std::nullopt;
        const auto* id = as<std::string>(*group_object, "id");
        const auto* name_text = as<std::string>(*group_object, "name");
        const auto* layout_text = as<std::string>(*group_object, "layout_template");
        const auto name = name_text ? wide(*name_text) : std::nullopt;
        const auto template_value = layout_text ? layout(*layout_text) : std::nullopt;
        const Json* ratios_json = field(*group_object, "divider_ratios");
        const auto* ratios = ratios_json ? array(*ratios_json) : nullptr;
        const Json* panes_json = field(*group_object, "panes");
        const auto* panes = panes_json ? array(*panes_json) : nullptr;
        const auto* active_pane = as<std::string>(*group_object, "active_pane_id");
        if (!id || !name || !template_value || !ratios || !panes || !active_pane) return std::nullopt;
        GroupState group{*id, *name, *template_value, {}, {}, *active_pane};
        reserve_panes(group);
        for (const auto& ratio : *ratios) {
            const auto* number = std::get_if<double>(&ratio.value);
            if (!number) return std::nullopt;
            group.divider_ratios.push_back(*number);
        }
        for (const auto& pane_json : *panes) {
            const auto* pane_object = object(pane_json);
            if (!pane_object) return std::nullopt;
            const auto* pane_id = as<std::string>(*pane_object, "id");
            const auto* active_tab = as<std::string>(*pane_object, "active_tab_id");
            const Json* tabs_json = field(*pane_object, "tabs");
            const auto* tabs = tabs_json ? array(*tabs_json) : nullptr;
            if (!pane_id || !active_tab || !tabs) return std::nullopt;
            PaneState pane{*pane_id, {}, *active_tab};
            for (const auto& tab_json : *tabs) {
                const auto* tab_object = object(tab_json);
                if (!tab_object) return std::nullopt;
                const auto* tab_id = as<std::string>(*tab_object, "id");
                const Json* location_json = field(*tab_object, "shell_location");
                const auto* location = location_json ? object(*location_json) : nullptr;
                const auto* view_mode = as<std::string>(*tab_object, "view_mode");
                const auto* sort_column = as<std::string>(*tab_object, "sort_column");
                const auto* ascending = as<bool>(*tab_object, "sort_ascending");
                if (!tab_id || !location || !view_mode || !sort_column || !ascending) return std::nullopt;
                const auto decoded_location =
                    location ? decode_shell_location(*location) : std::nullopt;
                if (!decoded_location) return std::nullopt;
                pane.tabs.push_back({*tab_id, *decoded_location,
                                     *view_mode, *sort_column, *ascending});
            }
            group.panes.push_back(std::move(pane));
        }
        application.groups.push_back(std::move(group));
    }
    return is_valid(application) ? std::optional{std::move(application)} : std::nullopt;
}

std::optional<SessionDocument> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    std::string json((std::istreambuf_iterator<char>(stream)), {});
    if (stream.bad()) return std::nullopt;
    return deserialize_session(json);
}

}  // namespace

std::string serialize_session(const SessionDocument& document) {
    Json root{Json::Object{}};
    if (!document.preserved_json.empty()) {
        if (auto parsed = Parser(document.preserved_json).parse()) root = std::move(*parsed);
    }
    return dump(encode(document.application, std::move(root),
                       document.clean_shutdown)) + '\n';
}

std::optional<SessionDocument> deserialize_session(std::string_view json) {
    auto parsed = Parser(json).parse();
    if (!parsed) return std::nullopt;
    auto application = decode(*parsed);
    if (!application) return std::nullopt;
    bool clean_shutdown = true;
    if (const auto* root = object(*parsed)) {
        if (const auto* value = as<bool>(*root, "clean_shutdown"))
            clean_shutdown = *value;
    }
    return SessionDocument{std::move(*application), std::string(json),
                           clean_shutdown};
}

bool write_session(const std::filesystem::path& directory,
                   const SessionDocument& document,
                   SessionDurabilityHook durability_hook) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return false;
    const auto primary = directory / kSessionFileName;
    const auto backup = directory / kSessionBackupFileName;
    const auto temporary = directory / kSessionTemporaryFileName;
    const auto backup_temporary =
        directory / kSessionBackupTemporaryFileName;
    std::filesystem::remove(temporary, error);
    if (error) return false;
    error.clear();
    std::filesystem::remove(backup_temporary, error);
    if (error) return false;
    error.clear();
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return false;
        stream << serialize_session(document);
        stream.flush();
        if (!stream) {
            stream.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
    if (durability_hook != nullptr && !durability_hook(temporary)) {
        std::filesystem::remove(temporary, error);
        return false;
    }
    const bool had_primary = std::filesystem::exists(primary, error);
    if (error) { std::filesystem::remove(temporary, error); return false; }
    if (had_primary) {
        // A corrupt primary must never replace the last known-good backup.
        if (read_file(primary)) {
            std::filesystem::copy_file(
                primary, backup_temporary,
                std::filesystem::copy_options::none, error);
            if (error) {
                std::filesystem::remove(temporary, error);
                std::filesystem::remove(backup_temporary, error);
                return false;
            }
            if (durability_hook != nullptr &&
                !durability_hook(backup_temporary)) {
                std::filesystem::remove(temporary, error);
                std::filesystem::remove(backup_temporary, error);
                return false;
            }
            error.clear();
            std::filesystem::rename(backup_temporary, backup, error);
            if (error) {
                std::filesystem::remove(temporary, error);
                std::filesystem::remove(backup_temporary, error);
                return false;
            }
        }
    }
    std::filesystem::rename(temporary, primary, error);
    if (!error) return true;
    std::filesystem::remove(temporary, error);
    std::filesystem::remove(backup_temporary, error);
    return false;
}

SessionReadResult read_session(const std::filesystem::path& directory,
                               ApplicationState default_state) {
    const auto primary = directory / kSessionFileName;
    const auto backup = directory / kSessionBackupFileName;
    std::error_code error;
    const bool directory_exists = std::filesystem::exists(directory, error);
    bool directory_status_error = bool(error);
    if (!directory_status_error && directory_exists) {
        error.clear();
        directory_status_error =
            !std::filesystem::is_directory(directory, error) || bool(error);
    }
    if (directory_status_error)
        return {{std::move(default_state), {}}, SessionSource::default_state,
                true};

    error.clear();
    const bool primary_exists = std::filesystem::exists(primary, error);
    const bool primary_status_error = bool(error);
    if (!primary_status_error && primary_exists) {
        if (auto document = read_file(primary)) return {std::move(*document), SessionSource::primary, false};
    }
    error.clear();
    const bool backup_exists = std::filesystem::exists(backup, error);
    const bool backup_status_error = bool(error);
    if (!backup_status_error && backup_exists) {
        if (auto document = read_file(backup)) return {std::move(*document), SessionSource::backup, true};
    }
    return {{std::move(default_state), {}}, SessionSource::default_state,
            primary_exists || backup_exists || primary_status_error ||
                backup_status_error};
}

}  // namespace panedock::core
