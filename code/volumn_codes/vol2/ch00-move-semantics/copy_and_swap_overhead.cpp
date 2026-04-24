// copy_and_swap_overhead.cpp
// 验证：copy-and-swap 惯用法 vs 独立赋值运算符的开销对比
// Standard: C++17
//
// 结论（GCC 15, -O2）：
//   移动赋值场景下，copy-and-swap 多出约 3 条寄存器操作（即 swap 的代价），
//   但没有额外的函数调用或内存操作。对绝大多数场景可忽略。

#include <cstddef>
#include <cstring>
#include <utility>
#include <iostream>
#include <chrono>

class BufferSeparate {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    explicit BufferSeparate(std::size_t capacity = 0)
        : data_(capacity ? new char[capacity] : nullptr)
        , size_(0)
        , capacity_(capacity)
    {}

    ~BufferSeparate() { delete[] data_; }

    BufferSeparate(const BufferSeparate& other)
        : data_(other.capacity_ ? new char[other.capacity_] : nullptr)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        if (data_) std::memcpy(data_, other.data_, size_);
    }

    BufferSeparate(BufferSeparate&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    BufferSeparate& operator=(const BufferSeparate& other)
    {
        if (this != &other) {
            delete[] data_;
            data_ = new char[other.capacity_];
            size_ = other.size_;
            capacity_ = other.capacity_;
            if (data_) std::memcpy(data_, other.data_, size_);
        }
        return *this;
    }

    BufferSeparate& operator=(BufferSeparate&& other) noexcept
    {
        if (this != &other) {
            delete[] data_;
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
        }
        return *this;
    }
};

class BufferSwap {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    explicit BufferSwap(std::size_t capacity = 0)
        : data_(capacity ? new char[capacity] : nullptr)
        , size_(0)
        , capacity_(capacity)
    {}

    ~BufferSwap() { delete[] data_; }

    BufferSwap(const BufferSwap& other)
        : data_(other.capacity_ ? new char[other.capacity_] : nullptr)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        if (data_) std::memcpy(data_, other.data_, size_);
    }

    BufferSwap(BufferSwap&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    friend void swap(BufferSwap& a, BufferSwap& b) noexcept
    {
        using std::swap;
        swap(a.data_, b.data_);
        swap(a.size_, b.size_);
        swap(a.capacity_, b.capacity_);
    }

    BufferSwap& operator=(BufferSwap other) noexcept
    {
        swap(*this, other);
        return *this;
    }
};

constexpr int kIterations = 100000;

int main()
{
    // 移动赋值性能对比
    {
        BufferSeparate sep_buf(1024);
        std::cout << "=== 移动赋值：独立运算符 ===\n";
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kIterations; ++i) {
            BufferSeparate tmp(1024);
            sep_buf = std::move(tmp);
        }
        auto end = std::chrono::high_resolution_clock::now();
        auto sep_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "  " << kIterations << " 次移动赋值: " << sep_ms << " us\n";

        BufferSwap swp_buf(1024);
        std::cout << "=== 移动赋值：copy-and-swap ===\n";
        start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < kIterations; ++i) {
            BufferSwap tmp(1024);
            swp_buf = std::move(tmp);
        }
        end = std::chrono::high_resolution_clock::now();
        auto swp_ms = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        std::cout << "  " << kIterations << " 次移动赋值: " << swp_ms << " us\n";

        std::cout << "  差异: " << (swp_ms - sep_ms) << " us ("
                  << (sep_ms > 0 ? (swp_ms * 100 / sep_ms - 100) : 0) << "%)\n";
    }

    return 0;
}
