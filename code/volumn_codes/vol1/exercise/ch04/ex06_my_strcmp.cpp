/**
 * @file ex06_my_strcmp.cpp
 * @brief 练习：指针实现字符串比较
 *
 * 实现 int my_strcmp(const char* a, const char* b)，
 * 返回值语义与标准 strcmp 一致：
 *   < 0 表示 a < b
 *   = 0 表示 a == b
 *   > 0 表示 a > b
 */

#include <cstring>
#include <iostream>

// 手写 strcmp：逐字符比较
int my_strcmp(const char* a, const char* b) {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b) {
            return static_cast<int>(
                static_cast<unsigned char>(*a)
                - static_cast<unsigned char>(*b));
        }
        ++a;
        ++b;
    }
    // 至少一个到达末尾，比较剩余字符
    return static_cast<int>(
        static_cast<unsigned char>(*a)
        - static_cast<unsigned char>(*b));
}

// 测试辅助函数
void test_compare(const char* a, const char* b) {
    int mine = my_strcmp(a, b);
    int standard = std::strcmp(a, b);

    // 为了方便比较，将结果归一化为 -1 / 0 / 1
    auto normalize = [](int v) -> int {
        return (v > 0) ? 1 : ((v < 0) ? -1 : 0);
    };

    int my_norm = normalize(mine);
    int std_norm = normalize(standard);

    std::cout << "  \"" << a << "\" vs \"" << b << "\":\n";
    std::cout << "    my_strcmp = " << my_norm
              << ", std::strcmp = " << std_norm
              << " -> " << (my_norm == std_norm ? "一致" : "不一致") << "\n";
}

int main() {
    std::cout << "===== my_strcmp 与 std::strcmp 对比 =====\n\n";

    test_compare("apple", "banana");    // a < b
    test_compare("banana", "apple");    // b > a
    test_compare("hello", "hello");     // 相等
    test_compare("", "");               // 空字符串
    test_compare("abc", "abcd");        // 前缀相等但 a 较短
    test_compare("abcd", "abc");        // 前缀相等但 b 较短
    test_compare("ABC", "abc");         // 大小写不同（ASCII 值差 32）

    std::cout << "\n注意：比较基于字符的 ASCII 值，大写字母小于小写字母。\n";

    return 0;
}
