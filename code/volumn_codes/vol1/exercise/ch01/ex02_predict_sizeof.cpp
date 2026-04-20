/**
 * @file ex02_predict_sizeof.cpp
 * @brief 练习：预测 sizeof 的结果
 *
 * 打印 sizeof('A'), sizeof(true), sizeof(3.14), sizeof(3.14f),
 * sizeof(3.14L) 并在注释中解释结果。
 *
 * 要点：sizeof('A') 在 C++ 中是 sizeof(char) == 1，
 * 这与 C 语言不同（C 中字符字面量的类型是 int，sizeof 为 4）。
 */

#include <iostream>

int main() {
    // 'A' 的类型是 char，所以 sizeof('A') == 1
    std::cout << "sizeof('A')    = " << sizeof('A') << '\n';
    // 解释：C++ 中字符字面量 'A' 的类型是 char（1 字节）
    // 注意：在 C 语言中，字符字面量的类型是 int，sizeof('A') == 4

    // true 的类型是 bool，通常为 1 字节
    std::cout << "sizeof(true)   = " << sizeof(true) << '\n';
    // 解释：bool 类型占用 1 字节

    // 3.14 的类型是 double（8 字节），不是 float
    std::cout << "sizeof(3.14)   = " << sizeof(3.14) << '\n';
    // 解释：浮点字面量默认类型为 double（8 字节）

    // 3.14f 的类型是 float（4 字节）
    std::cout << "sizeof(3.14f)  = " << sizeof(3.14f) << '\n';
    // 解释：f 后缀将字面量指定为 float（4 字节）

    // 3.14L 的类型是 long double
    std::cout << "sizeof(3.14L)  = " << sizeof(3.14L) << '\n';
    // 解释：L 后缀将字面量指定为 long double
    // 在大多数 64 位平台上为 8 或 16 字节（依平台而异）

    return 0;
}
