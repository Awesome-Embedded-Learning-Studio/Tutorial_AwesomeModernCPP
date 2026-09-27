// 右值引用只绑右值：prvalue（42、x + 1）和 xvalue（std::move(x)）都行，左值不行
// 把 r4 那行解注释再点「运行」，看 GCC 当场报绑定错误
#include <iostream>
#include <utility>

int main() {
    int x = 10;

    int&& r1 = 42;           // OK：42 是 prvalue
    int&& r2 = x + 1;        // OK：x + 1 是 prvalue
    int&& r3 = std::move(x); // OK：std::move(x) 是 xvalue

    // int&& r4 = x;         // 编译错误：x 是 lvalue，不能绑定到右值引用

    std::cout << r1 << " " << r2 << " " << r3 << "\n";
    return 0;
}
