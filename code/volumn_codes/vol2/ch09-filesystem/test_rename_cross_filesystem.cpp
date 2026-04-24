// 验证：跨文件系统 rename 会失败，而非自动复制+删除
#include <filesystem>
#include <iostream>
#include <fstream>

namespace fs = std::filesystem;

int main() {
    // 创建测试文件
    fs::create_directory("sandbox");
    {
        std::ofstream("sandbox/source.txt") << "Test content";
    }

    std::cout << "Testing cross-filesystem rename...\n";

    // 尝试跨文件系统重命名（如果 /tmp 是独立文件系统）
    // 这通常会失败，而不是自动执行 copy + remove

    std::error_code ec;
    fs::rename("sandbox/source.txt", "/tmp/source.txt", ec);

    if (ec) {
        std::cout << "Cross-filesystem rename failed (as expected): " << ec.message() << "\n";
        std::cout << "Error code: " << ec.value() << "\n";
        std::cout << "\n跨文件系统移动需要显式使用 copy + remove：\n";

        // 正确的做法
        fs::copy_file("sandbox/source.txt", "/tmp/source.txt", ec);
        if (!ec) {
            std::cout << "Copy successful\n";
            fs::remove("sandbox/source.txt");
            std::cout << "Source removed\n";
        }
    } else {
        std::cout << "Rename successful (same filesystem)\n";
    }

    fs::remove_all("sandbox");
    fs::remove("/tmp/source.txt", ec);
    return 0;
}
