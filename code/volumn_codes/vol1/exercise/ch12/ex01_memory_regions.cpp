/**
 * @file ex01_memory_regions.cpp
 * @brief 练习：判断存储区域
 *
 * 根据变量的声明方式，判断它们分别存储在哪个内存区域
 * （栈、堆、数据段、BSS 段、代码段）。
 */

#include <iostream>
int main();
// ---- 全局变量 ----

// 已初始化全局变量 -> 数据段（Data segment）
int g_initialized = 42;

// 未初始化全局变量 -> BSS 段（自动初始化为 0）
int g_uninitialized;

// 全局 const char* 指针本身 -> 数据段
// 指向的字符串字面量 "global message" -> 代码段（只读）
const char* kGlobalMsg = "global message";

// ---- 函数定义 ----

void classify_regions() {
    std::cout << "===== ex01: 判断存储区域 =====\n\n";

    // 栈变量（局部变量）
    int local_var = 10;                    // 栈（Stack）
    double local_arr[3] = {1.0, 2.0, 3.0}; // 栈（Stack）

    // 堆变量（动态分配）
    int* heap_ptr = new int[10]; // heap_ptr 在栈上，指向的内存在堆（Heap）上
    for (int i = 0; i < 10; ++i) {
        heap_ptr[i] = i;
    }

    // static 局部变量
    static int s_counter = 0; // 数据段（Data segment），首次调用时初始化
    static int s_uninit;      // BSS 段，自动初始化为 0
    ++s_counter;

    // 局部 const char* 指针
    const char* msg = "error"; // msg 在栈上，"error" 在代码段（只读）

    // ---- 打印分类结果 ----
    std::cout << "变量分类表:\n";
    std::cout << "----------------------------------------------\n";
    std::cout << "变量                       | 存储区域\n";
    std::cout << "----------------------------------------------\n";
    std::cout << "g_initialized (全局已初始化)    | 数据段\n";
    std::cout << "  地址: " << &g_initialized << "\n\n";

    std::cout << "g_uninitialized (全局未初始化)  | BSS 段\n";
    std::cout << "  地址: " << &g_uninitialized << "\n\n";

    std::cout << "kGlobalMsg (全局指针)           | 数据段\n";
    std::cout << "  指针地址: " << &kGlobalMsg << "\n";
    std::cout << "  指向内容 (\"global message\")   | 代码段 (只读)\n";
    std::cout << "  指向地址: " << static_cast<const void*>(kGlobalMsg) << "\n\n";

    std::cout << "local_var (局部变量)            | 栈\n";
    std::cout << "  地址: " << &local_var << "\n\n";

    std::cout << "local_arr (局部数组)            | 栈\n";
    std::cout << "  地址: " << local_arr << "\n\n";

    std::cout << "heap_ptr (指针本身)             | 栈\n";
    std::cout << "  指针地址: " << &heap_ptr << "\n";
    std::cout << "heap_ptr 指向的数组             | 堆\n";
    std::cout << "  指向地址: " << heap_ptr << "\n\n";

    std::cout << "s_counter (static 局部，已初始化) | 数据段\n";
    std::cout << "  地址: " << &s_counter << "\n\n";

    std::cout << "s_uninit (static 局部，未初始化)  | BSS 段\n";
    std::cout << "  地址: " << &s_uninit << " 值: " << s_uninit << "\n\n";

    std::cout << "msg (局部指针)                  | 栈\n";
    std::cout << "  指针地址: " << &msg << "\n";
    std::cout << "  指向内容 (\"error\")            | 代码段 (只读)\n";
    std::cout << "  指向地址: " << static_cast<const void*>(msg) << "\n\n";

    std::cout << "main() 函数地址                | 代码段\n";
    std::cout << "  地址: " << reinterpret_cast<void*>(reinterpret_cast<int*>(main)) << "\n\n";

    // ---- 地址大小关系验证 ----
    std::cout << "=== 地址大小关系验证 ===\n";
    std::cout << "栈地址 > 堆地址? " << (&local_var > heap_ptr ? "是 (符合预期)" : "否") << "\n";
    std::cout << "栈地址 > 数据段地址? " << (&local_var > &g_initialized ? "是 (符合预期)" : "否")
              << "\n";
    std::cout << "数据段地址 > 代码段地址? "
              << (&g_initialized > reinterpret_cast<int*>(main) ? "是 (符合预期)" : "否") << "\n";
    std::cout << "static 局部与全局地址接近? "
              << (std::abs(reinterpret_cast<char*>(&s_counter) -
                           reinterpret_cast<char*>(&g_initialized)) < 0x100000
                      ? "是 (同在静态区)"
                      : "否")
              << "\n";

    // 释放堆内存
    delete[] heap_ptr;
}

int main() {
    classify_regions();

    std::cout << "\n要点:\n";
    std::cout << "  1. 栈变量：函数内的局部变量（不含 static）\n";
    std::cout << "  2. 堆变量：new/malloc 分配的内存\n";
    std::cout << "  3. 数据段：已初始化的全局变量和 static 局部变量\n";
    std::cout << "  4. BSS 段：未初始化的全局变量和 static 局部变量（自动为 0）\n";
    std::cout << "  5. 代码段：机器指令和字符串字面量（只读）\n";

    return 0;
}
