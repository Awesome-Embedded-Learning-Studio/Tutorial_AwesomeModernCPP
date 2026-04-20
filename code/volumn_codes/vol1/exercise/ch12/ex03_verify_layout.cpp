/**
 * @file ex03_verify_layout.cpp
 * @brief 练习：验证布局模型
 *
 * 在同一函数中声明不同区域的变量，打印地址，
 * 并通过调用子函数验证栈的增长方向。
 */

#include <cstdint>
#include <iostream>

// 全局变量 —— 数据段（已初始化）
int g_value = 100;

// 全局变量 —— BSS 段（未初始化）
int g_zero;

// 子函数：打印子函数中局部变量的地址
void sub_function()
{
    int sub_local = 999;
    std::cout << "  sub_function() 中 sub_local 地址: " << &sub_local << "\n";
}

int main()
{
    std::cout << "===== ex03: 验证布局模型 =====\n\n";

    // 局部变量 —— 栈
    int stack_var = 1;

    // 堆变量
    int* heap_var = new int(2);

    // static 局部变量 —— 数据段
    static int s_static_local = 3;

    // ---- 打印各区域变量的地址 ----
    std::cout << "=== 各区域变量地址 ===\n";
    std::cout << "代码段 (函数地址):     main()         @ "
              << reinterpret_cast<void*>(
                     reinterpret_cast<std::uintptr_t>(main))
              << "\n";
    std::cout << "数据段 (全局已初始化): g_value        @ " << &g_value << "\n";
    std::cout << "BSS 段 (全局未初始化): g_zero         @ " << &g_zero << "\n";
    std::cout << "数据段 (static 局部):  s_static_local @ " << &s_static_local
              << "\n";
    std::cout << "栈:                    stack_var      @ " << &stack_var << "\n";
    std::cout << "堆:                    heap_var       @ " << heap_var << "\n";

    // ---- 验证地址大小关系 ----
    std::cout << "\n=== 地址大小关系 ===\n";
    std::cout << "栈地址 > 堆地址?    "
              << ((&stack_var > heap_var) ? "是" : "否") << "\n";
    std::cout << "栈地址 > 数据段地址? "
              << ((&stack_var > &g_value) ? "是" : "否") << "\n";
    std::cout << "数据段地址 > 代码段地址? "
              << ((&g_value > reinterpret_cast<int*>(main)) ? "是" : "否")
              << "\n";

    // ---- 验证栈增长方向 ----
    std::cout << "\n=== 验证栈增长方向 ===\n";
    std::cout << "main() 中 stack_var 地址:       " << &stack_var << "\n";
    sub_function();

    // 在 main 中再声明一个局部变量，验证它在 stack_var 的低地址方向
    int another_local = 42;
    std::cout << "main() 中 another_local 地址:   " << &another_local << "\n";

    // 注意：编译器可能调整局部变量的布局顺序，
    // 但子函数的栈帧一定在调用者栈帧的低地址方向
    std::cout << "\n  注: 子函数的局部变量地址通常比父函数的更小 (栈向低地址增长)\n";
    std::cout << "  注: 同一函数内的局部变量顺序由编译器决定，不保证与声明顺序一致\n";

    // ---- 展示地址区间概览 ----
    std::cout << "\n=== 内存布局概览 ===\n";
    std::cout << "高地址\n";
    std::cout << "  ┌───────────────┐\n";
    std::cout << "  │ 栈 (Stack)     │  栈变量 @ " << &stack_var << "\n";
    std::cout << "  │     ↓↓↓       │\n";
    std::cout << "  ├───────────────┤\n";
    std::cout << "  │   (空闲)      │\n";
    std::cout << "  ├───────────────┤\n";
    std::cout << "  │ 堆 (Heap)     │  堆变量 @ " << heap_var << "\n";
    std::cout << "  │     ↑↑↑       │\n";
    std::cout << "  ├───────────────┤\n";
    std::cout << "  │ 数据段/BSS    │  全局/static @ " << &g_value << "\n";
    std::cout << "  ├───────────────┤\n";
    std::cout << "  │ 代码段 (Text) │  main() @ "
              << reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(main))
              << "\n";
    std::cout << "  └───────────────┘\n";
    std::cout << "低地址\n";

    delete heap_var;

    std::cout << "\n要点:\n";
    std::cout << "  1. 栈在高地址向低地址增长，堆在低地址向高地址增长\n";
    std::cout << "  2. 子函数的栈变量地址比父函数更小（更低的地址）\n";
    std::cout << "  3. 全局/static 变量在静态区，地址远小于栈变量\n";
    std::cout << "  4. 每次运行栈地址可能不同（ASLR 安全机制）\n";

    return 0;
}
