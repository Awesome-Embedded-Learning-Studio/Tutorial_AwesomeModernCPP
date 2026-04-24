// noexcept_vector_realloc.cpp
// 验证：std::vector 扩容时根据移动构造函数的 noexcept 声明选择移动或拷贝
// Standard: C++17
// 预期输出：
//   [Noexcept版] 触发扩容时使用移动构造
//   [Throwing版] 触发扩容时退化为拷贝构造

#include <iostream>
#include <cstring>
#include <vector>

class BufferNoexcept {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    explicit BufferNoexcept(std::size_t capacity)
        : data_(new char[capacity])
        , size_(0)
        , capacity_(capacity)
    {}

    ~BufferNoexcept() { delete[] data_; }

    BufferNoexcept(const BufferNoexcept& other)
        : data_(new char[other.capacity_])
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        std::memcpy(data_, other.data_, size_);
        std::cout << "  [Noexcept版] 拷贝构造\n";
    }

    BufferNoexcept(BufferNoexcept&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        std::cout << "  [Noexcept版] 移动构造\n";
    }
};

class BufferThrowing {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    explicit BufferThrowing(std::size_t capacity)
        : data_(new char[capacity])
        , size_(0)
        , capacity_(capacity)
    {}

    ~BufferThrowing() { delete[] data_; }

    BufferThrowing(const BufferThrowing& other)
        : data_(new char[other.capacity_])
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        std::memcpy(data_, other.data_, size_);
        std::cout << "  [Throwing版] 拷贝构造\n";
    }

    // 没有 noexcept 的移动构造函数
    BufferThrowing(BufferThrowing&& other)
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        std::cout << "  [Throwing版] 移动构造\n";
    }
};

int main()
{
    std::cout << "=== noexcept 移动 + vector 扩容 ===\n";
    {
        std::vector<BufferNoexcept> vec;
        vec.reserve(1);
        vec.emplace_back(64);
        std::cout << "--- 触发扩容 ---\n";
        vec.emplace_back(64);
    }

    std::cout << "\n=== 非 noexcept 移动 + vector 扩容 ===\n";
    {
        std::vector<BufferThrowing> vec;
        vec.reserve(1);
        vec.emplace_back(64);
        std::cout << "--- 触发扩容 ---\n";
        vec.emplace_back(64);
    }

    return 0;
}
