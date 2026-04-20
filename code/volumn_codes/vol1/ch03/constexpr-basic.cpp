#include <iostream>

constexpr int square(int x)
{
    return x * x;
}

int main()
{
    constexpr int kResult = square(5);  // 编译期求值，kResult = 25

    int x = 0;
    std::cin >> x;
    int runtime_result = square(x);    // 运行时求值，退化为普通调用

    return 0;
}
