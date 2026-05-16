/*
 * 验证：x86-64 下 square 函数的汇编输出
 *
 * 背景：文章 02-reading-assembly-and-registers-abi.md 中展示了 square 函数
 *       在不同优化等级下的汇编输出。本文用于验证实际编译器输出。
 *
 * 预期结果：
 *   -O2: imul edi,edi; mov eax,edi; ret（三指令，无 lea）
 *   -O0: 完整的栈帧操作，参数从 edi 存入栈再读回
 *
 * 编译命令：
 *   g++ -std=c++20 -O2 -S -o /tmp/square_O2.s 02-08-abi-square-x86.cpp
 *   g++ -std=c++20 -O0 -S -o /tmp/square_O0.s 02-08-abi-square-x86.cpp
 *   g++ -std=c++20 -O2 -c -o /tmp/square_O2.o 02-08-abi-square-x86.cpp
 *   objdump -d -M intel /tmp/square_O2.o
 *
 * 运行：
 *   ./02-08-abi-square-x86
 *   预期输出：square(3) = 9
 *
 * 参考资料：
 *   - System V AMD64 ABI: https://gitlab.com/x86-psABIs/x86-64-ABI
 *
 * 编译器：GCC 16.1.1, x86-64
 */

#include <cstdio>

int square(int x) {
    return x * x;
}

int main() {
    int result = square(3);
    printf("square(3) = %d\n", result);
    return 0;
}
