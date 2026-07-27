// 配套 04-tmp-core-techniques.md「type_traits 的内幕:特化就是编译期的 if-else」
// 手写一个 is_pointer,看清 type_traits 「主模板 + 偏特化」的实现机制
// 编译运行:g++ -std=c++20 -Wall -Wextra traits_from_scratch.cpp -o tfs && ./tfs
#include <iostream>
#include <type_traits>

// 手写一个 is_pointer:主模板兜底 false
template <typename T> struct is_pointer_impl {
    static constexpr bool value = false;
};

// 偏特化:匹配「指向 T 的指针」,true
template <typename T> struct is_pointer_impl<T*> {
    static constexpr bool value = true;
};

template <typename T> constexpr bool is_pointer_v = is_pointer_impl<T>::value;

int main() {
    std::cout << std::boolalpha;
    std::cout << "is_pointer_v<int>:    " << is_pointer_v<int> << "\n";
    std::cout << "is_pointer_v<int*>:   " << is_pointer_v<int*> << "\n";
    std::cout << "is_pointer_v<int**>:  " << is_pointer_v<int**> << "\n";
    std::cout << "is_pointer_v<double*>:" << is_pointer_v<double*> << "\n";
    // 和标准库对照,结果应一致
    static_assert(is_pointer_v<int*> == std::is_pointer_v<int*>);
    static_assert(is_pointer_v<int> == std::is_pointer_v<int>);
    std::cout << "与 std::is_pointer 结果一致\n";
}
