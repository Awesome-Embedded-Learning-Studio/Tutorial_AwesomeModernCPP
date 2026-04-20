/**
 * @file ex05_temp_converter.cpp
 * @brief 练习：安全的温度转换器
 *
 * 从用户输入读取摄氏温度（支持小数），使用 static_cast<double>
 * 转换为华氏温度，输出保留 1 位小数。
 * 公式：F = C * 9.0 / 5.0 + 32.0
 */

#include <iomanip>
#include <iostream>

double celsius_to_fahrenheit(double celsius) {
    return celsius * 9.0 / 5.0 + 32.0;
}

int main() {
    std::cout << "请输入摄氏温度: ";

    double celsius = 0.0;
    std::cin >> celsius;

    // 使用 static_cast 确保类型安全（此处输入已是 double，仅作演示）
    double fahrenheit = celsius_to_fahrenheit(static_cast<double>(celsius));

    // 输出保留 1 位小数
    std::cout << std::fixed << std::setprecision(1);
    std::cout << celsius << " °C = " << fahrenheit << " °F\n";

    // 一些常见参考值
    std::cout << "\n===== 常见参考值 =====\n";
    double references[] = {0.0, 36.5, 100.0, -40.0};
    const char* labels[] = {"冰点", "体温", "沸点", "相等点"};
    for (int i = 0; i < 4; ++i) {
        double f = celsius_to_fahrenheit(references[i]);
        std::cout << std::fixed << std::setprecision(1);
        std::cout << labels[i] << ": " << references[i]
                  << " °C = " << f << " °F\n";
    }

    return 0;
}
