/**
 * @file ex01_array_sum_avg.cpp
 * @brief 练习：数组求和与均值
 *
 * 声明一个 10 元素的 int 数组，分别实现求和函数与求均值函数，
 * 均值函数返回 double 类型。
 */

#include <iostream>

// 计算数组所有元素之和
int array_sum(const int* arr, std::size_t size) {
    int sum = 0;
    for (std::size_t i = 0; i < size; ++i) {
        sum += arr[i];
    }
    return sum;
}

// 计算数组均值，返回 double 以保留小数
double array_average(const int* arr, std::size_t size) {
    if (size == 0) {
        return 0.0;
    }
    return static_cast<double>(array_sum(arr, size))
           / static_cast<double>(size);
}

int main() {
    int scores[] = {85, 92, 78, 90, 88, 76, 95, 89, 83, 91};
    constexpr std::size_t kSize = sizeof(scores) / sizeof(scores[0]);

    std::cout << "===== 数组求和与均值 =====\n\n";
    std::cout << "成绩: [";
    for (std::size_t i = 0; i < kSize; ++i) {
        std::cout << scores[i];
        if (i + 1 < kSize) {
            std::cout << ", ";
        }
    }
    std::cout << "]\n\n";

    int sum = array_sum(scores, kSize);
    double avg = array_average(scores, kSize);

    std::cout << "总和: " << sum << "\n";
    std::cout << "均值: " << avg << "\n";

    return 0;
}
