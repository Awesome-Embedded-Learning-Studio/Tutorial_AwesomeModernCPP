#include <iostream>
#include <cstdint>

int main()
{
    // static_cast 示例
    int i = 42;
    double d = static_cast<double>(i);       // int -> double，输出 42
    double pi = 3.14159;
    int truncated = static_cast<int>(pi);    // double -> int，输出 3

    std::cout << d << " " << truncated << std::endl;

    // reinterpret_cast 示例

    // 场景一：void* 和类型指针之间的转换
    int value = 100;
    void* pv = &value;
    int* pi2 = reinterpret_cast<int*>(pv);
    std::cout << *pi2 << std::endl;  // 100

    // 场景二：查看浮点数的底层位模式
    float f = 1.0f;
    uint32_t bits = reinterpret_cast<uint32_t&>(f);
    // 1.0f 的 IEEE 754 表示：0x3f800000
    std::cout << std::hex << bits << std::endl;

    return 0;
}
