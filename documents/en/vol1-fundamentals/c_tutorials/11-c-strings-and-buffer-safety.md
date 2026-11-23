---
chapter: 1
cpp_standard:
- 11
- 17
description: Understand the memory model of `\0`-terminated C strings, master the core
  `string.h` functions and safe formatting with `snprintf`, and learn to recognize and
  guard against buffer overflow vulnerabilities
difficulty: beginner
order: 15
platform: host
prerequisites:
- Pointers, Arrays, const, and Null Pointers
reading_time_minutes: 28
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: C Strings and Buffer Safety
translation:
  source: documents/vol1-fundamentals/c_tutorials/11-c-strings-and-buffer-safety.md
  source_hash: fd42eecea265a6ec10e079f6facafd105ee930f0aa760003b4db72acdcd56d4d
  translated_at: '2026-11-22T16:39:43+00:00'
  engine: glm-5.3-refresh
  token_count: 17491
---
# C Strings and Buffer Safety

C has no true "string type"—a lament every developer who moves from C to C++ eventually utters. In the world of C, a string is nothing more than a `char` array that ends with `\0`, and every operation is built on top of that convention. The convention is touchingly simple and heartbreakingly fragile: forget to write that `\0` and your entire program's behavior is undefined; copy a 100-byte string into a 50-byte buffer and you stomp all over the memory behind it.

Countless security vulnerabilities throughout history, from the early Morris Worm to all sorts of recent CVEs, trace back to the same root cause: **buffer overflow**. In this tutorial we're going to take C strings apart from the inside out—understand what they really are, master the safe ways to operate on them, get to know the classic traps, and ultimately lay a solid low-level foundation for the C++ `std::string` you'll meet later.

We strongly recommend adding the `-fsanitize=address` compiler flag while practicing—AddressSanitizer catches the vast majority of out-of-bounds buffer accesses at runtime, making it your safety net for C string operations.

## Step 1 — See What a C String Looks Like in Memory

### It's Just an Array, Plus a `\0`

A C string is, in essence, a `char` array with one extra byte of value `0` (`\0`, the null character) placed after the end of the real content. The compiler doesn't check whether that terminator exists, and neither do the standard library's string functions—maintaining the convention is entirely up to you.

Here's what it actually looks like in memory:

```c
char greeting[] = "Hello";
// 下标：[0] [1] [2] [3] [4] [5]
// 内容：'H' 'e' 'l' 'l' 'o' '\0'
// sizeof(greeting) == 6  （包含终止符）
// strlen(greeting) == 5  （不包含终止符）
```

Here's a point people confuse constantly: the difference between `sizeof` and `strlen`. `sizeof` is a compile-time operator that returns the number of bytes the whole array occupies, `\0` included; `strlen` is a runtime function that counts characters from the start until it meets a `\0`, returning the length without the terminator.

Now compare three ways of initializing:

```c
// 方式一：字符串字面量自动加 \0
char a[] = "Hi";              // sizeof == 3, strlen == 2

// 方式二：逐字符初始化——不会自动加 \0
char b[] = {'H', 'i'};        // sizeof == 2，这不是 C 字符串！

// 方式三：手动加终止符
char c[] = {'H', 'i', '\0'};  // sizeof == 3, strlen == 2，这才是合法的 C 字符串
```

Option 2 is a perfectly legal `char` array, but it is **not** a C string—hand it to `strlen` or `printf("%s")` and the call keeps reading memory onward until it happens to stumble upon a zero byte. That is undefined behavior.

Mixing up `sizeof` and `strlen` is one of the most common beginner mistakes. Remember: `sizeof` is the whole array's size computed at compile time (including `\0`), while `strlen` is the character count obtained at runtime by scanning up to the `\0` (excluding it). Once an array is passed to a function it decays into a pointer, and `sizeof` then returns just the pointer's size—at that point `strlen` is all you have left.

### The Difference Between String Literals and Pointers

String literals are stored in the program's read-only data segment; modifying one is undefined behavior:

```c
const char* s = "Hello";   // s 指向只读内存中的 "Hello\0"
// s[0] = 'h';            // 未定义行为！很可能段错误

char t[] = "Hello";        // 数组拷贝，数据在栈上，可以修改
t[0] = 'h';               // 没问题
```

`const char* s = "Hello"` makes the pointer refer to the string in the read-only data segment, while `char t[] = "Hello"` copies the string's contents into an array on the stack. The former cannot be modified; the latter can. Mix the two up and debugging later on will be a world of pain.

## Step 2 — Master the Core Functions of string.h

`<string.h>` is the core header for string and memory operations in C. We'll look at it in three groups: length and copying, concatenation and comparison, and raw memory operations.

### Length and Copying

`strlen` returns the string's length (terminator excluded); the way it works is a byte-by-byte scan from the start until a `\0` turns up—O(n) time. Calling `strlen` on the same string over and over inside a loop is a classic performance waste.

`strcpy` copies the entire source string into the destination buffer. The problem is that it **doesn't care in the least** how large the destination buffer is—if the source string is longer than the destination buffer, it overflows.

`strncpy` is the length-bounded version, but its behavior is a little subtle: it copies at most `n` characters. If `strlen(src) >= n`, it stops after copying those `n` characters, **without automatically appending a terminator**. That behavior has burned countless people.

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    char src[] = "Hello, World!";  // 13 字符 + \0
    char dst[8];

    strncpy(dst, src, sizeof(dst) - 1);  // 最多复制 7 个字符
    dst[sizeof(dst) - 1] = '\0';          // 手动保证终止！

    printf("dst = \"%s\"\n", dst);
    return 0;
}
```

```bash
gcc -Wall -Wextra -std=c17 str_copy.c -o str_copy && ./str_copy
```

Output:

```text
dst = "Hello, "
```

This pattern shows up again and again in C code: `strncpy` plus a manual `\0` termination. If you ever spot a `strncpy` without `\0`-termination handling right behind it, odds are you're looking at a latent bug.

`strncpy` does not guarantee termination! If the source string's length is >= n, it stops after copying n characters and never appends that `\0` on its own. After every `strncpy` call you must manually write `\0` into the last position.

### Concatenation and Comparison

`strcat` appends the source string to the end of the destination string—and likewise pays no attention to how much space is left in the destination buffer. `strncat` is the length-bounded version: its third argument `n` is the **maximum number of characters to append**, and `strncat` does guarantee a `\0` is added after the appended content (that point differs from `strncpy`).

```c
char buffer[32] = "Hello";
strncat(buffer, ", World", sizeof(buffer) - strlen(buffer) - 1);
// buffer 现在是 "Hello, World"
```

`strcmp` compares two strings character by character and returns `0` when they are equal. Using `==` on two strings compares pointer addresses, not contents—a classic beginner mistake.

```c
if (strcmp(cmd, "START") == 0) {
    start_motor();
}
```

### Memory Operations: memcpy, memmove, memset

These three functions operate on raw memory: they don't care about `\0` terminators, they count bytes, and they handle data of any type.

`memcpy` copies `n` bytes from the source address to the destination address and requires that source and destination do not overlap. `memmove` does the same job but handles overlapping regions correctly—the price being that it may be slightly slower. `memset` sets every byte of a block of memory to a given value.

```c
#include <stdio.h>
#include <string.h>

int main(void)
{
    int src[] = {1, 2, 3, 4, 5};
    int dst[5];

    // 不涉及重叠，用 memcpy
    memcpy(dst, src, sizeof(src));

    // 在同一数组内移动——涉及重叠，必须用 memmove
    memmove(src + 1, src, 3 * sizeof(int));

    printf("dst: %d %d %d %d %d\n", dst[0], dst[1], dst[2], dst[3], dst[4]);
    printf("src: %d %d %d %d %d\n", src[0], src[1], src[2], src[3], src[4]);
    return 0;
}
```

Output:

```text
dst: 1 2 3 4 5
src: 1 1 2 3 5
```

Using `memcpy` on overlapping regions is undefined behavior. If you're not sure whether two blocks of memory overlap, just use `memmove`—the performance difference is negligible, but the safety difference is night and day.

## Step 3 — Safe Formatting with snprintf

`sprintf` is the function that formats output into a string, but like `strcpy` it ignores the destination buffer's size. `snprintf` is its safe counterpart: the second argument gives the buffer size, and the function guarantees that no more bytes than that ever get written (terminator included).

```c
#include <stdio.h>

int main(void)
{
    char buf[32];
    int value = 42;
    const char* unit = "degrees";

    int written = snprintf(buf, sizeof(buf), "Temperature: %d %s", value, unit);
    printf("Result: \"%s\"\n", buf);
    printf("Written: %d, Buffer size: %zu\n", written, sizeof(buf));

    if (written >= (int)sizeof(buf)) {
        printf("Output was truncated!\n");
    }
    return 0;
}
```

```bash
gcc -Wall -Wextra -std=c17 snprintf_demo.c -o snprintf_demo && ./snprintf_demo
```

Output:

```text
Result: "Temperature: 42 degrees"
Written: 23, Buffer size: 32
```

`snprintf`'s return value is remarkably useful: it returns **how many characters would have been written without truncation** (terminator excluded). If that value is greater than or equal to the buffer size, the output was truncated.

In embedded development, `snprintf` is pretty much the only recommended way to build strings—log formatting, stitching together sensor data, assembling commands for communication protocols: all of it should go through `snprintf`.

## Step 4 — Understand Why Buffer Overflow Is So Dangerous

We've brought up "buffer overflow" again and again by now; it's time to formally take it apart and see what is actually happening.

### The Classic Overflow Scenario

The essence of a buffer overflow is simple: more data gets written into a buffer than it can hold, the excess spills into adjacent memory regions, and data that was never supposed to change gets overwritten. Overflows of buffers on the stack are especially dangerous, because a function's return address lives in its stack frame—an attacker can craft an over-long input to overwrite that return address and make the program jump to code of the attacker's choosing. The Morris Worm spread through exactly this kind of attack in 1988.

```c
#include <stdio.h>
#include <string.h>

void vulnerable_function(const char* user_input)
{
    char buffer[16];
    strcpy(buffer, user_input);  // 如果 user_input 长度 >= 16，溢出！
    printf("You said: %s\n", buffer);
}
```

### Three Lines of Defense

The first line of defense: **always use length-bounded functions**.

| Dangerous function | Safer replacement | Notes |
|----------|----------|------|
| `strcpy` | `strncpy` + manual termination | or switch to `snprintf` |
| `strcat` | `strncat` | mind what the third argument means |
| `sprintf` | `snprintf` | the preferred choice |
| `gets` | `fgets` | `gets` was removed entirely in C11 |
| `scanf("%s")` | `%Ns` or `fgets` + `sscanf` | specify a maximum width |

The second line of defense is compiler flags. `-fstack-protector` inserts a canary value into the stack frame and checks whether it has been tampered with before the function returns. `-D_FORTIFY_SOURCE=2` has the compiler replace unsafe functions with safe versions at compile time.

The third line of defense is AddressSanitizer (`-fsanitize=address`), which pinpoints exactly where each out-of-bounds read or write happens.

```bash
# 推荐的开发编译命令
gcc -std=c17 -Wall -Wextra -g -fsanitize=address -fstack-protector-all your_code.c
```

## Bridging to C++

If you've been typing along all the way to this point, you've probably felt how tedious C string operations are—after every `strncpy` you must add a `\0` by hand, and before every concatenation you must compute the remaining space. C++ solves these problems at the root through a few core components.

`std::string` maintains a dynamically allocated character array internally and handles `\0` termination, memory allocation and release, and capacity growth automatically. You never specify a buffer size by hand, and you never worry about overflow:

```cpp
#include <string>

std::string s1 = "Hello";
std::string s2 = "World";
std::string result = s1 + ", " + s2 + "!";  // 自动扩容
printf("C string: %s\n", result.c_str());    // 和 C API 无障碍交互
```

`std::string_view` (C++17) does not own the string data; it holds just a pointer and a length—in essence a wrapper around `(const char*, size_t)`. Passing one as an argument is zero-copy, and it accepts both C strings and `std::string`. Just remember that it doesn't own the data: a `string_view` pointing at a temporary is the classic dangling-reference trap.

With these two tools in hand, `strcpy`, `strcat`, `sprintf`, and `strlen` should hardly ever appear directly in C++ code again. Of course, when interfacing with C APIs, or in extremely resource-constrained embedded environments, these functions are still necessary—which is exactly why we spent a whole article learning them.

## Common Pitfalls

| Pitfall | What goes wrong | Fix |
|------|------|----------|
| `strncpy` doesn't guarantee termination | no `\0` is appended when the source string's length is >= n | always set the last byte to `\0` manually |
| Comparing strings with `==` | compares pointer addresses, not contents | use `strcmp` |
| Modifying a string literal | stored in a read-only segment; writing to it triggers a segfault | use an array copy: `char s[] = "Hello"` |
| `strncat`'s third argument | it's the "maximum number of characters to append", not the buffer's total size | first confirm within bounds that `dst` is terminated, then subtract the current length and the terminator's slot from the capacity |
| `memcpy` on overlapping regions | undefined behavior | use `memmove` when the regions overlap |

## Exercises

### Exercise 1: A Safe String Library

**Difficulty: Intermediate** · Wrapping buffer-size-aware string operations

Implement two safe string functions that both know the size of the destination buffer and handle truncation and termination automatically:

```c
#include <stddef.h>

/// @brief 安全地复制字符串到目标缓冲区
/// @param dst 目标缓冲区
/// @param src 源字符串
/// @param dst_size 目标缓冲区总大小（含终止符）
/// @return 源字符串的完整长度（不含终止符）；返回值 >= dst_size 表示发生截断；
///         dst/src 为 NULL 或 dst_size 为 0 时返回 0
size_t safe_str_copy(char* dst, const char* src, size_t dst_size);

/// @brief 安全地拼接字符串
/// @param dst 目标缓冲区（已有内容）
/// @param src 要追加的字符串
/// @param dst_size 目标缓冲区总大小（含终止符）
/// @return 完整拼接所需的总长度（不含终止符）；返回值 >= dst_size 表示发生截断；
///         dst/src 为 NULL 或 dst_size 为 0 时返回 0
size_t safe_str_cat(char* dst, const char* src, size_t dst_size);
```

Hints: `safe_str_copy` can be built on `strncpy`, but since `strncpy` won't write the terminator for you when src is too long, you have to add it yourself; once the source string's full length is returned, the caller can use `return value >= dst_size` to detect truncation. `safe_str_cat` must first determine `dst`'s current length within the `dst_size` bound, then compute the remaining usable space. `src` and `dst` may overlap, but `dst_size` must be the destination buffer's true capacity.

::: details Reference Solution

```c
#include <stddef.h>
#include <stdio.h>
#include <string.h>

size_t safe_str_copy(char *dst, const char *src, size_t dst_size)
{
    size_t source_length = 0;
    size_t copy_length = 0;

    if (dst == NULL || src == NULL || dst_size == 0)
    {
        return 0;
    }

    // 先扫描完整源串，既能报告截断，也能避免重叠复制时读取到已覆盖的数据。
    while (src[source_length] != '\0')
    {
        source_length++;
    }

    copy_length = source_length < dst_size - 1 ? source_length : dst_size - 1;

    memmove(dst, src, copy_length);
    dst[copy_length] = '\0';

    return source_length;
}

size_t safe_str_cat(char *dst, const char *src, size_t dst_size)
{
    size_t dst_length = 0;
    size_t src_length = 0;
    size_t copy_length = 0;
    size_t available;

    if (dst == NULL || src == NULL || dst_size == 0)
    {
        return 0;
    }

    // 只在目标缓冲区范围内查找终止符，避免 strlen 越界读取。
    while (dst_length < dst_size && dst[dst_length] != '\0')
    {
        dst_length++;
    }

    // 调用者传入的 dst 不是以空字符结尾的字符串时，先截断并终止它。
    if (dst_length == dst_size)
    {
        dst[dst_size - 1] = '\0';
        return dst_size; // 目标串无终止符，按截断情况报告
    }

    // 先完成源串长度扫描，再写目标，避免 src/dst 重叠时读到刚写入的数据。
    while (src[src_length] != '\0')
    {
        src_length++;
    }

    available = dst_size - dst_length - 1;
    if (src_length < available)
    {
        copy_length = src_length;
    }
    else
    {
        copy_length = available;
    }

    // memmove 支持源、目标区域重叠；终止符不计入 copy_length。
    memmove(dst + dst_length, src, copy_length);
    dst[dst_length + copy_length] = '\0';

    return dst_length + src_length; // 返回未截断时的完整长度（不含终止符）
}

int main(void)
{
    char copied_text[32]; // 目标缓冲区
    char short_buffer[8]; // 较小的缓冲区
    char combined_text[32] = "STM32"; // 已有内容的目标缓冲区
    char short_combined[8] = "STM32"; // 用于演示拼接截断
    char self_combined[16] = "ab"; // 用于演示源、目标重叠
    size_t copied_length; // 源字符串的完整长度
    size_t short_length; // 源字符串的完整长度
    size_t combined_length; // 拼接后的总字节数
    size_t short_combined_length; // 完整拼接所需的长度
    size_t self_combined_length; // 重叠拼接后的完整长度

    copied_length = safe_str_copy(copied_text, "Hello, Embedded!",
                                  sizeof(copied_text));
    printf("复制结果：%s\n", copied_text);
    printf("源字符串的完整长度：%u\n", (unsigned int)copied_length);

    short_length = safe_str_copy(short_buffer, "Embedded",
                                 sizeof(short_buffer));
    printf("缓冲区较小时的截断结果：%s\n", short_buffer);
    printf("源字符串的完整长度：%u\n", (unsigned int)short_length);

    combined_length = safe_str_cat(combined_text, " Board",
                                   sizeof(combined_text));
    printf("拼接结果：%s\n", combined_text);
    printf("拼接后的总字节数：%u\n", (unsigned int)combined_length);

    short_combined_length = safe_str_cat(short_combined, " Board",
                                         sizeof(short_combined));
    printf("空间不足时的拼接结果：%s\n", short_combined);
    printf("完整拼接所需的字节数：%u\n", (unsigned int)short_combined_length);

    self_combined_length = safe_str_cat(self_combined, self_combined,
                                        sizeof(self_combined));
    printf("源、目标重叠时的拼接结果：%s\n", self_combined);
    printf("完整拼接所需的字节数：%u\n", (unsigned int)self_combined_length);

    return 0;
}

```

Compile and run:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic safe_strings.c -o safe_strings && ./safe_strings
```

Output:

```text
复制结果：Hello, Embedded!
源字符串的完整长度：16
缓冲区较小时的截断结果：Embedde
源字符串的完整长度：8
拼接结果：STM32 Board
拼接后的总字节数：11
空间不足时的拼接结果：STM32 B
完整拼接所需的字节数：11
源、目标重叠时的拼接结果：abab
完整拼接所需的字节数：4
```

`safe_str_cat` first looks for `dst`'s terminator within the `dst_size` bound. If the entire buffer contains no `\0`,
the function changes the last byte to `\0` and returns `dst_size`, which the caller may treat as truncation or invalid input. With normal input,
the return value is the length a full concatenation would require; a return value greater than or equal to `dst_size` means the destination string was truncated. The function requires
`src` to be a valid `\0`-terminated string, and `dst_size` to be the capacity of `dst`'s actual buffer.

`safe_str_copy` likewise returns the source string's full length rather than the number of bytes actually written; a return value greater than or equal to `dst_size`
therefore indicates truncation. That way, even a source string that exactly fills the buffer cannot be confused with the truncated case.

:::

**Challenge extension** (optional): add a `safe_str_format(char* dst, size_t dst_size, const char* format, ...)`. It requires `vsnprintf` and the variadic-argument machinery from `<stdarg.h>` (not covered in this article, and the functions article only touches on it), so please look up `vsnprintf` on cppreference before implementing it.

::: details Challenge Extension Reference Solution (Optional)

```c
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>

/// @brief 安全地格式化字符串
/// @param dst 目标缓冲区
/// @param dst_size 目标缓冲区总大小（含终止符）
/// @param format 格式字符串
/// @param ... 可变参数
/// @return 格式化后字符串的总长度（不含终止符）
size_t safe_str_format(char *dst, size_t dst_size, const char *format, ...);

size_t safe_str_format(char *dst, size_t dst_size, const char *format, ...)
{
    if (dst == NULL || format == NULL || dst_size == 0)
    {
        return 0;
    }

    dst[0] = '\0';

    va_list args;
    va_start(args, format);
    int written = vsnprintf(dst, dst_size, format, args);
    va_end(args);

    // 无论格式化是否成功，都保证缓冲区以空字符结尾。
    dst[dst_size - 1] = '\0';

    // 返回“如果缓冲区足够大本来需要写入的长度”，截断时也保留这个信息。
    // 返回负数表示格式化失败，此时返回 0。
    return written >= 0 ? (size_t)written : 0;
}

int main(void)
{
    char message[64]; // 缓冲区
    char short_message[12]; // 较小的缓冲区
    size_t message_length; // 格式化后消息的长度
    size_t short_length; // 完整格式化结果的长度

    message_length = safe_str_format(message, sizeof(message),
                                     "设备：%s，温度：%d 摄氏度",
                                     "STM32", 26);
    printf("正常格式化结果：%s\n", message);
    printf("完整格式化结果的长度：%u\n", (unsigned int)message_length);

    short_length = safe_str_format(short_message, sizeof(short_message),
                                   "ID=%d,STATUS=%s", 1001, "OK");
    printf("缓冲区不足时的截断结果：%s\n", short_message);
    printf("完整格式化结果的长度：%u\n", (unsigned int)short_length);

    return 0;
}

```

Compile and run:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic safe_str_format.c -o safe_str_format && ./safe_str_format
```

Output:

```text
正常格式化结果：设备：STM32，温度：26 摄氏度
完整格式化结果的长度：38
缓冲区不足时的截断结果：ID=1001,STA
完整格式化结果的长度：17
```

`vsnprintf`'s return value is "the number of bytes that would have been written had the buffer been large enough". The solution preserves
truncation information, so the caller can use a return value `>= dst_size` to detect truncation, and it explicitly guarantees that the last byte is `\0`.

:::

### Exercise 2: A String Splitting Function

**Difficulty: Basic** · Walk a string with pointers and slice it on a delimiter

Implement a function that splits a string on a delimiter:

```c
/// @brief 将字符串按分隔符切分，返回各子串的起止位置
/// @param input 待分割的字符串（函数不会修改 input）
/// @param delim 分隔字符（单字符）
/// @param out_starts 输出数组：各子串的起始位置
/// @param out_lengths 输出数组：各子串的长度
/// @param max_tokens out_starts/out_lengths 数组的容量
/// @return 实际找到的子串数量
size_t str_split(
    const char* input,
    char delim,
    const char** out_starts,
    size_t* out_lengths,
    size_t max_tokens
);
```

Contract: empty fields are kept. For example, `"a,,b,"` yields four fields: `"a"`, `""`, `"b"`, `""`;
if the output arrays lack sufficient capacity, only the first `max_tokens` fields already written are returned.

Hints: walk through `input`, recording each field's start pointer and length. A delimiter ends the current field, and when you reach the
`\0` you must also record the final field. The function neither copies nor modifies the original string.

::: details Reference Solution

```c
#include <stddef.h>
#include <stdio.h>

size_t str_split(
    const char* input,
    char delim,
    const char** out_starts,
    size_t* out_lengths,
    size_t max_tokens
)
{
    // 检查输入指针和输出数组是否有效，同时确保输出数组至少能容纳一个子串。
    if (input == NULL || out_starts == NULL ||
        out_lengths == NULL || max_tokens == 0)
    {
        return 0;
    }

    size_t token_count = 0;
    const char* start = input; // 当前子串的起始位置
    const char* end = input;   // 当前扫描位置

    // 从左向右扫描字符串；输出数组写满后立即停止。
    while (*end != '\0' && token_count < max_tokens)
    {
        if (*end == delim)
        {
            // 只记录子串的起始指针和长度，不复制或修改原字符串。
            out_starts[token_count] = start;
            out_lengths[token_count] = (size_t)(end - start);
            token_count++;

            // 跳过当前分隔符，下一个字符是下一段的起点。
            start = end + 1;
        }

        end++;
    }

    // 无论最后一个字段是否为空，都要记录它（例如 "a,b," 的末尾空字段）。
    if (token_count < max_tokens)
    {
        out_starts[token_count] = start;
        out_lengths[token_count] = (size_t)(end - start);
        token_count++;
    }

    return token_count;
}

int main(void)
{
    const char* input = "温度,湿度,气压"; // 原始字符串
    const char delimiter = ','; // 分割符号
    const char* token_starts[16]; // 存储子串起始指针的数组
    size_t token_lengths[16]; // 存储子串长度的数组
    size_t token_count; // 实际分割出的子串数量
    size_t i;

    token_count = str_split(input, delimiter,
                            token_starts, token_lengths,
                            sizeof(token_starts) / sizeof(token_starts[0]));

    printf("原始字符串：%s\n", input);
    printf("使用分隔符：%c\n", delimiter);
    printf("共分割出 %u 个字段：\n", (unsigned int)token_count);

    for (i = 0; i < token_count; i++)
    {
        printf("第 %u 个字段：%.*s（长度为 %u 字节）\n",
               (unsigned int)(i + 1),
               (int)token_lengths[i], token_starts[i],
               (unsigned int)token_lengths[i]);
    }

    return 0;
}

```

Compile and run:

```bash
gcc -std=c17 -Wall -Wextra -Wpedantic str_split.c -o str_split && ./str_split
```

```text
原始字符串：温度,湿度,气压
使用分隔符：,
共分割出 3 个字段：
第 1 个字段：温度（长度为 6 字节）
第 2 个字段：湿度（长度为 6 字节）
第 3 个字段：气压（长度为 6 字节）
```

`str_split` returns pointers and lengths into the original string, so those pointers are usable only while `input` remains valid.
Printing uses `%.*s` together with a length, so no field needs its own `\0` terminator; if you need to store fields
independently, copy them into a caller-provided buffer. An empty string is also treated as one empty field; for example `""` returns a single field of length
0, and `",,"` returns three fields each of length 0. When the output arrays lack capacity, it still returns only the first `max_tokens` fields.

:::

## References

- [string.h - cppreference](https://en.cppreference.com/w/c/string/byte)
- [stdio.h formatting functions - cppreference](https://en.cppreference.com/w/c/io)
- [Buffer Overflow - OWASP](https://owasp.org/www-community/vulnerabilities/Buffer_Overflow)
