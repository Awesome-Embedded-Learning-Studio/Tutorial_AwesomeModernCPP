/**
 * @file ex03_date_class.cpp
 * @brief 练习：实现 Date 类
 *
 * 实现 year/month/day 的 Date 类：
 * - 默认构造 (2000/1/1)
 * - 带参数构造（含日期合法性验证）
 * - print() 方法
 */

#include <iostream>

class Date {
  private:
    int year_;
    int month_;
    int day_;

    // 判断是否闰年
    static bool is_leap_year(int year) {
        return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    }

    // 获取某月的天数
    static int days_in_month(int year, int month) {
        constexpr int kDays[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month == 2 && is_leap_year(year)) {
            return 29;
        }
        return kDays[month];
    }

    // 验证日期合法性
    static bool is_valid_date(int year, int month, int day) {
        if (year < 1 || month < 1 || month > 12 || day < 1) {
            return false;
        }
        return day <= days_in_month(year, month);
    }

  public:
    // 默认构造：2000/1/1
    Date() : year_(2000), month_(1), day_(1) {}

    // 带参数构造（含验证）
    Date(int year, int month, int day) : year_(2000), month_(1), day_(1) {
        set_date(year, month, day);
    }

    // 设置日期，返回是否成功
    bool set_date(int year, int month, int day) {
        if (!is_valid_date(year, month, day)) {
            return false;
        }
        year_ = year;
        month_ = month;
        day_ = day;
        return true;
    }

    void print() const {
        std::cout << year_ << "/" << (month_ < 10 ? "0" : "") << month_ << "/"
                  << (day_ < 10 ? "0" : "") << day_;
    }

    // Getter
    int year() const { return year_; }
    int month() const { return month_; }
    int day() const { return day_; }
};

// 测试辅助
void test_date(int y, int m, int d) {
    Date date(y, m, d);
    std::cout << "  Date(" << y << ", " << m << ", " << d << ") -> ";
    date.print();
    std::cout << "\n";
}

int main() {
    std::cout << "===== Date 类 =====\n\n";

    // 默认构造
    Date default_date;
    std::cout << "默认构造: ";
    default_date.print();
    std::cout << "\n\n";

    // 合法日期
    std::cout << "合法日期:\n";
    test_date(2024, 1, 15);
    test_date(2024, 2, 29); // 2024 是闰年
    test_date(2000, 2, 29); // 2000 是闰年
    test_date(2023, 12, 31);

    // 非法日期
    std::cout << "\n非法日期 (回退到默认值):\n";
    test_date(2023, 2, 29); // 2023 非闰年
    test_date(2023, 13, 1); // 月份越界
    test_date(2023, 0, 1);  // 月份为 0
    test_date(2023, 6, 31); // 6 月没有 31 日
    test_date(-1, 1, 1);    // 负年份

    // 闰年验证
    std::cout << "\n闰年测试:\n";
    std::cout << "  2000 是闰年 (能被400整除)\n";
    test_date(2000, 2, 29);
    std::cout << "  1900 不是闰年 (能被100但不能被400整除)\n";
    test_date(1900, 2, 29);

    return 0;
}
