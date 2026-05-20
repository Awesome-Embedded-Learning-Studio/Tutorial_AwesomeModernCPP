// 02_basic_types.cpp
// 打印 C++ 所有基本类型的大小和固定宽度类型

#include <cstdint>
#include <iostream>
#include <limits>

int main() {
    std::cout << "=== 基本类型 sizeof 汇总 ===" << std::endl;
    std::cout << "bool:          " << sizeof(bool) << " 字节" << std::endl;
    std::cout << "char:          " << sizeof(char) << " 字节" << std::endl;
    std::cout << "short:         " << sizeof(short) << " 字节" << std::endl;
    std::cout << "int:           " << sizeof(int) << " 字节" << std::endl;
    std::cout << "long:          " << sizeof(long) << " 字节" << std::endl;
    std::cout << "long long:     " << sizeof(long long) << " 字节" << std::endl;
    std::cout << "float:         " << sizeof(float) << " 字节" << std::endl;
    std::cout << "double:        " << sizeof(double) << " 字节" << std::endl;
    std::cout << "long double:   " << sizeof(long double) << " 字节" << std::endl;
    std::cout << std::endl;

    std::cout << "=== 固定宽度类型 ===" << std::endl;
    std::cout << "int8_t:   " << sizeof(int8_t) << std::endl;
    std::cout << "int16_t:  " << sizeof(int16_t) << std::endl;
    std::cout << "int32_t:  " << sizeof(int32_t) << std::endl;
    std::cout << "int64_t:  " << sizeof(int64_t) << std::endl;
    std::cout << std::endl;

    std::cout << "=== int32_t 的范围 ===" << std::endl;
    std::cout << "最小值: " << std::numeric_limits<int32_t>::min() << std::endl;
    std::cout << "最大值: " << std::numeric_limits<int32_t>::max() << std::endl;

    return 0;
}
