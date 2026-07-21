// 手写 type traits:主模板 + 偏特化,看穿 <type_traits> 的实现原理
// 对应文章:documents/vol4-advanced/vol1-basics-cpp11-14/04-specialization-partial.md
// 编译:g++ -Wall -Wextra -std=c++20 type_traits_from_scratch.cpp -o type_traits && ./type_traits
#include <iostream>

// is_pointer:主模板 false,指针偏特化 true
template <typename T> struct is_pointer {
    static constexpr bool value = false;
};
template <typename T> struct is_pointer<T*> {
    static constexpr bool value = true;
};

// is_const:主模板 false,const T 偏特化 true(注意 const T* 不算,得 T 本身是 const)
template <typename T> struct is_const {
    static constexpr bool value = false;
};
template <typename T> struct is_const<const T> {
    static constexpr bool value = true;
};

// is_reference:主模板 false,T& / T&& 偏特化 true
template <typename T> struct is_reference {
    static constexpr bool value = false;
};
template <typename T> struct is_reference<T&> {
    static constexpr bool value = true;
};
template <typename T> struct is_reference<T&&> {
    static constexpr bool value = true;
};

// C++14 变量模板简写(和标准库 _v 后缀同一思路)
template <typename T> constexpr bool is_pointer_v = is_pointer<T>::value;

int main() {
    std::cout << std::boolalpha;
    std::cout << "is_pointer<int>      = " << is_pointer<int>::value << "\n";
    std::cout << "is_pointer<int*>     = " << is_pointer<int*>::value << "\n";
    std::cout << "is_pointer<int**>    = " << is_pointer<int**>::value << "\n";
    std::cout << "is_const<const int>  = " << is_const<const int>::value << "\n";
    std::cout << "is_const<int>        = " << is_const<int>::value << "\n";
    std::cout << "is_const<const int*> = " << is_const<const int*>::value
              << " (指针自身非 const,指向 const)\n";
    std::cout << "is_const<int* const> = " << is_const<int* const>::value << " (指针自身 const)\n";
    std::cout << "is_reference<int&>   = " << is_reference<int&>::value << "\n";
    std::cout << "is_reference<int&&>  = " << is_reference<int&&>::value << "\n";
    std::cout << "is_pointer_v<double> = " << is_pointer_v<double> << " (_v 简写)\n";
    return 0;
}
