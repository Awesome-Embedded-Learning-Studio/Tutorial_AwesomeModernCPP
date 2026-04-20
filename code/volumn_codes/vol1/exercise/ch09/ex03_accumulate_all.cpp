/**
 * @file ex03_accumulate_all.cpp
 * @brief 练习：模板与可选累加器类型参数
 *
 * accumulate_all<T, kSize, Accumulator> 将数组所有元素累加求和。
 * Accumulator 默认为 T，但可显式指定为 long long 以避免 int 溢出。
 * 分别测试默认累加与 long long 累加。
 */

#include <iostream>
#include <string>

/**
 * @brief 对定长数组的所有元素求和
 *
 * @tparam T          元素类型
 * @tparam kSize      数组长度
 * @tparam Accumulator 累加结果类型，默认与 T 相同
 * @param arr         数组引用
 * @return Accumulator 累加结果
 */
template <typename T, std::size_t kSize, typename Accumulator = T>
Accumulator accumulate_all(const T (&arr)[kSize])
{
    Accumulator sum = Accumulator{};
    for (std::size_t i = 0; i < kSize; ++i) {
        sum += static_cast<Accumulator>(arr[i]);
    }
    return sum;
}

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== ex03: accumulate_all 模板 =====\n\n";

    // --- int 数组，默认累加类型为 int ---
    {
        int arr[] = {1, 2, 3, 4, 5};
        std::cout << "int 数组 (默认累加):\n";
        std::cout << "  {1,2,3,4,5} => "
                  << accumulate_all(arr) << "\n\n";
    }

    // --- int 数组，显式使用 long long 累加 ---
    {
        int arr[] = {100000, 200000, 300000, 400000, 500000};
        std::cout << "int 数组 (long long 累加):\n";
        std::cout << "  {100000,200000,...,500000} => "
                  << accumulate_all<int, 5, long long>(arr)
                  << "\n\n";
    }

    // --- double 数组，默认累加 ---
    {
        double arr[] = {1.5, 2.5, 3.5};
        std::cout << "double 数组 (默认累加):\n";
        std::cout << "  {1.5,2.5,3.5} => "
                  << accumulate_all(arr) << "\n\n";
    }

    // --- double 数组，累加为 int（截断演示）---
    {
        double arr[] = {1.9, 2.9, 3.9};
        std::cout << "double 数组 (int 累加，截断):\n";
        std::cout << "  {1.9,2.9,3.9} => "
                  << accumulate_all<double, 3, int>(arr) << "\n";
    }

    return 0;
}
