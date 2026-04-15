// ring_buffer.h - 简单的循环缓冲区实现
#pragma once
#include <cstddef>
#include <array>

template<typename T, std::size_t Capacity>
class RingBuffer {
public:
    bool push(const T& value) {
        if (full()) {
            return false;  // 缓冲区满了
        }

        buffer_[head_] = value;
        head_ = (head_ + 1) % Capacity;
        return true;
    }

    bool pop(T& out) {
        if (empty()) {
            return false;  // 没数据
        }

        out = buffer_[tail_];
        tail_ = (tail_ + 1) % Capacity;
        return true;
    }

    bool empty() const {
        return head_ == tail_;
    }

    bool full() const {
        return (head_ + 1) % Capacity == tail_;
    }

    std::size_t size() const {
        if (head_ >= tail_) {
            return head_ - tail_;
        }
        return Capacity - (tail_ - head_);
    }

    std::size_t capacity() const {
        return Capacity - 1;  // 实际可用容量
    }

    void clear() {
        head_ = tail_ = 0;
    }

private:
    std::array<T, Capacity> buffer_{};
    std::size_t head_ = 0;
    std::size_t tail_ = 0;
};
