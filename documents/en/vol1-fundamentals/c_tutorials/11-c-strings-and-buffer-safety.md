---
title: C Strings and Buffer Safety
description: Understand the null-terminated memory model of C strings, master core
  `string.h` functions and safe formatting with `snprintf`, and identify and prevent
  buffer overflow vulnerabilities.
chapter: 1
order: 15
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
difficulty: beginner
platform: host
reading_time_minutes: 20
cpp_standard:
- 11
- 17
prerequisites:
- 指针与数组、const 和空指针
translation:
  source: documents/vol1-fundamentals/c_tutorials/11-c-strings-and-buffer-safety.md
  source_hash: eae877481b61978cb41bf9130f93eedaf517e01ec8f99a0e441271327adbfc8d
  translated_at: '2026-04-20T03:28:56.068417+00:00'
  engine: anthropic
  token_count: 2346
---
# C Strings and Buffer Safety

C has no real "string type"—every developer transitioning from C to C++ makes this observation. In the C world, a string is simply a `char` array terminated by `\0`, and all operations are built on this convention. This convention is simple enough to be elegant, yet fragile enough to be maddening—forget that `\0`, and the entire program exhibits undefined behavior (UB); copy a 100-byte string into a 50-byte buffer, and you trash the memory right after it.

Countless security vulnerabilities throughout history, from the early Morris Worm to modern CVEs, trace back to one root cause: **buffer overflow**. In this tutorial, we tear C strings apart from the inside out, understand their true nature, master safe operation techniques, recognize classic pitfalls, and ultimately build a solid low-level foundation for learning C++ `std::string` later on.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the `\0`-terminated memory model of C strings
> - [ ] Proficiently use core string and memory manipulation functions in `string.h`
> - [ ] Master `snprintf` for safe formatted output
> - [ ] Identify and prevent buffer overflow vulnerabilities

## Environment Setup

We run all subsequent experiments in this environment:

- Platform: Linux x86\_64 (WSL2 is also fine)
- Compiler: GCC 13+ or Clang 17+
- Compiler flags: `-Wall -Wextra -std=c17`

We strongly recommend adding the `-fsanitize=address` compiler flag during practice—AddressSanitizer catches most out-of-bounds memory accesses at runtime, serving as a safety net for C string operations.

## Step One — Understand What a C String Looks Like in Memory

### It's Just an Array, Plus a `\0`

A C string is essentially a `char` array with an extra byte of value `0` (`\0`, the null character) appended after the valid content. The compiler does not check whether this terminator exists, nor do the standard library string functions—maintaining this convention is entirely on you.

Let's see what it actually looks like in memory:

```c
char greeting[] = "Hello";
// 下标：   [0] [1] [2] [3] [4] [5]
// 内容：    'H' 'e' 'l' 'l' 'o' '\0'
// sizeof(greeting) == 6  （包含终止符）
// strlen(greeting) == 5  （不包含终止符）
```

There is a very easily confused point here: the difference between `sizeof` and `strlen`. `sizeof` is a compile-time operator that returns the total number of bytes occupied by the entire array, including `\0`; `strlen` is a runtime function that counts characters from the beginning until it encounters `\0`, returning the length excluding the terminator.

Let's look at the differences between three initialization methods:

```c
// 方式一：字符串字面量自动加 \0
char a[] = "Hi";              // sizeof == 3, strlen == 2

// 方式二：逐字符初始化——不会自动加 \0
char b[] = {'H', 'i'};        // sizeof == 2，这不是 C 字符串！

// 方式三：手动加终止符
char c[] = {'H', 'i', '\0'};  // sizeof == 3, strlen == 2，这才是合法的 C 字符串
```

Method two produces a valid `char` array, but it is **not** a C string—passing it to `strlen` or `printf("%s")` will cause them to keep reading memory until they happen to stumble upon a `0` byte. This is undefined behavior.

> ⚠️ **Pitfall Warning**
> Confusing `sizeof` and `strlen` is one of the most common mistakes beginners make. Remember: `sizeof` is calculated at compile time and gives the total array size (including `\0`), while `strlen` scans at runtime until `\0` and returns the character count (excluding `\0`). When an array is passed to a function, it decays to a pointer, and `sizeof` then returns only the pointer size—at that point, you can only rely on `strlen`.

### The Difference Between String Literals and Pointers

String literals are stored in the read-only data segment of the program; modifying them is undefined behavior:

```c
const char* s = "Hello";   // s 指向只读内存中的 "Hello\0"
// s[0] = 'h';            // 未定义行为！很可能段错误

char t[] = "Hello";        // 数组拷贝，数据在栈上，可以修改
t[0] = 'h';               // 没问题
```

`const char* s = "Hello"` makes the pointer point to a string in the read-only data segment, while `char t[] = "Hello"` copies the string contents to an array on the stack. The former cannot be modified; the latter can. Confusing these two makes debugging extremely painful later on.

## Step Two — Master the Core Functions of string.h

`<string.h>` is the core header file for C string and memory operations. We'll look at them in three groups: length and copying, concatenation and comparison, and memory operations.

### Length and Copying

`strlen` returns the string length (excluding the terminator). Its principle is to scan byte-by-byte from start to finish until it finds `\0`—time complexity is O(n), and repeatedly calling `strlen` on the same string inside a loop is a classic performance waste.

`strcpy` copies the entire source string to the destination buffer. The problem is that it **completely ignores** how large the destination buffer is—if the source string is longer than the destination buffer, it overflows.

`strncpy` is the length-limited version, but its behavior is a bit subtle: it copies at most `n` characters. If `strlen(src) >= n`, it stops after copying `n` characters, **but does not automatically append a terminator**. This behavior has tripped up countless people.

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

This pattern appears repeatedly in C code: `strncpy` + manual `\0` termination. If you see `strncpy` somewhere without an immediately following `\0` termination step, it is very likely a hidden hazard.

> ⚠️ **Pitfall Warning**
> `strncpy` does not guarantee termination! If the source string length is >= n, it copies n characters and stops, without automatically appending `\0`. Every time you use `strncpy`, you must manually write `\0` at the last position.

### Concatenation and Comparison

`strcat` appends the source string to the end of the destination string. It likewise ignores how much space remains in the destination buffer. `strncat` is the length-limited version, where the third parameter `n` refers to the **maximum number of characters to append**, and `strncat` guarantees it will automatically add a `\0` after appending (this differs from `strncpy`).

```c
char buffer[32] = "Hello";
strncat(buffer, ", World", sizeof(buffer) - strlen(buffer) - 1);
// buffer 现在是 "Hello, World"
```

`strcmp` compares two strings character by character, returning `0` if they are equal. Using `==` to compare two strings only compares pointer addresses, not contents—this is a classic beginner mistake.

```c
if (strcmp(cmd, "START") == 0) {
    start_motor();
}
```

### Memory Operations: memcpy, memmove, memset

These three functions operate on raw memory, ignoring `\0` terminators, counting by bytes, and handling data of any type.

`memcpy` copies `n` bytes from the source address to the destination address, requiring that the source and destination do not overlap. `memmove` does the same but correctly handles overlapping regions—at the cost of potentially being slightly slower. `memset` sets every byte in a block of memory to a specified value.

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

> ⚠️ **Pitfall Warning**
> `memcpy` with overlapping regions is undefined behavior. If you are not sure whether two memory blocks overlap, just use `memmove`—the performance difference is negligible, but the safety difference is night and day.

## Step Three — Use snprintf for Safe Formatting

`sprintf` is the function for formatted output to a string, but like `strcpy`, it ignores the destination buffer size. `snprintf` is its safe version; the second parameter specifies the buffer size, guaranteeing that no more than this number of bytes (including the terminator) will be written.

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

The return value of `snprintf` is very useful: it returns **how many characters would have been written if not truncated** (excluding the terminator). If this value is greater than or equal to the buffer size, it means the output was truncated.

In embedded development, `snprintf` is basically the only recommended way to construct strings—log formatting, sensor data concatenation, and communication protocol command assembly should all go through `snprintf`.

## Step Four — Understand Why Buffer Overflows Are So Dangerous

We have repeatedly mentioned "buffer overflow" up to this point; now let's formally break down what it actually is.

### Classic Overflow Scenarios

The essence of a buffer overflow is simple: the data written to a buffer exceeds its capacity, and the excess data overflows into adjacent memory regions, overwriting data that should not be modified. Buffer overflows on the stack are especially dangerous because the function's return address is stored right there in the stack frame—an attacker can craft an overly long input to overwrite the return address, making the program jump to attacker-specified code. The Morris Worm in 1988 spread using exactly this kind of attack.

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

The first line of defense: **always use length-limited functions**.

| Dangerous Function | Safe Alternative | Notes |
|----------|----------|------|
| `strcpy` | `strncpy` + manual termination | Or switch to `snprintf` |
| `strcat` | `strncat` | Note the meaning of the third parameter |
| `sprintf` | `snprintf` | Preferred choice |
| `gets` | `fgets` | `gets` was completely removed in C11 |
| `scanf("%s")` | `%Ns` or `fgets` + `sscanf` | Specify maximum width |

The second line of defense is compiler flags. `-fstack-protector` inserts canary values into stack frames and checks whether they have been tampered with before a function returns. `-D_FORTIFY_SOURCE=2` makes the compiler replace unsafe functions with safe versions at compile time.

The third line of defense is AddressSanitizer (`-fsanitize=address`), which can pinpoint the exact location of every out-of-bounds read or write.

```bash
# 推荐的开发编译命令
gcc -std=c17 -Wall -Wextra -g -fsanitize=address -fstack-protector-all your_code.c
```

## Bridging to C++

If you have followed along and typed out the code up to this point, you have probably felt the tedium of C string operations—after every `strncpy`, you must manually add `\0`, and for every concatenation, you must calculate the remaining space. C++ fundamentally solves these problems through a few core components.

`std::string` internally maintains a dynamically allocated character array, automatically handling `\0` termination, memory allocation and deallocation, and capacity growth. You do not need to manually specify a buffer size or worry about overflow:

```cpp
#include <string>

std::string s1 = "Hello";
std::string s2 = "World";
std::string result = s1 + ", " + s2 + "!";  // 自动扩容
printf("C string: %s\n", result.c_str());    // 和 C API 无障碍交互
```

`std::string_view` (C++17) does not own the string data; it merely holds a pointer and a length, essentially wrapping `(const char*, size_t)`. It provides zero-copy parameter passing and is compatible with both C strings and `std::string`. Note, however, that it does not own the data—a `string_view` pointing to a temporary object is a classic dangling reference trap.

With these two tools, `strcpy`, `strcat`, `sprintf`, and `strlen` should basically never appear directly in C++ code. Of course, when interacting with C APIs or in extremely resource-constrained embedded environments, these functions are still necessary—which is why we spent an entire tutorial learning them.

## Common Pitfalls

| Pitfall | Description | Solution |
|------|------|----------|
| `strncpy` does not guarantee termination | When source string length >= n, it does not append `\0` | Always manually set the last byte to `\0` |
| Using `==` to compare strings | Compares pointer addresses, not contents | Use `strcmp` |
| Modifying string literals | Stored in the read-only segment; modification triggers a segfault | Use an array copy: `char s[] = "Hello"` |
| Third parameter of `strncat` | It is the "maximum number of characters to append", not the total buffer size | Use `sizeof(dst) - strlen(dst) - 1` |
| `memcpy` with overlapping regions | Undefined behavior | Use `memmove` when overlapping |

## Summary

A C string is a `char` array terminated by `\0`, with no protection from the type system—all safety responsibility falls on the programmer. The function family provided by `string.h` is the basic tool for string manipulation; the unlimited versions (`strcpy`, `strcat`, `sprintf`) are the primary source of buffer overflows, and you should prefer the `n` versions or `snprintf`. `memcpy` is for non-overlapping memory copies, and `memmove` is for potentially overlapping cases. Compiler flags provide an additional safety net. C++'s `std::string` automatically manages memory, and `std::string_view` provides zero-copy references—understanding the underlying C string model is the prerequisite for understanding why these C++ tools are designed the way they are.

## Exercises

### Exercise 1: Safe String Library

Implement a set of safe string manipulation functions where each function knows the destination buffer size and automatically handles truncation and termination:

```c
#include <stddef.h>

/// @brief 安全地复制字符串到目标缓冲区
/// @param dst 目标缓冲区
/// @param src 源字符串
/// @param dst_size 目标缓冲区总大小（含终止符）
/// @return 实际复制的字符数（不含终止符）；如果 dst 为 NULL 返回 0
size_t safe_str_copy(char* dst, const char* src, size_t dst_size);

/// @brief 安全地拼接字符串
/// @param dst 目标缓冲区（已有内容）
/// @param src 要追加的字符串
/// @param dst_size 目标缓冲区总大小（含终止符）
/// @return 拼接后字符串的总长度（不含终止符）
size_t safe_str_cat(char* dst, const char* src, size_t dst_size);

/// @brief 安全地格式化字符串
/// @param dst 目标缓冲区
/// @param dst_size 目标缓冲区总大小
/// @param format 格式字符串
/// @param ... 格式参数
/// @return 实际写入的字符数（不含终止符）
size_t safe_str_format(char* dst, size_t dst_size, const char* format, ...);
```

Hint: `safe_str_copy` can be implemented based on `strncpy`, but must guarantee termination; `safe_str_cat` needs to first calculate the current length of the destination string, then compute the remaining available space; `safe_str_format` can be implemented directly using `vsnprintf`.

### Exercise 2: String Splitting Function

Implement a function that splits a string by a delimiter:

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

Hint: Iterate through `input`, recording the start pointer and length of each substring. When you encounter a delimiter, end the current substring and start the next one. Don't forget to handle the last substring at the end of the string.

## References

- [string.h - cppreference](https://en.cppreference.com/w/c/string/byte)
- [stdio.h formatted output functions - cppreference](https://en.cppreference.com/w/c/io)
- [Buffer Overflow - OWASP](https://owasp.org/www-community/vulnerabilities/Buffer_Overflow)
