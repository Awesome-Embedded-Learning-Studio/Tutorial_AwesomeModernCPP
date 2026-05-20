// 04_perfect_forwarding.cpp
// 完美转发基础：万能引用与引用折叠的类型推导验证

#include <iostream>
#include <string>
#include <type_traits>

template <typename T> void show_deduction(T&& /* arg */) {
    if constexpr (std::is_lvalue_reference_v<T>) {
        std::cout << "  T = 左值引用类型\n";
    } else {
        std::cout << "  T = 非引用类型（右值）\n";
    }
    using ParamType = T&&;
    if constexpr (std::is_lvalue_reference_v<ParamType>) {
        std::cout << "  T&& = 左值引用\n\n";
    } else {
        std::cout << "  T&& = 右值引用\n\n";
    }
}

int main() {
    std::string name = "Alice";
    const std::string cname = "Bob";

    std::cout << "传入非 const 左值:\n";
    show_deduction(name);

    std::cout << "传入 const 左值:\n";
    show_deduction(cname);

    std::cout << "传入右值（临时对象）:\n";
    show_deduction(std::string("Charlie"));

    std::cout << "传入右值（std::move）:\n";
    show_deduction(std::move(name));

    return 0;
}
