// dangling-reference.cpp —— 悬空引用演示（反面教材 + 正确写法）
// 注意：BROKEN 版本返回局部变量的引用，是未定义行为，可能崩溃或输出垃圾值。
// 下面同时展示了错误写法（注释中）和正确写法。

#include <iostream>

/// @brief 错误写法：返回局部变量的引用（悬空引用，未定义行为！）
/// 取消下面的注释会导致 UB，可能 segfault 或输出垃圾值。
///
// int& get_max_broken(int a, int b)
// {
//     int result = (a > b) ? a : b;
//     return result;    // 严重错误！result 在函数返回后销毁
// }

/// @brief 正确写法：按值返回
int get_max(int a, int b)
{
    int result = (a > b) ? a : b;
    return result;    // OK：返回值的拷贝，不依赖局部变量的生命周期
}

int main()
{
    // 错误写法演示（已注释，取消注释后运行结果不可预测）：
    // int& m = get_max_broken(3, 7);
    // std::cout << m << "\n";    // 可能输出 7，可能输出垃圾值，可能崩溃

    // 正确写法：
    int m = get_max(3, 7);
    std::cout << m << "\n";       // 稳定输出 7

    return 0;
}
