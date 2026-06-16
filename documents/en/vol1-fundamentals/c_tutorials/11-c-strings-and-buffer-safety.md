---
chapter: 1
cpp_standard:
- 11
- 17
description: Understand the memory model of null-terminated C strings, master core
  `string.h` functions and safe formatting with `snprintf`, and identify and prevent
  buffer overflow vulnerabilities.
difficulty: beginner
order: 15
platform: host
prerequisites:
- 指针与数组、const 和空指针
reading_time_minutes: 13
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: C Strings and Buffer Safety
translation:
  source: documents/vol1-fundamentals/c_tutorials/11-c-strings-and-buffer-safety.md
  source_hash: 1a1c6e35bdd3b819e3b0355f256de78a99d34f9924de72b36b2021db1a6b5125
  translated_at: '2026-06-16T05:51:23.329323+00:00'
  engine: anthropic
  token_count: 2342
---
# C Strings and Buffer Safety

C lacks a true "string type"—a realization every developer transitioning from C to C++ eventually has. In the world of C, a string is simply a `char` array terminated by a `\0`. All operations are built upon this convention. This convention is simple enough to be endearing, yet fragile enough to be maddening—if you forget that `\0`, your program's behavior becomes undefined; if you copy a 100-byte string into a 50-byte buffer, you will trample the memory following that buffer.

Countless security vulnerabilities throughout history, from the early Morris Worm to recent CVEs, trace back to a single root cause: **buffer overflow**. In this tutorial, we will dissect C strings inside and out, understand their essence, master safe handling techniques, recognize classic pitfalls, and ultimately build a solid low-level foundation for learning C++'s `std::string`.

> **Learning Objectives**
>
> After completing this chapter, you will be able to:
>
> - [ ] Understand the `\0`-terminated memory model of C strings.
> - [ ] Skillfully use core string and memory manipulation functions in `string.h`.
> - [ ] Master `snprintf` for safe formatted output.
> - [ ] Identify and prevent buffer overflow vulnerabilities.

## Environment Setup

We will conduct all subsequent experiments in the following environment:

- Platform: Linux x86_64 (WSL2 is acceptable).
- Compiler: GCC 13+ or Clang 17+.
- Compiler flags: `-Wall -Wextra -std=c17`.

We strongly recommend adding the `-fsanitize=address` compiler flag during practice. AddressSanitizer can catch most buffer out-of-bounds accesses at runtime, serving as a safety net for C string operations.

## Step 1 — Understanding the Memory Layout of C Strings

### It's Just an Array, Plus a `\0`

A C string is essentially a `char` array with an extra byte of value `0` (`\0`, the null character) placed at the end of the valid content. The compiler does not verify the existence of this terminator, nor do the standard library string functions—everything relies on you maintaining this convention.

Let's see what this actually looks like in memory:

```c
char greeting[] = "Hello";
// 下标：   [0] [1] [2] [3] [4] [5]
// 内容：    'H' 'e' 'l' 'l' 'o' '\0'
// sizeof(greeting) == 6  （包含终止符）
// strlen(greeting) == 5  （不包含终止符）
```

Here is a point that often causes confusion: the difference between `sizeof` and `strlen`. `sizeof` is a compile-time operator that returns the total number of bytes occupied by the entire array, including the `\0` (null terminator). `strlen` is a runtime function that counts characters from the beginning until it encounters `\0`, returning the length excluding the terminator.

Let's look at the differences between the three initialization methods:

```c
// 方式一：字符串字面量自动加 \0
char a[] = "Hi";              // sizeof == 3, strlen == 2

// 方式二：逐字符初始化——不会自动加 \0
char b[] = {'H', 'i'};        // sizeof == 2，这不是 C 字符串！

// 方式三：手动加终止符
char c[] = {'H', 'i', '\0'};  // sizeof == 3, strlen == 2，这才是合法的 C 字符串
```

Method two is a valid `char` array, but it is **not** a C string. Passing it to `strlen` or `printf("%s")` causes the program to read memory continuously until it happens to encounter a `0` byte. This is undefined behavior (UB).

> ⚠️ **Warning**
> Confusing `sizeof` with `strlen` is one of the most common mistakes beginners make. Remember: `sizeof` calculates the total size of the array at compile time (including `\0`), while `strlen` counts the number of characters up to the `\0` at runtime (excluding `\0`). When an array is passed to a function, it decays into a pointer, and `sizeof` will only return the pointer size—in this case, you must rely on `strlen`.

### Difference Between String Literals and Pointers

String literals are stored in the read-only data segment of the program; modifying them is undefined behavior (UB):

```c
const char* s = "Hello";   // s 指向只读内存中的 "Hello\0"
// s[0] = 'h';            // 未定义行为！很可能段错误

char t[] = "Hello";        // 数组拷贝，数据在栈上，可以修改
t[0] = 'h';               // 没问题
```

`const char* s = "Hello"` points the pointer to a string in the read-only data segment, while `char t[] = "Hello"` copies the string contents to an array on the stack. The former cannot be modified, but the latter can. Confusing these two will make debugging extremely painful later on.

## Step 2 — Master Core Functions in string.h

`<string.h>` is the core header file for C string and memory operations. We will look at them in three groups: length and copying, concatenation and comparison, and memory operations.

### Length and Copying

`strlen` returns the string length (excluding the terminator). Its principle is to scan byte-by-byte from start to finish until `\0` is found—time complexity is O(n). Calling `strlen` on the same string repeatedly inside a loop is a classic waste of performance.

`strcpy` copies the entire source string to the destination buffer. The problem is that it **completely ignores** the size of the destination buffer—if the source string is longer than the destination buffer, it overflows.

`strncpy` is the version with a length limit, but its behavior is a bit subtle: it copies up to `n` characters. If `strlen(src) >= n`, it stops after copying `n` characters, **but it does not automatically append a terminator**. This behavior has tripped up countless people.

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

**Execution Result:**

```text
dst = "Hello, "
```

This pattern appears repeatedly in C code: `strncpy` followed by manual `\0` termination. If you see `strncpy` somewhere without immediate `\0` termination handling, it is highly likely a bug.

> ⚠️ **Warning**
> `strncpy` does not guarantee termination! If the source string length is greater than or equal to `n`, it stops after copying `n` characters and does not automatically append a `\0`. You must manually write `\0` at the last position after every use of `strncpy`.

### Concatenation and Comparison

`strcat` appends the source string to the end of the destination string. It likewise ignores how much space remains in the destination buffer. `strncat` is the version with a length limit, where the third parameter `n` specifies the **maximum number of characters to append**. Furthermore, `strncat` guarantees that it will automatically add a `\0` after appending (this is different from `strncpy`).

```c
char buffer[32] = "Hello";
strncat(buffer, ", World", sizeof(buffer) - strlen(buffer) - 1);
// buffer 现在是 "Hello, World"
```

`strcmp` compares two strings character by character, returning `0` if they are equal. Using `==` to compare two strings only compares the pointer addresses, not the contents—this is a classic novice mistake.

```c
if (strcmp(cmd, "START") == 0) {
    start_motor();
}
```

### Memory Operations: `memcpy`, `memmove`, `memset`

These three functions operate on raw memory, ignore `\0` terminators, count by bytes, and handle data of any type.

`memcpy` copies `n` bytes from a source address to a destination address, requiring that the source and destination do not overlap. `memmove` has the same functionality but correctly handles overlapping regions—at the cost of being potentially slightly slower. `memset` sets every byte in a block of memory to a specified value.

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

Execution result:

```text
dst: 1 2 3 4 5
src: 1 1 2 3 5
```

> ⚠️ **Warning**
> `memcpy` has undefined behavior when handling overlapping memory regions. If you are unsure whether two memory blocks overlap, use `memmove` directly—the performance difference is negligible, but the safety difference is significant.

## Step 3 — Safe Formatting with `snprintf`

`sprintf` is a function that formats output into a string, but like `strcpy`, it does not check the size of the destination buffer. `snprintf` is its safe counterpart; the second argument specifies the buffer size, ensuring that no more than this number of bytes (including the null terminator) are written.

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

**Output:**

```text
Result: "Temperature: 42 degrees"
Written: 23, Buffer size: 32
```

The return value of `snprintf` is quite useful: it returns **the number of characters that would have been written if not truncated** (excluding the null terminator). If this value is greater than or equal to the buffer size, it indicates that the output was truncated.

In embedded development, `snprintf` is basically the only recommended way to construct strings—log formatting, sensor data concatenation, and communication protocol command assembly should all rely on `snprintf`.

## Step 4 — Understanding Why Buffer Overflows Are So Dangerous

We have mentioned "buffer overflow" repeatedly by now, so let's formally break down exactly what it is.

### Classic Overflow Scenario

The nature of a buffer overflow is simple: the data written to the buffer exceeds its capacity, and the excess data spills over into adjacent memory areas, overwriting data that should not be modified. Buffer overflows on the stack are particularly dangerous because a function's return address is stored within the stack frame—an attacker can craft a specially designed long input to overwrite the return address, causing the program to jump to code specified by the attacker. The Morris Worm in 1988 propagated using exactly this type of attack.

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

The first line of defense: **Always use length-limited functions**.

| Dangerous Function | Safe Alternative | Notes |
|--------------------|-----------------|-------|
| `strcpy` | `strncpy` + manual termination | Or switch to `snprintf` |
| `strcat` | `strncat` | Note the meaning of the third parameter |
| `sprintf` | `snprintf` | Preferred choice |
| `gets` | `fgets` | `gets` was completely removed in C11 |
| `scanf("%s")` | `%Ns` or `fgets` + `sscanf` | Specify maximum width |

The second line of defense is compiler options. `-fstack-protector` inserts a canary value into the stack frame and checks if it has been tampered with before the function returns. `-D_FORTIFY_SOURCE=2` instructs the compiler to replace unsafe functions with safe versions at compile time.

The third line of defense is AddressSanitizer (`-fsanitize=address`), which can precisely pinpoint the location of every out-of-bounds read or write.

```bash
# 推荐的开发编译命令
gcc -std=c17 -Wall -Wextra -g -fsanitize=address -fstack-protector-all your_code.c
```

## C++ Interoperability

If you have followed this tutorial and typed out the code up to this point, you have likely realized how tedious C string operations can be—manually adding `\0` after every `strncpy`, and calculating remaining space for every concatenation. C++ addresses these issues fundamentally through several core components.

`std::string` maintains a dynamically allocated character array internally, automatically handling `\0` termination, memory allocation and deallocation, and capacity growth. You do not need to specify buffer sizes manually, nor worry about overflows:

```cpp
#include <string>

std::string s1 = "Hello";
std::string s2 = "World";
std::string result = s1 + ", " + s2 + "!";  // 自动扩容
printf("C string: %s\n", result.c_str());    // 和 C API 无障碍交互
```

`std::string_view` (C++17) does not own string data; it only holds a pointer and a length, essentially encapsulating `(const char*, size_t)`. Passing it involves zero-copy, and it is compatible with C strings and `std::string`. However, note that it does not own the data—a `string_view` pointing to a temporary object is a classic dangling reference trap.

With these two tools, `strcpy`, `strcat`, `sprintf`, and `strlen` should basically never appear directly in C++ code. Of course, when interacting with C APIs or in extremely resource-constrained embedded environments, these functions are still necessary—which is why we spent an entire article learning them.

## Common Pitfalls

| Pitfall | Description | Solution |
|---------|-------------|----------|
| `strncpy` does not guarantee null termination | Does not append `\0` when source length >= n | Always manually set the last byte to `\0` |
| Comparing strings with `==` | Compares pointer addresses, not content | Use `strcmp` |
| Modifying string literals | Stored in read-only segments; modification triggers a segmentation fault | Use an array copy: `char s[] = "Hello"` |
| Third parameter of `strncat` | It is "maximum characters to append", not total buffer size | Use `sizeof(dst) - strlen(dst) - 1` |
| `memcpy` with overlapping regions | Undefined behavior | Use `memmove` when overlapping |

## Summary

A C string is simply a `\0`-terminated `char` array. Without the protection of the type system, the entire safety responsibility lies with the programmer. The function family provided by `string.h` are the basic tools for string manipulation. Versions without length limits (`strcpy`, `strcat`, `sprintf`) are the primary source of buffer overflows; we should prioritize the `n`-suffixed versions or `snprintf`. `memcpy` is for non-overlapping memory copying, while `memmove` handles potentially overlapping situations. Compiler flags provide an additional safety net. C++'s `std::string` manages memory automatically, and `std::string_view` provides zero-copy references—understanding the underlying C string model is the prerequisite for understanding why these C++ tools are designed this way.

## Exercises

### Exercise 1: Safe String Library

Implement a set of safe string manipulation functions where each function is aware of the destination buffer size and automatically handles truncation and termination:

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

**Hint:** We can implement `safe_str_copy` based on `strncpy`, but we must ensure null termination. For `safe_str_cat`, we need to calculate the current length of the destination string first, then determine the remaining available space. We can implement `safe_str_format` directly using `vsnprintf`.

### Exercise 2: String Splitting Function

Implement a function that splits a string based on a delimiter:

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

> **Tip:** Iterate through `input`, recording the start pointer and length of each substring. When a delimiter is encountered, terminate the current substring and start the next one. Do not forget to handle the last substring at the end of the string.

## Resources

- [string.h - cppreference](https://en.cppreference.com/w/c/string/byte)
- [stdio.h formatted output functions - cppreference](https://en.cppreference.com/w/c/io)
- [Buffer Overflow - OWASP](https://owasp.org/www-community/vulnerabilities/Buffer_Overflow)
