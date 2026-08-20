#pragma once

#include <array>
#include <cstddef>

namespace base {

/// Lock-free single-producer single-consumer circular buffer.
/// Designed for ISR-to-main communication: push() in ISR, pop() in main loop.
/// N must be a power of 2 for bitmask-based index wrapping (zero modulo overhead).
template <size_t N>
class CircularBuffer {
    static_assert(N > 0 && (N & (N - 1)) == 0, "N must be a power of 2");

  public:
    bool push(std::byte b) noexcept {
        if (full()) {
            return false;
        }
        buf_[mask(head_)] = b;
        head_              = next(head_);
        return true;
    }

    bool pop(std::byte& out) noexcept {
        if (empty()) {
            return false;
        }
        out   = buf_[mask(tail_)];
        tail_ = next(tail_);
        return true;
    }

    bool   empty() const noexcept { return head_ == tail_; }
    bool   full()  const noexcept { return ((head_ - tail_) & (2 * N - 1)) == N; }
    size_t size()  const noexcept { return (head_ - tail_) & (2 * N - 1); }

  private:
    static constexpr size_t mask(size_t v) noexcept { return v & (N - 1); }
    static constexpr size_t next(size_t v) noexcept { return (v + 1) & (2 * N - 1); }

    std::array<std::byte, N> buf_{};
    // Volatile: head written by ISR, tail read by ISR; tail written by main, head read by main.
    volatile size_t head_ = 0;
    volatile size_t tail_ = 0;
};

} // namespace base
