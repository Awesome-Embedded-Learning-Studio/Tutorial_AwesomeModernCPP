// swap_complexity_verify.cpp
// 验证: C++11 std::swap 确实由 3 次移动完成（O(1)），C++03 风格由 3 次拷贝完成（O(n)）
// Standard: C++17
// g++ -std=c++17 -O2 -Wall -Wextra -o swap_complexity_verify swap_complexity_verify.cpp

#include <iostream>
#include <utility>
#include <cstring>

/// @brief 带拷贝/移动计数的缓冲区，用于验证 swap 行为
class InstrumentedBuffer
{
    int* data_;
    std::size_t size_;
    static int copy_count_;
    static int move_count_;

public:
    explicit InstrumentedBuffer(std::size_t n)
        : data_(new int[n]()), size_(n) {}

    ~InstrumentedBuffer() { delete[] data_; }

    InstrumentedBuffer(const InstrumentedBuffer& other)
        : data_(new int[other.size_]), size_(other.size_)
    {
        ++copy_count_;
        std::memcpy(data_, other.data_, size_ * sizeof(int));
    }

    InstrumentedBuffer(InstrumentedBuffer&& other) noexcept
        : data_(other.data_), size_(other.size_)
    {
        ++move_count_;
        other.data_ = nullptr;
        other.size_ = 0;
    }

    InstrumentedBuffer& operator=(const InstrumentedBuffer& other)
    {
        if (this != &other) {
            delete[] data_;
            data_ = new int[other.size_];
            size_ = other.size_;
            std::memcpy(data_, other.data_, size_ * sizeof(int));
            ++copy_count_;
        }
        return *this;
    }

    InstrumentedBuffer& operator=(InstrumentedBuffer&& other) noexcept
    {
        if (this != &other) {
            delete[] data_;
            data_ = other.data_;
            size_ = other.size_;
            other.data_ = nullptr;
            other.size_ = 0;
            ++move_count_;
        }
        return *this;
    }

    static void reset_counters() { copy_count_ = 0; move_count_ = 0; }
    static int copy_count() { return copy_count_; }
    static int move_count() { return move_count_; }
};

int InstrumentedBuffer::copy_count_ = 0;
int InstrumentedBuffer::move_count_ = 0;

int main()
{
    // C++11 std::swap（基于移动）
    {
        InstrumentedBuffer a(1000000);
        InstrumentedBuffer b(1000000);
        InstrumentedBuffer::reset_counters();

        using std::swap;
        swap(a, b);

        std::cout << "=== C++11 std::swap（基于移动）===\n";
        std::cout << "  拷贝次数: " << InstrumentedBuffer::copy_count() << "\n";
        std::cout << "  移动次数: " << InstrumentedBuffer::move_count() << "\n";
    }

    // 模拟 C++03 swap（基于拷贝）
    {
        InstrumentedBuffer a(1000000);
        InstrumentedBuffer b(1000000);
        InstrumentedBuffer::reset_counters();

        InstrumentedBuffer temp(a);   // 拷贝构造
        a = b;                         // 拷贝赋值
        b = temp;                      // 拷贝赋值

        std::cout << "\n=== C++03 风格 swap（基于拷贝）===\n";
        std::cout << "  拷贝次数: " << InstrumentedBuffer::copy_count() << "\n";
        std::cout << "  移动次数: " << InstrumentedBuffer::move_count() << "\n";
    }

    std::cout << "\n结论: 对于管理动态内存的类型，C++11 swap 由 3 次移动完成（O(1)），\n";
    std::cout << "C++03 swap 由 3 次拷贝完成（O(n)，n 为资源大小）。\n";

    return 0;
}
