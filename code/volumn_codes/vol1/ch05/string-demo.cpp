// string_demo.cpp
#include <iostream>
#include <map>
#include <string>

/// @brief 把句子按空格拆分成单词，输出每个单词
void split_into_words(const std::string& sentence)
{
    std::cout << "--- 拆分单词 ---" << std::endl;
    std::size_t start = 0;
    std::size_t end = 0;

    while (start < sentence.size()) {
        start = sentence.find_first_not_of(' ', start);
        if (start == std::string::npos) {
            break;
        }
        end = sentence.find(' ', start);
        if (end == std::string::npos) {
            end = sentence.size();
        }
        std::cout << "  [" << sentence.substr(start, end - start) << "]\n";
        start = end + 1;
    }
}

/// @brief 统计每个字符出现的次数（区分大小写）
void count_char_frequency(const std::string& text)
{
    std::cout << "\n--- 字符频率统计 ---" << std::endl;
    std::map<char, int> freq;
    for (char c : text) {
        freq[c]++;
    }
    for (const auto& [ch, count] : freq) {
        std::cout << "  '" << ch << "': " << count << "\n";
    }
}

/// @brief 在 text 中查找所有 target 并替换为 replacement
std::string find_and_replace(std::string text,
                             const std::string& target,
                             const std::string& replacement)
{
    std::cout << "\n--- 查找替换 ---\n  原文: " << text << std::endl;
    std::size_t pos = 0;
    while ((pos = text.find(target, pos)) != std::string::npos) {
        text.replace(pos, target.size(), replacement);
        pos += replacement.size();  // 跳过已替换部分，避免死循环
    }
    std::cout << "  结果: " << text << std::endl;
    return text;
}

/// @brief 简单的 CSV 行解析（不处理引号转义）
void parse_csv_line(const std::string& line)
{
    std::cout << "\n--- CSV 解析 ---\n  输入: " << line << std::endl;
    std::size_t start = 0;
    int idx = 0;
    while (true) {
        std::size_t comma = line.find(',', start);
        if (comma == std::string::npos) {
            std::cout << "  字段 " << idx << ": [" << line.substr(start)
                      << "]\n";
            break;
        }
        std::cout << "  字段 " << idx << ": ["
                  << line.substr(start, comma - start) << "]\n";
        start = comma + 1;
        idx++;
    }
}

int main()
{
    split_into_words("C++ is a powerful and efficient language");
    count_char_frequency("hello world");
    find_and_replace("the cat sat on the mat", "the", "a");
    parse_csv_line("Alice,30,Engineer,New York");
    return 0;
}
