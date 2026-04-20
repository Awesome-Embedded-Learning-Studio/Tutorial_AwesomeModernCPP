/**
 * @file ex08_classify_number.cpp
 * @brief 练习：正数、负数、零
 *
 * 读取一个整数，判断它是正数、负数还是零。
 * 分别用 if-else 链和三元运算符两种方式实现。
 */

#include <iostream>
#include <string>

// 方式一：使用 if-else 链
std::string classify_if_else(int n) {
    if (n > 0) {
        return "正数";
    } else if (n < 0) {
        return "负数";
    } else {
        return "零";
    }
}

// 方式二：使用三元运算符（嵌套）
std::string classify_ternary(int n) {
    return (n > 0) ? "正数" : ((n < 0) ? "负数" : "零");
}

int main() {
    std::cout << "===== ex08: 正数、负数、零 =====\n\n";

    // 用户输入
    int n = 0;
    std::cout << "请输入一个整数: ";
    std::cin >> n;

    // if-else 方式
    std::cout << n << " 是" << classify_if_else(n) << "（if-else 判断）\n";

    // 三元运算符方式
    std::cout << n << " 是" << classify_ternary(n) << "（三元运算符判断）\n";

    // 批量测试
    std::cout << "\n===== 批量测试 =====\n";
    int test_values[] = {42, -7, 0, 100, -1, 9999, -9999};

    for (int val : test_values) {
        std::cout << val << " -> " << classify_if_else(val)
                  << " / " << classify_ternary(val) << "\n";
    }

    return 0;
}
