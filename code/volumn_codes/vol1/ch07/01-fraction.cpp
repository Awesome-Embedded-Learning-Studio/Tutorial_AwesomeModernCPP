// fraction.cpp
#include <iostream>

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

    Fraction& operator+=(const Fraction& rhs)
    {
        numerator_ = numerator_ * rhs.denominator_
                     + rhs.numerator_ * denominator_;
        denominator_ *= rhs.denominator_;
        normalize();
        return *this;
    }

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

    Fraction& operator/=(const Fraction& rhs)
    {
        numerator_ *= rhs.denominator_;
        denominator_ *= rhs.numerator_;
        if (denominator_ == 0) { denominator_ = 1; }
        normalize();
        return *this;
    }

    int num() const { return numerator_; }
    int den() const { return denominator_; }

    Fraction operator-() const { return Fraction(-numerator_, denominator_); }

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
        while (b != 0) { int t = b; b = a % b; a = t; }
        return (a == 0) ? 1 : a;
    }
};

// 二元算术（非成员）
Fraction operator+(Fraction lhs, const Fraction& rhs) { lhs += rhs; return lhs; }
Fraction operator-(Fraction lhs, const Fraction& rhs) { lhs -= rhs; return lhs; }
Fraction operator*(Fraction lhs, const Fraction& rhs) { lhs *= rhs; return lhs; }
Fraction operator/(Fraction lhs, const Fraction& rhs) { lhs /= rhs; return lhs; }

// 比较（非成员）
bool operator==(const Fraction& l, const Fraction& r)
{ return l.num() == r.num() && l.den() == r.den(); }
bool operator!=(const Fraction& l, const Fraction& r) { return !(l == r); }
bool operator<(const Fraction& l, const Fraction& r)
{ return l.num() * r.den() < r.num() * l.den(); }
bool operator>(const Fraction& l, const Fraction& r)  { return r < l; }
bool operator<=(const Fraction& l, const Fraction& r) { return !(r < l); }
bool operator>=(const Fraction& l, const Fraction& r) { return !(l < r); }

std::ostream& operator<<(std::ostream& os, const Fraction& f)
{ os << f.num() << "/" << f.den(); return os; }

int main()
{
    Fraction a(1, 2), b(1, 3);

    std::cout << a << " + " << b << " = " << (a + b) << std::endl;
    std::cout << a << " - " << b << " = " << (a - b) << std::endl;
    std::cout << a << " * " << b << " = " << (a * b) << std::endl;
    std::cout << a << " / " << b << " = " << (a / b) << std::endl;

    // 与整数的混合运算（隐式转换）
    std::cout << a << " + 1 = " << (a + 1) << std::endl;
    std::cout << "2 * " << b << " = " << (2 * b) << std::endl;

    a += b;
    std::cout << "a += b -> a = " << a << std::endl;

    Fraction c(1, 6), d(1, 4);
    std::cout << c << " == " << d << " : " << (c == d) << std::endl;
    std::cout << c << " < " << d << " : " << (c < d) << std::endl;
    std::cout << c << " >= " << d << " : " << (c >= d) << std::endl;

    Fraction e(3, 4);
    std::cout << "-" << e << " = " << (-e) << std::endl;

    return 0;
}
