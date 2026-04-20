// range_for.cpp
// Platform: host
// Standard: C++17

#include <array>
#include <cctype>
#include <iostream>
#include <string>

int main()
{
    // 求和
    std::array<int, 6> data = {3, 7, 1, 9, 4, 6};
    int sum = 0;
    for (const auto& x : data) {
        sum += x;
    }
    std::cout << "总和: " << sum << std::endl;

    // 计数
    int target = 6;
    int count = 0;
    for (const auto& x : data) {
        if (x == target) { ++count; }
    }
    std::cout << "值 " << target << " 出现了 " << count
              << " 次" << std::endl;

    // 原地修改：每个元素翻倍
    std::array<int, 6> doubled = data;
    for (auto& x : doubled) { x *= 2; }
    std::cout << "翻倍后: ";
    for (const auto& x : doubled) {
        std::cout << x << " ";
    }
    std::cout << std::endl;

    // 字符串转大写
    std::string message = "range-for is elegant";
    for (auto& c : message) {
        c = static_cast<char>(
            std::toupper(static_cast<unsigned char>(c)));
    }
    std::cout << "转大写: " << message << std::endl;

    return 0;
}
