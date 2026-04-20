// arrays.cpp
// C 风格数组综合演练：初始化、遍历、函数传参、矩阵操作

#include <iostream>

/// @brief 打印一维数组
void print_array(const int arr[], int size)
{
    for (int i = 0; i < size; ++i) {
        std::cout << arr[i];
        if (i < size - 1) {
            std::cout << ", ";
        }
    }
    std::cout << std::endl;
}

/// @brief 计算数组元素之和
int array_sum(const int arr[], int size)
{
    int total = 0;
    for (int i = 0; i < size; ++i) {
        total += arr[i];
    }
    return total;
}

/// @brief 打印矩阵（第二维固定为 4）
void print_matrix(const int matrix[][4], int rows)
{
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < 4; ++j) {
            std::cout << matrix[i][j] << "\t";
        }
        std::cout << std::endl;
    }
}

/// @brief 将 3x4 矩阵转置为 4x3 矩阵
void transpose_3x4(const int src[][4], int dst[][3])
{
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            dst[j][i] = src[i][j];
        }
    }
}

int main()
{
    // --- 初始化方式展示 ---
    std::cout << "=== 初始化方式 ===" << std::endl;

    int full_init[5] = {10, 20, 30, 40, 50};
    std::cout << "完全初始化: ";
    print_array(full_init, 5);

    int partial_init[5] = {1, 2};  // 后面自动填 0
    std::cout << "部分初始化: ";
    print_array(partial_init, 5);

    int zero_init[5] = {};  // 全部填 0
    std::cout << "零初始化:   ";
    print_array(zero_init, 5);

    int deduced[] = {2, 3, 5, 7, 11, 13};
    constexpr int kDeducedCount = sizeof(deduced) / sizeof(deduced[0]);
    std::cout << "大小推断:   ";
    print_array(deduced, kDeducedCount);
    std::cout << std::endl;

    // --- 遍历与求和 ---
    std::cout << "=== 遍历与求和 ===" << std::endl;
    int scores[] = {90, 85, 78, 92, 88};
    constexpr int kScoreCount = sizeof(scores) / sizeof(scores[0]);

    std::cout << "成绩: ";
    print_array(scores, kScoreCount);

    int total = array_sum(scores, kScoreCount);
    double average = static_cast<double>(total) / kScoreCount;
    std::cout << "总分: " << total << std::endl;
    std::cout << "均分: " << average << std::endl;
    std::cout << std::endl;

    // --- 矩阵操作 ---
    std::cout << "=== 矩阵操作 ===" << std::endl;
    int matrix[3][4] = {
        {1,  2,  3,  4},
        {5,  6,  7,  8},
        {9, 10, 11, 12}
    };

    std::cout << "原始矩阵 (3x4):" << std::endl;
    print_matrix(matrix, 3);

    int transposed[4][3] = {};
    transpose_3x4(matrix, transposed);

    std::cout << std::endl << "转置矩阵 (4x3):" << std::endl;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 3; ++j) {
            std::cout << transposed[i][j] << "\t";
        }
        std::cout << std::endl;
    }

    return 0;
}
