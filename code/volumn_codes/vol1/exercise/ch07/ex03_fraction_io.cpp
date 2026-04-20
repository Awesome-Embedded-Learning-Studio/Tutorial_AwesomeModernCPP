// ex03_fraction_io.cpp
// 练习：给 Fraction 添加流运算符
// operator<<：分母为 1 时只输出分子；否则输出 "num/den"。
// operator>>：以 "num/den" 格式读取，格式错误时设置 failbit。
// 编译：g++ -Wall -Wextra -std=c++17 -o ex03 ex03_fraction_io.cpp

#include <iostream>
#include <sstream>
#include <string>

class Fraction {
private:
    int numerator_;
    int denominator_;

public:
    Fraction(int num = 0, int den = 1)
        : numerator_(num), denominator_(den)
    {
        if (denominator_ == 0) { denominator_ = 1; }
        normalize();
    }

    int num() const { return numerator_; }
    int den() const { return denominator_; }

    friend std::ostream& operator<<(std::ostream& os, const Fraction& f)
    {
        // 分母为 1 时只输出分子
        if (f.denominator_ == 1) {
            os << f.numerator_;
        } else {
            os << f.numerator_ << "/" << f.denominator_;
        }
        return os;
    }

    friend std::istream& operator>>(std::istream& is, Fraction& f)
    {
        int num = 0;
        int den = 1;
        char slash = '\0';

        // 尝试读取 "num/den" 格式
        if (is >> num) {
            // 看下一个字符是不是 '/'
            if (is.peek() == '/') {
                is >> slash;  // 消耗 '/'
                if (is >> den) {
                    if (den == 0) {
                        // 分母为零，设置 failbit
                        is.setstate(std::ios::failbit);
                        return is;
                    }
                    f = Fraction(num, den);
                }
                // else: 读取分母失败，流已经处于失败状态
            } else {
                // 没有 '/'，视为整数
                f = Fraction(num, 1);
            }
        }
        return is;
    }

private:
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
};

// ============================================================
// main
// ============================================================
int main()
{
    // --- operator<< 测试 ---
    std::cout << "=== operator<< 测试 ===\n";

    Fraction a(3, 4);
    Fraction b(5, 1);   // 分母为 1
    Fraction c(0, 7);

    std::cout << "3/4 输出: " << a << "\n";
    std::cout << "5/1 输出: " << b << "\n";
    std::cout << "0/7 输出: " << c << "\n";

    // --- operator>> 测试：正常输入 ---
    std::cout << "\n=== operator>> 测试（正常） ===\n";

    std::istringstream iss1("3/4");
    Fraction f1;
    iss1 >> f1;
    std::cout << "读取 \"3/4\" -> " << f1 << "\n";

    std::istringstream iss2("7");
    Fraction f2;
    iss2 >> f2;
    std::cout << "读取 \"7\"   -> " << f2 << "\n";

    std::istringstream iss3("-2/5");
    Fraction f3;
    iss3 >> f3;
    std::cout << "读取 \"-2/5\" -> " << f3 << "\n";

    // --- operator>> 测试：错误输入 ---
    std::cout << "\n=== operator>> 测试（错误） ===\n";

    std::istringstream iss4("3/0");
    Fraction f4;
    iss4 >> f4;
    std::cout << "读取 \"3/0\" failbit = " << iss4.fail() << "\n";

    std::istringstream iss5("abc");
    Fraction f5;
    iss5 >> f5;
    std::cout << "读取 \"abc\" failbit = " << iss5.fail() << "\n";

    return 0;
}
