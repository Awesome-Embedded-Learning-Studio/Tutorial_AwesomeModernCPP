// 验证：copy_file 不提供原子性保证
// 如果复制过程中失败，目标文件可能处于部分写入状态

#include <filesystem>
#include <iostream>
#include <fstream>

namespace fs = std::filesystem;

int main() {
    fs::create_directory("sandbox");

    // 创建一个较大的源文件
    {
        std::ofstream out("sandbox/large_file.dat");
        for (int i = 0; i < 1000000; ++i) {
            out << "This is test data line " << i << "\n";
        }
    }

    std::cout << "Testing copy_file behavior...\n";
    std::cout << "Source file size: " << fs::file_size("sandbox/large_file.dat") << " bytes\n";

    // 注意：copy_file 不提供原子性
    // 如果操作中途失败（磁盘空间不足、断电等），
    // 目标文件可能只写入了一部分

    std::error_code ec;
    bool success = fs::copy_file("sandbox/large_file.dat", "sandbox/copied_file.dat", ec);

    if (success) {
        std::cout << "Copy successful\n";
        std::cout << "Destination file size: " << fs::file_size("sandbox/copied_file.dat") << " bytes\n";
    } else {
        std::cout << "Copy failed: " << ec.message() << "\n";
    }

    // 如需原子性，应使用：复制到临时文件 + 原子重命名
    // 参考 safe_write_file 模式

    fs::remove_all("sandbox");
    return 0;
}
