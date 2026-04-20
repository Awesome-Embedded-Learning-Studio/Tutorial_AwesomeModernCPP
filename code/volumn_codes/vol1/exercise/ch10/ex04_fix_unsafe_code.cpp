/**
 * @file ex04_fix_unsafe_code.cpp
 * @brief 练习：修复不安全的资源管理
 *
 * 原始代码在异常路径下泄漏 FILE* 和 buffer。
 * 用 unique_ptr（自定义 deleter 关闭文件）和 unique_ptr<int[]> 重写。
 */

#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>

// ---- 原始不安全版本（注释，仅作对比） ----
//
// void process_file_unsafe(const char* path) {
//     FILE* f = std::fopen(path, "r");       // 可能失败
//     int* buffer = new int[1024];           // 如果下面抛异常 -> 泄漏
//     if (!f) {
//         delete[] buffer;                   // 容易忘记
//         throw std::runtime_error("cannot open file");
//     }
//     // ... 使用 f 和 buffer ...
//     // 如果这里抛异常，f 和 buffer 都泄漏！
//     std::fclose(f);
//     delete[] buffer;
// }

/// @brief 用于 unique_ptr 的 FILE* deleter
struct FileCloser {
    void operator()(std::FILE* fp) const
    {
        if (fp) {
            std::fclose(fp);
            std::cout << "  [FileCloser] fclose 被调用\n";
        }
    }
};

/// @brief 安全版本的文件处理函数
/// @param path  文件路径
/// @return 读取到的整数数量
/// @throw std::runtime_error  文件无法打开时
std::size_t process_file(const char* path)
{
    // 用 unique_ptr + 自定义 deleter 管理 FILE*
    std::unique_ptr<std::FILE, FileCloser> file(std::fopen(path, "r"));
    if (!file) {
        throw std::runtime_error(
            std::string("process_file: cannot open '") + path + "'");
    }

    // 用 unique_ptr<int[]> 管理 buffer
    constexpr std::size_t kBufferSize = 1024;
    auto buffer = std::make_unique<int[]>(kBufferSize);

    std::cout << "  文件已打开，buffer 已分配\n";

    // 模拟读取（实际使用 fread 等）
    std::size_t count = 0;
    for (std::size_t i = 0; i < 10; ++i) {
        buffer[i] = static_cast<int>(i * 10);
        ++count;
    }

    // 即使这里抛异常，file 和 buffer 也会被正确释放
    // 模拟异常路径
    if (count == 0) {
        throw std::runtime_error("no data read");
    }

    std::cout << "  读取了 " << count << " 个值\n";
    return count;
    // file -> FileCloser 自动 fclose
    // buffer -> unique_ptr 析构自动 delete[]
}

int main()
{
    std::cout << "===== 修复不安全的资源管理 =====\n\n";

    // 测试 1: 文件不存在
    std::cout << "--- 测试：文件不存在 ---\n";
    try {
        process_file("nonexistent_file.txt");
    }
    catch (const std::runtime_error& e) {
        std::cout << "  捕获异常: " << e.what() << "\n";
    }

    // 测试 2: 正常路径（用临时文件）
    std::cout << "\n--- 测试：创建临时文件 ---\n";
    const char* kTempPath = "/tmp/cpp_exercise_test.txt";

    // 先创建一个临时文件
    {
        std::FILE* f = std::fopen(kTempPath, "w");
        if (f) {
            std::fprintf(f, "test data\n");
            std::fclose(f);
        }
    }

    try {
        std::size_t count = process_file(kTempPath);
        std::cout << "  成功，读取 " << count << " 条数据\n";
    }
    catch (const std::runtime_error& e) {
        std::cout << "  捕获异常: " << e.what() << "\n";
    }

    std::cout << "\n要点:\n";
    std::cout << "  1. unique_ptr<int[]> 自动管理 new[] 分配的内存\n";
    std::cout << "  2. unique_ptr<FILE, Deleter> 管理 C 风格的 FILE*\n";
    std::cout << "  3. RAII 保证异常路径下资源也能正确释放\n";

    return 0;
}
