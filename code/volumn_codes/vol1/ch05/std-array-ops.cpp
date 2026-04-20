#include <algorithm>
#include <array>
#include <iostream>

int main()
{
    std::array<int, 5> a = {1, 2, 3, 4, 5};
    std::array<int, 5> b = {10, 20, 30, 40, 50};

    // fill —— 全部设为同一值
    a.fill(0);
    std::cout << "fill 后: ";
    for (int x : a) { std::cout << x << " "; }
    std::cout << "\n";

    // swap —— 交换两个 array 的内容
    a = {1, 2, 3, 4, 5};
    a.swap(b);
    std::cout << "swap 后 a: ";
    for (int x : a) { std::cout << x << " "; }
    std::cout << "\n";

    // 配合 <algorithm>
    std::array<int, 5> c = {5, 3, 1, 4, 2};
    std::sort(c.begin(), c.end());
    std::cout << "排序后: ";
    for (int x : c) { std::cout << x << " "; }
    std::cout << "\n";

    return 0;
}
