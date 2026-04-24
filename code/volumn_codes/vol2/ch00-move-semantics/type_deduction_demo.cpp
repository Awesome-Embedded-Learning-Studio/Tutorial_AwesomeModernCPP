// type_deduction_demo.cpp -- 万能引用类型推导演示
// Standard: C++17

#include <iostream>
#include <string>
#include <type_traits>

template<typename T>
void show_type(T&& arg)
{
    using Decayed = std::decay_t<T>;

    if constexpr (std::is_lvalue_reference_v<T>) {
        std::cout << "  左值引用\n";
    } else {
        std::cout << "  右值引用（或非引用）\n";
    }
}

int main()
{
    std::string name = "Alice";
    show_type(name);                // T = std::string&, 输出"左值引用"
    show_type(std::string("Bob"));  // T = std::string, 输出"右值引用"
    show_type(std::move(name));     // T = std::string, 输出"右值引用"
    return 0;
}
