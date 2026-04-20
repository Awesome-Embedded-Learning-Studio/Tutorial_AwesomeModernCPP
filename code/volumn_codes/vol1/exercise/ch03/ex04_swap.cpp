/**
 * @file ex04_swap.cpp
 * @brief 练习：实现 swap
 *
 * 为 double 和 std::string 编写重载的 swap_values 函数。
 * 验证交换前后的值。
 */

#include <iostream>
#include <string>

// 交换两个 double
void swap_values(double& a, double& b) {
    double temp = a;
    a = b;
    b = temp;
}

// 交换两个 std::string（重载）
void swap_values(std::string& a, std::string& b) {
    std::string temp = std::move(a);
    a = std::move(b);
    b = std::move(temp);
}

int main() {
    // ===== double 交换 =====
    std::cout << "===== double 交换 =====\n";
    double x = 3.14;
    double y = 2.71;

    std::cout << "交换前: x = " << x << ", y = " << y << '\n';
    swap_values(x, y);
    std::cout << "交换后: x = " << x << ", y = " << y << '\n';

    // 验证
    bool double_ok = (x == 2.71 && y == 3.14);
    std::cout << "验证: " << (double_ok ? "PASS" : "FAIL") << "\n\n";

    // ===== std::string 交换 =====
    std::cout << "===== std::string 交换 =====\n";
    std::string s1 = "Hello";
    std::string s2 = "World";

    std::cout << "交换前: s1 = \"" << s1 << "\", s2 = \"" << s2 << "\"\n";
    swap_values(s1, s2);
    std::cout << "交换后: s1 = \"" << s1 << "\", s2 = \"" << s2 << "\"\n";

    // 验证
    bool string_ok = (s1 == "World" && s2 == "Hello");
    std::cout << "验证: " << (string_ok ? "PASS" : "FAIL") << "\n\n";

    // ===== 多次交换验证 =====
    std::cout << "===== 多次交换恢复 =====\n";
    double a = 100.0;
    double b = 200.0;
    std::cout << "初始: a = " << a << ", b = " << b << '\n';

    swap_values(a, b);
    std::cout << "第一次交换: a = " << a << ", b = " << b << '\n';

    swap_values(a, b);
    std::cout << "第二次交换: a = " << a << ", b = " << b << '\n';

    bool restore_ok = (a == 100.0 && b == 200.0);
    std::cout << "恢复验证: " << (restore_ok ? "PASS" : "FAIL") << '\n';

    return 0;
}
