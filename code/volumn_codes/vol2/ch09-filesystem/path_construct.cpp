#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main() {
    // 从 C 字符串构造
    fs::path p1 = "/usr/local/bin";
    // 从 std::string 构造
    std::string str = "/home/user/docs";
    fs::path p2(str);
    // 从字面量构造
    fs::path p3 = "C:\\Users\\Alice\\Documents";  // Windows 路径也可以
    // 在 Linux 上，反斜杠会被当作文件名的一部分（因为 \ 不是分隔符）
    // 但在 Windows 上会被正确识别为分隔符

    std::cout << "p1: " << p1 << "\n";
    std::cout << "p2: " << p2 << "\n";
    std::cout << "p3: " << p3 << "\n";
    return 0;
}
