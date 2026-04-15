// bump_allocator.h - 非线程安全的线性分配器
#pragma once
#include <cstddef>
#include <new>
#include <cstdint>
#include <cstring>

class BumpAllocator {
    char* start_;
    char* ptr_;
    char* end_;

public:
    BumpAllocator(void* buffer, std::size_t size)
        : start_(static_cast<char*>(buffer))
        , ptr_(start_)
        , end_(start_ + size)
    {}

    void* allocate(std::size_t n, std::size_t align = alignof(std::max_align_t)) noexcept {
        std::size_t space = end_ - ptr_;
        std::uintptr_t p = reinterpret_cast<std::uintptr_t>(ptr_);
        std::size_t mis = p % align;
        std::size_t offset = mis ? (align - mis) : 0;

        if (n + offset > space) return nullptr;

        ptr_ += offset;
        void* res = ptr_;
        ptr_ += n;
        return res;
    }

    // 不支持单个释放，只能重置
    void deallocate(void* p) noexcept {
        // Bump allocator 不支持单个释放
        (void)p;
    }

    void reset() noexcept { ptr_ = start_; }

    std::size_t used() const noexcept { return ptr_ - start_; }
    std::size_t available() const noexcept { return end_ - ptr_; }
    std::size_t capacity() const noexcept { return end_ - start_; }
};
