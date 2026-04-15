// subspan_example.cpp - span 切片操作演示
#include <span>
#include <array>
#include <iostream>

int main() {
    std::array<int, 10> data = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    std::span<int> s = data;

    std::cout << "Original data: ";
    for (auto v : s) std::cout << v << ' ';
    std::cout << '\n';

    // first(n) - 获取前 n 个元素的视图
    auto first_three = s.first(3);
    std::cout << "First 3: ";
    for (auto v : first_three) std::cout << v << ' ';
    std::cout << '\n';

    // first<N>() - 编译期获取前 N 个元素
    auto first_five = s.first<5>();
    std::cout << "First 5 (compile-time): ";
    for (auto v : first_five) std::cout << v << ' ';
    std::cout << '\n';

    // last(n) - 获取后 n 个元素的视图
    auto last_three = s.last(3);
    std::cout << "Last 3: ";
    for (auto v : last_three) std::cout << v << ' ';
    std::cout << '\n';

    // subspan(offset, count) - 获取从 offset 开始的 count 个元素
    auto middle = s.subspan(3, 4);
    std::cout << "Subspan [3:7]: ";
    for (auto v : middle) std::cout << v << ' ';
    std::cout << '\n';

    // subspan<Offset, Count>() - 编译期切片
    auto middle_static = s.subspan<2, 5>();
    std::cout << "Subspan <2,5> (compile-time): ";
    for (auto v : middle_static) std::cout << v << ' ';
    std::cout << '\n';

    // 修改视图会修改原始数据
    first_three[0] = 99;
    std::cout << "\nAfter modifying first_three[0] to 99:\n";
    std::cout << "Original data: ";
    for (auto v : data) std::cout << v << ' ';
    std::cout << '\n';

    return 0;
}
