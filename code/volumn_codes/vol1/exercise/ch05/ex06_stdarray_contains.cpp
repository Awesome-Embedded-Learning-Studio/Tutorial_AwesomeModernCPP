/**
 * @file ex06_stdarray_contains.cpp
 * @brief 练习：判断元素是否存在
 *
 * 实现 bool contains(const std::array<int, 5>&, int value)，
 * 使用 std::find 算法判断数组中是否包含指定值。
 */

#include <algorithm>
#include <array>
#include <iostream>

// 判断数组中是否包含指定值
bool contains(const std::array<int, 5>& arr, int value) {
    return std::find(arr.begin(), arr.end(), value) != arr.end();
}

int main() {
    std::cout << "===== 判断元素是否存在 =====\n\n";

    std::array<int, 5> numbers = {10, 25, 33, 47, 52};

    // 打印数组
    std::cout << "数组: [";
    for (std::size_t i = 0; i < numbers.size(); ++i) {
        std::cout << numbers[i];
        if (i + 1 < numbers.size()) {
            std::cout << ", ";
        }
    }
    std::cout << "]\n\n";

    // 测试各种值
    int test_values[] = {25, 42, 10, 99, 52};

    for (int val : test_values) {
        bool found = contains(numbers, val);
        std::cout << "  contains(" << val << ") = "
                  << (found ? "true" : "false") << "\n";
    }

    std::cout << "\n原理:\n";
    std::cout << "  std::find(begin, end, value) 返回指向找到元素的迭代器\n";
    std::cout << "  若返回 end() 则表示未找到\n";

    return 0;
}
