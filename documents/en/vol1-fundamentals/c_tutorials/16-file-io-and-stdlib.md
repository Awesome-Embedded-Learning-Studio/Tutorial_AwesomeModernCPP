---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master C file operations and core standard library utilities, including
  file reading and writing, formatted I/O, and command-line argument handling, while
  comparing them with C++ stream libraries and modern standard library tools.
difficulty: beginner
order: 20
platform: host
prerequisites:
- 11 C 字符串与缓冲区安全
- 12 结构体与内存对齐
- 14 动态内存管理
reading_time_minutes: 11
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: File I/O and Standard Library Overview
translation:
  source: documents/vol1-fundamentals/c_tutorials/16-file-io-and-stdlib.md
  source_hash: 88bc8438892bedfa8ab275295bdd523964722ec4274747e9050119318ef21109
  translated_at: '2026-04-20T03:34:11.894904+00:00'
  engine: anthropic
  token_count: 1857
---
# File I/O and Standard Library Overview

Every program we've written so far shares a common limitation—all data lives in memory and vanishes once the program ends. Real-world programs don't work that way: configurations are read from files, logs are written to files, and data is passed between programs. This is where file I/O comes in.

C's file operations are built on a concise but powerful API—`fopen` to open, `fread`/`fwrite` to read and write, `fclose` to close, plus the `printf`/`scanf` family for formatted I/O. These functions have survived from the 1970s to today. But they also carry the rough edges of that era—lack of type safety, error handling via global variables, and compilers turning a blind eye when format strings and arguments don't match. C++ later repackaged this system with stream libraries, `std::filesystem`, and `std::format`, but understanding C's original API remains foundational.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Proficiently use file operation functions like fopen/fclose/fread/fwrite
> - [ ] Understand the difference between text mode and binary mode
> - [ ] Master formatted I/O with the printf/scanf family
> - [ ] Use errno/perror/strerror for error handling
> - [ ] Write programs that accept command-line arguments
> - [ ] Understand core standard library utilities
> - [ ] Understand how C++'s stream library, std::filesystem, and std::format improve upon C's approach

## Environment Setup

All code in this chapter has been verified under the following environment:

- **Operating System**: Linux (Ubuntu 22.04+) / WSL2 / macOS
- **Compiler**: GCC 11+ (confirm version via `gcc --version`)
- **Compiler flags**: `gcc -Wall -Wextra -std=c11` (enable warnings, specify C11 standard)
- **Verification**: All code can be compiled and run directly

## Step One — Getting Started with File Operations

### Opening and Closing Files

```c
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FILE* fp = fopen("data.txt", "r");
    if (fp == NULL) {
        perror("Failed to open data.txt");
        return EXIT_FAILURE;
    }
    // ... 读写操作 ...
    fclose(fp);
    return 0;
}
```

> ⚠️ **Pitfall Warning**: **Always check if fopen returns NULL**. A missing file, insufficient permissions, or an incorrect path will cause the open to fail. If you use a NULL pointer without checking, the program will crash immediately—without any meaningful error message.

Mode string quick reference:

| Mode | Read | Write | When File Doesn't Exist | When File Already Exists |
|------|------|-------|-------------------------|--------------------------|
| `"r"`  | Yes | No | Fails | Reads from the beginning |
| `"w"`  | No | Yes | Creates a new file | **Clears existing content** |
| `"a"`  | No | Yes | Creates a new file | Appends to the end |
| `"r+"` | Yes | Yes | Fails | Reads and writes from the beginning |
| `"w+"` | Yes | Yes | Creates a new file | **Clears, then reads and writes** |
| `"a+"` | Yes | Yes | Creates a new file | Reads from the beginning, writes append to the end |

> ⚠️ **Pitfall Warning**: `"w"` and `"w+"` will **unconditionally clear** the contents of an existing file. If you meant to append but used the `"w"` mode instead, congratulations—your file content instantly drops to zero, with no confirmation step. Always double-check the mode before using it.

### Reading and Writing Binary Data

```c
typedef struct {
    uint16_t id;
    float value;
    uint32_t timestamp;
} Record;

// 写入
size_t written = fwrite(records, sizeof(Record), count, fp);

// 读取
size_t count = fread(buffer, sizeof(Record), max_count, fp);
```

The return value is the number of **complete blocks** successfully processed, not the number of bytes. If the return value is less than the requested number of blocks, it means either the end of the file was reached or an error occurred.

### Moving the File Position and Getting the Size

`fseek` moves the position pointer, and `ftell` queries the current position. A practical pattern for getting the file size is:

```c
long get_file_size(FILE* fp) {
    long original = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, original, SEEK_SET);
    return size;
}
```

### Don't Use feof as a Loop Condition

`feof` only returns true **after** a read operation has already failed. The correct approach is to check the return value of the read function directly:

```c
int ch;
while ((ch = fgetc(fp)) != EOF) {
    putchar(ch);
}
```

> ⚠️ **Pitfall Warning**: `fgetc` returns `int`, not `char`. If you use `char` to receive the return value, `EOF` (-1) will be truncated to a valid character value on some platforms, causing the loop to never end. This trap catches a new batch of beginners every year.

## Step Two — Mastering Formatted I/O

### The printf Family

`printf` outputs to stdout, `fprintf` outputs to a specified file, and `sprintf`/`snprintf` output to a string buffer. The return value is the actual number of characters output.

```c
char buf[64];
snprintf(buf, sizeof(buf), "%s:%d", name, age);
```

A clever use of `snprintf` is to probe the required buffer size:

```c
int needed = snprintf(NULL, 0, "Result: %d items", item_count);
char* buf = malloc(needed + 1);
snprintf(buf, needed + 1, "Result: %d items", item_count);
```

### The scanf Family

`scanf` returns the **number of successfully matched fields**. `sscanf` is very convenient for parsing from strings:

```c
const char* input = "2024-01-15";
int year, month, day;
int count = sscanf(input, "%d-%d-%d", &year, &month, &day);
```

> ⚠️ **Pitfall Warning**: `scanf`'s `%s` does not check buffer sizes. The safe approach is to use `%Ns` to specify the maximum length, or switch to a `fgets` + `sscanf` combination.

### Common Format Specifiers

| Specifier | Type | Specifier | Type |
|-----------|------|-----------|------|
| `%d` | int | `%f` | double |
| `%u` | unsigned | `%s` | string |
| `%x` | hex | `%zu` | size_t |
| `%ld` | long | `%lld` | long long |
| `%p` | pointer | `%%` | literal % |

## Step Three — Understanding Text Mode vs. Binary Mode

On Windows, text mode automatically converts `\n` to `\r\n`, while binary mode does no conversion. On Linux/macOS, there is practically no difference between the two. When handling binary data (images, struct dumps, protocol frames), always use `"rb"`/`"wb"`.

> ⚠️ **Pitfall Warning**: If you read a binary file in text mode on Windows, the read will terminate early when it encounters a `0x1A` byte—because `0x1A` is treated as EOF in Windows text mode. This is a classic cross-platform trap.

## Step Four — Error Handling with errno

`errno` (`<errno.h>`) is a global error code variable. It is **not** cleared to zero when a function succeeds; it is only set when an error occurs. The correct approach is to check the return value first to confirm an error, and then read `errno`.

`perror` concatenates your provided string with the system error message and prints it:

```c
FILE* fp = fopen("nonexistent.txt", "r");
if (fp == NULL) {
    perror("fopen failed");
    // 输出：fopen failed: No such file or directory
}
```

`strerror` returns a string description corresponding to the error code, suitable for use in custom error messages.

## Step Five — Handling Command-Line Arguments

```c
int main(int argc, char* argv[]) {
    printf("Program: %s\n", argv[0]);
    for (int i = 1; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }
    return 0;
}
```

`argv[0]` is the program name, `argv[1]` through `argv[argc-1]` are the arguments, and `argv[argc]` is `NULL`.

## Standard Library Quick Reference

### `<stdlib.h>`: General Utilities

`atoi` is simple but lacks error detection, while `strtol` is safer (it can detect overflow and partial parsing). `qsort` performs quicksort, and `bsearch` performs binary search, both comparing via function pointers. `rand`/`srand` provide pseudo-random numbers with relatively poor randomness quality—good enough for basic use, but don't rely on them for security-related tasks.

### `<math.h>`: Math Functions

Trigonometric functions (sin/cos/tan), exponentials and logarithms (pow/sqrt/log/exp), rounding (ceil/floor/round), and absolute values (fabs). All have three versions: float (f suffix), double, and long double (l suffix).

> ⚠️ **Pitfall Warning**: Linking the math library on GCC/Linux requires the `-lm` option. If you forget to add this option, the compiler will report errors like `undefined reference to 'sin'`—the code itself is fine, it's just missing a linker option.

### `<ctype.h>`: Character Classification

`isalpha`/`isdigit`/`isspace`/`isalnum`/`isupper`/`islower` determine character categories, and `tolower`/`toupper` convert between uppercase and lowercase. The argument must be explicitly cast to `unsigned char` first, otherwise negative values from a signed char will lead to undefined behavior.

### `<assert.h>`: Assert Macro

```c
assert(arr != NULL);   // Debug: 条件为假时终止程序
```

Defining `NDEBUG` completely removes all asserts. Use this to catch programming errors, not to handle runtime errors.

### `<stddef.h>`: Fundamental Types

`size_t` (object size), `NULL` (null pointer), `offsetof` (struct offset), `ptrdiff_t` (pointer difference). `size_t` is unsigned, so watch out for underflow when iterating in reverse: `for (size_t i = count; i-- > 0; )` is the safe way to write it.

## C++ Connection

### Stream Library (iostream/fstream/sstream)

The C++ stream library achieves **type safety** through operator overloading—passing the wrong type directly causes a compilation failure. Destructors automatically close files (RAII). `std::getline` directly returns `std::string`, eliminating the risk of buffer overflows.

### std::filesystem (C++17)

Cross-platform directory traversal, file attribute queries, and path manipulation—no more need to write `#ifdef _WIN32`.

### std::format (C++20)

Combines printf's concise syntax with type safety:

```cpp
std::string s = std::format("{} is {} years old", name, age);
```

### std::span (C++17)

`std::span<const int>` binds a pointer and a length together, solving the old problem of arrays decaying and losing their length information.

### `<system_error>`

`std::error_code` is a value type and thread-safe, making it much safer than the global `errno`.

## Summary

The core of file operations is `FILE*` and `fopen`/`fclose`/`fread`/`fwrite`, formatted I/O relies on the `printf`/`scanf` family, and error handling depends on `errno` + `perror`. The standard library provides fundamental utilities like numeric conversion, sorting and searching, math functions, character classification, and assertions. C++ delivers a comprehensive type-safe upgrade to these tools with stream libraries, `std::filesystem`, `std::format`, and `std::error_code`.

## Exercises

### Exercise 1: Configuration File Parser

Parse a configuration file in `key=value` format, ignoring `#` comments and blank lines.

```c
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define MAX_LINE 256
#define MAX_KEY 64
#define MAX_VALUE 128

typedef struct {
    char key[MAX_KEY];
    char value[MAX_VALUE];
} ConfigEntry;

/// @brief 去除字符串首尾的空白字符
char* trim(char* str);

/// @brief 解析配置文件
size_t parse_config(const char* path, ConfigEntry* entries, size_t max_entries);

/// @brief 在配置项中查找指定 key
const char* find_config(const ConfigEntry* entries, size_t count, const char* key);

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        return 1;
    }
    // TODO: 调用 parse_config 和 find_config
    return 0;
}
```

Hint: Use `fgets` to read line by line, `strchr` to find the `=` position, and `trim` to strip whitespace.

### Exercise 2: File Copy Tool

Specify the source and destination files via command-line arguments, support binary file copying, and display progress.

```c
#include <stdio.h>
#include <stdlib.h>

#define kBufferSize 4096

/// @brief 复制文件
int copy_file(const char* src_path, const char* dst_path)
{
    // TODO: 实现
    // 1. "rb" 打开源文件，"wb" 打开目标文件
    // 2. 循环 fread/fwrite
    // 3. 用 fseek/ftell 获取总大小，打印进度
    // 4. 错误处理：先打开的后关闭
    return -1;
}

int main(int argc, char* argv[]) {
    // TODO: 解析命令行参数，调用 copy_file
    return 0;
}
```

Hint: Use `fseek` + `ftell` to get the source file size, and use `\r` to overwrite the same line to implement a progress bar.
