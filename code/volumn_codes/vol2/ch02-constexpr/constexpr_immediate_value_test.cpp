/**
 * @file constexpr_immediate_value_test.cpp
 * @brief 验证 constexpr 变量是否生成立即数的测试代码
 *
 * 编译环境：GCC 15.2.1, C++17, -O2 优化
 * 验证目的：
 * 1. 验证 constexpr 变量确实被编译为立即数
 * 2. 对比 const 和 constexpr 的汇编输出
 * 3. 提供可查看汇编的测试案例
 *
 * 查看汇编命令：
 * g++ -std=c++17 -O2 -S -o constexpr_immediate_value_test.s constexpr_immediate_value_test.cpp
 */

#include <cstdint>

// constexpr 变量：编译期常量
constexpr int kBufferSize = 256;
constexpr int kMask = kBufferSize - 1;

// const 变量：用编译期常量初始化
const int kConstSize = 256;

// 测试函数：返回 constexpr 变量
int get_buffer_size()
{
    return kBufferSize;
}

// 测试函数：返回 const 变量
int get_const_size()
{
    return kConstSize;
}

// 测试函数：返回 constexpr 计算结果
int get_mask()
{
    return kMask;
}

int main()
{
    int size1 = get_buffer_size();
    int size2 = get_const_size();
    int mask = get_mask();

    return size1 + size2 + mask;
}

/*
 * 预期汇编输出（GCC 15.2.1, -O2）：
 *
 * get_buffer_size():
 *     movl    $256, %eax
 *     ret
 *
 * get_const_size():
 *     movl    $256, %eax
 *     ret
 *
 * get_mask():
 *     movl    $255, %eax
 *     ret
 *
 * 结论：在 -O2 优化下，const 和 constexpr 变量都被编译为立即数
 * 这验证了文章中关于"编译器给你算好了写个立即数"的断言
 */
