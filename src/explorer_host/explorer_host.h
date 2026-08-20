#pragma once

#include <string_view>

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
    void set_rect(const RECT& rect) noexcept;
    void destroy() noexcept;

private:
    Microsoft::WRL::ComPtr<IExplorerBrowser> browser_;
    Microsoft::WRL::ComPtr<IServiceProvider> site_;
    Microsoft::WRL::ComPtr<IExplorerBrowserEvents> events_;
    LiveViewRegistration live_view_;
    DWORD advise_cookie_{0};
    bool advised_{false};
    bool initialized_{false};
    bool destroying_{false};
};

}  // namespace panedock::explorer_host
