/**
 * @file ex03_float_precision.cpp
 * @brief 练习：体验浮点精度陷阱
 *
 * 用 float 累加 0.1 十次，检查结果是否 == 1.0。
 * 然后用 double 做同样的实验。每步都用 setprecision(20) 打印。
 *
 * 要点：0.1 在二进制中是无限循环小数，float 和 double 都无法精确表示。
 */

#include <iomanip>
#include <iostream>

int main() {
    std::cout << std::setprecision(20);

    // ===== float 实验 =====
    std::cout << "===== float: 累加 0.1f 十次 =====\n";
    float f_sum = 0.0f;
    for (int i = 1; i <= 10; ++i) {
        f_sum += 0.1f;
        std::cout << "  第 " << i << " 次累加后: " << f_sum << '\n';
    }
    std::cout << "  最终结果:  " << f_sum << '\n';
    std::cout << "  是否 == 1.0f? " << (f_sum == 1.0f ? "是" : "否") << '\n';
    // 大多数情况下结果不等于 1.0f

    // ===== double 实验 =====
    std::cout << "\n===== double: 累加 0.1 十次 =====\n";
    double d_sum = 0.0;
    for (int i = 1; i <= 10; ++i) {
        d_sum += 0.1;
        std::cout << "  第 " << i << " 次累加后: " << d_sum << '\n';
    }
    std::cout << "  最终结果:  " << d_sum << '\n';
    std::cout << "  是否 == 1.0?  " << (d_sum == 1.0 ? "是" : "否") << '\n';
    // double 精度更高，但同样存在微小误差

    // ===== 结论 =====
    std::cout << "\n===== 结论 =====\n";
    std::cout << "0.1 在二进制中是无限循环小数，无法被精确表示。\n";
    std::cout << "float 的精度约为 7 位有效数字，double 约为 15 位。\n";
    std::cout << "永远不要用 == 比较浮点数，应使用 epsilon 比较。\n";

    return 0;
}
