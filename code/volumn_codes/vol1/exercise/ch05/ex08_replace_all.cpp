/**
 * @file ex08_replace_all.cpp
 * @brief 练习：简单查找替换工具
 *
 * 实现 replace_all 函数，将字符串中所有出现的子串替换为另一个子串。
 * 正确处理空搜索字符串的情况。
 */

#include <iostream>
#include <string>

// 将 src 中所有出现的 search 替换为 replace
std::string replace_all(const std::string& src,
                        const std::string& search,
                        const std::string& replace) {
    // 空搜索字符串直接返回原串，避免无限循环
    if (search.empty()) {
        return src;
    }

    std::string result;
    std::size_t pos = 0;
    std::size_t last_pos = 0;

    while ((pos = src.find(search, last_pos)) != std::string::npos) {
        // 追加匹配位置之前的文本
        result.append(src, last_pos, pos - last_pos);
        // 追加替换文本
        result += replace;
        // 跳过已匹配的部分
        last_pos = pos + search.size();
    }

    // 追加剩余部分
    result.append(src, last_pos, std::string::npos);
    return result;
}

int main() {
    std::cout << "===== 简单查找替换工具 =====\n\n";

    // 测试用例
    struct TestCase {
        std::string source;
        std::string search;
        std::string replace;
    };

    TestCase tests[] = {
        {"Hello World", "World", "C++"},
        {"aaa bbb aaa ccc aaa", "aaa", "XXX"},
        {"no match here", "xyz", "replacement"},
        {"abababab", "ab", "cd"},
        {"hello", "", "nope"},        // 空搜索串
        {"", "a", "b"},               // 空源串
        {"aaaa", "aa", "b"},          // 重叠匹配
        {"path/to/file", "/", "\\"},  // 路径分隔符替换
    };

    for (const auto& test : tests) {
        std::string result = replace_all(test.source, test.search, test.replace);
        std::cout << "  源:   \"" << test.source << "\"\n";
        std::cout << "  查找: \"" << test.search << "\"\n";
        std::cout << "  替换: \"" << test.replace << "\"\n";
        std::cout << "  结果: \"" << result << "\"\n\n";
    }

    return 0;
}
