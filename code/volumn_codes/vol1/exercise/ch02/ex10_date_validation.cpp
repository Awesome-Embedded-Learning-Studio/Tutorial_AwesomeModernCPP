/**
 * @file ex10_date_validation.cpp
 * @brief 练习：日期合法性检查
 *
 * 接收年、月、日三个整数，判断日期是否合法：
 *   - 月份范围 1-12
 *   - 每月天数上限不同
 *   - 闰年的二月有 29 天
 *
 * 闰年规则：能被 4 整除但不能被 100 整除，或能被 400 整除。
 */

#include <iostream>

// 判断是否为闰年
bool is_leap_year(int year) {
    // 闰年：能被 4 整除且不能被 100 整除，或者能被 400 整除
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

// 获取某年某月的天数
int days_in_month(int year, int month) {
    switch (month) {
        case 1:  // 一月
        case 3:  // 三月
        case 5:  // 五月
        case 7:  // 七月
        case 8:  // 八月
        case 10: // 十月
        case 12: // 十二月
            return 31;
        case 4:  // 四月
        case 6:  // 六月
        case 9:  // 九月
        case 11: // 十一月
            return 30;
        case 2:  // 二月
            return is_leap_year(year) ? 29 : 28;
        default:
            return 0;  // 无效月份
    }
}

// 验证日期合法性
bool is_valid_date(int year, int month, int day) {
    // 检查月份范围
    if (month < 1 || month > 12) {
        return false;
    }

    // 检查天数范围
    if (day < 1) {
        return false;
    }

    // 检查天数是否超过当月最大天数
    int max_day = days_in_month(year, month);
    if (day > max_day) {
        return false;
    }

    return true;
}

int main() {
    std::cout << "===== ex10: 日期合法性检查 =====\n\n";

    // 用户输入
    int year = 0, month = 0, day = 0;
    std::cout << "请输入日期（年 月 日，用空格分隔）: ";
    std::cin >> year >> month >> day;

    bool valid = is_valid_date(year, month, day);
    std::cout << year << "-" << month << "-" << day
              << (valid ? " 是" : " 不是") << "合法日期\n";

    if (valid) {
        std::cout << "该月有 " << days_in_month(year, month) << " 天\n";
        if (is_leap_year(year)) {
            std::cout << year << " 年是闰年\n";
        } else {
            std::cout << year << " 年不是闰年\n";
        }
    }

    // 批量测试
    std::cout << "\n===== 批量测试 =====\n";

    struct TestCase {
        int y, m, d;
        bool expected;
    };

    TestCase tests[] = {
        {2024, 2, 29, true},   // 闰年二月 29 天
        {2023, 2, 29, false},  // 平年二月没有 29 天
        {2024, 2, 28, true},   // 闰年二月 28 天也合法
        {2023, 4, 30, true},   // 四月 30 天
        {2023, 4, 31, false},  // 四月没有 31 天
        {2023, 1, 31, true},   // 一月 31 天
        {2023, 6, 0, false},   // 天数为 0
        {2023, 13, 1, false},  // 月份超出范围
        {2023, 0, 15, false},  // 月份为 0
        {2000, 2, 29, true},   // 2000 能被 400 整除，闰年
        {1900, 2, 29, false},  // 1900 能被 100 但不能被 400 整除，平年
        {2023, -1, 1, false},  // 负月份
    };

    for (const auto& t : tests) {
        bool result = is_valid_date(t.y, t.m, t.d);
        std::cout << t.y << "-" << t.m << "-" << t.d
                  << " -> " << (result ? "合法" : "不合法")
                  << "（期望：" << (t.expected ? "合法" : "不合法") << "）"
                  << (result == t.expected ? " [OK]" : " [FAIL]") << "\n";
    }

    return 0;
}
