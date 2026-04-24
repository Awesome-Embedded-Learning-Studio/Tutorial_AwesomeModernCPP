/**
 * @file test_assembly_optimization.cpp
 * @brief 验证 unique_ptr 在 -O2 优化下的零开销特性
 *
 * 编译环境：g++ (GCC) 15.2.1, x86_64-linux
 * 编译命令：g++ -std=c++17 -O2 -S test_assembly_optimization.cpp -o test_assembly_optimization.s
 *
 * 预期结果：use_unique_ptr 和 use_raw_ptr 生成相同的汇编代码（仅返回常数 42）
 */

#include <memory>

/**
 * @brief 使用 unique_ptr 管理对象的函数
 * @return 托管对象的值
 */
int use_unique_ptr() {
    auto p = std::make_unique<int>(42);
    return *p;
}

/**
 * @brief 使用裸指针管理对象的函数
 * @return 托管对象的值
 */
int use_raw_ptr() {
    int* p = new int(42);
    int v = *p;
    delete p;
    return v;
}

// 验证：
// 在 -O2 优化级别，两个函数都生成：
//   movl    $42, %eax
//   ret
//
// 这证明了 unique_ptr 在优化后是真正的零开销抽象
