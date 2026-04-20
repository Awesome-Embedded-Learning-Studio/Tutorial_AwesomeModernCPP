/**
 * @file ex09_trim.cpp
 * @brief 练习：trim 函数
 *
 * 使用 find_first_not_of 和 find_last_not_of 实现
 * ltrim（去除前导空白）、rtrim（去除尾部空白）、trim（两端去除空白）。
 */

#include <iostream>
#include <string>

// 去除前导空白字符
std::string ltrim(const std::string& s) {
    std::size_t start = s.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) {
        return "";  // 全是空白
    }
    return s.substr(start);
}

// 去除尾部空白字符
std::string rtrim(const std::string& s) {
    std::size_t end = s.find_last_not_of(" \t\n\r\f\v");
    if (end == std::string::npos) {
        return "";  // 全是空白
    }
    return s.substr(0, end + 1);
}

// 去除两端空白字符
std::string trim(const std::string& s) {
    std::size_t start = s.find_first_not_of(" \t\n\r\f\v");
    if (start == std::string::npos) {
        return "";
    }
    std::size_t end = s.find_last_not_of(" \t\n\r\f\v");
    return s.substr(start, end - start + 1);
}

// 辅助：用方括号包裹字符串，方便观察空白
void print_trimmed(const std::string& original,
                   const std::string& result,
                   const char* func_name) {
    std::cout << "  " << func_name << "(\"" << original << "\")"
              << " -> \"" << result << "\"\n";
}

int main() {
    std::cout << "===== trim 函数 =====\n\n";

    std::string test1 = "   Hello, World!   ";
    std::string test2 = "\t\n  mixed whitespace  \t\n";
    std::string test3 = "no spaces";
    std::string test4 = "    ";
    std::string test5 = "  left only";
    std::string test6 = "right only  ";

    std::cout << "ltrim 测试:\n";
    print_trimmed(test1, ltrim(test1), "ltrim");
    print_trimmed(test2, ltrim(test2), "ltrim");
    print_trimmed(test3, ltrim(test3), "ltrim");
    print_trimmed(test4, ltrim(test4), "ltrim");
    print_trimmed(test5, ltrim(test5), "ltrim");

    std::cout << "\nrtrim 测试:\n";
    print_trimmed(test1, rtrim(test1), "rtrim");
    print_trimmed(test2, rtrim(test2), "rtrim");
    print_trimmed(test3, rtrim(test3), "rtrim");
    print_trimmed(test4, rtrim(test4), "rtrim");
    print_trimmed(test6, rtrim(test6), "rtrim");

    std::cout << "\ntrim 测试:\n";
    print_trimmed(test1, trim(test1), "trim");
    print_trimmed(test2, trim(test2), "trim");
    print_trimmed(test3, trim(test3), "trim");
    print_trimmed(test4, trim(test4), "trim");
    print_trimmed(test5, trim(test5), "trim");
    print_trimmed(test6, trim(test6), "trim");

    std::cout << "\n要点:\n";
    std::cout << "  find_first_not_of  — 找到第一个不是空白的位置\n";
    std::cout << "  find_last_not_of   — 找到最后一个不是空白的位置\n";
    std::cout << "  返回 npos 表示全部是空白或字符串为空\n";

    return 0;
}
