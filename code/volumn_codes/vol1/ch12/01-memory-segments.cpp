#include <iostream>

int global_var = 10;          // 数据段：已初始化全局变量
int global_uninit;             // BSS 段：未初始化全局变量（自动为 0）
const char* kMessage = "hello"; // 数据段：指针本身在数据段
                               // "hello" 字面量在代码段（只读）

void demo()
{
    static int call_count = 0; // 数据段：首次调用时初始化
    ++call_count;
    std::cout << "第 " << call_count << " 次调用\n";
}

int main()
{
    std::cout << "global_var = " << global_var << "\n";
    std::cout << "global_uninit = " << global_uninit << "\n";

    demo(); // 第 1 次
    demo(); // 第 2 次
    demo(); // 第 3 次

    return 0;
}
