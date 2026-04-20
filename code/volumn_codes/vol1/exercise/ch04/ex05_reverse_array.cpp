/**
 * @file ex05_reverse_array.cpp
 * @brief 练习：双指针反转数组
 *
 * 实现 void reverse_array(int* begin, int* end)，
 * 使用首尾双指针技术原地反转数组元素。
 * end 指向最后一个元素的下一位置（STL 风格）。
 */

#include <iostream>

// 原地反转 [begin, end) 区间内的元素
void reverse_array(int* begin, int* end) {
    if (begin == nullptr || end == nullptr || begin >= end) {
        return;
    }

    int* left = begin;
    int* right = end - 1;  // right 指向最后一个元素

    while (left < right) {
        // 交换左右指针所指的元素
        int temp = *left;
        *left = *right;
        *right = temp;
        ++left;
        --right;
    }
}

// 辅助：打印数组
void print_array(const int* arr, std::size_t size) {
    std::cout << "[";
    for (std::size_t i = 0; i < size; ++i) {
        std::cout << arr[i];
        if (i + 1 < size) {
            std::cout << ", ";
        }
    }
    std::cout << "]";
}

int main() {
    std::cout << "===== 双指针反转数组 =====\n\n";

    // 测试 1：偶数个元素
    int arr1[] = {1, 2, 3, 4, 5, 6};
    std::cout << "测试 1 (偶数个元素):\n";
    std::cout << "  反转前: ";
    print_array(arr1, 6);
    std::cout << "\n";

    reverse_array(arr1, arr1 + 6);
    std::cout << "  反转后: ";
    print_array(arr1, 6);
    std::cout << "\n\n";

    // 测试 2：奇数个元素
    int arr2[] = {10, 20, 30, 40, 50};
    std::cout << "测试 2 (奇数个元素):\n";
    std::cout << "  反转前: ";
    print_array(arr2, 5);
    std::cout << "\n";

    reverse_array(arr2, arr2 + 5);
    std::cout << "  反转后: ";
    print_array(arr2, 5);
    std::cout << "\n\n";

    // 测试 3：空数组 / 单元素
    int arr3[] = {42};
    std::cout << "测试 3 (单元素):\n";
    std::cout << "  反转前: ";
    print_array(arr3, 1);
    std::cout << "\n";
    reverse_array(arr3, arr3 + 1);
    std::cout << "  反转后: ";
    print_array(arr3, 1);
    std::cout << "\n\n";

    // 测试 4：双指针再反转一次应恢复原序
    int arr4[] = {1, 2, 3, 4, 5};
    reverse_array(arr4, arr4 + 5);  // 反转
    reverse_array(arr4, arr4 + 5);  // 再反转回来
    std::cout << "测试 4 (双重反转恢复):\n";
    std::cout << "  结果: ";
    print_array(arr4, 5);
    std::cout << "\n";

    return 0;
}
