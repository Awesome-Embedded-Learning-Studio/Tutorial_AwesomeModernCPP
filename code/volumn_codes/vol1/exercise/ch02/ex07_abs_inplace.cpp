/**
 * @file ex07_abs_inplace.cpp
 * @brief 练习：原地修改
 *
 * 使用 range-for 引用（for (auto& x : arr)）将数组中所有负数
 * 转换为绝对值，演示 range-for 的原地修改能力。
 */

#include <array>
#include <cmath>
#include <iostream>

template <typename T, std::size_t N>
void print_array(const std::array<T, N>& arr) {
    for (const auto& elem : arr) {
        std::cout << elem << ' ';
    }
    std::cout << '\n';
}

int main() {
    std::array<int, 8> numbers = {3, -7, -5, 42, -8, 0, -23, 11};

    std::cout << "原始数组: ";
    print_array(numbers);

    // 使用 range-for 引用原地修改
    for (auto& num : numbers) {
        if (num < 0) {
            num = -num;  // 取绝对值
        }
    }

    std::cout << "转换后:   ";
    print_array(numbers);

    // 使用浮点数演示
    std::array<double, 6> doubles = {-1.5, 2.7, -3.14, 0.0, -99.9, 42.0};

    std::cout << "\n浮点数组原始: ";
    print_array(doubles);

    // 使用 std::abs 取绝对值
    for (auto& val : doubles) {
        if (val < 0.0) {
            val = std::abs(val);
        }
    }

    std::cout << "浮点数组转换: ";
    print_array(doubles);

    // 要点：range-for 使用 auto& 可以修改元素
    //       使用 const auto& 或 auto 则不能修改
    std::cout << "\n要点：\n";
    std::cout << "  for (auto& x : arr)       — 可以修改元素\n";
    std::cout << "  for (const auto& x : arr) — 只读访问\n";
    std::cout << "  for (auto x : arr)        — 拷贝，不影响原数组\n";

    return 0;
}
