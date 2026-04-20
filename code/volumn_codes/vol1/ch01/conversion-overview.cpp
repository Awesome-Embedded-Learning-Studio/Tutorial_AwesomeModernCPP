// conversion.cpp —— 类型转换综合演示
// Platform: host
// Standard: C++11

#include <iostream>
#include <cmath>
#include <climits>

int main()
{
    // 1. 隐式转换：double -> int
    double price = 9.99;
    int rounded = price;
    std::cout << "[隐式转换] 9.99 -> int: " << rounded << std::endl;

    // 2. static_cast：显式转换
    int count = 7;
    double avg = static_cast<double>(count) / 2;
    std::cout << "[static_cast] 7 / 2 = " << avg << std::endl;

    // 3. 整数除法陷阱
    int wrong = count / 2;
    std::cout << "[整数除法] 7 / 2 = " << wrong << std::endl;

    // 4. 有符号与无符号
    int neg = -1;
    unsigned int pos = static_cast<unsigned int>(neg);
    std::cout << "[有符号转无符号] -1 -> " << pos << std::endl;

    // 5. 浮点精度
    double x = 0.1 + 0.2;
    double y = 0.3;
    std::cout << "[浮点比较] (0.1+0.2) == 0.3: "
              << (x == y ? "true" : "false") << std::endl;

    // 6. 安全的浮点比较
    double epsilon = 1e-9;
    bool safe_eq = std::abs(x - y) < epsilon;
    std::cout << "[安全比较] approx equal: "
              << (safe_eq ? "true" : "false") << std::endl;

    // 7. 溢出
    int big = INT_MAX;
    std::cout << "[溢出] INT_MAX = " << big
              << ", +1 = " << big + 1 << std::endl;

    return 0;
}
