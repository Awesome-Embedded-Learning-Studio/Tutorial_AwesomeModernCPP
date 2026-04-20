/**
 * @file ex09_overload_ambiguity.cpp
 * @brief 练习：能编译还是歧义
 *
 * 测试 func(int) vs func(short) 被字符 'A' 调用时的重载决议。
 * 展示整数提升（integral promotion）vs 整数转换（integer conversion）。
 */

#include <iostream>

void func(int x) {
    std::cout << "func(int) 被调用, x = " << x << '\n';
}

void func(short x) {
    std::cout << "func(short) 被调用, x = " << x << '\n';
}

int main() {
    // ===== 核心问题：func('A') 调用哪个重载？ =====
    std::cout << "===== func('A') 的重载决议 =====\n\n";

    // 'A' 的类型是 char
    // char -> int 是整数提升（integral promotion），优先级高
    // char -> short 是整数转换（integer conversion），优先级低
    // 因此选择 func(int)

    std::cout << "调用 func('A'):\n";
    func('A');  // 调用 func(int)

    std::cout << "\n===== 解释 =====\n";
    std::cout << "'A' 的类型是 char（值为 65）\n";
    std::cout << "char -> int  是「整数提升」（优先级更高）\n";
    std::cout << "char -> short 是「整数转换」（优先级更低）\n";
    std::cout << "重载决议选择优先级更高的转换，因此调用 func(int)\n";

    // ===== 更多重载决议示例 =====
    std::cout << "\n===== 更多示例 =====\n";

    // 显式调用
    std::cout << "func(static_cast<int>('A')):  ";
    func(static_cast<int>('A'));

    std::cout << "func(static_cast<short>('A')): ";
    func(static_cast<short>('A'));

    std::cout << "func(42):   ";  // int 字面量 -> func(int)
    func(42);

    std::cout << "func(static_cast<short>(42)): ";
    func(static_cast<short>(42));

    // ===== 重载决议优先级 =====
    std::cout << "\n===== 重载决议优先级（从高到低）=====\n";
    std::cout << "1. 精确匹配（Exact Match）\n";
    std::cout << "2. 整数提升（Integral Promotion）\n";
    std::cout << "   char/short -> int\n";
    std::cout << "   bool -> int\n";
    std::cout << "3. 浮点提升（Floating-point Promotion）\n";
    std::cout << "   float -> double\n";
    std::cout << "4. 整数/浮点转换（Conversion）\n";
    std::cout << "   int -> short, double -> int 等\n";
    std::cout << "5. 用户定义转换（User-defined Conversion）\n";
    std::cout << "6. 省略号匹配（Ellipsis Match）\n";

    return 0;
}
