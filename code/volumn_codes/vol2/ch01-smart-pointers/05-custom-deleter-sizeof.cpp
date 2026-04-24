/**
 * @file 05-custom-deleter-sizeof.cpp
 * @brief 验证不同删除器类型对 unique_ptr 大小的影响
 * @details 测试函数指针、lambda、函数对象作为删除器时的内存占用
 * @compile g++ -std=c++17 -O0 -o 05-custom-deleter-sizeof 05-custom-deleter-sizeof.cpp
 * @run ./05-custom-deleter-sizeof
 */

#include <cstdio>
#include <memory>
#include <iostream>
#include <cstdlib>

// 函数指针删除器
void close_file(FILE* f) noexcept {
    if (f) {
        std::fclose(f);
    }
}

// Lambda 删除器（无捕获）
auto file_closer_lambda = [](FILE* f) noexcept {
    if (f) std::fclose(f);
};

// 函数对象删除器（空类，可 EBO）
struct FcloseDeleter {
    void operator()(FILE* f) noexcept {
        if (f) std::fclose(f);
    }
};

// 空删除器（用于测试 EBO）
struct EmptyDeleter {
    void operator()(int* p) noexcept { delete p; }
};

// 有状态删除器（无法 EBO）
struct StatefulDeleter {
    int extra_data = 0;
    void operator()(int* p) noexcept { delete p; }
};

// free 删除器
struct FreeDeleter {
    void operator()(void* p) noexcept {
        std::free(p);
    }
};

int main() {
    std::cout << "=== 自定义删除器 sizeof 测试 ===\n";
    std::cout << "平台: x86_64-linux-gnu\n";
    std::cout << "编译器: g++ " << __VERSION__ << "\n\n";

    std::cout << "基础类型大小:\n";
    std::cout << "  sizeof(FILE*):                        " << sizeof(FILE*) << "\n";
    std::cout << "  sizeof(int*):                          " << sizeof(int*) << "\n";
    std::cout << "  sizeof(void(*)(FILE*)):                " << sizeof(void(*)(FILE*)) << "\n\n";

    std::cout << "unique_ptr<int> 变体:\n";
    std::cout << "  sizeof(unique_ptr<int>):               " << sizeof(std::unique_ptr<int>) << "\n";
    std::cout << "  sizeof(unique_ptr<int, EmptyDeleter>): " << sizeof(std::unique_ptr<int, EmptyDeleter>) << "\n";
    std::cout << "  sizeof(unique_ptr<int, StatefulDeleter>): " << sizeof(std::unique_ptr<int, StatefulDeleter>) << "\n";
    std::cout << "  sizeof(unique_ptr<int, void(*)(int*)>): " << sizeof(std::unique_ptr<int, void(*)(int*)>) << "\n\n";

    std::cout << "unique_ptr<FILE> 变体:\n";
    std::cout << "  sizeof(unique_ptr<FILE, void(*)(FILE*)>):   " << sizeof(std::unique_ptr<FILE, void(*)(FILE*)>) << "\n";
    std::cout << "  sizeof(unique_ptr<FILE, decltype(file_closer_lambda)>): " << sizeof(std::unique_ptr<FILE, decltype(file_closer_lambda)>) << "\n";
    std::cout << "  sizeof(unique_ptr<FILE, FcloseDeleter>):     " << sizeof(std::unique_ptr<FILE, FcloseDeleter>) << "\n\n";

    std::cout << "unique_ptr<char, FreeDeleter>: " << sizeof(std::unique_ptr<char, FreeDeleter>) << "\n\n";

    // 测试捕获 lambda（有状态）
    int log_fd = 42;
    auto logging_closer = [log_fd](FILE* f) noexcept {
        if (f) std::fclose(f);
    };
    std::cout << "unique_ptr<FILE, decltype(logging_closer)> (捕获 lambda): "
              << sizeof(std::unique_ptr<FILE, decltype(logging_closer)>) << "\n";

    std::cout << "\n=== 结论 ===\n";
    std::cout << "1. 空删除器（函数对象、无捕获 lambda）= 0 开销（EBO）\n";
    std::cout << "2. 函数指针删除器 = 8 字节开销\n";
    std::cout << "3. 有状态删除器 = 状态大小 + 对齐开销\n";

    return 0;
}
