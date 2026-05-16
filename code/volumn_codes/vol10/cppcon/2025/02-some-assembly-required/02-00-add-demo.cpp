/*
 * 验证：add(int, int) 在 -O0 和 -O1 下的汇编输出
 *
 * 背景：文章 01-personal-journey-and-from-assembly-to-cpp.md 展示了 add 函数的汇编输出，
 *       需要验证其准确性。
 *
 * 预期结果：
 *   -O0: 参数进栈再读回，寄存器做加法，有 pushq/movq 栈帧建立
 *   -O1: 直接 leal (%rdi,%rsi), %eax，main 里常量折叠为 movl $7, %eax
 *
 * 编译命令：
 *   g++ -std=c++17 -O0 -S -o /tmp/add_demo_O0.s 02-00-add-demo.cpp
 *   g++ -std=c++17 -O1 -S -o /tmp/add_demo_O1.s 02-00-add-demo.cpp
 *
 * 运行：
 *   g++ -std=c++17 -O0 -o /tmp/add_demo 02-00-add-demo.cpp && /tmp/add_demo
 *   预期退出码：7
 *
 * 参考资料：
 *   - System V ABI AMD64: https://gitlab.com/x86-psABIs/x86-64-ABI
 *
 * 编译器：GCC 16.1.1 20260430
 */

int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(3, 4);
    return result;
}
