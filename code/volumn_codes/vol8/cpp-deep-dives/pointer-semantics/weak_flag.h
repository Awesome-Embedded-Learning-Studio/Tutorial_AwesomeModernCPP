#pragma once

#include <atomic>

class WeakFlag {
  public:
    WeakFlag() = default;

    WeakFlag(const WeakFlag&) = delete;
    WeakFlag& operator=(const WeakFlag&) = delete;

    void add_ref() { ref_count_.fetch_add(1, std::memory_order_relaxed); }

    void release() {
        if (ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }

    void invalidate() { is_valid_.store(false, std::memory_order_release); }

    bool is_valid() const { return is_valid_.load(std::memory_order_acquire); }

  private:
    std::atomic<bool> is_valid_{true};
    std::atomic<int> ref_count_{1};
    ~WeakFlag() = default;
};
