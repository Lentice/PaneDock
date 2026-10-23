#include "managed_list/row_store.h"
#include "unit/test_util.h"

#include <string>
#include <vector>

using namespace panedock;
namespace ml = panedock::managed_list;

namespace {

ml::Row row(const wchar_t* name, std::uint64_t bytes = 1) {
    ml::Row value{name, bytes, false};
    value.identity = {name, false};
    return value;
}

std::size_t counted(const std::vector<ml::Row>& rows) {
    std::size_t bytes = 0;
    for (const auto& value : rows) bytes += ml::owned_row_bytes(value);
    return bytes;
}

ml::Batch change(ml::Change kind, std::vector<ml::Row> rows = {},
                 ml::Phase phase = ml::Phase::complete) {
    ml::Batch batch;
    batch.change = kind;
    batch.rows = std::move(rows);
    batch.phase = phase;
    return batch;
}

std::vector<std::wstring> names(const std::vector<ml::Row>& rows) {
    std::vector<std::wstring> result;
    for (const auto& value : rows) result.push_back(value.name);
    return result;
}

// Appending an enumeration batch accounts every owned byte it keeps.
void append_accounts_bytes() {
    ml::RowStore store;
    std::vector<ml::Row> rows;
    std::vector<ml::Row> incoming{row(L"a"), row(L"b")};
    const auto accepted = store.append(rows, incoming);
    EXPECT(accepted == std::optional<std::size_t>{2});
    EXPECT(store.name_bytes() == counted(rows));
    EXPECT(store.fits(rows, 0, 0));
    EXPECT(!store.fits(rows, 0, ml::kMaxRowBytes));
}

// A delta updates, renames, removes and appends; the changed rows are moved
// behind the untouched sorted run for the caller to sort and merge.
void delta_moves_changed_rows_behind_the_sorted_run() {
    ml::RowStore store;
    std::vector<ml::Row> rows{row(L"a"), row(L"b"), row(L"c"), row(L"d")};
    store.recount(rows);
    auto batch = change(ml::Change::delta, {row(L"b", 99), row(L"e"), row(L"z")});
    batch.removed = {L"c"};
    batch.renamed = {{L"d", L"z"}};
    const auto result = store.apply(rows, batch, false);
    EXPECT(!result.aborted && !result.watch_failed && !result.recovered);
    // "a" stays sorted; updated "b" follows the appended "e" and "z".
    EXPECT((names(rows) == std::vector<std::wstring>{L"a", L"e", L"z", L"b"}));
    EXPECT(result.sorted_prefix == std::optional<std::size_t>{1});
    EXPECT(store.name_bytes() == counted(rows));
    EXPECT(store.index_bytes() == 0);
    // c, d->z, b, e, z: every identity whose image may be stale.
    EXPECT(result.touched.size() == 6);
}

// A rescan keeps unchanged rows, adds new ones and, once complete, drops the
// rows it never saw.
void complete_rescan_reconciles_unseen_rows() {
    ml::RowStore store;
    std::vector<ml::Row> rows{row(L"a"), row(L"gone"), row(L"b")};
    store.recount(rows);
    EXPECT(store.begin_recovery(rows) == 0);
    EXPECT(store.work().indexed_rows == 3);
    EXPECT(store.index_bytes() > 0);
    auto first = change(ml::Change::rescan, {row(L"a"), row(L"new")}, ml::Phase::loading);
    auto partial = store.apply(rows, first, true);
    EXPECT(!partial.recovered && !partial.watch_failed && !partial.sorted_prefix);
    EXPECT(store.index_bytes() > 0); // still recovering: the index stays
    auto last = change(ml::Change::rescan, {row(L"b")}, ml::Phase::complete);
    const auto done = store.apply(rows, last, true);
    EXPECT(done.recovered && !done.watch_failed);
    EXPECT((names(rows) == std::vector<std::wstring>{L"a", L"b", L"new"}));
    EXPECT(store.name_bytes() == counted(rows));
    EXPECT(store.index_bytes() == 0);
    EXPECT(store.work().lookups == 3);
}

// An incomplete rescan never purges: the rows stay and the watch stops.
void incomplete_rescan_keeps_rows_and_fails_the_watch() {
    ml::RowStore store;
    std::vector<ml::Row> rows{row(L"a"), row(L"b")};
    store.recount(rows);
    EXPECT(store.begin_recovery(rows) == 0);
    auto failed = change(ml::Change::rescan, {row(L"a")}, ml::Phase::error);
    failed.error = ERROR_ACCESS_DENIED;
    const auto result = store.apply(rows, failed, true);
    EXPECT(!result.aborted && result.watch_failed == std::optional<DWORD>{ERROR_ACCESS_DENIED});
    EXPECT(rows.size() == 2);
    EXPECT(store.index_bytes() == 0);
}

// Over-limit or malformed batches stop the watch before touching any row.
void bad_batches_abort_untouched() {
    ml::RowStore store;
    std::vector<ml::Row> rows{row(L"a")};
    store.recount(rows);
    auto oversized = change(ml::Change::delta);
    oversized.removed.assign(ml::kBatchRows + 1, L"a");
    auto result = store.apply(rows, oversized, false);
    EXPECT(result.aborted && result.watch_failed == std::optional<DWORD>{ERROR_INVALID_DATA});
    EXPECT(rows.size() == 1);
    auto invalid = change(ml::Change::delta, {row(L"bad\\name")});
    result = store.apply(rows, invalid, false);
    EXPECT(result.aborted && result.watch_failed == std::optional<DWORD>{ERROR_INVALID_DATA});
}

// A local delta during recovery (a rename committed mid-rescan) fails the
// watch instead of discarding the seen marks the next rescan batch writes.
void delta_during_recovery_fails_the_watch() {
    ml::RowStore store;
    std::vector<ml::Row> rows{row(L"a"), row(L"b")};
    store.recount(rows);
    EXPECT(store.begin_recovery(rows) == 0);
    auto local = change(ml::Change::delta, {row(L"c")});
    const auto result = store.apply(rows, local, true);
    EXPECT(result.aborted && result.watch_failed == std::optional<DWORD>{ERROR_INVALID_STATE});
    EXPECT(rows.size() == 2);
    EXPECT(store.index_bytes() == 0);
}

// A lowered index ceiling fails recovery cleanly instead of growing past it.
void index_ceiling_fails_recovery() {
    ml::RowStore store;
    store.limit_index_bytes(1);
    std::vector<ml::Row> rows{row(L"a")};
    EXPECT(store.begin_recovery(rows) == ERROR_NOT_ENOUGH_MEMORY);
    EXPECT(store.index_bytes() == 0);
}

}  // namespace

int main() {
    append_accounts_bytes();
    delta_moves_changed_rows_behind_the_sorted_run();
    complete_rescan_reconciles_unseen_rows();
    incomplete_rescan_keeps_rows_and_fails_the_watch();
    bad_batches_abort_untouched();
    delta_during_recovery_fails_the_watch();
    index_ceiling_fails_recovery();
    return panedock::test::summary("managed_list_row_store");
}
