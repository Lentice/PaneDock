#include "managed_list/row_store.h"

#include <utility>

namespace panedock::managed_list {

void RowStore::recount(const std::vector<Row>& rows) noexcept {
    name_bytes_ = 0;
    for (const auto& row : rows) name_bytes_ += owned_row_bytes(row);
}

std::optional<std::size_t> RowStore::append(std::vector<Row>& rows,
                                            std::vector<Row>& incoming) {
    const auto count = std::min(incoming.size(), kMaxRows - rows.size());
    const auto required = rows.size() + count;
    if (required > rows.capacity()) {
        auto capacity = std::min(kMaxRows, std::max(required, rows.capacity() * 2));
        if (name_bytes_ + capacity * sizeof(Row) > kMaxRowBytes) capacity = required;
        if (name_bytes_ + capacity * sizeof(Row) > kMaxRowBytes) return std::nullopt;
        rows.reserve(capacity);
    }
    std::size_t accepted = 0;
    for (; accepted < count; ++accepted) {
        const auto bytes = owned_row_bytes(incoming[accepted]);
        if (name_bytes_ + bytes + rows.capacity() * sizeof(Row) > kMaxRowBytes) break;
        name_bytes_ += bytes;
        rows.push_back(std::move(incoming[accepted]));
    }
    return accepted;
}

void RowStore::clear_index() noexcept {
    index_.clear();
    decltype(seen_) empty{IndexAllocator<unsigned char>{&index_memory_}};
    seen_.swap(empty);
}

bool RowStore::index_row(std::size_t index) noexcept try {
    index_.insert(index);
    return true;
} catch (...) {
    // The allocator rejects before exceeding its separate 4 MiB ceiling.
    clear_index();
    return false;
}

DWORD RowStore::begin_recovery(const std::vector<Row>& rows) {
    rows_ = &rows;
    clear_index();
    work_ = {};
    try {
        // Fixed mark storage shares the SAME 4 MiB allocator ceiling.
        // No per-row padding is added to the authoritative row snapshot.
        seen_.assign(kMaxRows, 0);
    } catch (...) {
        clear_index();
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!index_row(i)) return ERROR_NOT_ENOUGH_MEMORY;
        ++work_.indexed_rows;
    }
    return 0;
}

RowStore::ChangeResult RowStore::apply(std::vector<Row>& rows, Batch& batch,
                                       bool recovering) {
    rows_ = &rows;
    ChangeResult result;
    const auto abort = [&](DWORD error) -> ChangeResult& {
        clear_index();
        result.aborted = true;
        result.watch_failed = error;
        return result;
    };
    if (!batch_within_limits(batch)) return abort(ERROR_INVALID_DATA);
    const bool rescan = batch.change == Change::rescan;
    // Recovery's index keys and seen marks stay valid only while nothing but
    // rescan batches touch the rows until its terminal batch.
    if (recovering && !rescan) return abort(ERROR_INVALID_STATE);
    auto& touched = result.touched;
    for (const auto& leaf : batch.removed) touched.push_back({leaf, false});
    for (const auto& [old_leaf, new_leaf] : batch.renamed) {
        touched.push_back({old_leaf, false});
        touched.push_back({new_leaf, false});
    }
    // A rename removes the old identity here; the new one arrives as a row.
    for (const auto& [old_leaf, new_leaf] : batch.renamed) {
        const ItemIdentity from{old_leaf, false};
        std::erase_if(rows, [&](const auto& row) {
            if (row.identity != from) return false;
            name_bytes_ -= owned_row_bytes(row);
            return true;
        });
    }
    if (!batch.removed.empty()) std::erase_if(rows, [&](const auto& row) {
        if (row.identity.shell_item || std::find(batch.removed.begin(), batch.removed.end(),
                row.identity.key) == batch.removed.end()) return false;
        name_bytes_ -= owned_row_bytes(row);
        return true;
    });
    // Recovery builds this index exactly once. No erasing/reordering until its
    // terminal batch: stable integer keys survive row-vector reallocations.
    // Ordinary deltas build once per coalesced batch, never once per row.
    if (!rescan) {
        clear_index();
        for (std::size_t i = 0; i < rows.size(); ++i)
            if (!index_row(i)) return abort(ERROR_NOT_ENOUGH_MEMORY);
    }
    // A delta's updated rows (by index) and appended rows leave the order;
    // the untouched rows remain one sorted run.
    const std::size_t delta_base = rows.size();
    std::vector<std::size_t> delta_updated;
    for (auto& row : batch.rows) {
        if (!prepare_row(row)) return abort(ERROR_INVALID_DATA);
        if (rescan) ++work_.lookups;
        const auto match = index_.find(row.identity);
        if (match != index_.end()) {
            auto* found = &rows[*match];
            // Recovery can keep known metadata for unchanged rows; ordinary
            // notifications invalidate properties even if size/time are equal.
            const bool unchanged = rescan && found->bytes == row.bytes &&
                found->date_modified == row.date_modified && found->directory == row.directory &&
                found->name == row.name && found->child_location == row.child_location &&
                found->cloud == row.cloud;
            if (!unchanged) {
                const auto old_bytes = owned_row_bytes(*found);
                const auto new_bytes = owned_row_bytes(row);
                if (!fits(rows, old_bytes, new_bytes)) return abort(ERROR_NOT_ENOUGH_MEMORY);
                account(old_bytes, new_bytes);
                touched.push_back(row.identity);
                if (batch.change == Change::delta) delta_updated.push_back(*match);
                *found = std::move(row);
            } else {
                // A cache hit cleared the retained properties, but the worker
                // already resolved Type and Duration's missing state while
                // enumerating (PD-256). Adopt those instead of re-requesting
                // them per row. Slot 2 belongs to the current sort key only.
                for (std::size_t slot = 0; slot < 2; ++slot) {
                    auto& target = found->properties[slot];
                    auto& value = row.properties[slot];
                    if (target.state != PropertyState::unresolved ||
                        value.state == PropertyState::unresolved) continue;
                    const auto old_bytes = owned_string_bytes(target.text);
                    const auto new_bytes = owned_string_bytes(value.text);
                    // Over budget: stay unresolved; on-demand requests apply the cap.
                    if (!fits(rows, old_bytes, new_bytes)) continue;
                    account(old_bytes, new_bytes);
                    target = std::move(value);
                }
            }
            if (rescan) seen_[*match] = 1;
        } else {
            const auto bytes = owned_row_bytes(row);
            if (rows.size() == kMaxRows ||
                name_bytes_ + bytes + std::max(rows.capacity(), rows.size() + 1) *
                    sizeof(Row) > kMaxRowBytes) {
                result.limited = true;
                return abort(ERROR_NOT_ENOUGH_MEMORY);
            }
            if (rows.size() == rows.capacity()) {
                const auto capacity = std::min(kMaxRows, std::max<std::size_t>(
                    rows.size() + 1, rows.capacity() * 2));
                // Reserve headroom for owned strings as well as Row slots.
                // Filling all remaining bytes with empty slots would make the
                // very next recovery append fail despite ample usable budget.
                const auto average = (name_bytes_ + bytes + rows.size()) / (rows.size() + 1);
                const auto available = std::max(rows.size() + 1,
                    kMaxRowBytes / (sizeof(Row) + average));
                rows.reserve(std::min(capacity, available));
            }
            name_bytes_ += bytes;
            touched.push_back(row.identity);
            rows.push_back(std::move(row));
            if (!index_row(rows.size() - 1)) return abort(ERROR_NOT_ENOUGH_MEMORY);
            if (rescan) seen_[rows.size() - 1] = 1;
        }
        if (rescan) ++work_.accounted_rows;
    }
    if (rescan && batch.phase != Phase::loading) {
        if (batch.phase == Phase::complete || batch.phase == Phase::empty) {
            index_.clear(); // keys must be gone before compaction/sort
            std::size_t index = 0;
            std::erase_if(rows, [&](const auto& row) {
                ++work_.accounted_rows;
                if (seen_[index++]) return false;
                name_bytes_ -= owned_row_bytes(row);
                return true;
            });
            result.recovered = true;
        } else {
            // Never purge unseen rows after an incomplete recovery.
            result.watch_failed = batch.error ? batch.error : ERROR_BUFFER_OVERFLOW;
        }
    }
    const bool still_recovering = recovering && !result.recovered && !result.watch_failed;
    if (still_recovering) return result;
    clear_index();
    if (batch.change == Change::delta) {
        // Move updated rows behind the untouched sorted run, after the
        // appended ones, in one pass; the caller then sorts only that short
        // suffix and merges (O(n + k log k)) instead of re-sorting the folder.
        std::sort(delta_updated.begin(), delta_updated.end());
        // A Source may update one identity more than once in a batch. Move
        // each row once so the remaining prefix and its length stay valid.
        delta_updated.erase(std::unique(delta_updated.begin(), delta_updated.end()),
                            delta_updated.end());
        std::vector<Row> moved;
        moved.reserve(delta_updated.size());
        std::size_t write = 0, next = 0;
        for (std::size_t read = 0; read < rows.size(); ++read) {
            if (read < delta_base && next < delta_updated.size() && delta_updated[next] == read) {
                moved.push_back(std::move(rows[read])); ++next; continue;
            }
            if (write != read) rows[write] = std::move(rows[read]);
            ++write;
        }
        for (auto& row : moved) rows[write++] = std::move(row);
        result.sorted_prefix = delta_base - delta_updated.size();
    }
    return result;
}

}  // namespace panedock::managed_list
