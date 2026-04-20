/**
 * @file ex03_celsius_to_fahrenheit.cpp
 * @brief 练习：摄氏转华氏
 *
 * 从 std::cin 读取摄氏温度，使用公式 F = C * 9 / 5 + 32
 * 转换为华氏温度并输出。练习算术运算和输入输出。
 */

#include <iomanip>
#include <iostream>

// 摄氏转华氏
double celsius_to_fahrenheit(double celsius) {
    return celsius * 9.0 / 5.0 + 32.0;
}

int main() {
    std::cout << "===== ex03: 摄氏转华氏 =====\n\n";

    // 读取摄氏温度
    std::cout << "请输入摄氏温度: ";
    double celsius = 0.0;
    std::cin >> celsius;

    // 计算华氏温度
    double fahrenheit = celsius_to_fahrenheit(celsius);

    // 输出结果，保留 1 位小数
    std::cout << std::fixed << std::setprecision(1);
    std::cout << celsius << " °C = " << fahrenheit << " °F\n";

    // 一些参考值
    std::cout << "\n===== 参考值 =====\n";
    std::cout << "冰点:  0 °C = " << celsius_to_fahrenheit(0.0) << " °F\n";
    std::cout << "体温: 37 °C = " << celsius_to_fahrenheit(37.0) << " °F\n";
    std::cout << "沸点: 100 °C = " << celsius_to_fahrenheit(100.0) << " °F\n";
    std::cout << "相等: -40 °C = " << celsius_to_fahrenheit(-40.0) << " °F\n";

    return 0;
}
