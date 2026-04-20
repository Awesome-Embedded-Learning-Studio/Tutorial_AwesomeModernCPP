/**
 * @file ex02_matrix_transpose.cpp
 * @brief 练习：矩阵转置
 *
 * 使用 C 风格二维数组存储一个 2x3 矩阵，
 * 将其转置为 3x2 矩阵并打印结果。
 */

#include <iostream>

// 打印 rows x cols 的矩阵
void print_matrix(const int* mat, int rows, int cols) {
    for (int i = 0; i < rows; ++i) {
        std::cout << "  [";
        for (int j = 0; j < cols; ++j) {
            std::cout << mat[i * cols + j];
            if (j + 1 < cols) {
                std::cout << ", ";
            }
        }
        std::cout << "]\n";
    }
}

int main() {
    std::cout << "===== 矩阵转置 =====\n\n";

    // 原始矩阵：2 行 3 列
    constexpr int kRows = 2;
    constexpr int kCols = 3;
    int matrix[kRows][kCols] = {
        {1, 2, 3},
        {4, 5, 6}
    };

    // 转置结果：3 行 2 列
    constexpr int kTRows = kCols;  // 转置后行数 = 原列数
    constexpr int kTCols = kRows;  // 转置后列数 = 原行数
    int transposed[kTRows][kTCols];

    // 执行转置：transposed[j][i] = matrix[i][j]
    for (int i = 0; i < kRows; ++i) {
        for (int j = 0; j < kCols; ++j) {
            transposed[j][i] = matrix[i][j];
        }
    }

    // 打印原始矩阵
    std::cout << "原始矩阵 (" << kRows << "x" << kCols << "):\n";
    for (int i = 0; i < kRows; ++i) {
        std::cout << "  [";
        for (int j = 0; j < kCols; ++j) {
            std::cout << matrix[i][j];
            if (j + 1 < kCols) {
                std::cout << ", ";
            }
        }
        std::cout << "]\n";
    }

    std::cout << "\n";

    // 打印转置矩阵
    std::cout << "转置矩阵 (" << kTRows << "x" << kTCols << "):\n";
    for (int i = 0; i < kTRows; ++i) {
        std::cout << "  [";
        for (int j = 0; j < kTCols; ++j) {
            std::cout << transposed[i][j];
            if (j + 1 < kTCols) {
                std::cout << ", ";
            }
        }
        std::cout << "]\n";
    }

    return 0;
}
