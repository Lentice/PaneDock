#include "core/columns.h"
#include "unit/test_util.h"

using namespace panedock;
using namespace panedock::core;

int main() {
    // Empty means Explorer's default set and order: Name, Date modified, Type, Size.
    const auto defaults = parse_columns("");
    EXPECT(defaults.size() == 4 && defaults[0].id == "name" && defaults[1].id == "date" &&
           defaults[2].id == "type" && defaults[3].id == "size");
    EXPECT(find_column("date") && std::wstring_view(find_column("date")->label) == L"Date modified");
    // A layout persisted by an earlier build (all six columns) is kept as is.
    const auto persisted = parse_columns("name:320,size:160,date:180,type:160,duration:120,availability:200");
    EXPECT(persisted.size() == 6 && persisted[1].id == "size" && persisted[5].id == "availability");
    // The header menu lists the defaults first, then every other known column once.
    const auto menu = column_menu_order();
    EXPECT(menu.size() == kKnownColumns.size() && menu[1]->id == "date" && menu[3]->id == "size" &&
           menu[4]->id == "duration" && menu[5]->id == "availability");
    EXPECT(parse_columns(format_columns(defaults)) == defaults);

    // Order, widths and the visible set round-trip.
    auto columns = parse_columns("name:300,type:90,size:200");
    EXPECT(columns.size() == 3 && columns[1].id == "type" && columns[1].width == 90);
    EXPECT(format_columns(columns) == "name:300,type:90,size:200");

    // Name is forced in and first; duplicates and empty tokens are dropped; widths are
    // clamped; an id this build does not know is kept (as is, with a default width).
    columns = parse_columns("size:150,junk,,date:5,size:999,name:10:20,date:999999");
    EXPECT(columns.size() == 4);
    EXPECT(columns[0].id == "name" && columns[1].id == "size" && columns[2].id == "junk" &&
           columns[3].id == "date");
    EXPECT(columns[1].width == 150 && columns[3].width == kMinColumnWidth);
    EXPECT(parse_columns("size:150")[0].id == "name");

    // Unknown ids from a newer build survive the round trip.
    columns = parse_columns("name:300,tags:77,size:200");
    EXPECT(columns.size() == 3 && columns[1].id == "tags" && columns[1].width == 77);
    EXPECT(format_columns(columns) == "name:300,tags:77,size:200");

    // Toggling: Name is fixed, hiding removes, showing appends with the default width.
    columns = parse_columns("name:300,size:200");
    toggle_column(columns, "name");
    EXPECT(column_visible(columns, "name"));
    toggle_column(columns, "size");
    EXPECT(!column_visible(columns, "size") && columns.size() == 1);
    toggle_column(columns, "duration");
    EXPECT(columns.size() == 2 && columns[1].id == "duration" && columns[1].width == 120);
    toggle_column(columns, "no-such-column");
    EXPECT(columns.size() == 2);

    // Widths are stored as 96-DPI pixels and scaled to the pane's DPI; the round trip
    // is stable so a capture right after an apply does not drift.
    EXPECT(column_width_to_pixels(320, 96) == 320);
    EXPECT(column_width_to_pixels(320, 144) == 480);
    EXPECT(column_width_to_pixels(181, 120) == 226);
    EXPECT(column_width_from_pixels(480, 144) == 320);
    for (int width = kMinColumnWidth; width <= 1000; ++width)
        for (const unsigned dpi : {96u, 120u, 144u, 168u, 192u, 288u})
            EXPECT(column_width_from_pixels(column_width_to_pixels(width, dpi), dpi) == width);
    // Capturing the header keeps a newer build's column after the shown ones.
    {
        const std::vector<ColumnSpec> shown{{"name", 300}, {"size", 150}};
        EXPECT(captured_columns("name:320,future:90,size:160", shown) ==
               std::optional<std::string>{"name:300,size:150,future:90"});
        // A known column the user hid is not resurrected.
        EXPECT(captured_columns("name:320,size:160,type:160", shown) ==
               std::optional<std::string>{"name:300,size:150"});
        // No change, and the default set under an empty setting, write nothing.
        EXPECT(!captured_columns("name:300,size:150", shown));
        EXPECT(!captured_columns("", default_columns()));
        EXPECT(captured_columns("", shown) == std::optional<std::string>{"name:300,size:150"});
    }
    return test::summary("columns");
}
