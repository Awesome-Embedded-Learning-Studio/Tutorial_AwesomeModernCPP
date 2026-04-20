/**
 * @file ex06_count_vowels.cpp
 * @brief 练习：统计元音
 *
 * 使用 range-for 循环遍历 std::string，统计元音字母（a/e/i/o/u）
 * 的出现次数，不区分大小写。
 */

#include <cctype>
#include <iostream>
#include <string>

bool is_vowel(char ch) {
    char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return lower == 'a' || lower == 'e' || lower == 'i' ||
           lower == 'o' || lower == 'u';
}

int count_vowels(const std::string& text) {
    int count = 0;
    for (char ch : text) {
        if (is_vowel(ch)) {
            ++count;
        }
    }
    return count;
}

int main() {
    std::string text = "Hello, Modern C++ Programming Tutorial!";

    std::cout << "文本: \"" << text << "\"\n";

    int vowel_count = count_vowels(text);
    std::cout << "元音字母数: " << vowel_count << '\n';

    // 逐字符分析
    std::cout << "\n逐字符分析：\n";
    int vowel_count_manual = 0;
    for (char ch : text) {
        if (is_vowel(ch)) {
            ++vowel_count_manual;
            std::cout << ch << ' ';
        }
    }
    std::cout << "\n共 " << vowel_count_manual << " 个元音\n";

    // 从用户输入统计
    std::cout << "\n请输入一行文本: ";
    std::string input;
    std::getline(std::cin, input);

    std::cout << "元音字母数: " << count_vowels(input) << '\n';

    return 0;
}
