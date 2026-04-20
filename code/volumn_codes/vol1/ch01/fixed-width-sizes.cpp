#include <cstdint>
#include <iostream>

int main()
{
    std::cout << "=== 固定宽度类型大小（字节） ===" << std::endl;
    std::cout << "int8_t:   " << sizeof(int8_t) << std::endl;
    std::cout << "int16_t:  " << sizeof(int16_t) << std::endl;
    std::cout << "int32_t:  " << sizeof(int32_t) << std::endl;
    std::cout << "int64_t:  " << sizeof(int64_t) << std::endl;
    std::cout << std::endl;
    std::cout << "uint8_t:  " << sizeof(uint8_t) << std::endl;
    std::cout << "uint16_t: " << sizeof(uint16_t) << std::endl;
    std::cout << "uint32_t: " << sizeof(uint32_t) << std::endl;
    std::cout << "uint64_t: " << sizeof(uint64_t) << std::endl;

    return 0;
}
