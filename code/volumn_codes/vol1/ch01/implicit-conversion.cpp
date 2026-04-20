#include <iostream>

int main()
{
    // 赋值转换：double -> int，小数部分直接截断
    double pi = 3.14159;
    int truncated = pi;
    std::cout << "3.14159 -> int: " << truncated << std::endl;  // 3

    // 算术转换：int + double -> double
    int i = 5;
    double d = 2.5;
    auto result = i + d;
    std::cout << "5 + 2.5 = " << result << " (double)" << std::endl;  // 7.5

    // 布尔转换：零 -> false，非零 -> true
    bool b1 = 42;   // true，输出为 1
    bool b2 = -3;   // true
    bool b3 = 0;    // false，输出为 0
    std::cout << "42->" << b1 << ", -3->" << b2 << ", 0->" << b3
              << std::endl;  // 1, 1, 0
    return 0;
}
