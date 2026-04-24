// ref_collapsing.cpp -- 引用折叠验证
// Standard: C++17

#include <iostream>
#include <type_traits>
#include <string>

template<typename T>
void show_deduction(T&& /* arg */)
{
    // T 的推导结果
    if constexpr (std::is_lvalue_reference_v<T>) {
        std::cout << "  T = 左值引用类型\n";
    } else {
        std::cout << "  T = 非引用类型（右值）\n";
    }

    // T&& 的最终类型（经过引用折叠）
    using ParamType = T&&;
    if constexpr (std::is_lvalue_reference_v<ParamType>) {
        std::cout << "  T&& = 左值引用\n\n";
    } else {
        std::cout << "  T&& = 右值引用\n\n";
    }
}

int main()
{
    std::string name = "Alice";
    const std::string cname = "Bob";

    std::cout << "传入非 const 左值:\n";
    show_deduction(name);
    // T = std::string&, T&& = std::string& && → std::string&

    std::cout << "传入 const 左值:\n";
    show_deduction(cname);
    // T = const std::string&, T&& = const std::string& && → const std::string&

    std::cout << "传入右值（临时对象）:\n";
    show_deduction(std::string("Charlie"));
    // T = std::string, T&& = std::string&&

    std::cout << "传入右值（std::move）:\n";
    show_deduction(std::move(name));
    // T = std::string, T&& = std::string&&

    return 0;
}
