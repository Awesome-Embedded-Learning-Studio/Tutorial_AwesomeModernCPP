// Standard: C++20
// NaN 不等于自己：IEEE 754 规定 NaN 与任何值（含自身）比较都返回 false。
// 所以判 NaN 永远只能用 std::isnan，绝不能用 ==——这是浮点世界最经典的坑。
#include <cmath>
#include <iostream>
#include <limits>

int main() {
    double nan = std::numeric_limits<double>::quiet_NaN();
    std::cout << std::boolalpha;
    std::cout << "NaN == NaN?   " << (nan == nan) << '\n';
    std::cout << "NaN != NaN?   " << (nan != nan) << '\n';
    std::cout << "isnan(NaN)?   " << std::isnan(nan) << "   (判 NaN 的正确写法)\n";
    return 0;
}
