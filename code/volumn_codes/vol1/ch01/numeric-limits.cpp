#include <cstdint>
#include <iostream>
#include <limits>

int main()
{
    std::cout << "=== int32_t 的范围 ===" << std::endl;
    std::cout << "最小值: " << std::numeric_limits<int32_t>::min()
              << std::endl;
    std::cout << "最大值: " << std::numeric_limits<int32_t>::max()
              << std::endl;
    std::cout << std::endl;

    std::cout << "=== uint32_t 的范围 ===" << std::endl;
    std::cout << "最小值: " << std::numeric_limits<uint32_t>::min()
              << std::endl;
    std::cout << "最大值: " << std::numeric_limits<uint32_t>::max()
              << std::endl;

    return 0;
}
