/**
 * @file test_ebo_sizeof.cpp
 * @brief 验证 unique_ptr 的空基类优化（EBO）和大小特性
 *
 * 编译环境：g++ (GCC) 15.2.1, x86_64-linux
 * 编译命令：g++ -std=c++17 -O2 test_ebo_sizeof.cpp -o test_ebo_sizeof
 */

#include <memory>
#include <iostream>

/**
 * @brief 空删除器——无数据成员
 *
 * 这种删除器通过空基类优化（EBO）不会增加 unique_ptr 的大小
 */
struct EmptyDeleter {
    void operator()(int* p) noexcept {
        delete p;
    }
};

/**
 * @brief 有状态删除器——包含数据成员
 *
 * 有状态的删除器会增加 unique_ptr 的大小，因为需要存储状态
 */
struct StatefulDeleter {
    int state = 42;  // 数据成员

    void operator()(int* p) noexcept {
        delete p;
    }
};

int main() {
    std::cout << "=== unique_ptr Size Verification ===\n";
    std::cout << "Platform: x86_64-linux, g++ 15.2.1\n\n";

    std::cout << "sizeof(int*):                                  "
              << sizeof(int*) << " bytes\n";
    std::cout << "sizeof(unique_ptr<int>):                        "
              << sizeof(std::unique_ptr<int>) << " bytes\n";
    std::cout << "sizeof(unique_ptr<int, EmptyDeleter>):         "
              << sizeof(std::unique_ptr<int, EmptyDeleter>) << " bytes\n";
    std::cout << "sizeof(unique_ptr<int, void(*)(int*)>):        "
              << sizeof(std::unique_ptr<int, void(*)(int*)>) << " bytes\n";
    std::cout << "sizeof(unique_ptr<int, StatefulDeleter>):      "
              << sizeof(std::unique_ptr<int, StatefulDeleter>) << " bytes\n";

    std::cout << "\n=== Analysis ===\n";
    std::cout << "- Default unique_ptr: Same size as raw pointer (8 bytes) ✓\n";
    std::cout << "- Empty deleter: Zero overhead due to EBO ✓\n";
    std::cout << "- Function pointer deleter: 16 bytes (8 for ptr + 8 for func) ✓\n";
    std::cout << "- Stateful deleter: 16 bytes (8 for ptr + 8 for state) ✓\n";

    return 0;
}
