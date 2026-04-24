// 验证：path 构造函数支持 string_view (C++17)
#include <filesystem>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;

int main() {
    // C++17 起，path 构造函数直接支持 string_view
    std::string_view sv = "/tmp/test";
    fs::path p1(sv);
    std::cout << "path from string_view: " << p1 << std::endl;

    // 也可以显式转换
    fs::path p2(std::string(sv));
    std::cout << "path from string: " << p2 << std::endl;

    return 0;
}
