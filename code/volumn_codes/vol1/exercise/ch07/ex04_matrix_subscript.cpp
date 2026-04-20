// ex04_matrix_subscript.cpp
// 练习：实现 Matrix 的 operator[]
// 使用一维数组存储，通过 Row 代理类实现 matrix[i][j] 双重下标访问。
// 编译：g++ -Wall -Wextra -std=c++17 -o ex04 ex04_matrix_subscript.cpp

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

class Matrix {
private:
    std::vector<int> data_;
    std::size_t rows_;
    std::size_t cols_;

public:
    Matrix(std::size_t rows, std::size_t cols, int init = 0)
        : data_(rows * cols, init), rows_(rows), cols_(cols)
    {}

    std::size_t rows() const { return rows_; }
    std::size_t cols() const { return cols_; }

    // --- Row 代理类：支持 matrix[i][j] 的第二层下标 ---
    class Row {
    private:
        int* ptr_;
        std::size_t cols_;

    public:
        Row(int* ptr, std::size_t cols) : ptr_(ptr), cols_(cols) {}

        int& operator[](std::size_t col)
        {
            if (col >= cols_) {
                throw std::out_of_range("列下标越界");
            }
            return ptr_[col];
        }

        const int& operator[](std::size_t col) const
        {
            if (col >= cols_) {
                throw std::out_of_range("列下标越界");
            }
            return ptr_[col];
        }
    };

    // 可读写的 operator[]
    Row operator[](std::size_t row)
    {
        if (row >= rows_) {
            throw std::out_of_range("行下标越界");
        }
        return Row(data_.data() + row * cols_, cols_);
    }

    // 只读的 operator[]
    Row operator[](std::size_t row) const
    {
        if (row >= rows_) {
            throw std::out_of_range("行下标越界");
        }
        return Row(const_cast<int*>(data_.data()) + row * cols_, cols_);
    }

    // 打印矩阵（方便演示）
    void print() const
    {
        for (std::size_t i = 0; i < rows_; ++i) {
            for (std::size_t j = 0; j < cols_; ++j) {
                std::cout << (*this)[i][j];
                if (j + 1 < cols_) {
                    std::cout << "\t";
                }
            }
            std::cout << "\n";
        }
    }
};

// ============================================================
// main
// ============================================================
int main()
{
    Matrix m(3, 4, 0);

    // 填充数据：m[i][j] = i * 10 + j
    for (std::size_t i = 0; i < m.rows(); ++i) {
        for (std::size_t j = 0; j < m.cols(); ++j) {
            m[i][j] = static_cast<int>(i * 10 + j);
        }
    }

    std::cout << "矩阵内容:\n";
    m.print();

    // 修改单个元素
    m[1][2] = 99;
    std::cout << "\n修改 m[1][2] = 99 后:\n";
    m.print();

    // 越界测试
    try {
        (void)m[10][0];
    } catch (const std::out_of_range& e) {
        std::cout << "\n捕获越界异常: " << e.what() << "\n";
    }

    try {
        (void)m[0][100];
    } catch (const std::out_of_range& e) {
        std::cout << "捕获越界异常: " << e.what() << "\n";
    }

    return 0;
}
