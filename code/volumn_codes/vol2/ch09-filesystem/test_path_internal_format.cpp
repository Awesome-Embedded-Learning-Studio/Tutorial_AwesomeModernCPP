// 验证：path 内部使用原生格式，而非通用格式
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

int main() {
    // path 内部存储的是原生格式
    // generic_string() 是按需转换的

    fs::path p1 = "dir/subdir/file.txt";
    fs::path p2 = "dir\\subdir\\file.txt";  // Windows 风格

    std::cout << "Path 1 (input: dir/subdir/file.txt):\n";
    std::cout << "  Native string: " << p1.string() << "\n";
    std::cout << "  Generic string: " << p1.generic_string() << "\n";

    std::cout << "\nPath 2 (input: dir\\\\subdir\\\\file.txt):\n";
    std::cout << "  Native string: " << p2.string() << "\n";
    std::cout << "  Generic string: " << p2.generic_string() << "\n";

    // 在 Linux 上：
    // - p1 和 p2 的 native 格式都是输入时的样子
    // - generic_string() 会把反斜杠转换为正斜杠

    return 0;
}
