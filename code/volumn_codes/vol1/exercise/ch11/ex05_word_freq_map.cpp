/**
 * @file ex05_word_freq_map.cpp
 * @brief 练习：用 unordered_map 重写词频统计
 *
 * 使用 std::unordered_map<std::string, int> 统计文本中每个单词的出现频率。
 * 按空格和标点分词，按频率降序输出结果。
 * 对比 map 与 unordered_map 在遍历顺序上的差异。
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

/// @brief 判断字符是否为单词分隔符（空格或常见标点）
/// @param ch  待判断的字符
/// @return    是否为分隔符
bool is_delimiter(char ch)
{
    return std::isspace(static_cast<unsigned char>(ch))
        || ch == ',' || ch == '.' || ch == '!'
        || ch == '?' || ch == ';' || ch == ':'
        || ch == '\'' || ch == '"' || ch == '('
        || ch == ')' || ch == '-';
}

/// @brief 将文本按分隔符拆分为小写单词列表
/// @param text  输入文本
/// @return      单词列表（已转小写）
std::vector<std::string> tokenize(const std::string& text)
{
    std::vector<std::string> words;
    std::string current;

    for (char ch : text) {
        if (is_delimiter(ch)) {
            if (!current.empty()) {
                words.push_back(current);
                current.clear();
            }
        } else {
            current += static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
        }
    }
    if (!current.empty()) {
        words.push_back(current);
    }

    return words;
}

/// @brief 使用 unordered_map 统计词频
/// @param text  输入文本
/// @return      词频表
std::unordered_map<std::string, int>
count_words_unordered(const std::string& text)
{
    auto words = tokenize(text);
    std::unordered_map<std::string, int> freq;
    for (const auto& w : words) {
        ++freq[w];
    }
    return freq;
}

/// @brief 使用 map 统计词频（用于对比）
/// @param text  输入文本
/// @return      词频表
std::map<std::string, int>
count_words_ordered(const std::string& text)
{
    auto words = tokenize(text);
    std::map<std::string, int> freq;
    for (const auto& w : words) {
        ++freq[w];
    }
    return freq;
}

/// @brief 按频率降序输出词频表
/// @param freq  词频表
/// @param label 输出标签
void print_by_frequency(
    const std::unordered_map<std::string, int>& freq,
    const std::string& label)
{
    // 将 unordered_map 内容拷贝到 vector 以排序
    std::vector<std::pair<std::string, int>> items(freq.begin(), freq.end());

    // 按频率降序排序；频率相同时按字典序升序
    std::sort(items.begin(), items.end(),
        [](const auto& a, const auto& b) {
            if (a.second != b.second) {
                return a.second > b.second;
            }
            return a.first < b.first;
        });

    std::cout << label << "（按频率降序）:\n";
    for (const auto& [word, count] : items) {
        std::cout << "  " << word << ": " << count << "\n";
    }
}

/// @brief 生成大量随机单词用于性能对比
/// @param count  单词总数
/// @return       单词列表
std::vector<std::string> generate_random_words(std::size_t count)
{
    const std::vector<std::string> kPool = {
        "apple", "banana", "cherry", "date", "elderberry",
        "fig", "grape", "honeydew", "kiwi", "lemon",
        "mango", "nectarine", "orange", "peach", "quince"
    };

    std::mt19937 rng(42);  // 固定种子，结果可复现
    std::uniform_int_distribution<std::size_t> dist(0, kPool.size() - 1);

    std::vector<std::string> words;
    words.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        words.push_back(kPool[dist(rng)]);
    }
    return words;
}

/// @brief 对比 map 和 unordered_map 在大数据量下的性能
void benchmark_comparison()
{
    constexpr std::size_t kWordCount = 100000;
    auto words = generate_random_words(kWordCount);

    // 用 unordered_map 统计
    auto start_unordered = std::chrono::high_resolution_clock::now();
    std::unordered_map<std::string, int> freq_unordered;
    for (const auto& w : words) {
        ++freq_unordered[w];
    }
    auto end_unordered = std::chrono::high_resolution_clock::now();
    auto ms_unordered = std::chrono::duration_cast<
        std::chrono::microseconds>(end_unordered - start_unordered).count();

    // 用 map 统计
    auto start_ordered = std::chrono::high_resolution_clock::now();
    std::map<std::string, int> freq_ordered;
    for (const auto& w : words) {
        ++freq_ordered[w];
    }
    auto end_ordered = std::chrono::high_resolution_clock::now();
    auto ms_ordered = std::chrono::duration_cast<
        std::chrono::microseconds>(end_ordered - start_ordered).count();

    std::cout << "\n--- 性能对比 (" << kWordCount << " 个单词) ---\n";
    std::cout << "  unordered_map: " << ms_unordered << " us\n";
    std::cout << "  map:           " << ms_ordered << " us\n";

    if (ms_ordered > 0) {
        double ratio = static_cast<double>(ms_ordered) / ms_unordered;
        std::cout << "  map / unordered_map = "
                  << ratio << "x\n";
    }
}

int main()
{
    std::cout << "===== ex05: 用 unordered_map 重写词频统计 =====\n\n";

    // 测试文本
    std::string text =
        "The quick brown fox jumps over the lazy dog. "
        "The fox was very quick, and the dog was very lazy. "
        "Quick, quick! The brown fox jumps again.";

    std::cout << "原始文本:\n  \"" << text << "\"\n\n";

    // 用 unordered_map 统计
    auto freq = count_words_unordered(text);

    std::cout << "unordered_map 遍历（顺序不可预测）:\n";
    for (const auto& [word, count] : freq) {
        std::cout << "  " << word << ": " << count << "\n";
    }
    std::cout << "\n";

    // 按频率降序输出
    print_by_frequency(freq, "\n排序后");
    std::cout << "\n";

    // 对比 map 的遍历顺序
    auto freq_map = count_words_ordered(text);
    std::cout << "map 遍历（按字典序）:\n";
    for (const auto& [word, count] : freq_map) {
        std::cout << "  " << word << ": " << count << "\n";
    }
    std::cout << "\n";

    // 性能对比
    benchmark_comparison();

    std::cout << "\n要点:\n";
    std::cout << "  1. unordered_map 平均 O(1) 查找，适合纯计数场景\n";
    std::cout << "  2. 遍历顺序不可预测，需额外排序才能按频率输出\n";
    std::cout << "  3. map 按 key 有序，但每次插入是 O(log n)\n";
    std::cout << "  4. 大数据量下 unordered_map 通常比 map 更快\n";

    return 0;
}
