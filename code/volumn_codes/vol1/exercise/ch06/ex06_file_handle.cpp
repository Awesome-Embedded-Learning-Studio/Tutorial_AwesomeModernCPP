/**
 * @file ex06_file_handle.cpp
 * @brief 练习：FileHandle 类
 *
 * 使用 RAII 管理文件句柄：构造时打开文件，析构时自动关闭。
 * 提供 read_line() 逐行读取，is_valid() 检查文件状态。
 * 删除拷贝构造和拷贝赋值，防止重复关闭文件。
 */

#include <cstdio>
#include <cstdarg>
#include <iostream>
#include <string>

class FileHandle {
private:
    std::FILE* file_;
    std::string path_;

public:
    // 构造函数：打开文件
    explicit FileHandle(const std::string& path, const char* mode = "r")
        : file_(nullptr), path_(path) {
        file_ = std::fopen(path.c_str(), mode);
        if (!file_) {
            std::cout << "  警告: 无法打开文件 \"" << path << "\"\n";
        }
    }

    // 析构函数：关闭文件
    ~FileHandle() {
        if (file_) {
            std::fclose(file_);
            std::cout << "  已关闭文件: \"" << path_ << "\"\n";
        }
    }

    // 删除拷贝构造和拷贝赋值
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    // 检查文件是否有效
    bool is_valid() const {
        return file_ != nullptr;
    }

    // 读取一行，返回字符串（不含换行符）
    std::string read_line() {
        if (!file_) {
            return "";
        }

        std::string line;
        int ch;
        while ((ch = std::fgetc(file_)) != EOF) {
            if (ch == '\n') {
                break;
            }
            line += static_cast<char>(ch);
        }
        return line;
    }

    // 获取文件路径
    const std::string& path() const {
        return path_;
    }

    // 写入格式化字符串
    void write(const char* fmt, ...) {
        if (!file_) return;
        va_list args;
        va_start(args, fmt);
        std::vfprintf(file_, fmt, args);
        va_end(args);
    }

    // 是否到达文件末尾
    bool is_eof() const {
        return file_ ? std::feof(file_) != 0 : true;
    }
};

int main() {
    std::cout << "===== FileHandle 类 =====\n\n";

    // 测试 1：写入测试文件
    {
        std::cout << "测试 1: 写入测试文件\n";
        FileHandle writer("/tmp/cpp_exercise_test.txt", "w");
        if (writer.is_valid()) {
            writer.write("第一行：Hello C++\n");
            writer.write("第二行：RAII 管理资源\n");
            writer.write("第三行：FileHandle 示例\n");
            std::cout << "  写入完成\n";
        }
    }
    // writer 在此处析构，自动关闭文件
    std::cout << "\n";

    // 测试 2：读取测试文件
    {
        std::cout << "测试 2: 逐行读取\n";
        FileHandle reader("/tmp/cpp_exercise_test.txt");
        if (reader.is_valid()) {
            int line_num = 1;
            while (true) {
                std::string line = reader.read_line();
                if (line.empty() && reader.is_eof()) {
                    break;
                }
                std::cout << "  行 " << line_num << ": \"" << line << "\"\n";
                ++line_num;
            }
        }
    }
    // reader 在此处析构
    std::cout << "\n";

    // 测试 3：打开不存在的文件
    {
        std::cout << "测试 3: 打开不存在的文件\n";
        FileHandle bad("/tmp/nonexistent_file_12345.txt");
        std::cout << "  is_valid() = "
                  << (bad.is_valid() ? "true" : "false") << "\n";
    }

    return 0;
}
