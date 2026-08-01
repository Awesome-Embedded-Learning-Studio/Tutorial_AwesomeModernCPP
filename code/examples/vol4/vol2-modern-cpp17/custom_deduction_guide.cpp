// 配套 04-ctad.md「自己写一条推导指南」
// 演示自定义类模板配手写推导指南:固定一个模板参数、保留元素类型
// 编译运行:g++ -std=c++17 -Wall -Wextra custom_deduction_guide.cpp -o custom_deduction_guide &&
// ./custom_deduction_guide
#include <iostream>
#include <type_traits>

// 包一个值和它的「放大系数」:Scale 是非类型模板参数
template <typename T, int Scale> struct Scaled {
    T value;
    constexpr Scaled(T v) : value(v * Scale) {}
};

// 手写推导指南:构造参数只有一个 T,固定 Scale=1
// 形式:TemplateName(params) -> Name<deduced>
template <typename T> Scaled(T) -> Scaled<T, 1>;

// 标准库风格的指南:转发引用,保留元素类型
template <typename T> struct Wrapper {
    T data;
    Wrapper(T d) : data(d) {}
};

template <typename T> Wrapper(T) -> Wrapper<T>;

int main() {
    // 推导指南出手:Scale 被固定为 1
    constexpr Scaled s(42); // Scaled<int, 1>
    static_assert(std::is_same_v<decltype(s), const Scaled<int, 1>>);
    static_assert(s.value == 42);

    Scaled d(2.5); // Scaled<double, 1>
    static_assert(std::is_same_v<decltype(d), Scaled<double, 1>>);

    // 显式指定 Scale,绕开推导
    constexpr Scaled<int, 10> big(5); // value = 50
    static_assert(big.value == 50);

    // Wrapper 把 const char* 推成元素类型
    Wrapper w("hello"); // Wrapper<const char*>
    static_assert(std::is_same_v<decltype(w), Wrapper<const char*>>);

    std::cout << "s.value=" << s.value << "\n";
    std::cout << "d.value=" << d.value << "\n";
    std::cout << "big.value=" << big.value << "\n";
    std::cout << "w.data=" << w.data << "\n";
    return 0;
}
