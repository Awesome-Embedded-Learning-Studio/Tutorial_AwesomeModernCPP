/**
 * @file ex04_my_strlen.cpp
 * @brief 练习：手写 strlen
 *
 * 用指针遍历实现 std::size_t my_strlen(const char* s)，
 * 并与标准库 std::strlen 的结果对比验证。
 */

#include <cstring>
#include <iostream>

// 手写 strlen：指针遍历直到遇到 '\0'
std::size_t my_strlen(const char* s) {
    const char* p = s;          // 保存起始地址
    while (*p != '\0') {        // 遍历直到结尾
        ++p;
    }
    return static_cast<std::size_t>(p - s);  // 指针差 = 字符数
}

int main() {
    const char* kTestStr1 = "Hello, World!";
    const char* kTestStr2 = "";
    const char* kTestStr3 = "C++17";
    const char* kTestStr4 = "你好";  // UTF-8 中文，每个字符占多个字节

    std::cout << "===== my_strlen 与 std::strlen 对比 =====\n\n";

    const char* tests[] = {kTestStr1, kTestStr2, kTestStr3, kTestStr4};

    for (const char* test : tests) {
        std::size_t mine = my_strlen(test);
        std::size_t standard = std::strlen(test);
        bool match = (mine == standard);

        std::cout << "字符串: \"";
        if (std::strlen(test) == 0) {
            std::cout << "(空字符串)";
        } else {
            std::cout << test;
        }
        std::cout << "\"\n";
        std::cout << "  my_strlen   = " << mine << "\n";
        std::cout << "  std::strlen = " << standard << "\n";
        std::cout << "  结果: " << (match ? "一致 ✓" : "不一致 ✗") << "\n\n";
    }

    return 0;
}
