#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

/// 将字符串按空格拆分成单词列表
std::vector<std::string> split_words(const std::string& text)
{
    std::vector<std::string> words;
    std::istringstream iss(text);
    std::string word;
    while (iss >> word) {
        words.push_back(word);
    }
    return words;
}

/// 使用 map 统计每个单词的出现频率
void word_frequency_demo()
{
    std::string text = "the cat sat on the mat and the cat slept";
    auto words = split_words(text);

    std::map<std::string, int> freq;
    for (const auto& w : words) {
        // operator[] 在这里正好合适：不存在则插入 0，然后 ++ 自增
        ++freq[w];
    }

    std::cout << "=== Word Frequency ===\n";
    for (const auto& [word, count] : freq) {
        std::cout << "  " << word << ": " << count << "\n";
    }
}

/// 使用 set 做简单的拼写检查
void spell_check_demo()
{
    // 构建一个小词典
    std::set<std::string> dictionary = {
        "the", "cat", "sat", "on", "mat", "and", "slept",
        "dog", "ran", "in", "park", "hello", "world"
    };

    std::string text = "the cat danced on the roof";
    auto words = split_words(text);

    std::cout << "\n=== Spell Check ===\n";
    std::cout << "Input: \"" << text << "\"\n";
    for (const auto& w : words) {
        if (!dictionary.contains(w)) {
            std::cout << "  Unknown word: \"" << w << "\"\n";
        }
    }
}

/// 对比 map 和 unordered_map 的遍历顺序
void map_order_demo()
{
    std::map<std::string, int> ordered = {
        {"delta", 4}, {"alpha", 1}, {"charlie", 3}, {"bravo", 2}
    };

    std::cout << "\n=== std::map (ordered) ===\n";
    for (const auto& [key, val] : ordered) {
        std::cout << "  " << key << ": " << val << "\n";
    }
}

int main()
{
    word_frequency_demo();
    spell_check_demo();
    map_order_demo();
    return 0;
}
