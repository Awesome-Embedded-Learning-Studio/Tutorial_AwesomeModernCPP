/**
 * @file ex05_find_max.cpp
 * @brief 练习：找最大值
 *
 * 使用 range-for 循环遍历 std::array<int, 8>，找到数组中的最大值。
 */

#include <array>
#include <iostream>

int main() {
    std::array<int, 8> numbers = {3, 17, -5, 42, 8, 0, 23, 11};

    std::cout << "数组内容：";
    for (const auto& num : numbers) {
        std::cout << num << ' ';
    }
    std::cout << '\n';

    // 使用 range-for 查找最大值
    // 初始假设第一个元素为最大值
    int max_val = numbers[0];
    for (const auto& num : numbers) {
        if (num > max_val) {
            max_val = num;
        }
    }

    std::cout << "最大值: " << max_val << '\n';

    // 也可以用 range-for 同时查找最大值和最小值
    int min_val = numbers[0];
    max_val = numbers[0];
    for (const auto& num : numbers) {
        if (num > max_val) {
            max_val = num;
        }
        if (num < min_val) {
            min_val = num;
        }
    }

    std::cout << "最小值: " << min_val << '\n';
    std::cout << "最大值: " << max_val << '\n';

    return 0;
}
