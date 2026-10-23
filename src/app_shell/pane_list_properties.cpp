#include "app_shell/pane.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace panedock::app_shell {
int Pane::expensive_sort_slot() const noexcept {
    const auto* tab = active_tab();
    if (!tab || shell_core::parse_sort_property(tab->sort_column)) return -1;
    const auto& key = tab->sort_column;
    if (key == "System.ItemTypeText" || key == "{B725F130-47EF-101A-A5F1-02608C9EEBAC} 4") return 0;
    if (key == "System.Media.Duration" || key == "{64440490-4C8B-11D1-8B70-080036B11A03} 3") return 1;
    // Preserve unknown future settings and their name fallback. Existing Shell
    // PROPERTYKEY strings and canonical System names are queried on the worker.
    return key.size() <= 128 && (key.starts_with("{") || key.starts_with("System.")) ? 2 : -1;
}
void Pane::append_property_requests(std::vector<managed_list::IconRequest>& requests,
                                    const std::vector<std::size_t>& visible) {
    if (property_capacity_failed_) return; // explicit Refresh retry, never polling
    const auto add = [&](std::size_t index, int slot, const std::string& property) {
        if (index >= list_.rows.size()) return true;
        if (slot == 1 && list_.rows[index].directory) {
            list_.rows[index].properties[slot].state = managed_list::PropertyState::missing;
            return true;
        }
        if (list_.rows[index].properties[slot].state != managed_list::PropertyState::unresolved) return true;
        if (std::any_of(requests.begin(), requests.end(), [&](const auto& r) {
            return r.row_index == index && r.property_slot == slot && !r.property.empty();
        })) return true;
        if (managed_list::append_icon_request(requests,
                {list_.request, icon_viewport_, list_icon_key(index), property, slot, index})) return true;
        // A full byte/count window continues on the next completion. A single
        // unrepresentable request uses the existing capacity-failure policy.
        if (requests.empty()) property_capacity_failed_ = true;
        return false;
    };
    const int sort_slot = expensive_sort_slot();
    if (property_sort_pending_ && sort_slot >= 0) {
        // Visible keys first, but a sort needs every retained row, not just the
        // viewport. Keep one linear cursor between bounded dispatches.
        for (auto index : visible) if (!add(index, sort_slot, property_sort_key_)) break;
        while (property_sort_cursor_ < list_.rows.size() && requests.size() < managed_list::kIconInterest) {
            if (!add(property_sort_cursor_, sort_slot, property_sort_key_)) break;
            ++property_sort_cursor_;
        }
        return;
    }
    // Only intersecting Details columns create display demand. Tiles/Content
    // continue to use their cached size/date summary and need no slow metadata.
    if (list_window_ && list_mode_ == FVM_DETAILS) {
        const HWND header = ListView_GetHeader(list_window_);
        RECT client{}; GetClientRect(list_window_, &client);
        for (int slot = 0; slot < 2; ++slot) {
            RECT column{};
            const int position = list_column_index(slot == 0 ? "type" : "duration");
            if (!header || position < 0 || !Header_GetItemRect(header, position, &column)) continue;
            MapWindowPoints(header, list_window_, reinterpret_cast<POINT*>(&column), 2);
            if (column.right <= client.left || column.left >= client.right || column.right <= column.left) continue;
            for (auto index : visible)
                add(index, slot, slot == 0 ? "System.ItemTypeText" : "System.Media.Duration");
        }
    }
}
void Pane::accept_property_result(managed_list::IconResult& result) {
    const auto& request = result.request;
    if (request.property_slot < 0 || request.property_slot > 2 || request.row_index >= list_.rows.size()) return;
    auto& row = list_.rows[request.row_index];
    // Index is just a lookup hint. Identity/version is authoritative after any
    // enumeration merge, reorder, navigation or viewport replacement.
    if (!request.key.matches(row)) return;
    if (request.property_slot == 2 && request.property != property_sort_key_) return;
    auto& value = result.property;
    if (value.state == managed_list::PropertyState::unresolved || value.text.size() > 128 ||
        (value.numeric && (!std::isfinite(value.number) || (request.property_slot == 1 &&
            (value.number < 0 || value.number > std::numeric_limits<std::uint64_t>::max()))))) {
        value = {}; value.state = managed_list::PropertyState::failed;
    }
    if (value.text.empty()) std::wstring{}.swap(value.text);
    auto& target = row.properties[request.property_slot];
    if (target.state != managed_list::PropertyState::unresolved) return;
    const auto old_bytes = managed_list::owned_string_bytes(target.text);
    const auto bytes = managed_list::owned_string_bytes(value.text);
    if (!row_store_.fits(list_.rows, old_bytes, bytes)) {
        value = {}; value.state = managed_list::PropertyState::failed;
        property_capacity_failed_ = true;
    }
    row_store_.account(old_bytes, managed_list::owned_string_bytes(value.text));
    target = std::move(value);
    if (list_window_) ListView_RedrawItems(list_window_, static_cast<int>(request.row_index),
                                         static_cast<int>(request.row_index));
}
void Pane::finish_property_batch() noexcept try {
    if (!active() ||
        !content_request_is_current(list_.request.binding) || !enrichment_interest_.empty()) return;
    if (property_sort_pending_ && list_.watch != managed_list::WatchState::recovering &&
        list_.phase != managed_list::Phase::loading &&
        (property_sort_cursor_ >= list_.rows.size() || property_capacity_failed_)) {
        if (!property_order_.active()) property_order_.start(list_.rows.size());
        if (!property_order_.step(2048, [this](auto a, auto b) { return property_row_less(a, b); })) {
            // Bounded active work, not a polling timer: yield to input between
            // merge steps. Rows/selection stay untouched until one final commit.
            queue_list_icon_viewport();
            update_managed_list();
            return;
        }
        cancel_list_rename();
        const auto interaction = capture_managed_list_interaction();
        property_sort_pending_ = false;
        property_order_.apply(list_.rows);
        restore_managed_list_interaction(interaction);
        if (list_window_) InvalidateRect(list_window_, nullptr, FALSE);
        // Reordered indices invalidate all previous viewport lookups.
        cancel_list_icons();
        queue_list_icon_viewport();
    }
    update_managed_list();
} catch (...) {
    property_capacity_failed_ = true;
    property_sort_pending_ = false;
    cancel_list_icons();
    update_managed_list();
}
} // namespace panedock::app_shell
