#include "app_shell/pinned_locations_dialog.h"

#include <algorithm>
#include <utility>

#include "app_shell/window_helpers.h"

namespace panedock::app_shell {
namespace {

constexpr wchar_t kWindowClassName[] = L"PaneDockPinnedLocationsWindow";
// IsDialogMessageW (main loop) turns Enter into WM_COMMAND IDOK (1) and Esc
// into IDCANCEL (2), so no control may use those ids for anything else:
// Esc must never arrive as "Remove".
constexpr int kListId = 100;
constexpr int kRemoveId = 101;
constexpr int kMoveUpId = 102;
constexpr int kMoveDownId = 103;
constexpr int kApplyId = 104;
constexpr int kOkId = IDOK;
constexpr int kCancelId = IDCANCEL;
constexpr int kButtonCount = 6;
constexpr int kWindowWidth = 440;
constexpr int kWindowHeight = 320;
constexpr int kSpaceBase = 12;
constexpr int kSpaceSnug = 8;

constexpr std::array<const wchar_t*, kButtonCount> kButtonLabels{
    L"Remove", L"Move Up", L"Move Down", L"Apply", L"OK", L"Cancel"};
constexpr std::array<int, kButtonCount> kButtonIds{
    kRemoveId, kMoveUpId, kMoveDownId, kApplyId, kOkId, kCancelId};

}  // namespace

bool PinnedLocationsDialog::register_window_class(HINSTANCE instance) noexcept {
    return register_simple_window_class(kWindowClassName, &window_proc,
                                        instance,
                                        reinterpret_cast<HBRUSH>(
                                            COLOR_WINDOW + 1));
}

void PinnedLocationsDialog::show(
    HWND owner, const std::vector<core::ShellLocation>& pinned,
    std::vector<std::wstring> display_labels, HFONT font) {
    if (window_ != nullptr) {
        refresh();
        ShowWindow(window_, SW_SHOWNORMAL);
        SetForegroundWindow(window_);
        SetFocus(list_);
        return;
    }

    std::vector<Row> rows;
    rows.reserve(pinned.size());
    for (std::size_t index = 0; index < pinned.size(); ++index)
        rows.push_back({pinned[index], index < display_labels.size()
                                           ? std::move(display_labels[index])
                                           : pinned[index].parsing_name});
    draft_ = std::move(rows);
    applied_ = pinned;
    result_.reset();
    font_ = font;
    const UINT owner_dpi = owner == nullptr ? 96 : GetDpiForWindow(owner);
    const UINT dpi = owner_dpi == 0 ? 96 : owner_dpi;
    const int width = MulDiv(kWindowWidth, static_cast<int>(dpi), 96);
    const int height = MulDiv(kWindowHeight, static_cast<int>(dpi), 96);
    HWND manager = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_CONTROLPARENT, kWindowClassName,
        L"Manage Pinned Locations", WS_POPUP | WS_CAPTION | WS_SYSMENU, 0, 0,
        width, height, owner, nullptr, GetModuleHandleW(nullptr), this);
    if (manager == nullptr) {
        draft_.reset();
        applied_.reset();
        return;
    }

    center_over_owner(manager, owner, width, height);
    ShowWindow(manager, SW_SHOWNORMAL);
    UpdateWindow(manager);
    SetForegroundWindow(manager);
    SetFocus(list_);
}

void PinnedLocationsDialog::destroy() noexcept {
    if (window_ != nullptr) DestroyWindow(window_);
    draft_.reset();
    applied_.reset();
}

void PinnedLocationsDialog::apply_font(HFONT font) noexcept {
    font_ = font;
    if (list_ != nullptr && font != nullptr)
        SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    for (HWND button : buttons) {
        if (button != nullptr && font != nullptr)
            SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font),
                         TRUE);
    }
}

void PinnedLocationsDialog::add_location(core::ShellLocation location,
                                         std::wstring display_label) {
    if (!draft_.has_value() || !applied_.has_value()) return;
    // Pinned from a pane while the manager is open: already applied.
    if (!core::can_pin_location(draft_locations(), location)) return;
    if (core::can_pin_location(*applied_, location)) applied_->push_back(location);
    draft_->push_back({std::move(location), std::move(display_label)});
    refresh();
}

std::optional<std::vector<core::ShellLocation>>
PinnedLocationsDialog::take_result() noexcept {
    return std::exchange(result_, std::nullopt);
}

std::vector<core::ShellLocation> PinnedLocationsDialog::draft_locations() const {
    std::vector<core::ShellLocation> locations;
    if (!draft_.has_value()) return locations;
    locations.reserve(draft_->size());
    for (const auto& row : *draft_) locations.push_back(row.location);
    return locations;
}

void PinnedLocationsDialog::refresh_buttons() noexcept {
    if (list_ == nullptr || !draft_.has_value() || !applied_.has_value()) return;
    const auto& rows = *draft_;
    const LRESULT selected = SendMessageW(list_, LB_GETCURSEL, 0, 0);
    const std::size_t index = selected >= 0
                                  ? static_cast<std::size_t>(selected)
                                  : rows.size();
    const bool has_selection = index < rows.size();
    EnableWindow(buttons[0], has_selection);
    EnableWindow(buttons[1], has_selection && index > 0);
    EnableWindow(buttons[2], has_selection && index + 1 < rows.size());
    EnableWindow(buttons[3], draft_locations() != *applied_);
    EnableWindow(buttons[4], TRUE);
    EnableWindow(buttons[5], TRUE);
}

void PinnedLocationsDialog::refresh(
    std::optional<std::size_t> selected_index) {
    if (list_ == nullptr || !draft_.has_value()) return;
    const auto& rows = *draft_;
    if (!selected_index.has_value()) {
        const LRESULT selected = SendMessageW(list_, LB_GETCURSEL, 0, 0);
        if (selected >= 0)
            selected_index = static_cast<std::size_t>(selected);
    }

    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    for (const auto& row : rows)
        SendMessageW(list_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(row.label.c_str()));
    if (selected_index.has_value() && *selected_index < rows.size()) {
        SendMessageW(list_, LB_SETCURSEL,
                     static_cast<WPARAM>(*selected_index), 0);
    }
    refresh_buttons();
}

void PinnedLocationsDialog::layout(HWND window) noexcept {
    if (window == nullptr || list_ == nullptr) return;
    RECT client{};
    if (!GetClientRect(window, &client)) return;
    const UINT dpi = std::max<UINT>(96, GetDpiForWindow(window));
    const auto scaled = [dpi](int value) {
        return MulDiv(value, static_cast<int>(dpi), 96);
    };
    const int width = std::max(0, static_cast<int>(client.right - client.left));
    const int height = std::max(0, static_cast<int>(client.bottom - client.top));
    const int margin = scaled(kSpaceBase);
    const int gap = scaled(kSpaceSnug);
    const int button_height = scaled(28);
    const int button_y = std::max(margin, height - margin - button_height);
    const int list_bottom = std::max(margin, button_y - gap);
    const int list_width = std::max(0, width - 2 * margin);
    SetWindowPos(list_, nullptr, margin, margin, list_width,
                 std::max(0, list_bottom - margin),
                 SWP_NOZORDER | SWP_NOACTIVATE);

    const int available = std::max(
        0, width - 2 * margin - (kButtonCount - 1) * gap);
    const int button_width = std::max(1, available / kButtonCount);
    int x = margin;
    for (HWND button : buttons) {
        SetWindowPos(button, nullptr, x, button_y, button_width,
                     button_height, SWP_NOZORDER | SWP_NOACTIVATE);
        x += button_width + gap;
    }
}

void PinnedLocationsDialog::apply_draft(bool close) {
    if (!draft_.has_value() || !applied_.has_value()) return;
    auto locations = draft_locations();
    if (locations != *applied_) result_ = locations;
    if (!close) {
        *applied_ = std::move(locations);
        refresh();
        return;
    }
    DestroyWindow(window_);
}

LRESULT CALLBACK PinnedLocationsDialog::window_proc(HWND window, UINT message,
                                                     WPARAM wparam,
                                                     LPARAM lparam) {
    auto* self = reinterpret_cast<PinnedLocationsDialog*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = window_state_from_create<PinnedLocationsDialog>(window, lparam);
        if (self == nullptr) return FALSE;
        self->window_ = window;
    }

    switch (message) {
        case WM_CREATE: {
            if (self == nullptr) return -1;
            const HINSTANCE instance = GetModuleHandleW(nullptr);
            self->list_ = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY |
                    LBS_NOINTEGRALHEIGHT,
                0, 0, 0, 0, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)),
                instance, nullptr);
            if (self->list_ == nullptr) return -1;
            for (std::size_t index = 0; index < kButtonLabels.size(); ++index) {
                self->buttons[index] = CreateWindowExW(
                    0, L"BUTTON", kButtonLabels[index],
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                        (kButtonIds[index] == kOkId ? BS_DEFPUSHBUTTON
                                                    : BS_PUSHBUTTON),
                    0, 0,
                    0, 0, window,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(
                        kButtonIds[index])),
                    instance, nullptr);
                if (self->buttons[index] == nullptr) return -1;
            }
            self->apply_font(self->font_);
            self->layout(window);
            self->refresh();
            return 0;
        }
        case WM_SIZE:
            if (self != nullptr) self->layout(window);
            return 0;
        case DM_GETDEFID:
            return MAKELRESULT(kOkId, DC_HASDEFID);
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            if (suggested != nullptr)
                SetWindowPos(window, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            if (self != nullptr) {
                self->apply_font(self->font_);
                self->layout(window);
            }
            return 0;
        }
        case WM_COMMAND:
            if (self == nullptr) break;
            if (LOWORD(wparam) == kListId && HIWORD(wparam) == LBN_SELCHANGE) {
                self->refresh_buttons();
                return 0;
            }
            if (HIWORD(wparam) != BN_CLICKED) break;
            if (LOWORD(wparam) == kCancelId) {
                self->draft_.reset();
                DestroyWindow(window);
                return 0;
            }
            if (LOWORD(wparam) == kApplyId || LOWORD(wparam) == kOkId) {
                self->apply_draft(LOWORD(wparam) == kOkId);
                return 0;
            }

            {
                const LRESULT selected = SendMessageW(
                    self->list_, LB_GETCURSEL, 0, 0);
                if (selected == LB_ERR || selected < 0) return 0;
                const std::size_t index = static_cast<std::size_t>(selected);
                if (!self->draft_.has_value() || index >= self->draft_->size())
                    return 0;
                auto& rows = *self->draft_;
                bool changed = false;
                std::optional<std::size_t> next_selection;
                if (LOWORD(wparam) == kRemoveId) {
                    rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(index));
                    changed = true;
                    if (!rows.empty()) next_selection = std::min(index, rows.size() - 1);
                } else if (LOWORD(wparam) == kMoveUpId && index > 0) {
                    std::swap(rows[index], rows[index - 1]);
                    changed = true;
                    next_selection = index - 1;
                } else if (LOWORD(wparam) == kMoveDownId && index + 1 < rows.size()) {
                    std::swap(rows[index], rows[index + 1]);
                    changed = true;
                    next_selection = index + 1;
                }
                if (changed) self->refresh(next_selection);
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_NCDESTROY:
            if (self != nullptr && self->window_ == window) {
                self->window_ = nullptr;
                self->list_ = nullptr;
                self->buttons.fill(nullptr);
                self->draft_.reset();
                self->applied_.reset();
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            }
            break;
        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace panedock::app_shell
