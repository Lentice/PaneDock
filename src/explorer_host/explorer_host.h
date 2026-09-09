#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shobjidl.h>
#include <shlobj.h>
#include <wrl/client.h>

#include "core/model.h"
#include "explorer_host/live_view_count.h"
#include "explorer_host/pane_error_overlay.h"

namespace panedock::explorer_host {

class ExplorerHost final {
public:
    using NavigationGeneration = std::uint64_t;
    using ShellCallCallback = void (*)(void* context,
                                       bool entering) noexcept;

    class ShellCallScope final {
    public:
        explicit ShellCallScope(ExplorerHost& host) noexcept;
        ~ShellCallScope() noexcept;

        ShellCallScope(const ShellCallScope&) = delete;
        ShellCallScope& operator=(const ShellCallScope&) = delete;

    private:
        ExplorerHost& host_;
    };

    struct ItemCounts {
        int total{};
        int selected{};
        unsigned long long selected_bytes{};
        bool selected_bytes_valid{};
    };

    ExplorerHost() noexcept = default;
    ~ExplorerHost();

    ExplorerHost(const ExplorerHost&) = delete;
    ExplorerHost& operator=(const ExplorerHost&) = delete;

    void set_shell_call_callback(void* context,
                                 ShellCallCallback callback) noexcept;
    NavigationGeneration begin_navigation() noexcept;
    HRESULT initialize(HWND parent, const RECT& rect,
                       const core::ShellLocation& location);
    HRESULT navigate(const core::ShellLocation& location);
    HRESULT navigate(const core::ShellLocation& location,
                     NavigationGeneration generation);
    HRESULT navigate_up() noexcept;
    HRESULT navigate_up(NavigationGeneration generation) noexcept;
    HRESULT refresh();
    HRESULT set_view_mode(FOLDERVIEWMODE mode, int image_size = -1) noexcept;
    HRESULT get_view_mode(FOLDERVIEWMODE& mode,
                          int* image_size = nullptr) const noexcept;
    HRESULT set_sort(std::string_view column, bool ascending) noexcept;
    HRESULT get_sort(std::string& column, bool& ascending) const noexcept;
    void set_navigation_callback(
        std::function<void(NavigationGeneration,
                           const core::ShellLocation&)> callback);
    void set_navigation_failed_callback(
        std::function<void(NavigationGeneration)> callback);
    void set_selection_changed_callback(std::function<void()> callback);
    HRESULT item_counts(ItemCounts& counts) const noexcept;
    void selection_changed() noexcept;
    void set_rect(const RECT& rect, HDWP* deferred = nullptr) noexcept;
    void set_visible(bool visible) noexcept;
    void focus() noexcept;
    HRESULT translate_accelerator(MSG* message) noexcept;
    void process_retry_request() noexcept;
    void destroy() noexcept;
    const core::ShellLocation& location() const noexcept { return location_; }
    bool show_folder_context_menu(HWND owner, POINT screen_point) noexcept;

    void navigation_complete(PCIDLIST_ABSOLUTE pidl) noexcept;
    void navigation_failed() noexcept;
    void navigation_pending() noexcept;

private:
    struct NavigationRequestRecord final {
        NavigationGeneration generation{};
        bool pending_notified{};
    };

    // Returns the generation actually enqueued, or 0 if the record could not
    // be allocated. Callers keep the returned value so a synchronous failure
    // can withdraw its own record instead of consuming the queue front.
    NavigationGeneration enqueue_navigation(
        NavigationGeneration generation) noexcept;
    NavigationGeneration take_navigation_generation() noexcept;
    // Synchronous-failure path: drops this request's own queued record, then
    // reports the failure for it. Never call it for a Shell event -- those
    // have no request token and must go through take_navigation_generation().
    void fail_enqueued_navigation(NavigationGeneration generation) noexcept;
    void report_navigation_failed(NavigationGeneration generation) noexcept;
    void enter_shell_call() noexcept;
    void leave_shell_call() noexcept;
    void install_context_menu_subclass() noexcept;
    void remove_context_menu_subclass() noexcept;
    bool show_background_context_menu(HWND owner, LPARAM lparam) noexcept;
    bool show_folder_context_menu_at(HWND owner, POINT screen_point,
                                     bool clear_selection,
                                     bool align_above) noexcept;
    LRESULT handle_context_menu_message(HWND window, UINT message,
                                        WPARAM wparam,
                                        LPARAM lparam) noexcept;
    static LRESULT CALLBACK context_menu_subclass_proc(
        HWND window, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR subclass_id, DWORD_PTR ref_data) noexcept;
    Microsoft::WRL::ComPtr<IExplorerBrowser> browser_;
    Microsoft::WRL::ComPtr<IServiceProvider> site_;
    Microsoft::WRL::ComPtr<IExplorerBrowserEvents> events_;
    LiveViewRegistration live_view_;
    DWORD advise_cookie_{0};
    bool advised_{false};
    bool initialized_{false};
    bool destroying_{false};
    HWND parent_{nullptr};
    RECT rect_{};
    PaneErrorOverlay error_overlay_;
    core::ShellLocation location_;
    std::function<void(NavigationGeneration, const core::ShellLocation&)>
        navigation_callback_;
    std::function<void(NavigationGeneration)> navigation_failed_callback_;
    std::function<void()> selection_changed_callback_;
    void* shell_call_context_{nullptr};
    ShellCallCallback shell_call_callback_{nullptr};
    Microsoft::WRL::ComPtr<IShellView> current_view_;
    mutable std::optional<ItemCounts> item_counts_cache_;
    Microsoft::WRL::ComPtr<IShellFolderViewCB> previous_view_callback_;
    Microsoft::WRL::ComPtr<IShellFolderViewCB> view_callback_;
    // IExplorerBrowserEvents has no request token; preserve start order so a
    // completion can carry the generation assigned when its navigation began.
    std::deque<NavigationRequestRecord> navigation_requests_;
    NavigationGeneration next_navigation_generation_{0};
    NavigationGeneration latest_navigation_generation_{0};
    NavigationGeneration completed_navigation_generation_{0};
    NavigationGeneration prepared_navigation_generation_{0};
    HWND context_menu_view_window_{nullptr};
    Microsoft::WRL::ComPtr<IContextMenu> context_menu_;
    Microsoft::WRL::ComPtr<IContextMenu2> context_menu2_;
    Microsoft::WRL::ComPtr<IContextMenu3> context_menu3_;
    bool context_menu_active_{false};
};

}  // namespace panedock::explorer_host
