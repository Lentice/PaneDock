#pragma once

#include <string_view>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shobjidl.h>
#include <wrl/client.h>

#include "explorer_host/live_view_count.h"

namespace panedock::explorer_host {

class ExplorerHost final {
public:
    ExplorerHost() noexcept = default;
    ~ExplorerHost();

    ExplorerHost(const ExplorerHost&) = delete;
    ExplorerHost& operator=(const ExplorerHost&) = delete;

    HRESULT initialize(HWND parent, const RECT& rect,
                       std::wstring_view location);
    HRESULT navigate(std::wstring_view location);
    void set_rect(const RECT& rect) noexcept;
    void set_visible(bool visible) noexcept;
    void set_active(bool active) noexcept;
    void focus() noexcept;
    HRESULT translate_accelerator(MSG* message) noexcept;
    void destroy() noexcept;
    const std::wstring& location() const noexcept { return location_; }

    void navigation_complete(PCIDLIST_ABSOLUTE pidl) noexcept;
    void navigation_failed() noexcept;

private:
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
    bool error_visible_{false};
    std::wstring location_;
};

}  // namespace panedock::explorer_host
