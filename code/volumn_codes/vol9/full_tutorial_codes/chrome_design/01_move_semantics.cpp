// 移动语义、std::move 与完美转发
// 来源：OnceCallback 前置知识速查 (pre-00)
// 编译：g++ -std=c++17 -Wall -Wextra 01_move_semantics.cpp -o 01_move_semantics

#include <cstddef>
#include <utility>
#include <iostream>
#include <string>

// --- 移动语义示例：Buffer 类 ---

class Buffer {
    int* data_;
    std::size_t size_;
public:
    explicit Buffer(std::size_t n) : data_(new int[n]), size_(n) {
        std::cout << "  Buffer(" << n << "): allocated\n";
    }

    // 移动构造：偷走 other 的资源
    Buffer(Buffer&& other) noexcept
        : data_(other.data_), size_(other.size_) {
        other.data_ = nullptr;
        other.size_ = 0;
        std::cout << "  Buffer(move): resource stolen\n";
    }

    ~Buffer() {
        delete[] data_;
        std::cout << "  Buffer::~Buffer(): " << (data_ ? "freed" : "null") << "\n";
    }

    std::size_t size() const { return size_; }
};

// --- 完美转发示例 ---

void target(int& x) {
    std::cout << "  target(lvalue ref): " << x << "\n";
}

void target(int&& x) {
    std::cout << "  target(rvalue ref): " << x << "\n";
}

template<typename T>
void wrapper(T&& arg) {
    // std::forward 保持 arg 的原始值类别
    target(std::forward<T>(arg));
}

// --- 可变参数模板示例 ---

template<typename... Types>
void print_all(Types... args) {
    std::cout << "  print_all: " << sizeof...(Types) << " arguments\n";
    // C++17 折叠表达式打印
    ((std::cout << "    " << args << "\n"), ...);
}

int main() {
    std::cout << "=== 移动语义 ===\n";
    {
        Buffer a(100);
        Buffer b = std::move(a);
        std::cout << "  b.size() = " << b.size() << "\n";
    }

    std::cout << "\n=== 完美转发 ===\n";
    {
        int x = 10;
        wrapper(x);     // arg 是左值引用，forward 返回左值引用
        wrapper(20);    // arg 是右值引用，forward 返回右值引用
    }

    std::cout << "\n=== 可变参数模板 ===\n";
    {
        print_all(1, 2.5, std::string("hello"));
        print_all();  // 空参数包
    }

    return 0;
}
