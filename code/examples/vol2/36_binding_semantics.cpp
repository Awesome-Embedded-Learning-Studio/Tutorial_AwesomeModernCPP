// binding_semantics.cpp -- auto（拷贝）与 auto&（引用）两种绑定语义的差异
// Standard: C++17

#include <iostream>
#include <utility>

int main() {
    std::pair<int, int> range{1, 10};

    // 拷贝：r1、r2 引用的是匿名拷贝，不影响 range
    auto [r1, r2] = range;

    // 引用：直接操作原对象
    auto& [r3, r4] = range;
    r3 = 5; // range.first 变成 5

    std::cout << "range.first  after auto& mutation: " << range.first << "\n";
    std::cout << "r1 (copy,    unaffected)         : " << r1 << "\n";

    return 0;
}
