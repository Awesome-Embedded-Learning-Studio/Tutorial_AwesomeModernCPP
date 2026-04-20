/**
 * @file ex07_word_count.cpp
 * @brief 练习：单词计数器
 *
 * 实现 count_words(const std::string& s)，
 * 统计字符串中的单词数（以空格分隔），正确处理连续空格。
 */

#include <iostream>
#include <string>

// 统计字符串中的单词数
// 规则：连续空格视为一个分隔符，前后导空格不产生空单词
int count_words(const std::string& s) {
    if (s.empty()) {
        return 0;
    }

    int count = 0;
    bool in_word = false;

    for (char c : s) {
        if (c != ' ') {
            if (!in_word) {
                ++count;    // 进入一个新单词
                in_word = true;
            }
        } else {
            in_word = false;  // 遇到空格，离开单词
        }
    }
    return count;
}

int main() {
    std::cout << "===== 单词计数器 =====\n\n";

    // 测试用例
    struct TestCase {
        std::string text;
        int expected;
    };

    TestCase tests[] = {
        {"Hello World", 2},
        {"  leading spaces", 2},
        {"trailing spaces  ", 2},
        {"  both  sides  ", 2},
        {"one", 1},
        {"", 0},
        {"    ", 0},
        {"a b c d e", 5},
        {"multiple   spaces   between", 3},
        {"Hello, World! How are you?", 5}
    };

    for (const auto& test : tests) {
        int result = count_words(test.text);
        std::cout << "  \"" << test.text << "\"\n";
        std::cout << "    单词数: " << result
                  << " (期望: " << test.expected << ")"
                  << (result == test.expected ? " [通过]" : " [失败]")
                  << "\n\n";
    }

    return 0;
}
