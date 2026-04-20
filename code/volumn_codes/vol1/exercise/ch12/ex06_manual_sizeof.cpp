/**
 * @file ex06_manual_sizeof.cpp
 * @brief 练习：手动计算 sizeof
 *
 * 给出多个结构体定义，先手动预测 sizeof 和成员偏移量，
 * 然后打印实际的 sizeof 和偏移量进行验证。
 * 涵盖不同的对齐/填充场景。
 */

#include <cstddef>
#include <cstdint>
#include <iostream>

// ============================================================
// 结构体定义（来自教程中的练习题目）
// ============================================================

// 结构体 X：char + double + int
// 预测分析（64 位系统）：
//   a (char)  : 偏移 0，大小 1
//   填充       : 偏移 1-7 (7 字节，为了对齐 double)
//   b (double): 偏移 8，大小 8
//   c (int)   : 偏移 16，大小 4
//   尾部填充   : 偏移 20-23 (4 字节，整体大小须为 8 的倍数)
//   预测 sizeof = 24
struct X {
    char   a;
    double b;
    int    c;
};

// 结构体 Y：double + int + char
// 预测分析：
//   a (double): 偏移 0，大小 8
//   b (int)   : 偏移 8，大小 4
//   c (char)  : 偏移 12，大小 1
//   尾部填充   : 偏移 13-15 (3 字节，整体大小须为 8 的倍数)
//   预测 sizeof = 16
struct Y {
    double a;
    int    b;
    char   c;
};

// 结构体 Z：char + char + int + int
// 预测分析：
//   a (char) : 偏移 0，大小 1
//   b (char) : 偏移 1，大小 1
//   填充      : 偏移 2-3 (2 字节，为了对齐 int)
//   c (int)  : 偏移 4，大小 4
//   d (int)  : 偏移 8，大小 4
//   尾部无需填充（12 已经是 4 的倍数）
//   预测 sizeof = 12
struct Z {
    char a;
    char b;
    int  c;
    int  d;
};

// 额外的结构体用于更多对齐场景

// 结构体 W：包含 short 和 double
// 预测分析：
//   a (short) : 偏移 0，大小 2
//   填充       : 偏移 2-7 (6 字节，为了对齐 double)
//   b (double): 偏移 8，大小 8
//   c (char)  : 偏移 16，大小 1
//   尾部填充   : 偏移 17-23 (7 字节)
//   预测 sizeof = 24
struct W {
    short  a;
    double b;
    char   c;
};

// 结构体 V：紧凑排列
// 预测分析：
//   a (double): 偏移 0，大小 8
//   b (short) : 偏移 8，大小 2
//   c (char)  : 偏移 10，大小 1
//   尾部填充   : 偏移 11-15 (5 字节)
//   预测 sizeof = 16
struct V {
    double a;
    short  b;
    char   c;
};

// ============================================================
// 辅助函数：打印结构体信息
// ============================================================

template <typename T>
void print_struct_info(const char* name,
                       int predicted_size,
                       int predicted_align)
{
    int actual_size = static_cast<int>(sizeof(T));
    int actual_align = static_cast<int>(alignof(T));

    std::cout << name << ":\n";
    std::cout << "  预测 sizeof = " << predicted_size
              << "  实际 sizeof = " << actual_size
              << "  " << (predicted_size == actual_size ? "[正确]" : "[错误!]")
              << "\n";
    std::cout << "  预测 alignof = " << predicted_align
              << "  实际 alignof = " << actual_align
              << "  " << (predicted_align == actual_align ? "[正确]" : "[错误!]")
              << "\n";
}

int main()
{
    std::cout << "===== ex06: 手动计算 sizeof =====\n\n";

    std::cout << "=== 结构体 X (char + double + int) ===\n";
    print_struct_info<X>("X", 24, 8);
    std::cout << "  偏移量: a=" << offsetof(X, a)
              << ", b=" << offsetof(X, b)
              << ", c=" << offsetof(X, c) << "\n";
    std::cout << "  预测偏移: a=0, b=8, c=16\n\n";

    std::cout << "=== 结构体 Y (double + int + char) ===\n";
    print_struct_info<Y>("Y", 16, 8);
    std::cout << "  偏移量: a=" << offsetof(Y, a)
              << ", b=" << offsetof(Y, b)
              << ", c=" << offsetof(Y, c) << "\n";
    std::cout << "  预测偏移: a=0, b=8, c=12\n\n";

    std::cout << "=== 结构体 Z (char + char + int + int) ===\n";
    print_struct_info<Z>("Z", 12, 4);
    std::cout << "  偏移量: a=" << offsetof(Z, a)
              << ", b=" << offsetof(Z, b)
              << ", c=" << offsetof(Z, c)
              << ", d=" << offsetof(Z, d) << "\n";
    std::cout << "  预测偏移: a=0, b=1, c=4, d=8\n\n";

    std::cout << "=== 结构体 W (short + double + char) ===\n";
    print_struct_info<W>("W", 24, 8);
    std::cout << "  偏移量: a=" << offsetof(W, a)
              << ", b=" << offsetof(W, b)
              << ", c=" << offsetof(W, c) << "\n";
    std::cout << "  预测偏移: a=0, b=8, c=16\n\n";

    std::cout << "=== 结构体 V (double + short + char) ===\n";
    print_struct_info<V>("V", 16, 8);
    std::cout << "  偏移量: a=" << offsetof(V, a)
              << ", b=" << offsetof(V, b)
              << ", c=" << offsetof(V, c) << "\n";
    std::cout << "  预测偏移: a=0, b=8, c=10\n\n";

    // 对比 X 和 Y：同样的成员，不同排列
    std::cout << "=== 对比: 成员排列对大小的影响 ===\n";
    std::cout << "X (char+double+int) = " << sizeof(X) << " 字节\n";
    std::cout << "Y (double+int+char) = " << sizeof(Y) << " 字节\n";
    std::cout << "  相同成员，重排后节省 " << sizeof(X) - sizeof(Y) << " 字节\n\n";

    std::cout << "W (short+double+char) = " << sizeof(W) << " 字节\n";
    std::cout << "V (double+short+char) = " << sizeof(V) << " 字节\n";
    std::cout << "  相同成员，重排后节省 " << sizeof(W) - sizeof(V) << " 字节\n";

    std::cout << "\n要点:\n";
    std::cout << "  1. 每个成员必须放在其自然对齐要求的整数倍地址上\n";
    std::cout << "  2. 结构体整体大小必须是最大对齐要求的整数倍\n";
    std::cout << "  3. 把大对齐的成员放前面可以减少填充，减小结构体大小\n";
    std::cout << "  4. char=1, short=2, int=4, double=8 (64 位系统)\n";
    std::cout << "  5. 使用 offsetof 宏可以验证成员的实际偏移量\n";

    return 0;
}
