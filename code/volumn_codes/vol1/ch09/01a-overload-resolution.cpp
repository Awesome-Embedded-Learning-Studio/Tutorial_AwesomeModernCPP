// 01a-overload-resolution.cpp
#include <iostream>

template <typename T>
T max_value(T a, T b)
{
    return (a > b) ? a : b;
}

// 普通重载：int 版本
int max_value(int a, int b)
{
    std::cout << "int overload\n";
    return (a > b) ? a : b;
}

int main()
{
    max_value(3, 5);       // 调用普通重载（精确匹配优先于模板）
    max_value(1.0, 2.0);   // 调用模板实例化（double 无重载版本）
    max_value<>(3, 5);     // 强制使用模板，跳过普通重载
}
