/**
 * @file ex07_constexpr_circle.cpp
 * @brief 练习：把 #define 改造成 constexpr
 *
 * 用 constexpr 常量和函数替代 #define 宏，演示类型安全的常量定义。
 * 实现 constexpr circle_area(double radius) 并在编译期求值。
 */

#include <cmath>
#include <iostream>

// ===== 旧方式（不推荐）=====
// #define PI 3.14159265358979323846
// #define MAX_RADIUS 100.0
// #define MIN_RADIUS 0.0
// 缺点：无类型检查、调试困难、宏展开容易出问题

// ===== 新方式（推荐）=====
constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxRadius = 1000.0;
constexpr double kMinRadius = 0.0;

// constexpr 函数：可在编译期求值
constexpr double circle_area(double radius) {
    return kPi * radius * radius;
}

// constexpr 函数：计算圆的周长
constexpr double circle_circumference(double radius) {
    return 2.0 * kPi * radius;
}

int main() {
    // 编译期求值
    constexpr double area_at_r5 = circle_area(5.0);
    std::cout << "编译期计算：半径 5 的面积 = " << area_at_r5 << '\n';

    // 运行期求值（constexpr 函数也可以在运行时使用）
    double radius = 0.0;
    std::cout << "请输入圆的半径: ";
    std::cin >> radius;

    // 边界检查
    if (radius < kMinRadius) {
        std::cout << "错误：半径不能为负数\n";
        return 1;
    }
    if (radius > kMaxRadius) {
        std::cout << "错误：半径超过最大值 " << kMaxRadius << '\n';
        return 1;
    }

    double area = circle_area(radius);
    double circumference = circle_circumference(radius);

    std::cout << "半径 = " << radius << '\n';
    std::cout << "面积 = " << area << '\n';
    std::cout << "周长 = " << circumference << '\n';

    // constexpr 的优势：可以用于 static_assert
    static_assert(circle_area(1.0) > 3.14 && circle_area(1.0) < 3.15,
                  "circle_area(1.0) should be approximately pi");

    return 0;
}
