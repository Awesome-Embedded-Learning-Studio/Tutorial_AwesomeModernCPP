// type_sizes.cpp
// 打印 C++ 基本整数类型在当前平台上的大小

#include <iostream>

int main()
{
    std::cout << "=== 整数类型大小（字节） ===" << std::endl;
    std::cout << "short:          " << sizeof(short) << std::endl;
    std::cout << "int:            " << sizeof(int) << std::endl;
    std::cout << "long:           " << sizeof(long) << std::endl;
    std::cout << "long long:      " << sizeof(long long) << std::endl;
    std::cout << std::endl;

    std::cout << "=== 对应的无符号版本 ===" << std::endl;
    std::cout << "unsigned short: " << sizeof(unsigned short) << std::endl;
    std::cout << "unsigned int:   " << sizeof(unsigned int) << std::endl;
    std::cout << "unsigned long:  " << sizeof(unsigned long) << std::endl;
    std::cout << "unsigned long long: " << sizeof(unsigned long long)
              << std::endl;

    return 0;
}
