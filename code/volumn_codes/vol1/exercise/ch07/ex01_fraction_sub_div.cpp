// ex01_fraction_sub_div.cpp
// 练习：补全 Fraction 的减法和除法
// 实现 operator-= 和 operator/=，包含除零检查；
// 再通过复合赋值实现二元 operator- 和 operator/。
// 编译：g++ -Wall -Wextra -std=c++17 -o ex01 ex01_fraction_sub_div.cpp

#include <iostream>
#include <stdexcept>

class Fraction {
private:
    int numerator_;
    int denominator_;

public:
    Fraction(int num = 0, int den = 1)
        : numerator_(num), denominator_(den)
    {
        if (denominator_ == 0) {
            throw std::invalid_argument("分母不能为零");
        }
        normalize();
    }

    // 取分子/分母
    int num() const { return numerator_; }
    int den() const { return denominator_; }

    // --- 复合赋值运算符 ---

    Fraction& operator+=(const Fraction& rhs)
    {
        numerator_ = numerator_ * rhs.denominator_
                     + rhs.numerator_ * denominator_;
        denominator_ *= rhs.denominator_;
        normalize();
        return *this;
    }

    // 减法复合赋值
    Fraction& operator-=(const Fraction& rhs)
    {
        numerator_ = numerator_ * rhs.denominator_
                     - rhs.numerator_ * denominator_;
        denominator_ *= rhs.denominator_;
        normalize();
        return *this;
    }

    Fraction& operator*=(const Fraction& rhs)
    {
        numerator_ *= rhs.numerator_;
        denominator_ *= rhs.denominator_;
        normalize();
        return *this;
    }

    // 除法复合赋值——需要检查除数是否为零
    Fraction& operator/=(const Fraction& rhs)
    {
        if (rhs.numerator_ == 0) {
            throw std::runtime_error("除以零分数");
        }
        numerator_ *= rhs.denominator_;
        denominator_ *= rhs.numerator_;
        normalize();
        return *this;
    }

private:
    // 约分 + 保证分母为正
    void normalize()
    {
        int g = gcd(numerator_, denominator_);
        numerator_ /= g;
        denominator_ /= g;
        if (denominator_ < 0) {
            numerator_ = -numerator_;
            denominator_ = -denominator_;
        }
    }

    static int gcd(int a, int b)
    {
        a = (a < 0) ? -a : a;
        b = (b < 0) ? -b : b;
        while (b != 0) {
            int t = b;
            b = a % b;
            a = t;
        }
        return (a == 0) ? 1 : a;
    }

    // 二元运算符通过复合赋值实现（非成员友元）
    friend Fraction operator+(Fraction lhs, const Fraction& rhs)
    {
        lhs += rhs;
        return lhs;
    }
    friend Fraction operator-(Fraction lhs, const Fraction& rhs)
    {
        lhs -= rhs;
        return lhs;
    }
    friend Fraction operator*(Fraction lhs, const Fraction& rhs)
    {
        lhs *= rhs;
        return lhs;
    }
    friend Fraction operator/(Fraction lhs, const Fraction& rhs)
    {
        lhs /= rhs;
        return lhs;
    }

    friend std::ostream& operator<<(std::ostream& os, const Fraction& f)
    {
        return os << f.num() << "/" << f.den();
    }
};

// ============================================================
// main
// ============================================================
int main()
{
    Fraction a(3, 4);
    Fraction b(1, 6);

    // 减法
    Fraction diff = a - b;
    std::cout << a << " - " << b << " = " << diff << "\n";

    // 除法
    Fraction quot = a / b;
    std::cout << a << " / " << b << " = " << quot << "\n";

    // 复合赋值
    Fraction c(5, 8);
    c -= Fraction(1, 8);
    std::cout << "5/8 - 1/8 = " << c << "\n";

    Fraction d(2, 3);
    d /= Fraction(4);
    std::cout << "2/3 / 4 = " << d << "\n";

    // 除零检查
    try {
        Fraction zero(0, 1);
        Fraction bad = a / zero;
        (void)bad;  // 永远不会执行到这里
    } catch (const std::runtime_error& e) {
        std::cout << "捕获除零异常: " << e.what() << "\n";
    }

    // 构造时零分母检查
    try {
        Fraction bad(1, 0);
        (void)bad;
    } catch (const std::invalid_argument& e) {
        std::cout << "捕获构造异常: " << e.what() << "\n";
    }

    return 0;
}
