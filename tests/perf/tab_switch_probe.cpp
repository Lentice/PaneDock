#include "explorer_host/explorer_host.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace {

using Clock = std::chrono::steady_clock;
using Host = panedock::explorer_host::ExplorerHost;

bool pump_until(Clock::time_point deadline, const auto& done,
                DWORD max_wait_ms = 20) {
    while (!done()) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) return false;
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (done()) break;
        const auto now = Clock::now();
        if (now >= deadline) return false;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - now);
        const DWORD wait_ms = static_cast<DWORD>(
            remaining.count() < max_wait_ms ? remaining.count() : max_wait_ms);
        MsgWaitForMultipleObjectsEx(0, nullptr, wait_ms, QS_ALLINPUT,
                                    MWMO_INPUTAVAILABLE);
    }
    return true;
}

double elapsed_ms(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

bool measure(Host& host, const wchar_t* label, const wchar_t* path,
             int expected_items) {
    const auto generation = host.begin_navigation();
    Clock::time_point completed{};
    bool failed = false;
    host.set_navigation_callback([&](Host::NavigationGeneration value,
                                     const panedock::core::ShellLocation&) {
        if (value == generation) completed = Clock::now();
    });
    host.set_navigation_failed_callback(
        [&](Host::NavigationGeneration value) {
            if (value == generation) failed = true;
        });

    const auto start = Clock::now();
    const HRESULT result = host.navigate({path, {}, {}}, generation);
    const auto returned = Clock::now();
    const auto deadline = start + std::chrono::seconds(30);
    const bool finished = pump_until(deadline, [&] {
        return completed != Clock::time_point{} || failed;
    });

    int items = -1;
    Clock::time_point items_at{};
    if (finished && !failed) {
        pump_until(deadline, [&] {
            // The diagnostic probe asks for a fresh count each time. The
            // product's status-bar cache is not a list-population signal.
            host.selection_changed();
            Host::ItemCounts counts{};
            if (SUCCEEDED(host.item_counts(counts)) &&
                counts.total >= expected_items) {
                items = counts.total;
                items_at = Clock::now();
                return true;
            }
            return false;
        });
    }

    std::wprintf(L"%ls,0x%08lx,%.2f,%.2f,%.2f,%d\n", label,
                 static_cast<unsigned long>(result), elapsed_ms(start, returned),
                 completed == Clock::time_point{} ? -1.0
                                                  : elapsed_ms(start, completed),
                 items_at == Clock::time_point{} ? -1.0
                                                 : elapsed_ms(start, items_at),
                 items);
    std::fflush(stdout);
    host.set_navigation_callback({});
    host.set_navigation_failed_callback({});
    return SUCCEEDED(result) && finished && !failed && items_at != Clock::time_point{};
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 6) {
        std::fwprintf(stderr,
                      L"Usage: panedock_tab_switch_probe <from> <to> <from-count> <to-count> <idle-seconds>\n");
        return 2;
    }
    const int from_count = _wtoi(argv[3]);
    const int to_count = _wtoi(argv[4]);
    const int idle_seconds = _wtoi(argv[5]);
    if (from_count <= 0 || to_count <= 0 || idle_seconds < 0 ||
        idle_seconds > 3600) return 2;

    const HRESULT ole_result = OleInitialize(nullptr);
    if (FAILED(ole_result)) return 2;
    HWND window = CreateWindowExW(0, L"STATIC", L"PaneDock Shell timing probe",
                                  WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                  CW_USEDEFAULT, 800, 600, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    if (window == nullptr) {
        OleUninitialize();
        return 2;
    }
    ShowWindow(window, SW_SHOWNOACTIVATE);

    Host host;
    const RECT rect{0, 0, 780, 560};
    const HRESULT initialized =
        host.initialize(window, rect, {L"shell:Desktop", {}, {}});
    if (FAILED(initialized)) {
        DestroyWindow(window);
        OleUninitialize();
        return 2;
    }
    host.set_visible(true);

    std::wprintf(L"phase,hresult,call_ms,complete_ms,all_items_ms,item_count\n");
    const bool setup = measure(host, L"setup_from", argv[1], from_count);
    const bool warm = setup && measure(host, L"warm_to", argv[2], to_count) &&
                      measure(host, L"return_from", argv[1], from_count);
    bool after_idle = false;
    if (warm) {
        std::wprintf(L"idle_seconds,%d\n", idle_seconds);
        std::fflush(stdout);
        const auto until = Clock::now() + std::chrono::seconds(idle_seconds);
        pump_until(until, [until] { return Clock::now() >= until; }, 60000);
        after_idle = measure(host, L"after_idle_to", argv[2], to_count);
    }

    host.destroy();
    DestroyWindow(window);
    OleUninitialize();
    return after_idle ? 0 : 1;
}
