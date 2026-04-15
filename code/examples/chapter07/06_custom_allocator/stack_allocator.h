// stack_allocator.h - 支持标记回滚的栈分配器
#pragma once
#include <cstddef>
#include <cstdint>
#include <new>

class StackAllocator {
    char* start_;
    char* top_;
    char* end_;

public:
    StackAllocator(void* buf, std::size_t size)
        : start_(static_cast<char*>(buf))
        , top_(start_)
        , end_(start_ + size)
    {}

    void* allocate(std::size_t n, std::size_t align = alignof(std::max_align_t)) noexcept {
        // 对齐处理
        std::uintptr_t p = reinterpret_cast<std::uintptr_t>(top_);
        std::size_t mis = p % align;
        std::size_t offset = mis ? (align - mis) : 0;

        if (top_ + offset + n > end_) return nullptr;

        top_ += offset;
        void* res = top_;
        top_ += n;
        return res;
    }

    // 标记当前栈位置
    using Marker = char*;
    Marker mark() noexcept { return top_; }

    // 回滚到指定标记
    void rollback(Marker m) noexcept {
        if (m >= start_ && m <= top_) {
            top_ = m;
        }
    }

    void reset() noexcept { top_ = start_; }

    std::size_t used() const noexcept { return top_ - start_; }
    std::size_t available() const noexcept { return end_ - top_; }
    std::size_t capacity() const noexcept { return end_ - start_; }
};
