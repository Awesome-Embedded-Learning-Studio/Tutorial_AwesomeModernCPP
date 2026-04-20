// io_overload.cpp
// 流运算符和下标运算符综合演练

#include <iostream>
#include <stdexcept>
#include <cmath>

class Fraction {
private:
    int numerator;
    int denominator;

    void reduce()
    {
        int a = std::abs(numerator);
        int b = std::abs(denominator);
        while (b != 0) {
            int temp = b;
            b = a % b;
            a = temp;
        }
        int gcd = (a != 0) ? a : 1;
        numerator /= gcd;
        denominator /= gcd;
        if (denominator < 0) {
            numerator = -numerator;
            denominator = -denominator;
        }
    }

public:
    Fraction(int num = 0, int denom = 1)
        : numerator(num), denominator(denom)
    {
        if (denominator == 0) {
            throw std::invalid_argument("分母不能为零");
        }
        reduce();
    }

    double to_double() const
    {
        return static_cast<double>(numerator) / denominator;
    }

    // 加法
    Fraction operator+(const Fraction& other) const
    {
        return Fraction(
            numerator * other.denominator + other.numerator * denominator,
            denominator * other.denominator
        );
    }

    // 输出流
    friend std::ostream& operator<<(std::ostream& os, const Fraction& f)
    {
        if (f.denominator == 1) {
            os << f.numerator;
        }
        else {
            os << f.numerator << "/" << f.denominator;
        }
        return os;
    }

    // 输入流
    friend std::istream& operator>>(std::istream& is, Fraction& f)
    {
        int num = 0;
        int denom = 1;
        char slash = '\0';

        is >> num >> slash >> denom;

        if (is && slash == '/' && denom != 0) {
            f.numerator = num;
            f.denominator = denom;
            f.reduce();
        }
        else {
            is.setstate(std::ios::failbit);
        }

        return is;
    }
};

class IntArray {
private:
    int* data;
    std::size_t count;

public:
    explicit IntArray(std::size_t n)
        : data(new int[n]()), count(n)
    {
    }

    ~IntArray() { delete[] data; }

    IntArray(const IntArray&) = delete;
    IntArray& operator=(const IntArray&) = delete;

    int& operator[](std::size_t index)
    {
        return data[index];
    }

    const int& operator[](std::size_t index) const
    {
        return data[index];
    }

    const int& at(std::size_t index) const
    {
        if (index >= count) {
            throw std::out_of_range("IntArray::at: index out of range");
        }
        return data[index];
    }

    std::size_t size() const { return count; }

    /// @brief 打印所有元素
    void print(std::ostream& os = std::cout) const
    {
        os << "[";
        for (std::size_t i = 0; i < count; ++i) {
            os << data[i];
            if (i + 1 < count) {
                os << ", ";
            }
        }
        os << "]";
    }
};

int main()
{
    // --- Fraction 输出演示 ---
    Fraction a(3, 4);
    Fraction b(2, 6);   // 自动约分为 1/3
    Fraction c(6, 1);   // 整数形式

    std::cout << "a = " << a << std::endl;    // 3/4
    std::cout << "b = " << b << std::endl;    // 1/3
    std::cout << "c = " << c << std::endl;    // 6
    std::cout << "a + b = " << (a + b) << std::endl;  // 13/12
    std::cout << "a (double) = " << a.to_double() << std::endl;  // 0.75
    std::cout << std::endl;

    // --- IntArray 下标访问演示 ---
    IntArray arr(5);
    for (std::size_t i = 0; i < arr.size(); ++i) {
        arr[i] = static_cast<int>(i * 10);  // 通过 [] 写入
    }

    std::cout << "arr = ";
    arr.print();
    std::cout << std::endl;

    const IntArray& const_arr = arr;
    std::cout << "const_arr[2] = " << const_arr[2] << std::endl;  // 20

    // 边界检查
    try {
        std::cout << "arr.at(10) = " << arr.at(10) << std::endl;
    }
    catch (const std::out_of_range& e) {
        std::cout << "捕获异常: " << e.what() << std::endl;
    }

    return 0;
}
