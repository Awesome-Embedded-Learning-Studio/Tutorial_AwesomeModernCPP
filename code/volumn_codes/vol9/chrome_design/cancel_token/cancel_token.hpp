#pragma once
#include <atomic>
#include <memory>

namespace tamcpp::chrome {
class CancelableToken {
    struct Flag {
        std::atomic<bool> valid{true};
    };
    // All token should share a simple flags
    std::shared_ptr<Flag> flag_;

  public:
    CancelableToken() : flag_(std::make_shared<Flag>()) {}
    void invalidate() { flag_->valid.store(false, std::memory_order_release); }
    bool is_valid() const { return flag_->valid.load(std::memory_order_acquire); }
};
} // namespace tamcpp::chrome
