/**
 * @file ex10_constexpr_gcd.cpp
 * @brief 练习：constexpr 最大公约数
 *
 * 将 gcd 实现为 constexpr 函数，并使用 static_assert 在编译期验证结果。
 */

#include <iostream>

// constexpr 版本的 gcd，可在编译期求值
constexpr int gcd(int a, int b) {
    // 确保非负
    if (a < 0) a = -a;
    if (b < 0) b = -b;

    // 辗转相除法
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}

// 编译期验证
static_assert(gcd(48, 18) == 6, "gcd(48, 18) should be 6");
static_assert(gcd(100, 75) == 25, "gcd(100, 75) should be 25");
static_assert(gcd(7, 3) == 1, "gcd(7, 3) should be 1");
static_assert(gcd(0, 5) == 5, "gcd(0, 5) should be 5");
static_assert(gcd(17, 17) == 17, "gcd(17, 17) should be 17");

// 编译期计算可用于数组大小等常量表达式
constexpr int kArraySize = gcd(120, 45);  // == 15

int main() {
    // static_assert 在编译期已经通过
    std::cout << "所有 static_assert 检查已通过！\n\n";

    // 编译期结果
    constexpr int compile_time_result = gcd(48, 18);
    std::cout << "编译期 gcd(48, 18) = " << compile_time_result << '\n';

    // constexpr 变量作为数组大小
    int buffer[kArraySize] = {};  // 大小为 15 的数组
    std::cout << "buffer 大小（由 gcd(120,45) 决定）: "
              << sizeof(buffer) / sizeof(int) << '\n';

    // 运行期也可以使用
    int a = 0, b = 0;
    std::cout << "请输入两个整数: ";
    std::cin >> a >> b;
    std::cout << "gcd(" << a << ", " << b << ") = " << gcd(a, b) << '\n';

    return 0;
}
