#pragma once
#include <algorithm>
#include <cstddef>
#include <numeric>
#include <vector>
#include <utility>

namespace panedock::core {
// Cooperative merge sort of an owned permutation. No row copies or OS work.
// Two size_t arrays: <=1.6 MiB for the managed-list 100,000-row ceiling.
class BoundedOrder final {
    std::vector<std::size_t> order_, scratch_;
    std::size_t width_{1}, begin_{}, left_{}, middle_{}, right_{}, end_{}, out_{};
    void run() noexcept {
        left_ = begin_; middle_ = std::min(begin_ + width_, order_.size());
        right_ = middle_; end_ = std::min(middle_ + width_, order_.size()); out_ = begin_;
    }
  public:
    bool active() const noexcept { return !order_.empty(); }
    void start(std::size_t count) {
        order_.resize(count); scratch_.resize(count);
        std::iota(order_.begin(), order_.end(), 0);
        width_ = 1; begin_ = 0; run();
    }
    void clear() noexcept { std::vector<std::size_t>{}.swap(order_); std::vector<std::size_t>{}.swap(scratch_); }
    template<class Less> bool step(std::size_t budget, Less less) noexcept {
        if (order_.size() < 2) return true;
        while (budget != 0) {
            if (out_ == end_) {
                begin_ = end_;
                if (begin_ == order_.size()) {
                    order_.swap(scratch_); width_ *= 2; begin_ = 0;
                    if (width_ >= order_.size()) return true;
                }
                run();
            }
            scratch_[out_++] = left_ < middle_ &&
                (right_ == end_ || !less(order_[right_], order_[left_])) ? order_[left_++] : order_[right_++];
            --budget;
        }
        return false;
    }
    template<class Rows> void apply(Rows& rows) noexcept {
        for (std::size_t index = 0; index < order_.size(); ++index) {
            if (order_[index] == index) continue;
            auto saved = std::move(rows[index]);
            auto target = index;
            while (order_[target] != index) {
                const auto source = order_[target];
                rows[target] = std::move(rows[source]); order_[target] = target; target = source;
            }
            rows[target] = std::move(saved); order_[target] = target;
        }
    }
};
} // namespace panedock::core
