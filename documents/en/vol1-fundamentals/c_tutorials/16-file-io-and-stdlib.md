---
chapter: 1
cpp_standard:
- 11
- 14
- 17
description: Master C file operations and core standard library tools, including file
  I/O, formatted I/O, and command-line argument processing, while comparing them with
  C++ stream libraries and modern standard library tools.
difficulty: beginner
order: 20
platform: host
prerequisites:
- 11 C 字符串与缓冲区安全
- 12 结构体与内存对齐
- 14 动态内存管理
reading_time_minutes: 9
tags:
- host
- cpp-modern
- beginner
- 入门
- 基础
title: File I/O and Standard Library Overview
translation:
  source: documents/vol1-fundamentals/c_tutorials/16-file-io-and-stdlib.md
  source_hash: 8c7d47405d9a311a35806572b757d91a1f0fc1a323fc454d89757f6b887992e5
  translated_at: '2026-06-16T03:36:43.324876+00:00'
  engine: anthropic
  token_count: 1855
---
# File I/O and Standard Library Overview

Until now, every program we have written has shared a common limitation—data resides entirely in memory and vanishes once the program ends. Real-world programs do not work this way: configurations must be read from files, logs written to files, and data transferred between programs. This is where file I/O comes in.

C's file operations are built upon a concise yet powerful API—`fopen` to open, `fread`/`fwrite` to read and write, `fclose` to close, plus the `printf`/`scanf` family for formatted input and output. These functions have survived from the 1970s to today. However, they also carry the rough edges of that era—type unsafety, error handling via global variables, and lenient compilers regarding mismatches between format strings and arguments. C++ later repackaged this system with stream libraries, `std::filesystem`, and `std::format`, but understanding C's raw API remains foundational.

> **Learning Objectives**
>
> - After completing this chapter, you will be able to:
> - [ ] Skillfully use file operation functions like fopen/fclose/fread/fwrite
> - [ ] Understand the difference between text mode and binary mode
> - [ ] Master the printf/scanf family for formatted I/O
> - [ ] Use errno/perror/strerror for error handling
> - [ ] Write programs that accept command-line arguments
> - [ ] Understand core standard library utilities
> - [ ] Understand how C++'s stream libraries, std::filesystem, and std::format improve upon C's approach

## Environment Setup

All code in this chapter has been verified in the following environment:

- **Operating System**: Linux (Ubuntu 22.04+) / WSL2 / macOS
- **Compiler**: GCC 11+ (Confirm version via `gcc --version`)
- **Compiler Flags**: `-Wall -Wextra -std=c11` (Enable warnings, specify C11 standard)
- **Verification**: All code can be compiled and run directly

## Step 1 — Getting Started with File Operations

### Opening and Closing Files

```c
FILE *fp = fopen("log.txt", "w"); // Open for writing
if (!fp) {
    // Handle error
}
fclose(fp);
```

> ⚠️ **Watch Out**: **Always check if fopen returns NULL**. File not found, insufficient permissions, or incorrect paths will cause the open to fail. If you use a NULL pointer directly without checking, the program will crash immediately—without any meaningful error message.

Mode string quick reference:

| Mode | Read | Write | If file doesn't exist | If file exists |
|------|------|-------|-----------------------|----------------|
| `"r"`  | Yes | No | Fails | Read from start |
| `"w"`  | No | Yes | Create new file | **Truncate existing content** |
| `"a"`  | No | Yes | Create new file | Append to end |
| `"r+"` | Yes | Yes | Fails | Read/Write from start |
| `"w+"` | Yes | Yes | Create new file | **Truncate then Read/Write** |
| `"a+"` | Yes | Yes | Create new file | Read from start, Write appends to end |

> ⚠️ **Watch Out**: `"w"` and `"w+"` will **unconditionally truncate** existing file content. If you meant to append content but used the `"w"` mode, congratulations—your file content is instantly zeroed out with no confirmation step. Always verify the mode before use.

### Reading and Writing Binary Data

```c
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
```

The return value is the number of **complete blocks** successfully processed, not the number of bytes. If the return value is less than the requested number of blocks, it means either the end of file was reached or an error occurred.

### Moving File Position and Getting Size

`fseek` moves the position pointer, `ftell` queries the current position. A useful pattern is to get the file size:

```c
fseek(fp, 0, SEEK_END); // Jump to end
long size = ftell(fp);  // Get current position = size
fseek(fp, 0, SEEK_SET); // Jump back to start
```

### Don't Use feof as a Loop Condition

`feof` only returns true **after** a read operation has already failed. The correct approach is to check the return value of the read function directly:

```c
int c;
while ((c = fgetc(fp)) != EOF) {
    putchar(c);
}
```

> ⚠️ **Watch Out**: `fgetc` returns `int` instead of `char`. If you use `char` to receive the return value, on some platforms `EOF` (-1) will be truncated into a valid character value, causing the loop to never end. This pitfall catches a new batch of beginners every year.

## Step 2 — Mastering Formatted I/O

### The printf Family

`printf` outputs to stdout, `fprintf` outputs to a specified file, `sprintf`/`snprintf` output to a string buffer. The return value is the actual number of characters output.

```c
int count = printf("Value: %d\n", 42); // Returns 10 (including newline)
```

A clever use of `snprintf` is to probe the required buffer size:

```c
int needed = snprintf(NULL, 0, "Value: %d", 42); // Returns length needed
char *buf = malloc(needed + 1);
snprintf(buf, needed + 1, "Value: %d", 42);
```

### The scanf Family

`scanf` returns the number of **successfully matched fields**. `sscanf` is very convenient for parsing from strings:

```c
int year, month;
if (sscanf("2023-10", "%d-%d", &year, &month) == 2) {
    // Success
}
```

> ⚠️ **Watch Out**: `scanf`'s `%s` does not check buffer size. The safe way is to use `%ms` (GNU extension) to specify maximum length, or switch to the `fgets` + `sscanf` combination.

### Common Format Specifiers

| Specifier | Type | Specifier | Type |
|-----------|------|-----------|------|
| `%d` | int | `%f` | double |
| `%u` | unsigned | `%s` | string |
| `%x` | hex | `%zu` | size_t |
| `%ld` | long | `%lld` | long long |
| `%p` | pointer | `%%` | Literal % |

## Step 3 — Understanding Text Mode vs. Binary Mode

On Windows, text mode automatically converts `\r\n` to `\n`, while binary mode performs no conversion. On Linux/macOS, there is virtually no difference between the two. When handling binary data (images, structure dumps, protocol frames), always use `"rb"`/`"wb"`.

> ⚠️ **Watch Out**: If you read a binary file in text mode on Windows, encountering a `0x1A` byte will cause the read to terminate early—because `0x1A` (Ctrl+Z) is treated as EOF in Windows text mode. This is a classic cross-platform trap.

## Step 4 — Error Handling with errno

`errno` (in `<errno.h>`) is a global error code variable. Functions do **not** clear `errno` on success; they only set it when an error occurs. The correct practice is to check the return value first to confirm an error, then read `errno`.

`perror` concatenates your string with the system error message and outputs it:

```c
if (ferror(fp)) {
    perror("File read failed"); // Output: File read failed: Error description
}
```

`strerror` returns the string description corresponding to the error code, suitable for use in custom error messages.

## Step 5 — Handling Command-Line Arguments

```c
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <args>\n", argv[0]);
        return 1;
    }
}
```

`argv[0]` is the program name, `argv[1]` to `argv[argc-1]` are the arguments, and `argc` is the count.

## Standard Library Quick Reference

### `<stdlib.h>`: General Utilities

`atoi` is simple but offers no error detection; `strtol` is safer (can detect overflow and partial parsing). `qsort` for quicksort, `bsearch` for binary search, both using function pointers for comparison. `rand`/`srand` pseudo-random numbers have poor quality; they are sufficient but don't rely on them for security-related tasks.

### `<math.h>`: Math Functions

Trigonometric functions (sin/cos/tan), exponential/logarithmic (pow/sqrt/log/exp), rounding (ceil/floor/round), absolute value (fabs). All have three versions: float (f suffix), double, and long double (l suffix).

> ⚠️ **Watch Out**: Linking the math library on GCC/Linux requires the `-lm` option. If you forget this option, the compiler will report an `undefined reference` error—the code is fine, just missing a link option.

### `<ctype.h>`: Character Classification

`isalpha`/`isdigit`/`isxdigit`/`isspace`/`isupper`/`islower` determine character classes; `toupper`/`tolower` convert case. Arguments must be cast to `unsigned char` first, otherwise negative values of signed `char` can lead to undefined behavior.

### `<assert.h>`: Assertion Macro

```c
assert(ptr != NULL); // If false, abort program
```

Defining `NDEBUG` removes all asserts. Used to catch programming errors, not to handle runtime errors.

### `<stddef.h>`: Fundamental Types

`size_t` (object size), `NULL` (null pointer), `offsetof` (structure member offset), `ptrdiff_t` (pointer difference). `size_t` is unsigned, so watch for underflow when iterating in reverse: `i != (size_t)-1` is the safe way to write it.

## C++ Bridge

### Stream Libraries (iostream/fstream/sstream)

C++ stream libraries achieve **type safety** through operator overloading—passing the wrong type results in a compile error. Destructors automatically close files (RAII). `std::string` is returned directly from `std::string`, eliminating buffer overflow risks.

### std::filesystem (C++17)

Cross-platform directory traversal, file attribute queries, path manipulation—no more need to write `#ifdef _WIN32`.

### std::format (C++20)

Combines the concise syntax of printf with type safety:

```cpp
std::string s = std::format("Value: {}", 42);
```

### std::span (C++20)

`std::span` binds a pointer and a length together, solving the age-old problem of array decay losing length information.

### `<system_error>`

`std::error_code` is a value type and thread-safe, making it much safer than the global `errno`.

## Summary

The core of file operations lies in `fopen` and `fread`/`fwrite`/`fgets`/`fputs`, formatted I/O relies on the `printf`/`scanf` family, and error handling depends on `errno` + `perror`. The standard library provides fundamental tools like numeric conversion, sorting/searching, math functions, character classification, and assertions. C++ has comprehensively upgraded these tools for type safety using stream libraries, `std::filesystem`, `std::format`, and `std::span`.

## Exercises

### Exercise 1: Configuration File Parser

Parse a configuration file in `.ini` format, ignoring `#` comments and empty lines.

```c
// config.ini
# Server settings
host = 127.0.0.1
port = 8080
```

Hint: Use `fgets` to read line by line, `strchr` to find the `=` position, and trim whitespace.

### Exercise 2: File Copy Tool

Specify source and destination files via command-line arguments, support binary file copying, and display progress.

```bash
./copy source.bin destination.bin
```

Hint: Use `fseek` + `ftell` to get the source file size, and use `\r` to overwrite the same line to implement a progress bar.
