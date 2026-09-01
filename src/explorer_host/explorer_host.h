#pragma once

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

namespace panedock::explorer_host {

class ExplorerHost final {
public:
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
    HRESULT initialize(HWND parent, const RECT& rect,
                       const core::ShellLocation& location);
    HRESULT navigate(const core::ShellLocation& location);
    HRESULT navigate_up() noexcept;
    HRESULT refresh();
    HRESULT set_view_mode(FOLDERVIEWMODE mode, int image_size = -1) noexcept;
    HRESULT get_view_mode(FOLDERVIEWMODE& mode,
                          int* image_size = nullptr) const noexcept;
    HRESULT set_sort(std::string_view column, bool ascending) noexcept;
    HRESULT get_sort(std::string& column, bool& ascending) const noexcept;
    void set_navigation_callback(
        std::function<void(const core::ShellLocation&)> callback);
    void set_navigation_failed_callback(std::function<void()> callback);
    void set_selection_changed_callback(std::function<void()> callback);
    HRESULT item_counts(ItemCounts& counts) const noexcept;
    void selection_changed() noexcept;
    void set_rect(const RECT& rect, HDWP* deferred = nullptr) noexcept;
    void set_visible(bool visible) noexcept;
    void focus() noexcept;
    HRESULT translate_accelerator(MSG* message) noexcept;
    void destroy() noexcept;
    const core::ShellLocation& location() const noexcept { return location_; }
    bool show_folder_context_menu(HWND owner, POINT screen_point) noexcept;

    void navigation_complete(PCIDLIST_ABSOLUTE pidl) noexcept;
    void navigation_failed() noexcept;

private:
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
    static bool register_error_window_class() noexcept;
    static LRESULT CALLBACK error_window_proc(HWND window, UINT message,
                                               WPARAM wparam,
                                               LPARAM lparam) noexcept;
    void layout_error_controls() noexcept;
    void retry_navigation() noexcept;

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
    HWND error_window_{nullptr};
    HWND error_message_{nullptr};
    HWND retry_button_{nullptr};
    bool error_visible_{false};
    core::ShellLocation location_;
    std::function<void(const core::ShellLocation&)> navigation_callback_;
    std::function<void()> navigation_failed_callback_;
    std::function<void()> selection_changed_callback_;
    void* shell_call_context_{nullptr};
    ShellCallCallback shell_call_callback_{nullptr};
    Microsoft::WRL::ComPtr<IShellView> current_view_;
    mutable std::optional<ItemCounts> item_counts_cache_;
    Microsoft::WRL::ComPtr<IShellFolderViewCB> previous_view_callback_;
    Microsoft::WRL::ComPtr<IShellFolderViewCB> view_callback_;
    HWND context_menu_view_window_{nullptr};
    Microsoft::WRL::ComPtr<IContextMenu> context_menu_;
    Microsoft::WRL::ComPtr<IContextMenu2> context_menu2_;
    Microsoft::WRL::ComPtr<IContextMenu3> context_menu3_;
    bool context_menu_active_{false};
};

}  // namespace panedock::explorer_host
