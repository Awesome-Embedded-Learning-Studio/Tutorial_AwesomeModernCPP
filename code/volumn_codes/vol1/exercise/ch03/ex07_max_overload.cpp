/**
 * @file ex07_max_overload.cpp
 * @brief 练习：max 的重载家族
 *
 * 重载 max_value 函数支持 int、double 和 const char*（按字典序比较）。
 * 展示函数重载的机制和重载决议。
 */

#include <cstring>
#include <iostream>

// 重载 1：int 版本
int max_value(int a, int b) {
    std::cout << "  [调用 int 版本] ";
    return (a > b) ? a : b;
}

// 重载 2：double 版本
double max_value(double a, double b) {
    std::cout << "  [调用 double 版本] ";
    return (a > b) ? a : b;
}

// 重载 3：const char* 版本（按字典序比较）
const char* max_value(const char* a, const char* b) {
    std::cout << "  [调用 const char* 版本] ";
    return (std::strcmp(a, b) > 0) ? a : b;
}

int main() {
    // int 版本
    std::cout << "max_value(10, 20) = " << max_value(10, 20) << '\n';
    std::cout << "max_value(-5, 3) = " << max_value(-5, 3) << '\n';

    // double 版本
    std::cout << "max_value(3.14, 2.71) = " << max_value(3.14, 2.71) << '\n';
    std::cout << "max_value(-1.0, 0.0) = " << max_value(-1.0, 0.0) << '\n';

    // const char* 版本（字典序比较）
    const char* s1 = "apple";
    const char* s2 = "banana";
    std::cout << "max_value(\"apple\", \"banana\") = "
              << max_value(s1, s2) << '\n';

    const char* s3 = "cherry";
    const char* s4 = "apple";
    std::cout << "max_value(\"cherry\", \"apple\") = "
              << max_value(s3, s4) << '\n';

    // 注意：以下调用会导致歧义或意外匹配
    std::cout << "\n===== 重载决议注意事项 =====\n";

    // max_value(10, 3.14) 会调用哪个？
    // 10 是 int，3.14 是 double，需要隐式转换
    // 编译器会选择 double 版本（int -> double 是标准转换）
    // 但某些编译器可能报歧义错误
    // std::cout << max_value(10, 3.14) << '\n';  // 可能歧义

    // 显式指定类型
    std::cout << "显式转换: max_value(static_cast<double>(10), 3.14) = "
              << max_value(static_cast<double>(10), 3.14) << '\n';

    return 0;
}
