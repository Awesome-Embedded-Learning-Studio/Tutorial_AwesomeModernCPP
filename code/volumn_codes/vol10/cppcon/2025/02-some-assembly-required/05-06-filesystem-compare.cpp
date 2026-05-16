/*
 * 验证：std::filesystem 与 boost::filesystem 接口一致性
 *
 * 背景：文章声称 std::filesystem（C++17）与 Boost.Filesystem 的 API 几乎一样，
 *       仅头文件和 namespace 不同。GCC 16 + C++20 不需要 -lstdc++fs 链接标志。
 *
 * 预期结果：创建目录、遍历、删除均正常工作；
 *           operator<< 对 path 输出带双引号（标准行为）
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -o /tmp/05-06-filesystem-compare 05-06-filesystem-compare.cpp
 *
 * 运行：
 *   /tmp/05-06-filesystem-compare
 *
 * 参考资料：
 *   - https://en.cppreference.com/w/cpp/filesystem
 *
 * 编译器：GCC 16.1.1
 */

#include <iostream>
#include <filesystem>
namespace fs = std::filesystem;

int main() {
    fs::path p = "/tmp/test_dir_verify";

    if (!fs::exists(p)) {
        fs::create_directories(p);
        std::cout << "created: " << p << "\n";
    }

    for (const auto& entry : fs::directory_iterator(p)) {
        std::cout << "  " << entry.path().filename()
                  << " | size: " << entry.file_size() << "\n";
    }

    fs::remove_all(p);
    std::cout << "removed: " << p << "\n";

    return 0;
}
