/*
 * 验证：square 函数在不同优化级别下的汇编输出
 *
 * 背景：文章声称 -O0 下生成 imul eax, DWORD PTR [rbp-4]（从内存读取），
 * -O2 下生成"一条直接的 imul 指令"。实际验证 GCC 16.1.1 的输出。
 *
 * 预期结果：
 *   -O0: imull %eax, %eax（从寄存器自乘，非内存操作数）
 *   -O2: imull %edi, %edi; movl %edi, %eax; ret（两条指令，非一条）
 *
 * 编译命令：
 *   g++ -std=c++20 -O0 -S -o /tmp/square_O0.s 03-01-square-asm.cpp
 *   g++ -std=c++20 -O2 -S -o /tmp/square_O2.s 03-01-square-asm.cpp
 *
 * 编译器：GCC 16.1.1
 */

int square(int x) {
    return x * x;
}

// 防止函数被优化掉
volatile int sink = 0;

int main() {
    sink = square(42);
    return 0;
}
