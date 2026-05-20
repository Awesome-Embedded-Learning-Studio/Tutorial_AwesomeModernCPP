// 02_move_semantics.cpp
// 移动构造与移动赋值：Buffer 资源所有权转移与 vector 操作对比

#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

class Buffer {
    char* data_;
    std::size_t size_;
    std::size_t capacity_;

  public:
    explicit Buffer(std::size_t capacity)
        : data_(new char[capacity]), size_(0), capacity_(capacity) {
        std::cout << "  [Buffer] 分配 " << capacity << " 字节\n";
    }
    ~Buffer() {
        if (data_) {
            std::cout << "  [Buffer] 释放 " << capacity_ << " 字节\n";
            delete[] data_;
        }
    }
    Buffer(const Buffer& other)
        : data_(new char[other.capacity_]), size_(other.size_), capacity_(other.capacity_) {
        std::memcpy(data_, other.data_, size_);
        std::cout << "  [Buffer] 拷贝构造 " << capacity_ << " 字节\n";
    }
    Buffer(Buffer&& other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
        std::cout << "  [Buffer] 移动构造（指针转移）\n";
    }
    Buffer& operator=(Buffer&& other) noexcept {
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
    void append(const char* str, std::size_t len) {
        if (size_ + len <= capacity_) {
            std::memcpy(data_ + size_, str, len);
            size_ += len;
        }
    }
    std::size_t size() const { return size_; }
};

int main() {
    std::cout << "=== 1. 创建缓冲区 ===\n";
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
    std::cout << "  d.size() = " << d.size() << "\n\n";

    std::cout << "=== 4. vector 中的移动 ===\n";
    std::vector<Buffer> buffers;
    buffers.reserve(4);
    std::cout << "  push_back 左值:\n";
    buffers.push_back(c);
    std::cout << "  push_back std::move:\n";
    buffers.push_back(std::move(c));
    std::cout << "  emplace_back 原位构造:\n";
    buffers.emplace_back(512);

    return 0;
}
