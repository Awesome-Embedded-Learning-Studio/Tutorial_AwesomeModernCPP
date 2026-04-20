/**
 * @file ex01_find_index.cpp
 * @brief 练习：函数模板 — 在数组中查找元素下标
 *
 * 编写 find_index<T, kSize>(const T (&arr)[kSize], const T& target)，
 * 返回目标元素的下标；未找到返回 -1。
 * 分别用 int[]、double[]、std::string[] 进行测试。
 */

#include <iostream>
#include <string>

/**
 * @brief 在定长数组中线性查找目标元素
 *
 * @tparam T       元素类型
 * @tparam kSize   数组长度（编译期推导）
 * @param arr      数组引用
 * @param target   目标值
 * @return int     目标下标，未找到返回 -1
 */
template <typename T, std::size_t kSize>
int find_index(const T (&arr)[kSize], const T& target)
{
    for (std::size_t i = 0; i < kSize; ++i) {
        if (arr[i] == target) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// ============================================================
// main
// ============================================================
int main()
{
    std::cout << "===== ex01: find_index 函数模板 =====\n\n";

    // --- int 数组 ---
    int int_arr[] = {10, 20, 30, 40, 50};
    std::cout << "int 数组查找:\n";
    std::cout << "  find_index(30) = "
              << find_index(int_arr, 30) << "\n";
    std::cout << "  find_index(99) = "
              << find_index(int_arr, 99) << "\n\n";

    // --- double 数组 ---
    double dbl_arr[] = {1.1, 2.2, 3.3, 4.4};
    std::cout << "double 数组查找:\n";
    std::cout << "  find_index(3.3) = "
              << find_index(dbl_arr, 3.3) << "\n";
    std::cout << "  find_index(0.0) = "
              << find_index(dbl_arr, 0.0) << "\n\n";

    // --- std::string 数组 ---
    std::string str_arr[] = {
        std::string("alpha"),
        std::string("bravo"),
        std::string("charlie")
    };
    std::cout << "string 数组查找:\n";
    std::cout << "  find_index(\"bravo\")   = "
              << find_index(str_arr, std::string("bravo")) << "\n";
    std::cout << "  find_index(\"delta\")   = "
              << find_index(str_arr, std::string("delta")) << "\n";

    return 0;
}
