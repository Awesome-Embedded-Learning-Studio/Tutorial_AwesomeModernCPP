#include <iostream>
#include <cmath>

int main()
{
    double a = 0.1 + 0.2;
    double b = 0.3;

    // 直接比较：false！因为 0.1+0.2 实际存储为 0.30000000000000004
    std::cout << std::boolalpha << (a == b) << std::endl;  // false

    // 正确做法：判断差值是否足够小
    double epsilon = 1e-9;
    bool approx_equal = std::abs(a - b) < epsilon;
    std::cout << approx_equal << std::endl;  // true
    return 0;
}
