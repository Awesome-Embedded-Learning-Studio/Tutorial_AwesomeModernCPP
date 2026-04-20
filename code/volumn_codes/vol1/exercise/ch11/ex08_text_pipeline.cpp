/**
 * @file ex08_text_pipeline.cpp
 * @brief 练习：文本处理管道
 *
 * 链式使用 STL 算法处理文本：
 * 1. 删除空行（remove_if）
 * 2. 转小写（transform）
 * 3. 按字典序排序并去重（sort + unique + erase）
 * 4. 分词并统计词频
 * 5. 找出最常见的单词
 *
 * 每一步都用独立的算法调用完成，不写手动 for 循环。
 */

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <numeric>
#include <string>
#include <vector>

/// @brief 打印字符串列表
/// @param lines  字符串列表
/// @param label  标签
void print_lines(const std::vector<std::string>& lines,
                 const std::string& label)
{
    std::cout << label << " (" << lines.size() << " 行):\n";
    for (const auto& line : lines) {
        std::cout << "  \"" << line << "\"\n";
    }
    std::cout << "\n";
}

/// @brief 判断字符串是否为空或仅含空白字符
/// @param s  输入字符串
/// @return   是否为空行
bool is_blank_line(const std::string& s)
{
    return std::all_of(s.begin(), s.end(),
        [](unsigned char ch) { return std::isspace(ch); });
}

/// @brief 将字符串转为全小写
/// @param s  输入字符串（就地修改）
void to_lower(std::string& s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
}

/// @brief 将一行文本按空格拆分为单词
/// @param line  输入行
/// @return      单词列表
std::vector<std::string> split_line(const std::string& line)
{
    std::vector<std::string> words;
    std::string word;
    for (char ch : line) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        } else {
            word += ch;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }
    return words;
}

int main()
{
    std::cout << "===== ex08: 文本处理管道 =====\n\n";

    // 原始数据
    std::vector<std::string> lines = {
        "Hello World", "", "hello world", "Goodbye", "GOODBYE", "", "Alice"
    };

    print_lines(lines, "原始数据");

    // --- 第 1 步：删除空行（remove_if + erase）---
    auto blank_it = std::remove_if(lines.begin(), lines.end(), is_blank_line);
    lines.erase(blank_it, lines.end());

    print_lines(lines, "第 1 步: 删除空行后");

    // --- 第 2 步：全部转小写（transform）---
    std::for_each(lines.begin(), lines.end(), to_lower);

    print_lines(lines, "第 2 步: 转小写后");

    // --- 第 3 步：按字典序排序并去重（sort + unique + erase）---
    std::sort(lines.begin(), lines.end());
    auto unique_it = std::unique(lines.begin(), lines.end());
    lines.erase(unique_it, lines.end());

    print_lines(lines, "第 3 步: 排序并去重后");

    // --- 第 4 步：分词并统计词频 ---
    // 把所有行拆成单词，再用 map 统计
    std::vector<std::string> all_words;
    for (const auto& line : lines) {
        auto words = split_line(line);
        std::copy(words.begin(), words.end(), std::back_inserter(all_words));
    }

    std::map<std::string, int> freq;
    std::for_each(all_words.begin(), all_words.end(),
        [&freq](const std::string& w) { ++freq[w]; });

    std::cout << "第 4 步: 词频统计:\n";
    std::for_each(freq.begin(), freq.end(),
        [](const auto& p) {
            std::cout << "  \"" << p.first << "\": " << p.second << "\n";
        });
    std::cout << "\n";

    // --- 第 5 步：找出最常见的单词（max_element）---
    auto most_common = std::max_element(freq.begin(), freq.end(),
        [](const auto& a, const auto& b) {
            return a.second < b.second;
        });

    if (most_common != freq.end()) {
        std::cout << "第 5 步: 最常见的单词 = \""
                  << most_common->first << "\"（出现 "
                  << most_common->second << " 次）\n\n";
    }

    std::cout << "要点:\n";
    std::cout << "  1. remove_if + erase 删除满足条件的元素\n";
    std::cout << "  2. transform 对每个元素执行变换\n";
    std::cout << "  3. sort + unique + erase 实现去重\n";
    std::cout << "  4. max_element 配合 lambda 找极值\n";
    std::cout << "  5. 整个管道用算法组合，无需手写数据处理循环\n";

    return 0;
}
