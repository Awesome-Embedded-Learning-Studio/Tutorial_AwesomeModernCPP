/**
 * @file ex04_stdarray_rewrite.cpp
 * @brief 练习：重写 C 数组练习
 *
 * 使用 std::array 重新实现数组基本操作：
 * 声明、初始化、遍历、传参给函数、查找最大值。
 */

#include <array>
#include <iostream>

// 用引用传递 std::array，避免拷贝
template <std::size_t N>
void print_array(const std::array<int, N>& arr) {
    std::cout << "[";
    for (std::size_t i = 0; i < N; ++i) {
        std::cout << arr[i];
        if (i + 1 < N) {
            std::cout << ", ";
        }
    }
    std::cout << "]";
}

// 查找数组中的最大值
template <std::size_t N>
int find_max(const std::array<int, N>& arr) {
    int max_val = arr[0];
    for (std::size_t i = 1; i < N; ++i) {
        if (arr[i] > max_val) {
            max_val = arr[i];
        }
    }
    return max_val;
}

// 计算数组元素之和
template <std::size_t N>
int array_sum(const std::array<int, N>& arr) {
    int sum = 0;
    for (const auto& elem : arr) {
        sum += elem;
    }
    return sum;
}

int main() {
    std::cout << "===== std::array 基本操作 =====\n\n";

    // 1. 声明与初始化
    std::array<int, 6> arr = {15, 32, 8, 47, 23, 11};

    // 2. 基本信息
    std::cout << "数组大小: " << arr.size() << "\n";
    std::cout << "是否为空: " << (arr.empty() ? "是" : "否") << "\n";

    // 3. 遍历 — 下标方式
    std::cout << "下标遍历: ";
    print_array(arr);
    std::cout << "\n";

    // 4. 遍历 — 范围 for 方式
    std::cout << "范围 for:  [";
    bool first = true;
    for (const auto& elem : arr) {
        if (!first) {
            std::cout << ", ";
        }
        std::cout << elem;
        first = false;
    }
    std::cout << "]\n";

    // 5. 传参给函数
    std::cout << "\n调用函数:\n";
    std::cout << "  最大值: " << find_max(arr) << "\n";
    std::cout << "  总  和: " << array_sum(arr) << "\n";

    // 6. 使用 at() 安全访问（带边界检查）
    std::cout << "\n安全访问:\n";
    std::cout << "  arr.at(0) = " << arr.at(0) << "\n";
    std::cout << "  arr.at(5) = " << arr.at(5) << "\n";
    // arr.at(6) 会抛出 std::out_of_range 异常

    // 7. fill 填充
    std::array<int, 5> filled;
    filled.fill(0);
    std::cout << "\nfill(0):  ";
    print_array(filled);
    std::cout << "\n";

    return 0;
}
