#include <iostream>

void foo()
{
    int a = 1;    // 栈上分配
    double b = 2.0; // 紧随 a 之后
    std::cout << "a 的地址: " << &a << "\n";
    std::cout << "b 的地址: " << &b << "\n";
    // 函数返回，a 和 b 的空间自动回收
}

int main()
{
    foo();
    // 这里 a 和 b 已经不存在了
    return 0;
}
