/**
 * @file ex05_scoped_file.cpp
 * @brief 练习：ScopedFile RAII 封装
 *
 * 实现一个 ScopedFile 类：
 * - explicit 构造 (path, mode)
 * - 析构时 fclose
 * - 删除拷贝
 * - 支持移动语义
 * - get() 返回 FILE*
 * - explicit operator bool()
 */

#include <cstdio>
#include <iostream>
#include <utility>

/// @brief RAII 封装 FILE* 的文件句柄类
class ScopedFile {
private:
    std::FILE* file_;

public:
    /// @brief 打开文件，失败时 file_ 为 nullptr
    /// @param path  文件路径
    /// @param mode  fopen 模式字符串
    explicit ScopedFile(const char* path, const char* mode)
        : file_(std::fopen(path, mode))
    {
        if (file_) {
            std::cout << "  ScopedFile: 打开 '" << path << "'\n";
        }
        else {
            std::cout << "  ScopedFile: 无法打开 '" << path << "'\n";
        }
    }

    /// @brief 析构时自动关闭文件
    ~ScopedFile()
    {
        if (file_) {
            std::fclose(file_);
            std::cout << "  ScopedFile: 文件已关闭\n";
        }
    }

    // 禁止拷贝
    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;

    // 移动构造
    ScopedFile(ScopedFile&& other) noexcept : file_(other.file_)
    {
        other.file_ = nullptr;
        std::cout << "  ScopedFile: 移动构造\n";
    }

    // 移动赋值
    ScopedFile& operator=(ScopedFile&& other) noexcept
    {
        if (this != &other) {
            // 先关闭自己持有的文件
            if (file_) {
                std::fclose(file_);
                std::cout << "  ScopedFile: 移动赋值，关闭旧文件\n";
            }
            file_ = other.file_;
            other.file_ = nullptr;
        }
        return *this;
    }

    /// @brief 获取底层 FILE*
    /// @return FILE* 指针，可能为 nullptr
    std::FILE* get() const { return file_; }

    /// @brief 检查文件是否有效打开
    /// @return true 表示文件已成功打开
    explicit operator bool() const { return file_ != nullptr; }
};

int main()
{
    std::cout << "===== ScopedFile RAII 封装 =====\n\n";

    const char* kTempPath = "/tmp/scoped_file_test.txt";

    // 创建测试文件
    {
        ScopedFile wf(kTempPath, "w");
        if (wf) {
            std::fprintf(wf.get(), "Hello from ScopedFile!\n");
            std::cout << "  写入测试数据\n";
        }
    }
    // wf 离开作用域，自动关闭

    std::cout << "\n--- 读取测试 ---\n";
    {
        ScopedFile rf(kTempPath, "r");
        if (rf) {
            char buf[256];
            while (std::fgets(buf, sizeof(buf), rf.get())) {
                std::cout << "  读取: " << buf;
            }
        }
    }

    std::cout << "\n--- 移动语义测试 ---\n";
    {
        ScopedFile a(kTempPath, "r");
        std::cout << "  a 有效: " << std::boolalpha << static_cast<bool>(a)
                  << "\n";

        ScopedFile b = std::move(a);
        std::cout << "  移动后 a 有效: " << static_cast<bool>(a) << "\n";
        std::cout << "  移动后 b 有效: " << static_cast<bool>(b) << "\n";
    }

    std::cout << "\n--- 打开不存在的文件 ---\n";
    {
        ScopedFile bad("/tmp/nonexistent_abc_xyz.txt", "r");
        std::cout << "  bad 有效: " << static_cast<bool>(bad) << "\n";
        if (!bad) {
            std::cout << "  (符合预期)\n";
        }
    }

    std::cout << "\n要点:\n";
    std::cout << "  1. RAII 确保文件在任何退出路径下都会关闭\n";
    std::cout << "  2. 移动语义允许所有权转移，拷贝语义被删除\n";
    std::cout << "  3. explicit operator bool() 防止隐式转换误用\n";

    return 0;
}
