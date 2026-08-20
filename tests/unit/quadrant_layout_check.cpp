#include "unit/test_util.h"

#include "app_shell/quadrant_layout.h"

#include <algorithm>
#include <array>

namespace {

long long area(const RECT& rect) {
    return static_cast<long long>(rect.right - rect.left) *
           static_cast<long long>(rect.bottom - rect.top);
}

void check_layout(LONG width, LONG height) {
    const RECT client{0, 0, width, height};
    const auto rects = panedock::app_shell::quadrant_rects(client);

    long long total_area = 0;
    for (const RECT& rect : rects) {
        EXPECT(rect.left >= client.left);
        EXPECT(rect.top >= client.top);
        EXPECT(rect.right <= client.right);
        EXPECT(rect.bottom <= client.bottom);
        EXPECT(rect.right >= rect.left);
        EXPECT(rect.bottom >= rect.top);
        total_area += area(rect);
    }

    for (std::size_t first = 0; first < rects.size(); ++first) {
        for (std::size_t second = first + 1; second < rects.size();
             ++second) {
            const LONG overlap_width =
                std::min(rects[first].right, rects[second].right) -
                std::max(rects[first].left, rects[second].left);
            const LONG overlap_height =
                std::min(rects[first].bottom, rects[second].bottom) -
                std::max(rects[first].top, rects[second].top);
            EXPECT(overlap_width <= 0 || overlap_height <= 0);
        }
    }

    EXPECT(total_area == area(client));
}

void check_two_pane_layout(LONG width, LONG height) {
    const RECT client{0, 0, width, height};
    const auto rects = panedock::app_shell::two_pane_rects(client);

    EXPECT(rects[0].left == client.left);
    EXPECT(rects[0].top == client.top);
    EXPECT(rects[0].right == rects[1].left);
    EXPECT(rects[1].right == client.right);
    EXPECT(rects[0].bottom == client.bottom);
    EXPECT(rects[1].bottom == client.bottom);
    EXPECT(area(rects[0]) + area(rects[1]) == area(client));
    EXPECT(area(rects[2]) == 0);
    EXPECT(area(rects[3]) == 0);
}

}  // namespace

int main() {
    constexpr std::array<std::array<LONG, 2>, 9> cases{{
        {{0, 0}}, {{0, 5}}, {{1, 0}}, {{1, 1}}, {{1, 7}},
        {{3, 5}}, {{5, 3}}, {{7, 9}}, {{8, 6}},
    }};

    for (const auto dimensions : cases) {
        check_layout(dimensions[0], dimensions[1]);
        check_two_pane_layout(dimensions[0], dimensions[1]);
    }

    return panedock::test::summary("quadrant_layout_check");
}
