#include "unit/test_util.h"

#include "explorer_host/explorer_host.h"
#include "explorer_host/live_view_count.h"

#include <array>
#include <cstdio>
#include <propkey.h>
#include <propsys.h>

int main() {
    using panedock::explorer_host::LiveViewRegistration;
    using panedock::explorer_host::live_view_count;
    using ItemCounts = panedock::explorer_host::ExplorerHost::ItemCounts;

    EXPECT(live_view_count() == 0);
    const ItemCounts empty_counts{};
    EXPECT(empty_counts.selected_bytes == 0);
    EXPECT(!empty_counts.selected_bytes_valid);

    {
        LiveViewRegistration view;
        view.mark_initialized();
        EXPECT(live_view_count() == 1);
        view.reset();
        EXPECT(live_view_count() == 0);
    }

    {
        LiveViewRegistration view;
        view.mark_initialized();
        view.reset();
        view.reset();
        EXPECT(live_view_count() == 0);
    }

    const HRESULT ole_result = OleInitialize(nullptr);
    EXPECT(SUCCEEDED(ole_result));
    HWND parent = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW,
                                  0, 0, 640, 480, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    EXPECT(parent != nullptr);
    if (SUCCEEDED(ole_result) && parent != nullptr) {
        panedock::explorer_host::ExplorerHost host;
        const RECT rect{0, 0, 640, 480};
        const HRESULT initialize_result =
            host.initialize(parent, rect,
                            {L"shell:Desktop", {}, {}});
        EXPECT(SUCCEEDED(initialize_result));
        if (SUCCEEDED(initialize_result)) {
            ItemCounts counts;
            EXPECT(SUCCEEDED(host.item_counts(counts)));
            EXPECT(counts.selected == 0);
            EXPECT(counts.selected_bytes == 0);
            EXPECT(counts.selected_bytes_valid);
            FOLDERVIEWMODE mode{};
            int image_size{};
            EXPECT(SUCCEEDED(host.get_view_mode(mode, &image_size)));
            std::printf("initial view mode=%d image size=%d\n",
                        static_cast<int>(mode), image_size);
            EXPECT(host.set_sort({}, true) == E_INVALIDARG);
            EXPECT(host.set_sort(std::string(PKEYSTR_MAX, 'x'), true) ==
                   E_INVALIDARG);
            wchar_t name_key_text[PKEYSTR_MAX]{};
            EXPECT(SUCCEEDED(PSStringFromPropertyKey(
                PKEY_ItemNameDisplay, name_key_text, ARRAYSIZE(name_key_text))));
            std::string name_key;
            for (const wchar_t value : std::wstring_view(name_key_text))
                name_key.push_back(static_cast<char>(value));
            for (const bool expected_ascending : std::array{true, false}) {
                EXPECT(SUCCEEDED(host.set_sort(name_key, expected_ascending)));
                std::string actual_column;
                bool actual_ascending{};
                EXPECT(SUCCEEDED(host.get_sort(actual_column,
                                               actual_ascending)));
                EXPECT(actual_column == name_key);
                EXPECT(actual_ascending == expected_ascending);
            }
            for (const int expected_size : std::array{256, 96, 48, 16}) {
                EXPECT(SUCCEEDED(host.set_view_mode(FVM_ICON, expected_size)));
                int actual_size{};
                EXPECT(SUCCEEDED(host.get_view_mode(mode, &actual_size)));
                EXPECT(mode == FVM_ICON ||
                       (expected_size == 16 && mode == FVM_SMALLICON));
                EXPECT(actual_size == expected_size);
                std::printf("icon size requested=%d actual=%d mode=%d\n",
                            expected_size, actual_size, static_cast<int>(mode));
            }
            for (const FOLDERVIEWMODE expected_mode :
                 std::array{FVM_LIST, FVM_DETAILS, FVM_TILE, FVM_CONTENT}) {
                EXPECT(SUCCEEDED(host.set_view_mode(expected_mode)));
                int actual_size{};
                EXPECT(SUCCEEDED(host.get_view_mode(mode, &actual_size)));
                EXPECT(mode == expected_mode);
                std::printf("view mode requested=%d actual=%d image size=%d\n",
                            static_cast<int>(expected_mode), static_cast<int>(mode),
                            actual_size);
            }
            using Generation =
                panedock::explorer_host::ExplorerHost::NavigationGeneration;
            Generation failed_generation = 0;
            int failed_count = 0;
            host.set_navigation_failed_callback(
                [&](Generation generation) {
                    failed_generation = generation;
                    ++failed_count;
                });

            // Whether this name fails inside navigate() or later through
            // OnNavigationFailed is the Shell's call and is not stable across
            // repeats, so assert the contract instead of the verdict: a
            // navigate() that reports failure in its HRESULT must raise
            // exactly one failure callback, carrying its OWN generation.
            // navigate() used to return a bare S_OK on every failure path,
            // which made the previous SUCCEEDED() assertion here vacuous, and
            // it reported failures with whatever generation sat at the front
            // of the request queue -- a request still in flight.
            constexpr std::wstring_view missing =
                L"?:\\PaneDock-PD-022-definitely-not-there";
            int synchronous_failures = 0;
            for (int attempt = 0; attempt < 4; ++attempt) {
                const int before = failed_count;
                const Generation requested = host.begin_navigation();
                const HRESULT navigate_result =
                    host.navigate({std::wstring(missing), {}, {}}, requested);
                EXPECT(host.location().parsing_name == missing);
                if (FAILED(navigate_result)) {
                    ++synchronous_failures;
                    EXPECT(failed_count == before + 1);
                    EXPECT(failed_generation == requested);
                    // The previous folder may still have a live view. Its
                    // settings must not be captured for the failed target.
                    std::string old_sort;
                    bool old_ascending{};
                    EXPECT(host.get_view_mode(mode, &image_size) == E_PENDING);
                    EXPECT(host.get_sort(old_sort, old_ascending) == E_PENDING);
                    EXPECT(host.set_view_mode(FVM_DETAILS) == E_PENDING);
                    EXPECT(host.set_sort(name_key, true) == E_PENDING);
                } else {
                    EXPECT(failed_count == before);
                }
            }
            // Keep the loop above from passing vacuously.
            EXPECT(synchronous_failures > 0);
            std::printf("synchronous navigation failures=%d of 4\n",
                        synchronous_failures);
            host.set_navigation_failed_callback({});
        }
        host.destroy();
        EXPECT(live_view_count() == 0);
        EXPECT(GetWindow(parent, GW_CHILD) == nullptr);
        DestroyWindow(parent);
    }
    if (SUCCEEDED(ole_result)) OleUninitialize();

    return panedock::test::summary("explorer_host_lifetime_check");
}
