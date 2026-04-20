/**
 * @file ex01_type_report.cpp
 * @brief 练习：完整的大小和范围报告
 *
 * 打印所有基本整数类型的 sizeof 和 numeric_limits::min/max，
 * 包括 <cstdint> 中的固定宽度类型。
 */

#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

// 辅助：打印一行类型报告
template <typename T>
void print_type_info(const std::string& name) {
    std::cout << std::left << std::setw(20) << name
              << "  sizeof = " << sizeof(T)
              << "  min = " << std::numeric_limits<T>::min()
              << "  max = " << std::numeric_limits<T>::max()
              << '\n';
}

int main() {
    std::cout << "===== 标准整数类型 =====\n\n";

    print_type_info<short>("short");
    print_type_info<int>("int");
    print_type_info<long>("long");
    print_type_info<long long>("long long");

    std::cout << '\n';

    print_type_info<unsigned short>("unsigned short");
    print_type_info<unsigned int>("unsigned int");
    print_type_info<unsigned long>("unsigned long");
    print_type_info<unsigned long long>("unsigned long long");

    std::cout << "\n===== <cstdint> 固定宽度类型 =====\n\n";

    print_type_info<int8_t>("int8_t");
    print_type_info<int16_t>("int16_t");
    print_type_info<int32_t>("int32_t");
    print_type_info<int64_t>("int64_t");

    std::cout << '\n';

    print_type_info<uint8_t>("uint8_t");
    print_type_info<uint16_t>("uint16_t");
    print_type_info<uint32_t>("uint32_t");
    print_type_info<uint64_t>("uint64_t");

    std::cout << "\n===== 字符与布尔类型 =====\n\n";

    print_type_info<char>("char");
    print_type_info<signed char>("signed char");
    print_type_info<unsigned char>("unsigned char");
    print_type_info<bool>("bool");

    return 0;
}
