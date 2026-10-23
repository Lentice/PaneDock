#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>
#include <optional>
#include <set>
#include <vector>

#include "managed_list/list_worker.h"

namespace panedock::managed_list {

// The batch limits a change notification must respect before any of it is
// applied: at most kBatchRows rows, removals and renames each.
inline bool batch_within_limits(const Batch& batch) noexcept {
    return batch.rows.size() <= kBatchRows && batch.removed.size() <= kBatchRows &&
           batch.renamed.size() <= kBatchRows;
}

// The managed list's rows held to one byte budget, and the change-notification
// merge over them. The budget is kMaxRowBytes for owned strings (names, keys,
// child locations, property text and the list's namespace locations) plus the
// row vector's slots; the recovery index has its own kMaxIndexBytes ceiling.
//
// No HWND, no Source: the Pane keeps the row vector (its ListSnapshot) and
// passes it to every call, and performs what a result asks for -- stopping the
// watch, sorting a delta's suffix, dropping stale icons. Not copyable: the
// index's comparator and allocator point back into the store.
class RowStore final {
public:
    struct RecoveryWork final {
        std::size_t indexed_rows{}, lookups{}, comparisons{}, accounted_rows{};
    };

    RowStore() = default;
    RowStore(const RowStore&) = delete;
    RowStore& operator=(const RowStore&) = delete;

    // ---- Byte budget ------------------------------------------------------
    std::size_t name_bytes() const noexcept { return name_bytes_; }
    // Whether replacing `removed` owned bytes by `added` keeps `rows` within
    // the budget at their current capacity.
    bool fits(const std::vector<Row>& rows, std::size_t removed,
              std::size_t added) const noexcept {
        return name_bytes_ - removed + added + rows.capacity() * sizeof(Row) <=
               kMaxRowBytes;
    }
    void account(std::size_t removed, std::size_t added) noexcept {
        name_bytes_ = name_bytes_ - removed + added;
    }
    // The rows were replaced wholesale (navigation, a restored snapshot).
    void recount(const std::vector<Row>& rows) noexcept;
    // Appends what fits of an enumeration batch: within kMaxRows, reserving
    // capacity geometrically while the budget allows. nullopt when not even
    // the batch's own rows can be reserved; otherwise the rows accepted, which
    // are moved out of `incoming` from its front.
    std::optional<std::size_t> append(std::vector<Row>& rows, std::vector<Row>& incoming);

    // ---- Change merge -----------------------------------------------------
    struct ChangeResult final {
        // The watch must stop with this error; the rows are kept.
        std::optional<DWORD> watch_failed;
        // The batch was abandoned part-way: nothing after the failure ran.
        bool aborted{};
        // The row count or budget ran out appending a new row.
        bool limited{};
        // A complete rescan was reconciled: unseen rows are gone.
        bool recovered{};
        // Identities whose cached images may be stale.
        std::vector<ItemIdentity> touched;
        // A delta outside recovery: rows [sorted_prefix, end) were moved
        // behind the untouched sorted run and still need sorting and merging.
        std::optional<std::size_t> sorted_prefix;
    };
    // Change::rescan_begin: index every row and start marking which ones the
    // rescan sees. Returns 0, or the error the watch must stop with.
    DWORD begin_recovery(const std::vector<Row>& rows);
    // Change::delta and Change::rescan. `recovering` is whether a rescan is
    // in progress (the list's WatchState::recovering) when the batch arrives.
    ChangeResult apply(std::vector<Row>& rows, Batch& batch, bool recovering);
    void clear_index() noexcept;

    // ---- Evidence for the bounded-list harness ------------------------------
    RecoveryWork work() const noexcept { return work_; }
    std::size_t index_bytes() const noexcept { return index_memory_.used; }
    std::size_t index_peak_bytes() const noexcept { return index_memory_.peak; }
    // Lowers, never raises, the production index ceiling.
    void limit_index_bytes(std::size_t bytes) noexcept {
        index_memory_.limit = std::min(bytes, kMaxIndexBytes);
    }

private:
    struct IndexMemory final {
        std::size_t used{}, peak{}, limit{kMaxIndexBytes};
    };
    template<class T> struct IndexAllocator final {
        using value_type = T;
        IndexMemory* memory{};
        explicit IndexAllocator(IndexMemory* value) noexcept : memory(value) {}
        template<class U> IndexAllocator(const IndexAllocator<U>& other) noexcept
            : memory(other.memory) {}
        T* allocate(std::size_t n) {
            if (memory->used > memory->limit || n > (memory->limit - memory->used) / sizeof(T))
                throw std::bad_alloc();
            auto* result = std::allocator<T>{}.allocate(n);
            memory->used += n * sizeof(T);
            memory->peak = std::max(memory->peak, memory->used);
            return result;
        }
        void deallocate(T* value, std::size_t n) noexcept {
            memory->used -= n * sizeof(T);
            std::allocator<T>{}.deallocate(value, n);
        }
        template<class U> bool operator==(const IndexAllocator<U>& other) const noexcept {
            return memory == other.memory;
        }
    };
    struct RowIdentityLess final {
        using is_transparent = void;
        const RowStore* store{};
        const ItemIdentity& identity(std::size_t index) const {
            return (*store->rows_)[index].identity;
        }
        const ItemIdentity& identity(const ItemIdentity& value) const { return value; }
        template<class A, class B> bool operator()(const A& a, const B& b) const {
            ++store->work_.comparisons;
            // Preserve PD-232's exact, case-sensitive leaf comparison. Display
            // sorting's case folding must never merge distinct local keys.
            return identity(a) < identity(b);
        }
    };
    bool index_row(std::size_t index) noexcept;

    std::size_t name_bytes_{};
    mutable RecoveryWork work_;
    IndexMemory index_memory_;
    // The rows the index's integer keys refer to; set on every entry.
    const std::vector<Row>* rows_{};
    // Indices survive vector reallocation. Both entry count and allocator node
    // bytes are bounded; no duplicate row/property snapshot is retained.
    std::set<std::size_t, RowIdentityLess, IndexAllocator<std::size_t>> index_{
        RowIdentityLess{this}, IndexAllocator<std::size_t>{&index_memory_}};
    std::vector<unsigned char, IndexAllocator<unsigned char>> seen_{
        IndexAllocator<unsigned char>{&index_memory_}};
};

}  // namespace panedock::managed_list
