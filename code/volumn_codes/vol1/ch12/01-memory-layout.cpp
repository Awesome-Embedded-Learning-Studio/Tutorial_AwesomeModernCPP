// layout.cpp
// 编译: g++ -std=c++17 -O0 layout.cpp -o layout
// 注意: 使用 -O0 关闭优化，防止编译器对变量做激进优化

#include <cstdint>
#include <iostream>

// 全局变量 —— 数据段（已初始化）
int g_initialized = 42;

// 全局变量 —— BSS 段（未初始化，自动为 0）
int g_uninitialized;

// const 全局 —— 通常在只读段或被编译器内联
constexpr int kGlobalConst = 100;

int main()
{
    // 栈变量
    int stack_var = 1;

    // 堆变量
    int* heap_var = new int(2);

    // static 局部变量 —— 数据段
    static int s_static_local = 3;

    std::cout << "=== 各区域变量地址 ===\n";
    std::cout << "代码段 (函数地址):  main()    @ " << reinterpret_cast<void*>(main) << "\n";
    std::cout << "数据段 (已初始化):  g_initialized  @ " << &g_initialized << "\n";
    std::cout << "BSS段  (未初始化):  g_uninitialized @ " << &g_uninitialized << "\n";
    std::cout << "数据段 (static局部): s_static_local @ " << &s_static_local << "\n";
    std::cout << "栈:                 stack_var  @ " << &stack_var << "\n";
    std::cout << "堆:                 heap_var   @ " << heap_var << "\n";

    std::cout << "\n=== 地址大小关系 ===\n";
    std::cout << "栈地址 > 堆地址? " << (&stack_var > heap_var ? "是" : "否") << "\n";
    std::cout << "栈地址 > 数据段地址? " << (&stack_var > &g_initialized ? "是" : "否") << "\n";
    std::cout << "数据段地址 > 代码段地址? "
              << (&g_initialized > reinterpret_cast<int*>(main) ? "是" : "否") << "\n";

    delete heap_var;
    return 0;
}
