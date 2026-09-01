#include "app_shell/pane_chrome.h"
#include "unit/test_util.h"

namespace {

void test_rect_cache_reports_only_real_changes() {
    panedock::app_shell::PaneChrome chrome;
    const RECT first{10, 20, 110, 220};
    const RECT same{10, 20, 110, 220};
    const RECT different{10, 20, 111, 220};

    EXPECT(chrome.set_rect(first, nullptr));
    EXPECT(!chrome.set_rect(same, nullptr));
    EXPECT(chrome.set_rect(different, nullptr));
    EXPECT(chrome.laid_out_pane_rect().has_value());
    EXPECT(EqualRect(&chrome.laid_out_pane_rect().value(), &different));
}

}  // namespace

int main() {
    test_rect_cache_reports_only_real_changes();
    return panedock::test::summary("pane_chrome");
}
