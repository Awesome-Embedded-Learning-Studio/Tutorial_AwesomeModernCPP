// Standard: C++17
// 病态回溯（catastrophic backtracking）：模式 (a+)+b 喂 n 个 a（结尾不给 b），
// 回溯 NFA 对同一批 a 反复试各种分组组合，输入每长一点耗时翻几番——指数级。
// 注：n=28 实测约 22 秒（在线运行会超时），这里只跑到 n=24 展示增长趋势。
#include <chrono>
#include <iostream>
#include <regex>
#include <string>

int main() {
    std::regex bad_re(R"((a+)+b)");
    for (int n : {16, 20, 24}) {
        std::string s(static_cast<std::size_t>(n), 'a'); // n 个 a，结尾没有 b
        auto t0 = std::chrono::steady_clock::now();
        bool m = std::regex_match(s, bad_re);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        std::cout << "n=" << n << " matched=" << std::boolalpha << m << " " << ms << " ms\n";
    }
    return 0;
}
