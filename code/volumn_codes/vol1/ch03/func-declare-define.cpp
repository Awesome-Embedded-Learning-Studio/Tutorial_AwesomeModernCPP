#include <iostream>

// 先声明，告诉编译器这些函数存在
int add(int a, int b);
int multiply(int a, int b);

int main()
{
    std::cout << add(3, 4) << std::endl;       // 编译器知道 add 的签名
    std::cout << multiply(3, 4) << std::endl;  // 编译器知道 multiply 的签名
    return 0;
}

// 定义放在后面，完全没问题
int add(int a, int b)
{
    return a + b;
}

int multiply(int a, int b)
{
    return a * b;
}
