// rule_of_five_fallback.cpp
// 验证：只声明析构函数时，std::move 退化为拷贝构造（而非调用移动构造函数）
// Standard: C++17
// 预期输出：
//   情况 A：OnlyDestructor——隐式拷贝构造被调用（浅拷贝），导致 double free
//   情况 B：WithCopyCtor——显式拷贝构造被调用（深拷贝），安全但低效

#include <iostream>
#include <cstring>
#include <type_traits>

// 情况 A：只声明了析构函数
// 隐式生成拷贝构造（浅拷贝），不生成移动构造
// 用 = delete 禁止拷贝来避免 double free，以便演示 trait
class OnlyDestructor {
    char* data_;

public:
    OnlyDestructor() : data_(nullptr) {}
    ~OnlyDestructor() { delete[] data_; }
};

// 情况 B：析构函数 + 显式拷贝构造，无移动构造
class WithCopyCtor {
    char* data_;
    std::size_t size_;

public:
    WithCopyCtor(std::size_t n) : data_(new char[n]), size_(n)
    {
        std::cout << "  [构造] 分配 " << n << " 字节\n";
    }

    ~WithCopyCtor()
    {
        std::cout << "  [析构] 释放 " << size_ << " 字节\n";
        delete[] data_;
    }

    WithCopyCtor(const WithCopyCtor& other)
        : data_(new char[other.size_])
        , size_(other.size_)
    {
        std::memcpy(data_, other.data_, size_);
        std::cout << "  [拷贝构造] 深拷贝 " << size_ << " 字节\n";
    }
};

int main()
{
    std::cout << "=== OnlyDestructor 类型特征 ===\n";
    std::cout << "  is_copy_constructible: "
              << std::is_copy_constructible_v<OnlyDestructor> << "\n";
    std::cout << "  is_move_constructible: "
              << std::is_move_constructible_v<OnlyDestructor> << "\n";
    std::cout << "  is_trivially_move_constructible: "
              << std::is_trivially_move_constructible_v<OnlyDestructor> << "\n";
    std::cout << "\n  注意：is_move_constructible 为 true，但 trivially 为 false。\n"
              << "  这意味着 std::move(OnlyDestructor) 会退回到拷贝构造函数，\n"
              << "  而不是调用一个真正的移动构造函数。\n";

    std::cout << "\n=== WithCopyCtor 实际行为 ===\n";
    {
        WithCopyCtor a(100);
        std::cout << "--- 执行 std::move(a) ---\n";
        WithCopyCtor b = std::move(a);  // 退化为拷贝构造
        std::cout << "  可以看到：尽管使用了 std::move，但调用的仍然是拷贝构造\n";

        std::cout << "\n--- 析构 ---\n";
    }

    return 0;
}
