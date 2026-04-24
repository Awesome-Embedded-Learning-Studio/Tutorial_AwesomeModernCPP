// move_semantics_demo.cpp -- 移动构造与移动赋值演示
// Standard: C++17

#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

class Buffer
{
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

public:
    explicit Buffer(std::size_t capacity)
        : data_(new char[capacity])
        , size_(0)
        , capacity_(capacity)
    {
        std::cout << "  [Buffer] 分配 " << capacity << " 字节\n";
    }

    ~Buffer()
    {
        if (data_) {
            std::cout << "  [Buffer] 释放 " << capacity_ << " 字节\n";
            delete[] data_;
        }
    }

    Buffer(const Buffer& other)
        : data_(new char[other.capacity_])
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        std::memcpy(data_, other.data_, size_);
        std::cout << "  [Buffer] 拷贝构造 " << capacity_ << " 字节\n";
    }

    Buffer(Buffer&& other) noexcept
        : data_(other.data_)
        , size_(other.size_)
        , capacity_(other.capacity_)
    {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        std::cout << "  [Buffer] 移动构造（指针转移）\n";
    }

    Buffer& operator=(const Buffer& other)
    {
        if (this != &other) {
            delete[] data_;
            data_ = new char[other.capacity_];
            size_ = other.size_;
            capacity_ = other.capacity_;
            std::memcpy(data_, other.data_, size_);
            std::cout << "  [Buffer] 拷贝赋值 " << capacity_ << " 字节\n";
        }
        return *this;
    }

    Buffer& operator=(Buffer&& other) noexcept
    {
        if (this != &other) {
            delete[] data_;
            data_ = other.data_;
            size_ = other.size_;
            capacity_ = other.capacity_;
            other.data_ = nullptr;
            other.size_ = 0;
            other.capacity_ = 0;
            std::cout << "  [Buffer] 移动赋值（指针转移）\n";
        }
        return *this;
    }

    void append(const char* str, std::size_t len)
    {
        if (size_ + len <= capacity_) {
            std::memcpy(data_ + size_, str, len);
            size_ += len;
        }
    }

    std::size_t size() const { return size_; }
    std::size_t capacity() const { return capacity_; }
};

int main()
{
    std::cout << "=== 1. 创建两个缓冲区 ===\n";
    Buffer a(1024);
    a.append("Hello", 5);
    Buffer b(2048);
    b.append("World", 5);
    std::cout << '\n';

    std::cout << "=== 2. 拷贝构造 ===\n";
    Buffer c = a;
    std::cout << "  c.size() = " << c.size() << "\n\n";

    std::cout << "=== 3. 移动构造 ===\n";
    Buffer d = std::move(b);
    std::cout << "  d.size() = " << d.size() << "\n";
    std::cout << "  b.capacity() = " << b.capacity() << "\n\n";

    std::cout << "=== 4. 移动赋值 ===\n";
    a = std::move(d);
    std::cout << "  a.size() = " << a.size() << "\n";
    std::cout << "  d.capacity() = " << d.capacity() << "\n\n";

    std::cout << "=== 5. vector 中的移动 ===\n";
    std::vector<Buffer> buffers;
    buffers.reserve(4);
    std::cout << "  push_back 左值:\n";
    buffers.push_back(c);             // 拷贝
    std::cout << "  push_back std::move:\n";
    buffers.push_back(std::move(c));  // 移动
    std::cout << "  emplace_back 原位构造:\n";
    buffers.emplace_back(512);        // 直接在 vector 中构造
    std::cout << '\n';

    std::cout << "=== 6. 程序结束 ===\n";
    return 0;
}
