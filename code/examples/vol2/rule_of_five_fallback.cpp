// rule_of_five_fallback.cpp -- 只有析构函数时，std::move 退化为拷贝构造
// Standard: C++17

#include <iostream>
#include <type_traits>
#include <utility>

// 只定义了析构函数，没有声明任何拷贝/移动操作
class OnlyDestructor {
    char* data_;

  public:
    explicit OnlyDestructor(std::size_t n) : data_(new char[n]) {}

    ~OnlyDestructor() { delete[] data_; }
    // 注意：这里既没有声明移动构造，也没有声明拷贝构造
};

// 编译期验证：它「能移动构造」，但不是因为真有移动构造函数
static_assert(!std::is_trivially_move_constructible_v<OnlyDestructor>,
              "没有真正的（平凡的）移动构造函数");
static_assert(std::is_move_constructible_v<OnlyDestructor>,
              "但 is_move_constructible 为 true —— 编译器退回到拷贝构造来满足");

int main() {
    std::cout
        << "is_trivially_move_constructible_v: "
        << std::is_trivially_move_constructible_v<OnlyDestructor> << "  (没有真正的移动构造)\n";
    std::cout << "is_move_constructible_v:           "
              << std::is_move_constructible_v<OnlyDestructor> << "  (但能用拷贝构造蒙混过关)\n";

    // 真要执行 OnlyDestructor b = std::move(a)，隐式拷贝构造做浅拷贝，
    // a 和 b 的 data_ 指向同一块内存，两者析构时 double free。
    // 这里不真的跑（会崩），编译期 static_assert 已经给出结论。
    return 0;
}
