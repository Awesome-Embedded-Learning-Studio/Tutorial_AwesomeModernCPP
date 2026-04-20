/**
 * @file ex02_bubble_sort.cpp
 * @brief 练习：函数模板 — 冒泡排序
 *
 * 编写 bubble_sort<T, kSize> 对定长数组做原地冒泡排序。
 * 分别测试 int、double、std::string 数组，排序前后打印。
 */

#include <iostream>
#include <string>

/**
 * @brief 打印定长数组的所有元素
 */
template <typename T, std::size_t kSize>
void print_array(const T (&arr)[kSize])
{
    std::cout << "  [";
    for (std::size_t i = 0; i < kSize; ++i) {
        std::cout << arr[i];
        if (i + 1 < kSize) {
            std::cout << ", ";
        }
    }
    std::cout << "]\n";
}

/**
 * @brief 对定长数组执行冒泡排序（升序）
 *
 * @tparam T       元素类型，须支持 operator<
 * @tparam kSize   数组长度
 * @param arr      待排序数组
 */
template <typename T, std::size_t kSize>
void bubble_sort(T (&arr)[kSize])
{
    for (std::size_t i = 0; i < kSize - 1; ++i) {
        bool swapped = false;
        for (std::size_t j = 0; j < kSize - 1 - i; ++j) {
            if (arr[j + 1] < arr[j]) {
                T tmp = std::move(arr[j]);
                arr[j] = std::move(arr[j + 1]);
                arr[j + 1] = std::move(tmp);
                swapped = true;
            }
        }
        // 本轮无交换，已经有序
        if (!swapped) {
            break;
        }
    }
}

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== ex02: bubble_sort 函数模板 =====\n\n";

    // --- int 数组 ---
    {
        int arr[] = {5, 2, 9, 1, 5, 6};
        std::cout << "int 数组:\n";
        std::cout << "  排序前: ";
        print_array(arr);
        bubble_sort(arr);
        std::cout << "  排序后: ";
        print_array(arr);
        std::cout << "\n";
    }

    // --- double 数组 ---
    {
        double arr[] = {3.14, 1.41, 2.72, 0.58, 1.73};
        std::cout << "double 数组:\n";
        std::cout << "  排序前: ";
        print_array(arr);
        bubble_sort(arr);
        std::cout << "  排序后: ";
        print_array(arr);
        std::cout << "\n";
    }

    // --- std::string 数组 ---
    {
        std::string arr[] = {
            std::string("charlie"),
            std::string("alpha"),
            std::string("echo"),
            std::string("bravo"),
            std::string("delta")
        };
        std::cout << "string 数组:\n";
        std::cout << "  排序前: ";
        print_array(arr);
        bubble_sort(arr);
        std::cout << "  排序后: ";
        print_array(arr);
    }

    return 0;
}
