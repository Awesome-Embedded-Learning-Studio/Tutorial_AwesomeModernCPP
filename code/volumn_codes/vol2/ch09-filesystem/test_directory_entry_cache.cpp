// 验证：directory_entry 缓存行为是实现定义的
#include <filesystem>
#include <iostream>
#include <fstream>

namespace fs = std::filesystem;

int main() {
    fs::create_directory("sandbox");
    auto test_file = "sandbox/test.txt";

    // 创建初始文件
    {
        std::ofstream out(test_file);
        out << "Initial content";
    }

    // 获取 directory_entry
    fs::directory_entry entry(test_file);

    std::cout << "Initial file size: " << entry.file_size() << "\n";

    // 外部修改文件
    {
        std::ofstream out(test_file);
        out << "This is much longer content that will change the file size";
    }

    // 不调用 refresh()，缓存值可能是过期的
    // 这取决于实现——某些实现可能不缓存，或者缓存立即失效
    std::cout << "File size without refresh: " << entry.file_size() << "\n";

    // 调用 refresh() 获取最新状态
    entry.refresh();
    std::cout << "File size after refresh: " << entry.file_size() << "\n";

    fs::remove_all("sandbox");
    return 0;
}
