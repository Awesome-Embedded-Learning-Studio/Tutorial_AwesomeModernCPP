/**
 * @file ex07_ref_doubles.cpp
 * @brief 练习：改造指针函数
 *
 * 将 C 风格的「翻倍数组元素」函数从指针参数
 * 改造为使用 std::array<int, N> + 引用传递的版本。
 */

#include <array>
#include <iostream>

// C 风格版本：使用指针和长度
void double_elements_c(int* arr, int size) {
    for (int i = 0; i < size; ++i) {
        arr[i] *= 2;
    }
}

// C++ 版本：使用 std::array 引用，编译期已知大小
template <std::size_t N>
void double_elements_cpp(std::array<int, N>& arr) {
    for (auto& elem : arr) {
        elem *= 2;
    }
}

// 辅助：打印 C 数组
void print_c_array(const int* arr, int size) {
    std::cout << "[";
    for (int i = 0; i < size; ++i) {
        std::cout << arr[i];
        if (i + 1 < size) {
            std::cout << ", ";
        }
    }
    std::cout << "]";
}

// 辅助：打印 std::array
template <std::size_t N>
void print_std_array(const std::array<int, N>& arr) {
    std::cout << "[";
    for (std::size_t i = 0; i < N; ++i) {
        std::cout << arr[i];
        if (i + 1 < N) {
            std::cout << ", ";
        }
    }
    std::cout << "]";
}

int main() {
    std::cout << "===== 改造指针函数为引用版本 =====\n\n";

    // C 风格演示
    int c_arr[] = {1, 2, 3, 4, 5};
    std::cout << "C 风格:\n";
    std::cout << "  原始: ";
    print_c_array(c_arr, 5);
    std::cout << "\n";

    double_elements_c(c_arr, 5);
    std::cout << "  翻倍: ";
    print_c_array(c_arr, 5);
    std::cout << "\n\n";

    // C++ 风格演示
    std::array<int, 5> cpp_arr = {1, 2, 3, 4, 5};
    std::cout << "C++ 风格 (std::array + 引用):\n";
    std::cout << "  原始: ";
    print_std_array(cpp_arr);
    std::cout << "\n";

    double_elements_cpp(cpp_arr);
    std::cout << "  翻倍: ";
    print_std_array(cpp_arr);
    std::cout << "\n\n";

    std::cout << "对比:\n";
    std::cout << "  C 风格: 需要额外传递 size 参数，可能越界\n";
    std::cout << "  C++ 风格: 大小由模板参数保证，引用传递无拷贝开销\n";

    return 0;
}
