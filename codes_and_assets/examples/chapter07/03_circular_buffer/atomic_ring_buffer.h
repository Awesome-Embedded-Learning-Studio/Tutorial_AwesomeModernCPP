// atomic_ring_buffer.h - 线程安全的循环缓冲区实现
#pragma once
#include <cstddef>
#include <array>
#include <atomic>

template<typename T, std::size_t Capacity>
class AtomicRingBuffer {
public:
    AtomicRingBuffer() : head_(0), tail_(0) {}

    bool push(const T& value) {
        std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t next_head = (head + 1) % Capacity;
        std::size_t tail = tail_.load(std::memory_order_acquire);

        if (next_head == tail) {
            return false;  // 缓冲区满了
        }

        buffer_[head] = value;
        head_.store(next_head, std::memory_order_release);
        return true;
    }

    bool pop(T& out) {
        std::size_t tail = tail_.load(std::memory_order_relaxed);
        std::size_t head = head_.load(std::memory_order_acquire);

        if (tail == head) {
            return false;  // 没数据
        }

        out = buffer_[tail];
        std::size_t next_tail = (tail + 1) % Capacity;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    bool empty() const {
        std::size_t head = head_.load(std::memory_order_acquire);
        std::size_t tail = tail_.load(std::memory_order_acquire);
        return head == tail;
    }

    bool full() const {
        std::size_t head = head_.load(std::memory_order_acquire);
        std::size_t tail = tail_.load(std::memory_order_acquire);
        return (head + 1) % Capacity == tail;
    }

    std::size_t size() const {
        std::size_t head = head_.load(std::memory_order_acquire);
        std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head >= tail) {
            return head - tail;
        }
        return Capacity - (tail - head);
    }

    std::size_t capacity() const {
        return Capacity - 1;  // 实际可用容量
    }

private:
    std::array<T, Capacity> buffer_;
    std::atomic<std::size_t> head_;
    std::atomic<std::size_t> tail_;
};
