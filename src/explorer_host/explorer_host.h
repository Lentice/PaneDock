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
                       std::wstring_view location);
    HRESULT navigate(std::wstring_view location);
    HRESULT navigate_up() noexcept;
    HRESULT refresh();
    HRESULT set_view_mode(FOLDERVIEWMODE mode, int image_size = -1) noexcept;
    HRESULT get_view_mode(FOLDERVIEWMODE& mode,
                          int* image_size = nullptr) const noexcept;
    HRESULT set_sort(std::string_view column, bool ascending) noexcept;
    HRESULT get_sort(std::string& column, bool& ascending) const noexcept;
    void set_navigation_callback(
        std::function<void(std::wstring_view)> callback);
    void set_navigation_failed_callback(std::function<void()> callback);
    void set_selection_changed_callback(std::function<void()> callback);
    HRESULT item_counts(ItemCounts& counts) const noexcept;
    void selection_changed() noexcept;
    void set_rect(const RECT& rect, HDWP* deferred = nullptr) noexcept;
    void set_visible(bool visible) noexcept;
    void focus() noexcept;
    HRESULT translate_accelerator(MSG* message) noexcept;
    void destroy() noexcept;
    const std::wstring& location() const noexcept { return location_; }

    void navigation_complete(PCIDLIST_ABSOLUTE pidl) noexcept;
    void navigation_failed() noexcept;

private:
    void enter_shell_call() noexcept;
    void leave_shell_call() noexcept;
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
    std::wstring location_;
    std::function<void(std::wstring_view)> navigation_callback_;
    std::function<void()> navigation_failed_callback_;
    std::function<void()> selection_changed_callback_;
    void* shell_call_context_{nullptr};
    ShellCallCallback shell_call_callback_{nullptr};
    Microsoft::WRL::ComPtr<IShellView> current_view_;
    mutable std::optional<ItemCounts> item_counts_cache_;
    Microsoft::WRL::ComPtr<IShellFolderViewCB> previous_view_callback_;
    Microsoft::WRL::ComPtr<IShellFolderViewCB> view_callback_;
};

}  // namespace panedock::explorer_host
