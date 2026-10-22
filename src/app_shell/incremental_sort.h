#pragma once

#include <algorithm>

namespace panedock::app_shell::detail {

// The prefix is already ordered; only the newly appended suffix needs a sort
// before the two runs are merged.
template<class RandomAccessIterator, class Compare>
void sort_appended_and_merge(RandomAccessIterator first,
                             RandomAccessIterator middle,
                             RandomAccessIterator last,
                             Compare compare) {
    if (first == middle) {
        std::sort(first, last, compare);
        return;
    }
    if (middle == last) return;
    std::sort(middle, last, compare);
    std::inplace_merge(first, middle, last, compare);
}

} // namespace panedock::app_shell::detail
