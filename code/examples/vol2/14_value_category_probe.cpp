// value_category_probe.cpp -- 用 decltype 判断任意表达式的值类别
// Standard: C++17

#include <iostream>
#include <type_traits>
#include <utility>

template <class T> constexpr const char* value_category() {
    if constexpr (std::is_lvalue_reference_v<T>) {
        return "lvalue";
    } else if constexpr (std::is_rvalue_reference_v<T>) {
        return "xvalue";
    } else {
        return "prvalue";
    }
}

// decltype((expr)) 按表达式的值类别求类型：lvalue 得 T&，xvalue 得 T&&，prvalue 得 T
#define SHOW(expr) std::cout << "  " #expr "  ->  " << value_category<decltype((expr))>() << "\n"

int g = 100; // 全局变量

int main() {
    int x = 10;      // 普通变量
    int& lref = x;   // 左值引用
    int&& rref = 20; // 右值引用（但 rref 这个名字本身是左值！）

    std::cout << "--- 变量与引用 ---\n";
    SHOW(x);
    SHOW(lref);
    SHOW(rref); // 反直觉点：命名的右值引用是左值

    std::cout << "\n--- 字面量与运算 ---\n";
    SHOW(42);
    SHOW(x + 1);
    SHOW(std::move(x)); // std::move 的产物是 xvalue

    std::cout << "\n--- 解引用与成员 ---\n";
    SHOW(*(&x)); // 解引用得到左值
    SHOW(g);

    return 0;
}
