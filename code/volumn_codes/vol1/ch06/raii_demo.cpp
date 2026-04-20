// destructor.cpp
// 编译：g++ -std=c++17 -o destructor destructor.cpp

#include <chrono>
#include <cstdio>
#include <iostream>

/// @brief 作用域计时器
class ScopedTimer {
    const char* label_;
    std::chrono::steady_clock::time_point start_;
public:
    explicit ScopedTimer(const char* label)
        : label_(label), start_(std::chrono::steady_clock::now())
    { std::cout << "[" << label_ << "] started" << std::endl; }

    ~ScopedTimer() {
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start_);
        std::cout << "[" << label_ << "] finished: "
                  << us.count() << " us" << std::endl;
    }
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
};

/// @brief 自动管理 FILE* 的文件写入器
class FileWriter {
    FILE* handle_;
    const char* path_;
public:
    FileWriter(const char* path, const char* mode)
        : handle_(std::fopen(path, mode)), path_(path)
    {
        if (!handle_) std::cerr << "Error: cannot open " << path << std::endl;
    }

    ~FileWriter() {
        if (handle_) {
            std::fclose(handle_);
            std::cout << "[" << path_ << "] closed" << std::endl;
        }
    }

    void write_line(const char* text) {
        if (handle_) { std::fputs(text, handle_); std::fputc('\n', handle_); }
    }

    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;
};

int main() {
    std::cout << "--- RAII demo ---" << std::endl;
    ScopedTimer total("total");

    {
        ScopedTimer phase("phase 1: file writing");
        FileWriter writer("raii_demo.txt", "w");
        writer.write_line("Hello from RAII!");
        writer.write_line("No manual fclose needed.");
    }

    {
        ScopedTimer phase("phase 2: computation");
        volatile int sum = 0;
        for (int i = 0; i < 1000000; ++i) { sum += i; }
    }

    std::cout << "--- end of main ---" << std::endl;
    return 0;
}
