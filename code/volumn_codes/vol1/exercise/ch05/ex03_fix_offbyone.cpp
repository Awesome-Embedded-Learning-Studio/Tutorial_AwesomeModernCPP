/**
 * @file ex03_fix_offbyone.cpp
 * @brief 练习：修复越界 bug
 *
 * 展示经典的差一错误（off-by-one）：
 * 循环条件写成 i <= 5 而非 i < 5，导致数组越界访问。
 * 给出错误版本和修复版本。
 */

#include <iostream>

int main() {
    std::cout << "===== 修复越界 bug =====\n\n";

    int arr[5] = {10, 20, 30, 40, 50};

    // 错误版本（已注释，避免未定义行为）:
    // std::cout << "错误版本 (i <= 5):\n";
    // for (int i = 0; i <= 5; ++i) {  // Bug! 当 i=5 时越界
    //     std::cout << "  arr[" << i << "] = " << arr[i] << "\n";
    // }
    // arr[5] 不属于数组，访问是未定义行为

    // 修复版本：条件改为 i < 5
    std::cout << "修复版本 (i < 5):\n";
    for (int i = 0; i < 5; ++i) {
        std::cout << "  arr[" << i << "] = " << arr[i] << "\n";
    }

    std::cout << "\n更多越界陷阱:\n";

    // 常见错误 1：数组大小与循环上限不匹配
    constexpr int kSize = 5;
    int data[kSize] = {1, 2, 3, 4, 5};

    std::cout << "  正确: for (int i = 0; i < " << kSize << "; ++i)\n";

    // 常见错误 2：混淆 < 和 <=
    std::cout << "  记住: 长度为 N 的数组，有效下标是 0 ~ N-1\n";

    // 常见错误 3：使用 sizeof 计算元素个数
    std::cout << "  正确计算元素个数: sizeof(arr)/sizeof(arr[0]) = "
              << sizeof(data) / sizeof(data[0]) << "\n";

    // C++17 推荐做法
    std::cout << "\nC++17 推荐使用 std::size():\n";
    std::cout << "  std::size(data) = " << std::size(data) << "\n";

    return 0;
}
