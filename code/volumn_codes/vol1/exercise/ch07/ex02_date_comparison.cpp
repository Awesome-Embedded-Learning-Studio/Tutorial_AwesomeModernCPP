// ex02_date_comparison.cpp
// 练习：为 Date 类实现比较运算符
// 利用 C++20 的 operator<=>（或手动实现全部六个比较运算符），
// 按年、月、日逐级比较。这里使用 C++17 手动实现。
// 编译：g++ -Wall -Wextra -std=c++17 -o ex02 ex02_date_comparison.cpp

#include <iostream>
#include <tuple>

class Date {
private:
    int year_;
    int month_;
    int day_;

public:
    Date(int y, int m, int d) : year_(y), month_(m), day_(d) {}

    int year() const { return year_; }
    int month() const { return month_; }
    int day() const { return day_; }

    // 用 tuple 做逐级比较：年 > 月 > 日
    auto tie() const
    {
        return std::tie(year_, month_, day_);
    }

    friend bool operator<(const Date& lhs, const Date& rhs)
    {
        return lhs.tie() < rhs.tie();
    }
    friend bool operator<=(const Date& lhs, const Date& rhs)
    {
        return lhs.tie() <= rhs.tie();
    }
    friend bool operator>(const Date& lhs, const Date& rhs)
    {
        return lhs.tie() > rhs.tie();
    }
    friend bool operator>=(const Date& lhs, const Date& rhs)
    {
        return lhs.tie() >= rhs.tie();
    }
    friend bool operator==(const Date& lhs, const Date& rhs)
    {
        return lhs.tie() == rhs.tie();
    }
    friend bool operator!=(const Date& lhs, const Date& rhs)
    {
        return lhs.tie() != rhs.tie();
    }

    friend std::ostream& operator<<(std::ostream& os, const Date& d)
    {
        return os << d.year_ << "-" << d.month_ << "-" << d.day_;
    }
};

// ============================================================
// main
// ============================================================
int main()
{
    Date d1(2025, 3, 15);
    Date d2(2025, 3, 20);
    Date d3(2025, 3, 15);
    Date d4(2024, 12, 31);

    // <
    std::cout << d1 << " < " << d2 << " : " << (d1 < d2) << "\n";
    std::cout << d2 << " < " << d1 << " : " << (d2 < d1) << "\n";

    // ==
    std::cout << d1 << " == " << d3 << " : " << (d1 == d3) << "\n";
    std::cout << d1 << " == " << d2 << " : " << (d1 == d2) << "\n";

    // !=
    std::cout << d1 << " != " << d2 << " : " << (d1 != d2) << "\n";

    // <=
    std::cout << d1 << " <= " << d3 << " : " << (d1 <= d3) << "\n";
    std::cout << d1 << " <= " << d2 << " : " << (d1 <= d2) << "\n";

    // >
    std::cout << d2 << " > " << d1 << " : " << (d2 > d1) << "\n";

    // >=
    std::cout << d1 << " >= " << d4 << " : " << (d1 >= d4) << "\n";

    // 跨年比较
    std::cout << d4 << " < " << d1 << " : " << (d4 < d1) << "\n";

    return 0;
}
