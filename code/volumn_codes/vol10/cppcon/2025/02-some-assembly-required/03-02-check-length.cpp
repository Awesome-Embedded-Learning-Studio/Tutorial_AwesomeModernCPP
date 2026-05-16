/*
 * 验证：string_view 按值传递时的寄存器分配和汇编输出
 *
 * 背景：文章声称在 GCC/libstdc++ 下 string_view 的指针在 RDI、长度在 RSI。
 * 实际验证：GCC 的 libstdc++ 中 string_view 布局为 {size_t len; const char* str}，
 * 因此 RDI = 长度、RSI = 指针。此外 GCC -O1 使用 sete（无分支）而非 jne+xorl。
 *
 * 预期结果：
 *   GCC 16.1.1 -O1: cmpq $16, %rdi; sete %al; ret
 *   （RDI 存放 size，使用 sete 无分支返回）
 *
 * 编译命令：
 *   g++ -std=c++20 -O1 -S -o /tmp/check_length.s 03-02-check-length.cpp
 *
 * 编译器：GCC 16.1.1
 */

#include <string_view>

bool check_length(std::string_view sv) {
    if (sv.size() == 16) {
        return true;
    }
    return false;
}

// 使用指针和长度，验证寄存器分配
bool use_both(std::string_view sv) {
    return sv.size() == 16 && sv[0] == 'A';
}

volatile bool sink = false;

int main() {
    sink = check_length("ABCDEFGHIJKLMNOP");
    sink = use_both("ABCDEFGHIJKLMNOP");
    return 0;
}
