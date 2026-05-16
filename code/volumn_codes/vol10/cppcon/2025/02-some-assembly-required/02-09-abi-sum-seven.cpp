/*
 * 验证：x86-64 下超过 6 个参数的函数调用，第 7 个参数的栈偏移
 *
 * 背景：System V AMD64 ABI 规定前 6 个整数参数走寄存器（RDI, RSI, RDX, RCX, R8, R9），
 *       第 7 个参数在栈上。call 指令将返回地址压入 [rsp]，因此第 7 个参数在 [rsp+8]。
 *       原文章错误地写成了 [rsp]。
 *
 * 预期结果：
 *   汇编中访问第 7 个参数使用 [rsp+0x8]，而非 [rsp]
 *   运行输出：sum_seven(1,2,3,4,5,6,7) = 28
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -c -o /tmp/sum_seven.o 02-09-abi-sum-seven.cpp
 *   objdump -d -M intel /tmp/sum_seven.o
 *
 * 运行：
 *   ./02-09-abi-sum-seven
 *
 * 参考资料：
 *   - System V AMD64 ABI, Section 3.2.3: Parameter Passing
 *
 * 编译器：GCC 16.1.1, x86-64
 */

#include <cstdio>

long sum_seven(long a, long b, long c, long d,
               long e, long f, long g) {
    return a + b + c + d + e + f + g;
}

int main() {
    long result = sum_seven(1, 2, 3, 4, 5, 6, 7);
    printf("sum_seven(1,2,3,4,5,6,7) = %ld\n", result);
    return 0;
}
