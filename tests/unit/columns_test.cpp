#include "core/columns.h"
#include "unit/test_util.h"

using namespace panedock;
using namespace panedock::core;

int main() {
    // Empty means the default set, in the order the list always had.
    const auto defaults = parse_columns("");
    EXPECT(defaults.size() == 6 && defaults[0].id == "name" && defaults[5].id == "availability");
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
    return test::summary("columns");
}
