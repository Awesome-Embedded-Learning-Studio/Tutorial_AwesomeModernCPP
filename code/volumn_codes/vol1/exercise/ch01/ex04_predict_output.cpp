/**
 * @file ex04_predict_output.cpp
 * @brief 练习：预测输出 — 类型转换陷阱
 *
 * 演示整数除法 vs 浮点除法，以及有符号/无符号比较的陷阱。
 * 先在纸上预测输出，再运行验证。
 */

#include <iostream>

int main() {
    // ===== 整数除法 vs 浮点除法 =====
    std::cout << "===== 整数除法 vs 浮点除法 =====\n";

    int a = 7;
    int b = 2;

    // 整数除法：7 / 2 = 3（截断小数部分）
    std::cout << "7 / 2        = " << a / b << '\n';
    // 预测：3

    // 其中一个操作数是 double，触发浮点除法
    std::cout << "7.0 / 2      = " << 7.0 / b << '\n';
    // 预测：3.5

    std::cout << "7 / 2.0      = " << a / 2.0 << '\n';
    // 预测：3.5

    // static_cast 显式转换
    std::cout << "static_cast<double>(7) / 2 = "
              << static_cast<double>(a) / b << '\n';
    // 预测：3.5

    // ===== 有符号/无符号比较陷阱 =====
    std::cout << "\n===== 有符号/无符号比较陷阱 =====\n";

    int signed_val = -1;
    unsigned int unsigned_val = 10;

    // 危险！signed_val 被隐式转换为 unsigned int
    // -1 转为 unsigned int 变成 4294967295（在 32 位平台上）
    if (unsigned_val > signed_val) {
        // 这不会执行！因为 -1 转为 unsigned 后是一个很大的数
    }

    // 直接打印比较结果
    std::cout << "10 > -1  (直接比较 int vs unsigned):  "
              << (10 > -1) << '\n';
    // 预测：1（两个都是 int 字面量，正常的有符号比较）

    std::cout << "unsigned(10) > int(-1):  "
              << (unsigned_val > signed_val) << '\n';
    // 预测：0（-1 被转为 unsigned，变成 4294967295）

    // 打印转换后的值，直观展示
    std::cout << "static_cast<unsigned int>(-1) = "
              << static_cast<unsigned int>(-1) << '\n';

    // ===== 安全的比较方式 =====
    std::cout << "\n===== 安全的比较方式 =====\n";
    // 方法 1：显式转换为有符号类型
    std::cout << "显式转为 int 比较: "
              << (static_cast<int>(unsigned_val) > signed_val) << '\n';

    // 方法 2：先检查范围再比较
    if (signed_val < 0) {
        std::cout << "signed_val 为负数，显然 unsigned_val 更大\n";
    }

    return 0;
}
