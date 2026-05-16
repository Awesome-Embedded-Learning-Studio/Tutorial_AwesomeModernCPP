/*
 * count_char 函数 —— 验证 GCC 自动向量化输出
 *
 * 用于在 Compiler Explorer (godbolt.org) 上获取真实的汇编输出，
 * 替换文章中编造的汇编代码。
 *
 * 编译命令（获取汇编）：
 *   g++ -O2 -march=x86-64-v2 -S -fno-exceptions 05-04-count-char-vec.cpp -o -
 *   g++ -O3 -march=x86-64-v2 -S -fno-exceptions 05-04-count-char-vec.cpp -o -
 *
 * 在 Compiler Explorer 上：
 *   编译器选 GCC 13.x，参数填 -O2 -march=x86-64-v2
 *   观察是否出现 pcmpeqb / pmovmskb / popcnt 等 SIMD 指令
 *
 * 注意：
 *   - 需要添加 __attribute__((noinline)) 或编译为单独的翻译单元，
 *     否则编译器可能直接内联并优化掉整个函数
 *   - -march=x86-64-v2 启用 SSE4.2（包含 pcmpeqb 等）
 *   - 如果 GCC 选择调用 libc 的 strnlen/memchr 等，尝试 -fno-builtin
 */

#include <cstddef>

// noinline 防止编译器在测试场景中直接内联
__attribute__((noinline))
size_t count_char(const char* str, char ch) {
    size_t count = 0;
    while (*str) {
        if (*str == ch) {
            ++count;
        }
        ++str;
    }
    return count;
}

// 提供一个 main 作为入口，但重点看 count_char 的汇编输出
int main() {
    // volatile 防止编译器在常量传播后直接算出结果
    volatile const char* msg = "hello world, this is a test string for vectorization";
    volatile char target = 'o';
    volatile size_t result = count_char(
        const_cast<const char*>(msg),
        static_cast<char>(target));
    (void)result;
    return 0;
}
